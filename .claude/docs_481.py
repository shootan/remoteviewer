import io

p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()

# ---------------------------------------------------------------- correct #480's two overstatements
old = '''- **ACL**: Owner `shotan\\shotan`. `Users`·`Everyone`·`Authenticated Users` **없음**. 미해결 SID 1건(`S-1-5-21-…-2881406636`, Write+ReadAndExecute)이 있는데 **`%LOCALAPPDATA%` 전체에서 상속된 기존 조건**이고 계정이 해석되지 않는다(삭제된 계정). **이 디렉터리가 만든 것이 아니라 손대지 않았다** — 저장소 밖 ACL 을 요청 없이 바꾸지 않는다. 개인키는 DPAPI CurrentUser 라 읽기 권한만으로는 풀 수 없다.'''
new = '''- **ACL**(생성 시점): Owner `shotan\\shotan`. `Users`·`Everyone`·`Authenticated Users` **없음**. 미해결 SID 1건(`S-1-5-21-…-2881406636`, Write+ReadAndExecute)이 **`%LOCALAPPDATA%` 전체에서 상속**되고 있었다. 그 자리에서는 손대지 않았다 — 저장소 밖 ACL 을 요청 없이 바꾸지 않는다. **#481 에서 승인을 받아 키 폴더만 상속을 끊었다.**
  - ⚠️ **정정(#481)**: 여기 "계정이 해석되지 않는다(**삭제된 계정**)" 이라고 적었는데 **근거 없는 단정이었다.** SID 해석 실패는 도메인 미도달·다른 머신의 계정 등으로도 난다 — **해석 불가는 주체 없음이 아니다.** 실제로 그 SID 의 도메인부는 이 PC 사용자 SID(`S-1-5-21-3755351295-…`)와 **다르다**(`S-1-5-21-2456923259-…`). 다른 프로필/머신에서 온 것이고, **그것을 해석할 수 있는 환경이 존재할 수 있다.** 그래서 상속을 끊는 것이 필요했다.'''
assert old in d, 'acl para'
d = d.replace(old, new, 1)

entry = '''
### 481) 2026-09-09 키 폴더 ACL 상속 차단 — **해석 안 되는 SID 는 "주체 없음" 이 아니다**
- **범위**(Codex 승인, 이것만): `%LOCALAPPDATA%\\GNLink\\ReleaseSigning\\p256-a0e184579c5d4c2d\\` **폴더와 그 안 파일**. 상속 차단 + **명시 3주체만**(현재 사용자 · `SYSTEM` · `BUILTIN\\Administrators`).
- **왜**: 그 폴더가 `%LOCALAPPDATA%` 에서 미해결 SID 1건(`S-1-5-21-2456923259-…-2881406636`, Write+ReadAndExecute)을 상속하고 있었다. #480 에서 나는 이것을 "삭제된 계정" 이라 적었는데 **그렇게 단정할 근거가 없다** — 해석 실패는 도메인 미도달이나 **다른 머신의 계정**으로도 난다. 실제로 도메인부가 이 PC 사용자 SID 와 **다르다.** **해석 불가 ≠ 주체 없음** 이므로 끊는 것이 맞다.
- **절차**: 변경 전 **원 ACL 을 `.claude/key_acl_before.clixml` 로 보존**하고, 소유자가 현재 사용자인지 먼저 확인(아니면 중단). 실패하면 **원 ACL 로 되돌린다** — 여기서 잘못되면 DPAPI 로 보호된 개인키가 든 폴더에 **본인도 못 들어간다.**
- **결과**: `protected=True`, ACE 3개 전부 `inherited=False` — `NT AUTHORITY\\SYSTEM` · `BUILTIN\\Administrators` · `shotan\\shotan`, 각 FullControl. **미해결 SID 사라짐.** 파일 4개는 그 폴더에서만 상속받는다(`private.ecc.dpapi` 확인: 세 주체뿐).
- **변경 후 확인 3가지**: ⓐ 현재 사용자로 **파일 4개 읽기 성공** ⓑ **DPAPI 서명 성공**(Unprotect→Import→Sign→기록된 공개키로 Verify, **변조 probe 거부**까지) ⓒ **예상 외 주체 0** — 폴더와 파일 전부.
- **건드리지 않은 것**: `%LOCALAPPDATA%` · `GNLink` · `ReleaseSigning` **상위 3개는 그대로**(여전히 `protected=False`, 그 SID 유지). 범위가 키 폴더 하나였다. **키 재생성 없음, 파일 내용 변경 없음, 삭제 없음.** 개인키 원문 출력 없음.
- **여전히 미결**: **독립 백업.** DPAPI CurrentUser 는 이 PC·이 프로필 종속이고, **ACL 을 조인다고 백업이 되는 것은 아니다** — 프로필이 사라지면 사본도 못 푼다. 사용자 결정(외부 매체+별도 암호 사본 / 분실 시 재배포) 대기.
- 변경 파일: `docs/history.md`(#480 정정 2건 + 이 항목). 제품 코드·산출물·공유 벡터 변경 0.
'''
assert '### 481)' not in d
d = d.rstrip('\n') + '\n' + entry
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('history 481 and the two corrections to 480')
