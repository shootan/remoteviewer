# -*- coding: utf-8 -*-
import io

p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()

entry = u'''
### 501) 2026-09-10 전달 경로 3건 (5·6·7) — **업데이터가 자기 자신을 한 번도 갱신한 적이 없다**
검증용 승인 범위 7항목 중 **창이 필요 없는 3건**을 먼저 배선했다(RDP 활성이라 창 회귀는 콘솔 재접속 뒤).

**(5) 목록이 하나였던 것이 문제였다** (`update_process_targets.{hpp,cpp}` · `updater_effects.cpp:293`)
`payloadNames = product_image_names() + ui 2개` 였고, `product_image_names()` 는 **정지 목록**이다. 주석은 *"the set that is stopped and the set that is replaced cannot drift apart"* 라며 **일부러 같은 목록**을 썼다.
- ⚠️ **그 공유가 드리프트를 막지 못했다.** `GNLinkUpdater.exe` 가 **양쪽 모두에 없어서**, **어떤 릴리스도 업데이터를 교체한 적이 없다.** 업데이터 코드를 고쳐도 **인앱으로 전달되지 않는다** — Codex 질문의 답이 *"bootstrap 수동 설치 필요"* 인 이유다.
- 두 목록은 **다른 질문**에 답한다: *"교체하는 동안 실행 중이면 안 되는 것"* vs *"교체하는 것"*. 답이 다른 파일이 **정확히 하나** 있다.
- → `product_payload_names()` 신설, **정지 목록에서 파생**(`+ Updater + ui 2개`). 진짜 불변식은 *"정지 목록 ⊆ 교체 목록"* 이고 `product_payload_list_contract()` 가 그것을 단정한다. **같은 목록이 아니라 파생**이라 드리프트 방지는 유지된다.
- ⚠️ **업데이터를 정지 목록에 넣지 않았다.** 실행 중인 업데이터는 `installDir` 밖 working copy 이고(로그 `handed the update to the working copy`), 그 이름을 쓰는 다른 무엇은 우리가 닫을 것이 아니다. `validate()` 는 **이름이 아니라 목적지 전체 경로**로 비교하므로(`update_effects.cpp:145`) 통과한다 — 설계 주석이 *"업데이터는 교체 가능해야 한다"* 고 이미 적어 두었다.

**(6) 스왑에서야 알던 것을 다운로드 직후에** (`update_effects.cpp` `VerifyDownload`)
`Swap` 은 `payloadNames` 가 staging 에 없으면 거부한다 — **그런데 그때는 이미 제품에 종료를 요청한 뒤**고 사용자는 닫힌 앱을 보고 있다. 0.2.109 가 정확히 그랬다.
→ `VerifyDownload` 에서 **manifest 가 교체 대상 전부를 지명하는지** 검사한다. **디스크를 건드리기 전, 아무것도 닫기 전**이다.

**(7) 게시 전에 거부한다** (`automation/gnlink_check_payload_set.py`, preflight 배선)
기대 목록을 **`product_payload_names()` 에서 읽는다**(옮겨 적지 않는다 — 두 번째 사본이 곧 드리프트다).
- ⭐ **현장을 깨뜨린 그 manifest 로 음성 대조**: `.claude/rel/0.2.109/windows.manifest` → **REFUSING: the manifest does not name GNLinkSetup.exe.** 이 검사가 있었으면 **게시 전에 걸렸다.**

**회귀가 잡은 결함 2건 (둘 다 "통과하고 있었지만 아무것도 검사하지 않던" 것)**
1. `update_effects_test.cpp` 의 post-swap 롤백 케이스가 **불가능한 릴리스를 기술하고 있었다** — `payloadNames` 에 `GNLinkSetup.exe` 를 넣고 manifest 에는 안 넣었다. **게시된 0.2.109 와 같은 모양**이다. 아무도 검사하지 않아서 통과했다. manifest 에 지명하도록 고쳤다.
2. `gnlink_deploy_test.sh` 의 **서명 변조가 변조를 하지 않고 있었다** — `sed 's/^0/1/'` 인데 새 서명은 `8` 로 시작해 **아무것도 바꾸지 않았고**, 그 케이스는 **멀쩡한 릴리스를 게시하면서 "거부한다" 로 통과**했다. 첫 글자를 실제 값과 무관하게 바꾸고, **바뀌었는지 자체를 단정**하도록 고쳤다. 아티팩트 개수도 **manifest 에서 세도록** 바꿨다(하드코딩 9 는 10번째가 생긴 순간 조용히 틀렸다).

**결과**: `update_effects` **203** · `updater_assembly` **49** · `updater_scenarios` **142** · `update_release` **80** · `update_manifest` **85** · `update_relaunch` **107** · `payload_name` **54**, 전부 rc=0. `gnlink_deploy_test.sh` **PASS**. Debug 전체 빌드 rc=0.
**아직 안 한 것 (1·2·3·4)**: supervisor 우선 종료 · captured handle bounded wait · 부모 최종 exit 후 relaunch 판정 · SCM 분기와 AccessDenied false positive. **반례는 실제 windowed parent + console child 가 필요하고 지금 RDP 활성이라 실행하지 않는다.**
**하지 않은 것**: 새 자동 버전 게시 없음 · live 앱 설치·종료 없음 · `git push` 없음 · 영구 Host 껍데기/서비스 재설계 없음.
'''

d = d.rstrip('\n') + '\n' + entry
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok history 501')
