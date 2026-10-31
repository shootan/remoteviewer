"""Verify recent source audit remains valid; no deletions here."""
import hashlib
import json
import os
import zipfile
from pathlib import Path

ROOT = Path('C:/work/remote')
OLD = Path('D:/remote/remote')
REPORT = Path('E:/remote-output/migration/20261031-storage')
summary = json.loads((REPORT / 'removal_readiness_summary.json').read_text(encoding='utf-8'))
audit_file = REPORT / 'removal_readiness_hashes.json'
audit = json.loads(audit_file.read_text(encoding='utf-8'))
assert not audit['D_added'] and not audit['D_missing'] and not audit['errors']
assert all(x['D'] == 'unchanged' for x in audit['baseline_hash_changes'])
assert not audit['unexpected_C_changes'] and not audit['E_hash_failures']
assert set(p.name for p in OLD.parent.iterdir()) == {'remote', 'build', 'download', 'remote-worktrees'}
assert not OLD.parent.lstat().st_file_attributes & 0x400
assert not OLD.lstat().st_file_attributes & 0x400


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


baseline = {x['path']: x for x in json.loads((REPORT / 'copy_hashes.json').read_text(encoding='utf-8'))}
seen, links, rehashed = set(), [], []
cutoff = audit_file.stat().st_mtime_ns


def visit(folder):
    for ent in os.scandir(folder):
        path = Path(ent.path)
        rel = path.relative_to(OLD).as_posix()
        st = ent.stat(follow_symlinks=False)
        if st.st_file_attributes & 0x400:
            links.append({'path': rel, 'target': os.readlink(path)})
        elif ent.is_dir(follow_symlinks=False):
            visit(path)
        elif ent.is_file(follow_symlinks=False):
            assert rel in baseline, 'New D source file: ' + rel
            row = baseline[rel]
            assert st.st_size == row['bytes'], 'D source size changed: ' + rel
            if st.st_mtime_ns > cutoff or st.st_ctime_ns > cutoff:
                assert sha(path) == row['sha256'], 'D source content changed: ' + rel
                rehashed.append(rel)
            seen.add(rel)


visit(OLD)
assert seen == set(baseline), 'Missing D source files'
expected_links = audit['D_junctions_preserved']
assert sorted(links, key=lambda x: x['path']) == sorted(expected_links, key=lambda x: x['path'])
backup = json.loads((REPORT / 'legacy_sibling_backup.json').read_text(encoding='utf-8'))
archive = Path(backup['archive'])
assert archive.is_file() and archive.stat().st_size == backup['archive_bytes']
with zipfile.ZipFile(archive) as z:
    for i, row in enumerate(backup['manifest'], 1):
        source = OLD.parent / row['path']
        assert source.is_file() and source.stat().st_size == row['bytes']
        assert sha(source) == row['sha256'], 'Legacy source changed: ' + row['path']
        h = hashlib.sha256()
        with z.open(row['path']) as f:
            for block in iter(lambda: f.read(1024 * 1024), b''):
                h.update(block)
        assert h.hexdigest() == row['sha256'], 'Legacy backup changed: ' + row['path']
        if i % 500 == 0:
            print(f'Legacy source and E backup checked: {i}/{len(backup["manifest"])}', flush=True)
result = {'source_files': len(seen), 'source_changes_since_full_hash_audit': 0,
          'source_files_rehashed_after_audit': rehashed, 'source_junctions': links,
          'legacy_source_and_archive_files_verified': len(backup['manifest']),
          'archive': str(archive), 'predelete_check': 'PASS', 'deletion_executed': False}
(REPORT / 'predelete_check.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(result, ensure_ascii=False), flush=True)
