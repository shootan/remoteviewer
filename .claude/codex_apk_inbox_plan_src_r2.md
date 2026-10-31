# APK inbox r2 — 계측 / A/B plan SOURCE

2026-10-30. **79b5c25의 age/relay byte 계측 보수 수용. trace-off A/B 계획 수용. 다만 r11 sampler의 임시 출력 파일 경계 한 건은 보수 후 실행한다. 현재 그대로 기기 A/B를 시작하지 않는다.** 정책 변경0 상태를 유지하며 APK0.2.33 출하 판정은 아직 보류다.

## 고정

- 계측 HEAD `79b5c25744252f20a482fe6d6ddf51e38c99f85e`, 부모8868fb5, tracked clean(`apk_out/` 미추적), diff --check 통과.
- 보고 SHA-256 `747363171390a97b90553d9e3c2fe92effffb6e451217aee720a05bf5eb6844a`.
- design r2 `a44dd271798ad12800e474bf86d132cfcdb2d1ded3395a757957b1396e8d1ff8`.
- plan r2 `b1607fc5cfca710c33a629c814ca4493c5c338c3d1b3ec72b53351c1c2b47c27`.
- r11 snapshot SUMS `7348ba40f2fe027353afce79dacabc37ddc7183636e362182f23b2a9e11a5cea`,diff `20da6bcc6f9df385b46020a6241bc787209eeb5623a1d370840c573d73809c10`.
- snapshot13파일의 bytes/hash를 직접 대조했다.

## 계측 수용

AU gen+seq를 확인한 ring record로 enqueue 시각을 찾고, replay/unmatched를 따로 세는 방식은 다른 frame의 시각을 쓰던 지적을 해결한다. ring이 잃은 표본을0ms로 채우거나 전체 age 분포로 확대하지 않는다. 비교/회귀는 실제 고유 AU seq를 사용한다.

Codex가 고정 제품 sink/fake NDK에 실제 서로 다른 AU seq의 held IDR과 후속 delta를 넣어 독립 실행했다. 약284ms 기다린 IDR에 대해:

```text
held=1 queued=1 ... observed elapsed=284ms
ageQueueAU max=284ms n=1 ... unmatched=0 replay=0
```

앞8868은 같은 형태의 대기에서33ms로 오귀속했다. 현재의 실제 AU age 보수를 수용한다. `.claude/codex_decoder_liveness_review/inbox_age_r2.py`, `inbox_79b_age/{candidate_probe.cpp,build.log,candidate_probe.log}`. build/probe rc0, 로그 SHA-256 `34da99552811f4e2d61c002088b3fd8e45b0f69c6fa6f136ae227b6aa884ed13`.

admitted inboxHw와 trim 전 inboxPeakPre, rxLockScopeMax, 복합 createCfgStart 이름을 구분한 것도 수용한다. scope elapsed를 정확한 mutex hold로 다시 해석하지 않는다. 원인별 keyWant의 항상 존재/증가 비용은 이전 판정대로이며 “모든 진단이0비용”이라고 쓰지 않는다.

relay trace의 rx 한 곳에서 msg.length를 누적/1초 bucket에 더하는 보수는 source상 방향·단위가 명확하다. packet 수와 byte 수, c2h/h2c를 나누고 sent와 중복 합산하지 않는다. flag off의 전달 정책은 그대로다. 길이49/1200B·회귀/NDK 결과는 제출 보고에 귀속하며 Codex가 relay suite/기기 전체를 재실행했다고 하지 않는다. 이 서버 코드는 **local 계측 fixture 한정**이고 운영 server 게시 승인이 아니다.

## 남는 r11 / N1 — default TEMP의 자동 삭제 파일

`ke_avd.py`의 `_run()`(:378 부근)은 `tempfile.TemporaryFile()`을 **dir 없이** 호출한다. r11 slot/test에 process TEMP/TMP 또는 tempfile.tempdir를 승인된 run으로 고정하는 코드도 확인되지 않는다. 따라서 기본 TEMP에 파일을 만들고 close 때 자동 삭제할 수 있다. timeout을 pipe 대신 file로 해결한 방향은 맞지만 파일 경계가 고정되지 않았다.

이번 세션의 AGENTS 삭제 제한과 canonical CLAUDE.md 「삭제 후보」의 **“유일한 상시 예외…직접 만든 SLOT_REQUEST.txt…%TEMP%…으로 넓히지 않는다”**를 따른다. [해당 정책](D:/remote/remote/.claude/worktrees/main-stability-integration/CLAUDE.md:287). 새로운 삭제 허가를 사용자에게 물을 사안으로 만들지 않고, 보존 방식으로 고친다.

**필요 보수:** 검증된 자기 run/OUT 안에 CreateNew(exclusive)·유일 이름의 출력 파일을 만들어 subprocess stdout을 연결하고, 종료/timeout 뒤 close해 **보존**한다. default TEMP fallback·재사용/덮어쓰기·자동 정리는 하지 않는다. 기기 없는 ke_sample_test도 같은 경계에서 실행한다. 실패 경로에서도 root 밖에 만들거나 지우지 않는 대조와 새 snapshot/hash를 제시한다. 실제 sampler 동작/metric 정책을 다시 크게 바꿀 필요는 없다.

이미 보고된9/9에서 `_run`을 실행했다는 사실과, 실제 TempRoot/생성·close/삭제 결과를 기록했는지는 구분한다. 실행자에게 기존 로그/환경 근거로 범위를 정리하게 하되 **저장소 밖 TEMP 재탐색·정리·삭제는 하지 않는다.** 현재 파일이 없다는 사실로 과거 생성/삭제를 소급 확정하지 않는다. 이번 Codex는 이 helper/ke_sample_test를 실제 subprocess로 실행하지 않아 해당 자동 파일 경계를 추가로 건드리지 않았다.

## A/B plan 판정

기존 앱 trace 없는 A/B는 그대로 수용한다. 직접 파일 hash도 다음과 일치했다.

- A9f9a91c `8c6422fe7104f8272242a9c51a64ac3d7a98989886a855f1d89cbee55f23828d`.
- Bac587dd plain `32bb788f3972cb7b3603d6f0d6907efcd503853dcbfc5b55fadcd427e27579ec`.
- local relay server raw `372e6eb8224217fd894b8423b8d4e235c59aa738f17cface15fc6d41fec31b40`.

r11의 qemu console-port 소유자/pid/start 확인·unknown 처리, 표본 deadline/누락 기록, monotonic watch·select 반환부터의 지연 기록, 조기 종료 후 조작 중단, app log 부재를 무종료 PASS로 삼지 않는 보수는 수용한다.9개 sampler 대조는 작업용 보고의 범위로 유지한다.

측정은 다음처럼 표현한다(문구 보정만으로 별도 왕복 불필요).

- 120초는 **watch begin 이후**의 실제 관찰 구간이며 first-present와의 지연을 별도 적는다. 마지막 sample을 얻는 데 걸린 시간과 경계 시각을 섞어 정확히120초짜리 같은 분모였다고 주장하지 않는다. 표본이 밀리면 실제 구간/누락을 남긴다.
- relay snap 차이는 실제 두 snap 시각 차이를 분모로 쓰고 watch와의 경계 오차/해상도를 표시한다. bucket도1초 해상도다. 구간이 다른 전체 flow byte와 startup/steady 값을 혼용하지 않는다.
- Host `[keyframe]`은 accepted encoded IDR AU이고 wire 도착 보장은 아니다. bootstrap/주기/다른 이유 IDR이 있으므로 **IDR 수가 viewer keyframe-request 수와 반드시 같아야 하는 것은 아니다.** unique 요청·IDR·reasons를 분리한다. sampled wire를 합계 분모로 쓰지 않는 보정은 수용한다.
- IDR encoded bytes와 relay UDP payload bytes는 다른 층이다. 전체 encoded video bytes가 없으면 encoded IDR 비중을 정확한 값으로 계산하지 않는다. 창별 p95 분포와 구간 p95를 구분한다.
- r26의1.4–1.6KB IDR/8 frame overflow 관측으로 **그 run의 byte-cap 증폭 가설을 철회**하는 보정을 수용한다. 다른 해상도/장면의4MiB 초과 가능성까지 일반 배제하지 않는다.

파일 경계 보수의 작은 diff/기기 없는 대조/새 snapshot만 확인하면 기존 슬롯 조율 아래 **A→B, SH-off/120초 각각1회**를 잇는다. 그 전에는 현재 r11로 기기 run을 시작하지 않는다. 다른 앱/LDPlayer/adb 종료·전역 설정 변경·운영 계측 배포는 하지 않는다. old-apk-lane fixture-only 등 기존 필수 시험과 슬롯을 조율한다. C 제품 정책은 이 A/B 결과와 fixed SOURCE 뒤로 두며 이 두 arm에 넣지 않는다.

## 설계 r2의 구현 전 보충

seq/wrap·중복/재정렬과 실제 commit 회복, 같은 episode 재개, descriptor의 held/accepted 검사 방향은 앞 결정을 반영했다. 아래를 **C source 계약에서 구체화**한다. 이것을 A/B 실행의 추가 blocker로 새로 세지는 않는다.

1. 현재 문서의 “4번째 요청 뒤에도 commit 안 되면 exhausted”는 즉시 소진 판정으로 구현하지 않는다. **마지막 요청이 답을 기다릴 유한 window**를 명시하고, 그 안의 IDR 도착/queue 결과를 처리한다. exhausted가 늦은 유효 응답/명시 retry를 어떻게 다루는지도 분리한다. 첫3개의 backoff만 있고 마지막 응답 기회가 없는 상태 machine은 안 된다.
2. held/pending에서 원인을 합친 경우 descriptor도 bounded 원인 집합으로 보존한다. 한 원인이 무효가 되어도 다른 유효 원인이 있는 교환을 통째로 버리지 않는다. 송신 직전 validity check→교환 시작 표시를 invalidation과 정합되게 하고 state lock을 network 동안 잡지 않는다. 이미 시작한 교환/transport 재전송과 새 논리 retry를 구분한다.
3. non-Android/default sink의 항상 유효 구현을 Android adapter의 실제 owner/generation 취소 대신 쓰지 않는다. 새 pick/Bind/park/session 종료의 대조가 실제 adapter와 accepted action 경로를 지나야 한다.

이번 Codex는 source/보존 자료·APK hash를 읽고 자기 제품 sink/fake NDK age 대조만 실행했다. 실제 Host/Viewer/AVD/adb·GPU/OS 입력·설치·운영·삭제·제품 변경·commit/push는 없다. 미추적 검토 기록만 작성했으므로 제품 commit은 만들지 않았다. 사용자 현재 조치 없음.
