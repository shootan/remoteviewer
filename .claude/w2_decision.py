import io

BS = chr(92)

# ---------------------------------------------------------------- validate(): name -> path
p = 'apps/native_poc/src/update_effects.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''  if (!updaterImagePath.empty()) {
    const size_t slash = updaterImagePath.find_last_of(L"@@/");
    const std::wstring updaterName =
        (slash == std::wstring::npos) ? updaterImagePath : updaterImagePath.substr(slash + 1);
    for (const std::wstring& name : payloadNames) {
      if (_wcsicmp(name.c_str(), updaterName.c_str()) == 0) {
        return fail("payloadNames includes the running updater");
      }
    }
    // And the updater's own file must not sit inside the directory being replaced.'''.replace('@@', BS * 2)
new = '''  if (!updaterImagePath.empty()) {
    // Compared as full DESTINATION PATHS, not as file names.
    //
    // It used to compare names, and that was too coarse in a way that mattered. The updater has
    // to be replaceable by an update -- otherwise it becomes a second permanent maintenance
    // binary frozen at its compile-time constants, which is the exact trap that ruled out
    // re-running the old installer for registration (history #440). The way it stays replaceable
    // is to run from a copy of itself outside installDir while the installed copy is swapped.
    // With a name comparison that is refused, even though the file being replaced is not the file
    // running. What actually has to be true is narrower: no destination of this swap may be the
    // image this process is executing.
    for (const std::wstring& name : payloadNames) {
      const std::wstring destination = installDir + L"@@" + name;
      if (_wcsicmp(destination.c_str(), updaterImagePath.c_str()) == 0) {
        return fail("a payload destination is the running updater");
      }
    }
    // And the updater's own file must not sit inside the directory being replaced.'''.replace('@@', BS * 2)
assert old in d, 'validate anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ---------------------------------------------------------------- record the decision
p = 'docs/업데이트_배선_계획.md'
d = io.open(p, encoding='utf-8').read()
old = '''### W3. Host 의 update/later 승인 동작이 없다'''
new = '''### W2a. 업데이터를 어디에 설치할 것인가 — **결정: installDir 안, 실행은 자기 복사본으로**

W2 가 드러낸 구조 문제("목적지가 다른 payload 를 `do_install()` 이 다룰 자리가 없다")를 어느 쪽으로
푸는지 정한다. 두 안이 있었다.

**(가) 목적지를 항목별 속성으로** — `PayloadFile` 에 목적지를 붙이고 업데이터만 `installDir` 밖으로.
**(나) 업데이터도 보통 payload 로 `installDir` 에 넣고, 실행할 때 자기를 복사해 그 복사본으로 재실행.**

**(나)를 고른다.** (가)는 설치 코드가 덜 바뀌지만 **치명적인 결과**가 있다: `installDir` 밖에 있는
파일은 **교체 대상이 될 수 없다.** payload 이름은 설계상 상대 경로만 허용하고 traversal 을 거부하므로
(#442·#444), 밖에 있는 업데이터를 가리킬 방법이 아예 없다. 그러면 업데이터는 **컴파일 시점 상수에
묶인 채 영원히 남는 두 번째 유지보수 바이너리**가 된다 — 등록에서 (C)안을 탈락시킨 것과 **똑같은
덫**이다(history #440). 게다가 언인스톨은 `installDir` 을 지우므로 밖의 파일은 **고아로 남는다.**

(나)의 동작:

1. 설치기는 `GNLinkUpdater.exe` 를 **다른 payload 와 똑같이** `installDir` 에 넣는다. `kPayload`
   루프도, 언인스톨도 그대로다. **설치 코드에 새 구조가 생기지 않는다.**
2. 업데이트를 시작할 때, 실행된 업데이터는 **자기를 작업 디렉터리로 복사하고 그 복사본을 실행한 뒤
   종료한다.** 이후 진짜 작업은 복사본이 한다.
3. 복사본은 `installDir` 밖에 있으므로 `validate()` 의 "업데이터가 교체 대상 안에 있다" 검사를
   통과하고, 스왑은 `installDir\\GNLinkUpdater.exe` 를 **보통 파일처럼 교체·백업·롤백**한다.

⚠️ **작업 디렉터리는 관리자만 쓸 수 있어야 한다.** 복사본은 상승된 채로 실행되므로, 사용자 쓰기가
가능한 `%TEMP%` 에 두면 **복사와 실행 사이에 바꿔치기할 창**이 생긴다 — 권한 상승 취약점이다. 설치기가
`%ProgramData%\\GNLink\\work` 를 **관리자 전용 ACL**로 만들어 두고 거기에만 복사한다.

**함께 고친 것**: `UpdateEffectsConfig::validate()` 가 업데이터를 **파일 이름**으로 비교하고 있었다.
그러면 (나)가 원천 봉쇄된다 — 교체되는 파일과 실행 중인 파일이 다른데도 이름이 같아서 거부된다.
**전체 목적지 경로로 비교**하도록 바꿨다. 실제로 참이어야 하는 명제는 더 좁다: **이 스왑의 어떤
목적지도 지금 실행 중인 이미지여서는 안 된다.** 디렉터리 검사(업데이터가 `installDir` 안에 있으면
거부)는 그대로 남아 있으므로 방어는 줄지 않는다.

### W3. Host 의 update/later 승인 동작이 없다'''
assert old in d, 'W3 anchor'
d = d.replace(old, new, 1)

d = d.replace('2. **W1·W2** — 업데이터 바이너리와 payload. 나머지 배선이 얹힐 바닥.',
              '2. **W1·W2·W2a** — 업데이터 바이너리와 payload. 나머지 배선이 얹힐 바닥. '
              '설치 위치는 W2a 로 확정(`installDir` 안 + 자기 복사본 실행).', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('W2a decided and validate() widened to paths')
