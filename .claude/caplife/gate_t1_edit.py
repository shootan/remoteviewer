p = 'C:/work/gnlink-wt/ka-r3/automation/gnlink_check_fixed_server.py'
s = open(p, encoding='utf-8', newline='').read()
crlf = '\r\n' in s
if crlf:
    s = s.replace('\r\n', '\n')
start = s.index('def cleanup_run_dir(run):')
end = s.index('def self_test():')
new = '''def cleanup_run_dir(run):
    """Removes what is inside this self-test's own run folder, then the folder: files and folders only. A link -- a
    symlink or a directory junction -- is never entered, followed or removed: it is pruned BEFORE the walk would go
    below it (T1, Codex codex_avd_guard_src_r3.md: a bottom-up walk had already gone inside a junction before it was
    checked), left in place and said. Returns (files removed, folders removed, what was left)."""
    files = dirs = 0
    left = []
    run = os.path.abspath(run)
    if _is_link(run) or not os.path.isdir(run):
        return 0, 0, [run]  # not a plain folder of its own: nothing is touched
    head = run + os.sep
    to_files, to_dirs = [], []
    for here, subdirs, names in os.walk(run, topdown=True, followlinks=False):
        keep = []
        for d in subdirs:  # decided before the walk goes below: a link is pruned, never entered
            path = os.path.join(here, d)
            if not os.path.abspath(path).startswith(head) or _is_link(path):
                left.append(path)
            else:
                keep.append(d)
                to_dirs.append(path)
        subdirs[:] = keep
        for n in names:
            path = os.path.join(here, n)
            if not os.path.abspath(path).startswith(head) or _is_link(path):
                left.append(path)
            else:
                to_files.append(path)
    for path in to_files:
        try:
            os.remove(path)
            files += 1
        except OSError:
            left.append(path)
    for path in sorted(to_dirs, key=lambda x: x.count(os.sep), reverse=True):  # the deepest first
        try:
            os.rmdir(path)
            dirs += 1
        except OSError:
            left.append(path)
    try:
        os.rmdir(run)
        dirs += 1
    except OSError:
        left.append(run)
    return files, dirs, left


'''
s = s[:start] + new + s[end:]
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
