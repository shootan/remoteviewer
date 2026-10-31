"""Read-only source/copy audit; writes reports only, never removes anything."""
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path

ROOT = Path('C:/work/remote')
OLD = Path('D:/remote/remote')
REPORT = Path('E:/remote-output/migration/20261031-storage')


def scan(root):
    files, links = {}, []
    def visit(folder):
        for ent in os.scandir(folder):
            p = Path(ent.path)
            stat = ent.stat(follow_symlinks=False)
            rel = p.relative_to(root).as_posix()
            if stat.st_file_attributes & 0x400:
                links.append({'path': rel, 'target': os.readlink(p)})
            elif ent.is_dir(follow_symlinks=False):
                visit(p)
            elif ent.is_file(follow_symlinks=False):
                files[rel] = stat.st_size
    visit(root)
    return files, links


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def compare(row):
    rel, expected = row['path'], row['sha256']
    result = {'path': rel}
    try:
        source = OLD / rel
        result['D'] = 'missing' if not source.is_file() else (
            'unchanged' if digest(source) == expected else 'changed_since_copy')
        if not rel.startswith('.git/'):
            copy = ROOT / rel
            result['C'] = 'missing' if not copy.is_file() else (
                'matches_original' if digest(copy) == expected else 'changed_since_copy')
    except OSError as exc:
        result['error'] = str(exc)
    return result


def main():
    original = json.loads((REPORT / 'copy_hashes.json').read_text(encoding='utf-8'))
    old_files, old_links = scan(OLD)
    expected_names = {x['path'] for x in original}
    added = sorted(set(old_files) - expected_names)
    removed = sorted(expected_names - set(old_files))
    print(f'Hash audit: {len(original)} baseline files; D added={len(added)}, missing={len(removed)}', flush=True)
    changes, errors = [], []
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        for i, row in enumerate(pool.map(compare, original), 1):
            if row.get('error'):
                errors.append(row)
            if row.get('D') != 'unchanged' or row.get('C', 'matches_original') != 'matches_original':
                changes.append(row)
            if i % 10000 == 0:
                print(f'Baseline hashes checked: {i}/{len(original)}', flush=True)
    added_comparison = []
    for rel in added:
        copy = ROOT / rel
        added_comparison.append({'path': rel, 'copied_to_C': copy.is_file() and digest(OLD / rel) == digest(copy)})
    export_rows = json.loads((REPORT / 'export_hashes.json').read_text(encoding='utf-8'))
    export_bad = []
    for row in export_rows:
        p = Path(row['destination'])
        if not p.is_file() or p.stat().st_size != row['bytes'] or digest(p) != row['sha256']:
            export_bad.append(str(p))
    siblings = []
    for name in ['build', 'download', 'remote-worktrees']:
        p = OLD.parent / name
        files, links = scan(p)
        siblings.append({'path': str(p), 'files': len(files), 'bytes': sum(files.values()),
                         'links': links, 'sample_paths': sorted(files)[:12],
                         'covered_by_repository_copy_manifest': False})
    expected_C_changes = {
        '.claude/worktrees/main-stability-integration/docs/history.md',
        '.claude/worktrees/main-stability-integration/docs/구현계획.md',
    }
    expected_git_pointers = {
        '.claude/worktrees/' + name + '/.git'
        for name in ['main-stability-integration', 'rel-0164-e2e', 'verify-0164']
    }
    for rel in expected_git_pointers:
        assert (ROOT / rel).read_text(encoding='utf-8').strip().lower().startswith(
            'gitdir: c:/work/remote/.git/worktrees/')
    unexpected_C = [x for x in changes if x.get('C', 'matches_original') != 'matches_original'
                    and x['path'] not in expected_C_changes | expected_git_pointers]
    result = {'baseline_files': len(original), 'D_added': added_comparison, 'D_missing': removed,
              'baseline_hash_changes': changes, 'errors': errors,
              'unexpected_C_changes': unexpected_C, 'D_junctions_preserved': old_links,
              'expected_C_git_pointer_changes': [x for x in changes if x['path'] in expected_git_pointers],
              'E_files_checked': len(export_rows), 'E_hash_failures': export_bad,
              'outside_copied_repository': siblings, 'deletion_executed': False,
              'product_work_still_stopped': True}
    target = REPORT / 'removal_readiness_hashes.json'
    target.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'report': str(target), 'D_added': len(added), 'D_missing': len(removed),
                      'D_baseline_changes': sum(x.get('D') != 'unchanged' for x in changes),
                      'C_expected_document_changes': sum(x['path'] in expected_C_changes for x in changes),
                      'C_unexpected_changes': len(unexpected_C), 'errors': len(errors),
                      'E_hash_failures': len(export_bad), 'siblings': siblings}, ensure_ascii=False), flush=True)


if __name__ == '__main__':
    main()
