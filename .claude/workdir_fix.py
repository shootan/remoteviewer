import io

BS = chr(92)

# ---------------------------------------------------------------- the plan doc
p = 'docs/업데이트_배선_계획.md'
d = io.open(p, encoding='utf-8').read()
old = '''⚠️ **작업 디렉터리는 관리자만 쓸 수 있어야 한다.** 복사본은 상승된 채로 실행되므로, 사용자 쓰기가
가능한 `%TEMP%` 에 두면 **복사와 실행 사이에 바꿔치기할 창**이 생긴다 — 권한 상승 취약점이다. 설치기가
`%ProgramData%\\GNLink\\work` 를 **관리자 전용 ACL**로 만들어 두고 거기에만 복사한다.'''
new = '''⚠️ **작업 디렉터리는 관리자만 쓸 수 있어야 한다.** 복사본은 상승된 채로 실행되므로, 사용자 쓰기가
가능한 곳에 두면 **복사와 실행 사이에 바꿔치기할 창**이 생긴다 — 권한 상승 취약점이다.

**위치는 `%ProgramFiles%\\GNLink.update\\` 다.** 설계 1차 조사(history #427)가 이미 이 판단을 내려
뒀고, 거기서 **`%TEMP%` 와 `%ProgramData%` 를 명시적으로 배제**했다 — 사용자가 쓸 수 있어 설치기가
지키는 불변식(LocalSystem 서비스 바이너리가 사용자 쓰기 가능한 폴더에 있으면 그 폴더에 쓸 수 있는
누구나 SYSTEM 을 얻는다, `installer_main.cpp:1-13`)을 되돌리기 때문이다. **이 문서의 초안이 그
결정을 모르고 `%ProgramData%` 를 적었던 것을 정정한다.**

`%ProgramFiles%` 아래는 기본 ACL 이 이미 관리자 전용이라 **따로 ACL 을 세울 필요가 없다** — 세워야
하는 방어는 세우다 빠뜨릴 수 있는 방어다. 그리고 `GNLink.update` 는 `GNLink` 의 **형제**이지 하위가
아니므로(`GNLink` 다음 글자가 구분자가 아니라 `.`), "작업 디렉터리가 installDir 안" 검사에 걸리지
않는다.'''
assert old in d, 'workdir anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ---------------------------------------------------------------- the code comment
p = 'apps/native_poc/src/updater_main.cpp'
d = io.open(p, encoding='utf-8').read()
old = ''' * The working directory must be one only administrators can write. This process runs elevated,
 * so a user-writable location would leave a window between the copy and the launch in which the
 * file could be swapped for something else -- an elevation-of-privilege bug rather than an
 * untidiness. That is why the location is required on the command line instead of defaulting to
 * %TEMP%.'''
new = ''' * The working directory must be one only administrators can write. This process runs elevated, so
 * a user-writable location would leave a window between the copy and the launch in which the file
 * could be swapped for something else -- an elevation-of-privilege bug rather than an untidiness.
 * %TEMP% and %ProgramData% are both out for that reason; the intended location is
 * %ProgramFiles%\\GNLink.update, a sibling of the install directory, where the default ACL is
 * already administrators-only. A defence that has to be erected is a defence that can be
 * forgotten.
 *
 * It is still required on the command line rather than computed here, because this process
 * decides nothing for itself.'''
assert old in d, 'comment anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ---------------------------------------------------------------- the case, asserted
p = 'apps/native_poc/src/updater_options_test.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''    // A directory that merely starts with the same characters is not inside it.
    check("a sibling with a similar name is not 'inside'",
          accepts(with(L"--staging-dir", L"C:@@Program Files@@GNLinkStaging"), &why), why);'''.replace('@@', BS*2)
new = '''    // A directory that merely starts with the same characters is not inside it.
    check("a sibling with a similar name is not 'inside'",
          accepts(with(L"--staging-dir", L"C:@@Program Files@@GNLinkStaging"), &why), why);
    // The real one. %ProgramFiles%@@GNLink.update is where the working copy goes: a sibling of the
    // install directory, admin-only by the default ACL, and NOT inside the directory being
    // replaced -- the character after "GNLink" is a dot, not a separator. If this were rejected
    // the updater would have nowhere safe to run from.
    check("the intended working directory is accepted",
          accepts(with(L"--work-dir", L"C:@@Program Files@@GNLink.update"), &why), why);'''.replace('@@', BS*2)
assert old in d, 'sibling anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('work directory corrected to %ProgramFiles%\\\\GNLink.update')
