# apk-decoder-inbox C r3 — SOURCE 패킷 (2026-10-30, 정본 codex_apk_inbox_c_src_r2.md N2 잔여)

## 1. 고정
- worktree `C:\work\gnlink-wt\apk-inbox-c`, **HEAD 2b492ef**(d50415e 위 1 commit, tracked 변경 0).
- 제품 변경은 `decoder_key_request.hpp`의 `Merge` 하나입니다. `native_video_client_session.hpp`의 `TestPublishKey`는 `REMOTE60_CLIENT_SESSION_TEST_HOOKS` 전용입니다.
- diff:
  - r3만: `caplife/c_inbox_r3_delta.diff`(d50415e..2b492ef, `650ed924…`, 6파일 +254/−8)
  - 전체: `caplife/c_inbox_r3.diff`(79b5c25..2b492ef, `05e50767…`)
- SUMS: `caplife/c_inbox_r3_SHA256SUMS.txt`(`722f6f1b…`, 18파일 2b492ef blob).
- 실기, push·서명·게시, worktree 밖 생성은 모두 0입니다.
- APK는 다시 빌드하지 않았습니다. 독립 빌드는 검증용 몫이며, 고정 기준은 2b492ef의 깨끗한 tree입니다.

## 2. 보수
- **문제**: 각 producer(RX, control pass)는 descriptor를 ledger lock **밖에서** 취득합니다. 그래서 과거 descriptor가 나중에 게시될 수 있습니다.
- **원칙**: `Merge`는 게시 순서를 무시하고 **sink의 발급 순서**를 따릅니다. 발급 순서의 기준은 셋입니다.
  - `sinkEpoch`: `keyEpoch_++`로 증가합니다.
  - 같은 epoch 안의 recovery `episodeId`: episode의 `id_++`로 증가합니다.
  - owed `owedToken`: gate의 `owedToken_++`로 증가합니다.
- **원인별 처리**:
  - sink-scoped(recovery·codec-up·abort): 더 새 epoch 쪽이 남습니다. 오래된 쪽의 sink-scoped 원인은 **버리며, 새 epoch로 승격하지 않습니다**.
  - 같은 epoch의 recovery: 더 새 episode가 남습니다.
  - owed: 더 새 token이 남고, 오래된 쪽의 owed bit는 버립니다.
  - pipeline/legacy: 항상 보존합니다.
- lock, limiter, wire, §8 예산은 바꾸지 않았습니다.

## 3. 대조 — 같은 tree에서 **보수 전 Merge로 먼저 실행(전부 FAIL)한 뒤 보수(PASS)**
보수 전 실행 출력은 `build\r3_pre\*.txt`에 있습니다(exe sha 앞 16자: merge `c37dd584…`, c `10a2d758…`, session `26f7d0f4…`).

d50415e 헤더를 neg 디렉터리에 두는 방식은 쓰지 않았습니다. `decoder_key_request.hpp`는 src 헤더들이 `""`로 include하기 때문에, 한 바이너리 안에 옛 Merge와 새 Merge가 섞이기 때문입니다.

| 시험 | 보수 전 (d50415e Merge) | 보수 후 |
|---|---|---|
| `decoder_key_request_merge_test`(신규, pure) | **5 FAIL**:<br>• 새 epoch3 ← 늦은 epoch2: epoch=2<br>• 같은 epoch 새 ep3 ← 늦은 ep2: ep=2<br>• 혼합<br>• 승격(epoch2 recovery가 epoch3에 붙음)<br>• owed token 6 | 8/8 |
| inbox_c **[C12]**(제품 sink, 실제 상태) | **2 FAIL**:<br>• (1) ep 3/4 → merged ep=3 valid=0<br>• (2) 실제 abort descriptor 두 개(epoch 5/7) → merged epoch=5 valid=0 | 44/44<br>(merged ep=4 valid=recovery / epoch=7 valid=abort, 발급 순서도 유효) |
| session **[S10 accepted]**(새→늦은 옛 descriptor, 둘 다 채택·coalesce, scheduler pass를 잡아 둠) | droppedStale+1, host 수신 0 | host 1회, droppedStale+0 |
| session **[S10 held]**(pipeline으로 interval 소진 뒤 둘 다 held에서 merge) | droppedStale+1, host는 pipeline 1회뿐 | host 2회(held 요청 2005 ms), droppedStale+0 |

- S10은 S9(publish 원자성)와 별개입니다.
- S10의 "옛" descriptor는 sink의 **현재 epoch−1**로 만들었습니다. 유효성은 실제 sink가 판정합니다.
- 실제 sink가 만든 descriptor 쌍은 C12에 있습니다.

## 4. 관련 회귀 (보수본, 전체 tree 빌드 error 0, 각 1회; `caplife/c_r3_regress.txt`)
- 14 시험 모두 exit 0입니다: chain_inbox 51, r2_contrast 2, inbox 12, inbox_c 44, session 26, owed_key 32, geometry 10, binding 17, decode_stats 42, liveness 68, session_liveness 9, pending_slot 21, prelogin_select 60, idr_recovery 49.
- `decoder_key_request_merge_test` 8/8은 위 표에서 따로 실행했습니다.
- Codex 지시대로 ×5 반복은 하지 않았습니다.

## 5. 한계
- 실제 두 thread를 그 순서로 멈춰 wire 소실을 관찰한 시험은 아닙니다. S10은 test thread에서 게시 순서를 정해 제품 RequestKey, held/accepted, limiter를 지나게 한 것입니다.
- §8 (a), N1, N3은 r2 그대로입니다.
