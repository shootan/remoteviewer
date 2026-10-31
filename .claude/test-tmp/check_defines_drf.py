"""integ-0162 r2: the shipped targets carry no test define -- read from the GENERATED vcxproj (what MSBuild
actually compiles with), not from strings in the exe. Release configuration, every ClCompile item and the
ItemDefinitionGroup. Positive controls: test targets that must carry their seam/mutant defines."""
import re, sys, glob, os

build = sys.argv[1]
# output name -> CMake target (the vcxproj's Release <TargetName> maps one to the other)
SHIPPED = {'GNLinkStream': 'remote60_native_video_host_poc', 'GNLinkHost': 'remote60_host_app',
           'GNLinkViewer': 'remote60_native_video_client_poc', 'GNLinkClient': 'remote60_client_shell',
           'GNLinkInputService': 'remote60_secure_input_service', 'GNLinkUpdater': 'remote60_updater',
           'GNLinkCapture': 'remote60_gdi_capture_worker', 'GNLinkClipHelper': 'remote60_file_copy_helper',
           'GNLinkSetup': 'remote60_installer'}
CONTROLS = {'remote60_directory_retry_test': 'REMOTE60_DIRECTORY_TEST_HOOKS',
            'remote60_directory_retry_mutant_timeout_only_test': 'REMOTE60_DIRECTORY_MUTANT_TIMEOUT_ONLY',
            'remote60_directory_retry_mutant_no_legacy_attempt_test': 'REMOTE60_DIRECTORY_MUTANT_NO_LEGACY_ATTEMPT'}
BAD = re.compile(r'(TEST_SEAM|_MUTANT|TEST_MUTANT|_TEST_[A-Z]|GNLINK_[A-Z_]*TEST|REMOTE60_[A-Z_]*TEST)')


def find(target):
    hits = glob.glob(os.path.join(build, '**', target + '.vcxproj'), recursive=True)
    return hits[0] if hits else None


def release_defines(path):
    x = open(path, encoding='utf-8', errors='replace').read()
    out = set()
    # Release-conditioned ItemDefinitionGroups and per-file PreprocessorDefinitions with a Release condition
    for m in re.finditer(r"<ItemDefinitionGroup Condition=\"[^\"]*Release\|[^\"]*\">(.*?)</ItemDefinitionGroup>", x, re.S):
        for d in re.findall(r'<PreprocessorDefinitions>(.*?)</PreprocessorDefinitions>', m.group(1), re.S):
            out.update(v.strip() for v in d.split(';') if v.strip())
    for d in re.findall(r"<PreprocessorDefinitions Condition=\"[^\"]*Release\|[^\"]*\">(.*?)</PreprocessorDefinitions>", x, re.S):
        out.update(v.strip() for v in d.split(';') if v.strip())
    return out


fail = 0
for name, t in SHIPPED.items():
    p = find(t)
    if p and ('>' + name + '<') not in open(p, encoding='utf-8', errors='replace').read():
        print('FAIL', name, 'the vcxproj', t, 'does not build', name); fail += 1; continue
    if not p:
        print('FAIL', t, 'vcxproj not found'); fail += 1; continue
    defs = release_defines(p)
    bad = sorted(d for d in defs if BAD.search(d) and d != '%(PreprocessorDefinitions)')
    print(('PASS' if not bad and defs else 'FAIL'), name, '(' + t + ')', 'release defines=%d' % len(defs), 'test-like=%s' % bad)
    if bad or not defs: fail += 1
for t, want in CONTROLS.items():
    p = find(t)
    defs = release_defines(p) if p else set()
    ok = any(d.startswith(want) for d in defs)
    print(('PASS' if ok else 'FAIL'), 'control', t, 'carries', want, ok)
    if not ok: fail += 1
print('RESULT', 'ALL PASS' if fail == 0 else 'FAILED %d' % fail)
sys.exit(1 if fail else 0)
