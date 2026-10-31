import sys, shutil, os
src = 'C:\\work\\gnlink-wt\\fix-capture-lifecycle\\apps\\native_poc\\src\\'
bak = r'D:\remote\remote\.claude\caplife\mutant_bak' + '\\'
os.makedirs(bak, exist_ok=True)
MUTANTS = {
    # PS-A reverted: the cached frame only on "Default" (as before), and no full-frame ask.
    'M1': [
        ('host_capture_session.cpp',
         '  if (!capture.DesktopMayBeShown(desktopNow, desktopKnown)) return miss(KickMiss::DesktopNotShown);',
         '  if (!interactive_desktop_is_default_uncached()) return miss(KickMiss::DesktopNotShown);  // MUTANT M1'),
        ('host_stage_pop_frame.cpp',
         '  if (why != CaptureState::KickMiss::NothingCached && why != CaptureState::KickMiss::NotCurrent) return;',
         '  return;  // MUTANT M1: no full-frame ask'),
    ],
    # PS-B reverted: a secure input desktop sends the reattach to WGC before login too.
    'M2': [
        ('host_stage_stream_active.cpp',
         '        if (backend.active == DesktopCaptureBackend::Dxgi && capture.preloginServingDesktop.empty() &&',
         '        if (backend.active == DesktopCaptureBackend::Dxgi && /* MUTANT M2 */'),
    ],
}
MUTANTS['M3'] = [  # UA-1 reverted: no WGC rebuild on a desktop change
    ('host_stage_backend.cpp',
     '      const bool applicable = streamActiveNow && desktopMode && !beforeLogin &&',
     '      const bool applicable = false && /* MUTANT M3 */ streamActiveNow && desktopMode && !beforeLogin &&'),
]
MUTANTS['M4'] = [  # UA-2 reverted: a new session inherits the last session's backend request
    ('host_stage_backend.cpp',
     '      if (!beforeLogin && backend.requested != backend.startupRequested) {',
     '      if (false && /* MUTANT M4 */ !beforeLogin && backend.requested != backend.startupRequested) {'),
]
mode, name = sys.argv[1], sys.argv[2]
for f, old, new in MUTANTS[name]:
    if mode == 'apply':
        if not os.path.exists(bak + f):
            shutil.copy2(src + f, bak + f)
        s = open(src + f, 'rb').read().decode('utf-8')
        assert s.count(old) == 1, (f, old)
        s = s.replace(old, new)
        open(src + f, 'wb').write(s.encode('utf-8'))
    else:
        shutil.copyfile(bak + f, src + f)  # copyfile: a fresh mtime, so MSBuild sees the change
        os.remove(bak + f)
print(mode, name, 'ok')
