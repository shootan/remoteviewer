# -*- coding: utf-8 -*-
import io

p = 'docs/작업목록.md'
d = io.open(p, encoding='utf-8').read()

old = u'''0.0.8 [ ] **고아가 끝내 안 나가면 그 업데이트 시도는 실패한다** (알려진 좁은 한계, 수정 안 함) — 창 없는 자식의 부모가 이미 종료됐으면 요청할 상대가 없어 **기한 대기 → 미종료 시 abandon** 이다. 사유는 `orphaned windowless pid N did not exit … nobody to ask` 로 구분된다. **정상 경로에서는 생기지 않는다** — `host_app_main.cpp:470` 이 Host 종료 시 Stream 을 명시적으로 `TerminateChild` 한다. 실체는 **드문 경합**. `TerminateProcess` 승격은 설계 변경이라 범위 밖 (history #512)'''

new = u'''0.0.8 [~] **고아 Stream 이 포트를 쥐어 업데이트를 막는다 — 수정 완료, 실기 미검증** (`ecf340c`)
  - ⚠️ **앞서 "정상 경로에서는 생기지 않는다 / 드문 경합이라 기록만" 이라고 적은 것은 틀렸다.** 09-11 에 **두 번** 실제로 생겼고 기기를 못 쓰게 만들었다: ack 뒤 1초에 생성된 Stream(22204)이 부모(22544)보다 오래 살아 **UDP 43000·3478 을 쥐었고**, 새 Host 가 `udp bind failed on every candidate port` → `code=3` → `streak=36`. 복구는 사용자가 고아를 손으로 죽여서 됐다.
  - 수정(`ecf340c`): **인수인계 exit 이 `DestroyWindow` 전에 자식을 정리하고 결과를 기록**한다. `Stop(outDetail)` 이 자식 소멸을 **확인**하고, 남았으면 *"다음 호스트가 포트를 못 잡을 수 있다"* 고 경고한다. 광역 kill·`TerminateProcess` 승격·아키텍처 변경 없음.
  - 회귀: `update_stop_process_test` **28 PASS**. 신규 4건은 **`(static)` 배선 검사**이며 **실제 Host 거동 증명이 아니다** — 그건 실기 재시험의 몫.
  - 남은 것: **실기 재시험에서 "Host 1 · 고아 0" 확인** (0.0.9)
0.0.9 [ ] **진입점 포함 인앱 재시험** — 트레이 클릭 → 핸드오프 → ack → Quiesce → Swap 10 → Register → RequiredHealth → Commit → Optional 전 구간, **Client 를 켜둔 상태**로 5항목(정지 열거·WM_CLOSE 수신·실제 종료 / 재기동 / **토큰 승격 여부 `IsElevated=false`·integrity Medium** / 교체 바이트 / 안 닫힐 때 사유) + **최종 상태 Host 1 · 고아 0**. ⚠️ **안전한 bootstrap 방식 확정 전에는 실행하지 않는다** — 실패 시 기기가 불능이 될 수 있다 (history #516)
0.0.10 [ ] **업데이터 수명 기록 3건**(별건) — 11:48 업데이터 **트리거 정체**(Host·Client·작업용 모두 아님) · **`result:` 줄 부재**(= `effects.run()` 미반환 **가능성**, 확정 아님) · `CREATE_BREAKAWAY_FROM_JOB`(`updater_main.cpp:171`) **성공 시 무기록**. 성공 경로가 판정 줄을 남기지 않는 것 자체가 결함 (history #512·#516)
0.0.11 [ ] **0.2.114 Client 실팝업 미검증** — 로그인 후 확인 배선(`6d1d422`)은 **정적 배선 검사까지만** 증명됐다. 실제 UI·인증 완료 → 팝업 → worker 동작은 미검증이고, 사용자에게 "이제 뜹니다" 로 보장하지 않는다 (history #514·#516)'''

assert d.count(old) == 1, 'anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok')
