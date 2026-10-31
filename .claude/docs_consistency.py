import io

# ------------------------------------------------------------------ dead comment in the harness
p = 'apps/native_poc/src/updater_scenarios_test.cpp'
d = io.open(p, encoding='utf-8').read()
old = ''' * KNOWN WART, stated rather than hidden: this does not always succeed, and a run can leave a
 * directory in %TEMP% holding an inert copy of the command interpreter. A shell-routed launch
 * hands off asynchronously, so a process can appear after the final sweep has stopped looking,
 * and it keeps its directory alive. I did not get to the bottom of it and stopped digging -- the
 * leftovers are harmless and the suite's subject is the updater, not its own housekeeping.
 *
 * Scoped to this test's own name pattern, so it can never remove anything else.
 */'''
new = ''' * Names what earlier runs left behind. It does not remove any of it.
 *
 * The header used to describe a cleanup that ran here, and admitted it did not always work. Both
 * halves are gone: the cleanup because it was deleting outside the repository, and the excuse
 * because "scoped to this test's own name pattern, so it can never remove anything else" was the
 * exact reasoning that made it seem acceptable. A name pattern is not ownership of a directory.
 */'''
assert old in d, 'known wart'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ------------------------------------------------------------------ design doc
p = 'docs/업데이트_기능_설계.md'
d = io.open(p, encoding='utf-8').read()

old = '| `GNLinkClient.exe` | **셸을 통해 비승격으로** | 승격된 업데이터의 자식으로 띄우면 관리자 토큰을 물려받아 **이후 세션 전체가 관리자로 돈다** |'
new = ('| `GNLinkClient.exe` | **사용자 토큰으로 비승격 실행**(`CreateProcessWithTokenW`), 실패 시 셸 경유 | '
       '승격된 업데이터의 자식으로 띄우면 관리자 토큰을 물려받아 **이후 세션 전체가 관리자로 돈다**. '
       '두 경로 모두 무결성 수준은 Medium 으로 같고, **다른 것은 handle 을 돌려받는가**다 — 아래 3.11.1 |')
assert old in d, 'client row'
d = d.replace(old, new, 1)

old = '''- 클라이언트를 셸 경로로 띄우지 못하면 **띄우지 않는다.** 자식으로 대신 띄우는 폴백은 없다 — 그
  폴백이 바로 피하려던 결과이기 때문이다. 이 경우 "비승격으로 띄우지 못했고 승격으로 대신 띄우지도
  않았다" 를 기록한다.'''
new = '''- 클라이언트를 비승격으로 띄우지 못하면 **띄우지 않는다.** 자식으로 대신 띄우는 폴백은 없다 — 그
  폴백이 바로 피하려던 결과이기 때문이다. 이 경우 "비승격으로 띄우지 못했고 승격으로 대신 띄우지도
  않았다" 를 기록한다.

#### 3.11.1 비승격 실행 경로가 둘인 이유 — handle (2026-09-09 구현)

**기본 경로는 셸이 아니다.** 셸 창의 토큰을 복제해 `CreateProcessWithTokenW` 로 띄운다
(`GetShellWindow` → `OpenProcessToken` → `DuplicateTokenEx(TokenPrimary)`). 얻는 것은 무결성
수준이 아니라 — 그건 두 경로가 같다 — **`pi.hProcess`** 다. `SeImpersonatePrivilege` 가 필요하고
업데이터는 상승 권한이라 가지고 있다. **그래서 이 일이 업데이터의 몫이다.**

handle 이 필요한 이유는 롤백이다. 롤백은 방금 띄운 프로세스가 잡고 있는 파일을 되돌려야 하므로
**먼저 그것들을 멈춰야 하고**, 멈추려면 **어느 프로세스인지 확정**되어야 한다. 셸 경유는 실행을
다른 프로세스(탐색기)가 대신 하므로 아무것도 돌려주지 않는다.

> ⚠️ **스냅샷 차분은 소유의 증거가 아니다.** 한때 실행 전후 프로세스 목록을 비교해 새로 생긴 하나를
> "우리 것" 으로 삼았다. **틀렸다** — handoff 가 늦는 사이 사용자가 같은 프로그램을 켜면 차분도 정확히
> 하나이고, 그 위에서 연 handle 은 우리 신원을 세우는 게 아니라 **남의 신원을 고정**한다. 그리고
> 롤백은 그것을 종료시킨다.

셸 경유로 떨어지면 **소유를 주장하지 않는다**(`RelaunchOutcome::ownershipUnknown`). 결과는:
**강제 종료하지 않고, 그 시도의 파일 롤백에도 들어가지 않는다.** 거부는 대가가 있다 — 되돌리고 싶었던
빌드가 그대로 남는다. 반대쪽 대가가 더 크다: 이름이나 추측으로 종료하면 **남의 창을 죽이고**, 절반만
복구된 설치는 롤백 탓으로 읽힌다.'''
assert old in d, 'client bullet'
d = d.replace(old, new, 1)

old = '| 9 | `Relaunch` | 교체 전 상태에 맞춰 제품을 다시 띄운다((c)) | 재실행 실패는 롤백 사유가 아니라 보고 사유. 사용자가 시작메뉴로 켤 수 있는 상태는 이미 성립 |'
new = ('| 9 | `Relaunch` | 교체 전 상태에 맞춰 제품을 다시 띄운다((c)) | **필수(호스트·서비스)와 선택(클라이언트)을 나눈다.** '
       '선택 이미지 실패는 롤백 사유가 아니라 보고 사유이고, 필수 실패는 **롤백 사유**다. 판정 불가(`Unknown`)는 '
       '성공으로 세지 않는다 |')
assert old in d, 'relaunch row'
d = d.replace(old, new, 1)

old = '| 10 | `Health` | 재실행된 제품이 살아 있고 기능하는지 관측(3.6) | 시한 내 미달성 → `Rollback` |'
new = ('| 10 | `Health` | 재실행된 제품이 살아 있고 기능하는지 관측(3.6) | 시한 내 미달성 → `Rollback`. '
       '**선택 이미지가 안 떴어도 Health 를 먼저 묻는다** — "필수가 시작됐다" 는 `CreateProcess` 가 돌아왔다는 뜻이지 '
       '**호스트가 떴다는 뜻이 아니다**. 이 순서를 뒤집으면 건강하지 않은 설치를 부분 성공으로 commit 하고 '
       '백업까지 지운다(#470) |')
assert old in d, 'health row'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ------------------------------------------------------------------ wiring plan
p = 'docs/업데이트_배선_계획.md'
d = io.open(p, encoding='utf-8').read()
old = '| E5 | **셸 경유 실행이 실제로 동작하는가** | dummy 를 `launch_via_shell` 로 띄우고 뜬 프로세스의 **무결성 수준**을 읽는다 | 없음 (dummy) |'
new = ('| E5 | **비승격 실행이 실제로 동작하는가** | dummy 를 사용자 토큰 경로(`launch_as_shell_user`, `CreateProcessWithTokenW`)로 '
       '띄우고 **무결성 수준**과 **돌려받은 handle**을 읽는다. 셸 경유(`launch_via_shell`)는 그것이 실패할 때의 폴백으로 따로 본다 | 없음 (dummy) |\n'
       '| E5b | **셸 폴백이 소유를 주장하지 않는가** | 토큰 경로 실패를 강제 → 셸로 뜬 뒤 `ownershipUnknown` 이고 `stopStarted` 의 '
       '`unstoppable` 에 들어가며 **롤백이 시작되지 않는지** 확인(시나리오 7 + 역대조) | 없음 |')
assert old in d, 'E5'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('design and plan brought in line')
