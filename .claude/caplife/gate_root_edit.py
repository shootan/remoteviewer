p = 'C:/work/gnlink-wt/ka-r3/automation/gnlink_check_fixed_server.py'
s = open(p, encoding='utf-8', newline='').read()
crlf = '\r\n' in s
if crlf:
    s = s.replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


rep('''    python automation/gnlink_check_fixed_server.py --self-test
''', '''    python automation/gnlink_check_fixed_server.py --self-test   (REMOTE60_TEST_TMP: see self_test_run_dir)
''')
rep('''import argparse
import os
import sys
import tempfile
import zipfile
''', '''import argparse
import os
import re
import stat
import sys
import time
import zipfile
''')

rep('''def self_test():
    """The gate against files made to fail it. A gate that cannot fail is decoration."""
    outer = Report()
    with tempfile.TemporaryDirectory(prefix='gnlink-fixed-server-gate-') as scratch:
''', '''# test-injection-finding (Codex codex_avd_guard_src_r2.md T): the self-test's fixtures live ONLY inside the repository's
# test root -- REMOTE60_TEST_TMP, a plain folder directly under this anchor, no link anywhere from the drive down --
# in a NEW run folder of its own; its cleanup removes only what is inside that run folder, and nothing is ever made
# or removed under %TEMP% (it was tempfile.TemporaryDirectory until 2026-10-28: a creation and a deletion outside the
# repository). Anything else is refused BEFORE anything is created.
TEST_ANCHOR = r'D:\\remote\\remote\\.claude\\test-tmp'


def _is_link(path):
    try:
        st = os.lstat(path)
    except FileNotFoundError:
        return False
    return stat.S_ISLNK(st.st_mode) or bool(getattr(st, 'st_file_attributes', 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT)


def _no_link_on_path(path):
    """Every existing component of `path`, from the drive down, is not a link."""
    full = os.path.abspath(path)
    drive, rest = os.path.splitdrive(full)
    cur = drive + os.sep
    for part in [x for x in rest.split(os.sep) if x]:
        cur = os.path.join(cur, part)
        if _is_link(cur):
            return False, 'a link on the path: ' + cur
    return True, ''


def self_test_run_dir(environ=None):
    """(run folder, why): a NEW run folder of this self-test inside the validated test root, or (None, why) -- in which
    case nothing at all was created."""
    environ = os.environ if environ is None else environ
    given = environ.get('REMOTE60_TEST_TMP', '')
    if not given:
        return None, 'REMOTE60_TEST_TMP is not set (the self-test never falls back to %TEMP%)'
    full = os.path.abspath(given)
    name = os.path.basename(full)
    if not re.fullmatch(r'[A-Za-z0-9._-]{1,64}', name) or name in ('.', '..'):
        return None, 'the test root must be a plain ASCII folder name'
    if os.path.normcase(os.path.dirname(full)) != os.path.normcase(TEST_ANCHOR):
        return None, 'REMOTE60_TEST_TMP must be a folder directly under ' + TEST_ANCHOR
    if not os.path.isdir(TEST_ANCHOR):
        return None, 'the test anchor is missing'
    ok, why = _no_link_on_path(full)
    if not ok:
        return None, why
    if os.path.exists(full):
        if not os.path.isdir(full) or _is_link(full):
            return None, 'the test root is not a plain folder'
    else:
        try:
            os.mkdir(full)
        except OSError as e:
            return None, 'the test root could not be created: %s' % e
    for attempt in range(16):
        run = os.path.join(full, 'gate-selftest-%d-%d-%d' % (os.getpid(), int(time.time() * 1000), attempt))
        try:
            os.mkdir(run)  # only a folder this process creates (an existing one is never adopted)
        except FileExistsError:
            continue
        except OSError as e:
            return None, 'the run folder could not be created: %s' % e
        if _is_link(run):
            return None, 'the run folder is a link'
        return run, ''
    return None, 'no new run folder could be made'


def cleanup_run_dir(run):
    """Removes what is inside this self-test's own run folder, then the folder: files and folders only (a link is not
    followed or removed -- it is left and said). Returns (files removed, folders removed, what was left)."""
    files = dirs = 0
    left = []
    head = os.path.abspath(run) + os.sep
    for here, subdirs, names in os.walk(run, topdown=False, followlinks=False):
        for n in names:
            path = os.path.join(here, n)
            if not os.path.abspath(path).startswith(head) or _is_link(path):
                left.append(path)
                continue
            try:
                os.remove(path)
                files += 1
            except OSError:
                left.append(path)
        for d in subdirs:
            path = os.path.join(here, d)
            if not os.path.abspath(path).startswith(head) or _is_link(path):
                left.append(path)
                continue
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


def self_test():
    """The gate against files made to fail it. A gate that cannot fail is decoration."""
    outer = Report()
    scratch, why = self_test_run_dir()
    if scratch is None:
        print('FAIL  self-test refused: %s -- nothing created' % why)
        outer.check(False, 'a valid repository test root for the self-test')
        return outer
    print('NOTE  self-test run folder: %s' % scratch)
    try:
'''.replace("TEST_ANCHOR = r'D:\\\\remote\\\\remote\\\\.claude\\\\test-tmp'", "TEST_ANCHOR = r'D:\\remote\\remote\\.claude\\test-tmp'"))

rep('''        missing = make('missing', {})
        os.remove(os.path.join(missing, 'GNLinkViewer.exe'))
        outer.check(verdict(missing) == 1, 'a missing program fails rather than being skipped')
    return outer
''', '''        missing = make('missing', {})
        os.remove(os.path.join(missing, 'GNLinkViewer.exe'))
        outer.check(verdict(missing) == 1, 'a missing program fails rather than being skipped')
    finally:
        files, dirs, left = cleanup_run_dir(scratch)
        print('NOTE  self-test cleanup (its own run folder only): %d file(s), %d folder(s) removed%s'
              % (files, dirs, '' if not left else '; LEFT: ' + ', '.join(left)))
        outer.check(not left, 'the self-test left nothing in its run folder (and removed nothing else)')
    return outer
''')

if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
