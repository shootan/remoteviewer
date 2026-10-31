# C r2: N1-b's two guards -- keyOutside (no gap when an IDR after the loss is held outside the inbox) and the inbox's
# OnKeyCommitted (a committed IDR closes the gap). Each alone, and both.
import os
import shutil

WT = r'C:\work\gnlink-wt\apk-inbox-c'
NEG = WT + r'\neg'


def edit(path, a, b):
    raw = open(path, 'rb').read()
    crlf = b'\r\n' in raw
    s = raw.decode('utf-8').replace('\r\n', '\n')
    assert s.count(a) == 1, (path, s.count(a))
    s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(path, 'wb').write(s.encode('utf-8'))


KO_A = '  inbox_.MarkLoss(seq, keyOutside);'
KO_B = '  inbox_.MarkLoss(seq, false && keyOutside);  // mutant'
for name, base, ko in (('r2_no_keyoutside', 'r2_no_trim', True), ('r2_no_keyoutside_keycommit', 'r2_no_keycommit', True)):
    d = NEG + '\\' + name
    if os.path.exists(d):
        raise SystemExit(f'{d} exists')
    # start from a CURRENT copy (r2_no_keycommit's files are current except its one header edit)
    src = NEG + '\\' + ('r2_no_keycommit' if name.endswith('keycommit') else 'r2_held_clear_all')
    shutil.copytree(src, d)
    if not name.endswith('keycommit'):
        # r2_held_clear_all differs from the current source only in the session .cpp: take the current one back
        shutil.copyfile(WT + r'\apps\native_poc\src\native_video_client_session.cpp', d + r'\native_video_client_session.cpp')
    edit(d + r'\android_video_decoder.cpp', KO_A, KO_B)
    print('made', name)
