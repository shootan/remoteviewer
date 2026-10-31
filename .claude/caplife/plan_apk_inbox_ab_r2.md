# apk-decoder-inbox — A/B 측정 계획 r2 (r1을 대체; Codex codex_apk_inbox_plan_src_r1.md 보정 반영)

작성: 2026-10-30. 실행은 검증용이 기존 슬롯에서 하고, 이 r2 SOURCE 뒤에 합니다.

## 결정 (Codex)
- **A/B 모두 앱 trace 없이 대칭**으로 둡니다(수용됨).
  - A = 9f9a91c `ke-off.apk` `8c6422fe7104f8272242a9c51a64ac3d7a98989886a855f1d89cbee55f23828d`
  - B = ac587dd `plain-debug-r5.apk` `32bb788f3972cb7b3603d6f0d6907efcd503853dcbfc5b55fadcd427e27579ec`
  - A에 진단을 backport하지 않고, B에 계측 APK를 넣지 않습니다.
- **ApkOn은 쓰지 않는 artifact**입니다(e2 계열 계획은 ApkOff만 설치). 해시만 기록하고 설치하지 않습니다. A `ke-on.apk` `64a02118…`, B `ke-on-lt-r5.apk` `8829254…`.
- **B+trace 진단 run**(8868fb5/79b5c25 계측 APK)은 **별도**로 돌리고, 그 CPU·latency 수치를 trace-off B와 섞지 않습니다. C prototype이 들어간 build는 B+trace라고 부르지 않습니다.

## 고정
| 항목 | 값 |
|---|---|
| 하네스 | r11: `C:\work\gnlink-wt\feat-apk-0233\.claude\e2e`, snapshot `caplife/avd_guard_r11_snapshot`(SUMS `7348ba40…a5cea`), r10 대비 diff `caplife/avd_guard_r11.diff`(`20da6bcc…9c10`) |
| server.js (KE_SERVER_JS) | `C:\work\gnlink-wt\apk-decoder-liveness\apps\directory\server.js` @79b5c25, raw `372e6eb8224217fd894b8423b8d4e235c59aa738f17cface15fc6d41fec31b40` (relay trace에 rxBytes 추가) |
| Host / harness | `GNLinkStreamPinnedSelectTest.exe` `d806e375…`, `remote60_mobile_abr_session_e2e_test.exe` `722bf981…` (feat-apk-0233 build, 이전과 같음) |
| fixture | r10 plan-aware: `KE_PLAN=e2-shoff`이면 Host a만 시작 |
| 환경 | 시험 AVD emulator-5554, SDK adb pin `7035cf5e…`, Settings SH-off, 같은 target 창, 해상도·fps·bitrate 기본값(바꾸지 않음) |

## 실행
- 30 s adb 관찰 → **A** `-Tag ab-a-shoff` → **B** `-Tag ab-b-shoff`
  - 둘 다 `KE_PLAN=e2-shoff`, `KE_WATCH_SEC=120`, `KE_RELAY_TRACE=1`, `KE_SERVER_JS=<위>`, `-AdbSha256 7035cf5e…`
- 순서나 큰 외부 부하가 해석을 바꾸면 그때만 역순 B/A를 1회 추가합니다. 즉시 반복하지 않습니다.
- **조기 종료**: watch 중 첫 종료(keep_end / HOSTS)를 보면 r11은 watch를 그 자리에서 멈추고, sample과 구간 표지를 남긴 뒤 **아무것도 더 조작하지 않습니다**. PC 목록 단계는 NOT RUN이고, 실제 관찰 길이(`watched=ms`)를 남기며 120 s로 환산하지 않습니다.
- watch 동안 앱 로그(video_debug)를 하나도 얻지 못하면 "유지"로 판정하지 않습니다(FAIL "not judged").

## 측정 원천과 정의 (r2)
- **구간**: `SEGMENT startup / watch / list` begin·end에 epoch ms(서로 다른 로그와 맞추는 용)와 run 시각을 남깁니다. watch 길이는 monotonic으로 잽니다.
  - r11은 **첫 영상 관측(select 반환) → watch 시작** 지연을 ms로 기록합니다(`watch begins N ms after the first frame`).
- **부하 샘플**(시작 전 1회, watch 중 10 s마다 monotonic 일정으로, watch 끝 1회):
  - 호출마다 기한이 있습니다(adb 6 s, PowerShell 8 s). 출력은 pipe가 아니라 파일로 받아, 손자 프로세스가 있어도 기한에서 끊깁니다.
  - 샘플마다 자기 길이(`sample_ms`)와 놓친 항목(`miss=[...]`)을 기록합니다. 놓친 값은 `?`이며 0으로 채우지 않습니다.
  - app PSS(dumpsys meminfo TOTAL PSS), app CPU%(top: **header에서 %CPU 열을 찾음**, 형식을 모르면 `?`), 에뮬레이터 loadavg, qemu 누적 CPU ms를 남깁니다.
  - **qemu 식별**: emulator-5554의 console port 5554를 listen하는 **유일한** 프로세스이고, 이름이 `qemu-system*`이며, pid와 시작 시각이 맞아야 합니다. 못 찾거나 둘 이상이거나 시작 시각이 바뀌면 unknown이고 비교에서 뺍니다.
  - **qemu CPU 정의**: 구간 ΔCPU_ms / Δwall_ms = 이 PC 논리 CPU 1개 기준 점유율입니다. 호스트 논리 CPU 수로 나눈 값도 함께 적습니다.
- **Host 로그**(두 arm 같음):
  - `[keyframe] seq bytes`: IDR마다 한 줄이라(r26 SH-off에서 176줄 = 요청 수와 일치) **구간별 IDR 수와 바이트는 정확**합니다.
  - `keyframe-request seq=`: 구간별 unique seq 수입니다. `[keyframe]`(accepted IDR AU)와 구간별로 대조합니다.
  - `[client-present] fps gap…`: client가 보고한 창 단위 값입니다. 창별 p95를 평균해 구간 p95라 부르지 않습니다. 창 값의 분포(최소·중앙·최대)만 적습니다.
  - `wire seq key bytes`는 **표본**입니다(r26: 2969 frame 중 830줄). 전체 video bytes나 IDR 비중 계산에 쓰지 않습니다.
- **relay trace** (79b5c25 server.js, local fixture만):
  - 각 줄(5 s sweep의 snap, close)의 `rxBytes c2h=B/n h2c=B/n`은 flow·방향별로 **도착한 UDP payload bytes / 패킷 수 누적**입니다.
  - close 줄의 1 s bucket에는 `<dir>.rx.<kind>B`가 있습니다.
  - 구간 bytes/s는 구간 경계 시각(epoch ↔ wall0+mono 차)에 가장 가까운 snap 두 개의 차이로 냅니다(5 s 해상도). close bucket이 있으면 1 s 해상도로 냅니다.
  - **UDP payload bytes**(relay)와 **encoded AU bytes**(Host `[keyframe]`)는 다른 양이며 섞지 않습니다. rx만 세므로 sent와 이중으로 합산하지 않습니다.
  - `sendErr`는 socket callback 오류일 뿐, 수신이나 ACK의 증거가 아닙니다.
- **app 로그**(두 arm에 다 있는 것만): video_debug의 in/out, 첫 영상 시간, keep_end·scene, control close입니다. B에만 있는 inbox 계수는 B 쪽 참고값입니다.
- **알려진 사실(r26 SH-off Host 로그)**: 이 fixture의 IDR은 약 1.4–1.6 KB(480x270, 1.5 Mbps)입니다. r26의 inbox 넘침은 4 MiB 바이트 상한이 아니라 **8 frame 개수 상한**에서 일어났습니다. "큰 IDR이 바이트 상한을 친다"는 증폭 가설은 이 환경에 맞지 않습니다.

## 기기 없는 대조 (r2)
- `ke_sample_test.py` **9/9**: 실제 sample()과 qemu_identify()를 stub ka_avd와 fake adb로 돌렸습니다.
  - 알려진 응답에서 PSS / loadavg / toybox `S[%CPU]` header의 37.5 / qemu ms가 정확히 나옵니다.
  - qemu는 listener 0개, 2개, qemu가 아닌 소유자이면 unknown, 유일한 qemu면 그 pid입니다. 시작 시각이 바뀌면 `?`와 miss를 남깁니다.
  - 형식을 모르는 top은 `?`와 miss입니다.
  - 9 s 늦는 호출은 6 s 기한에서 끊기고(6.1 s) miss로 남습니다.
- `relay_trace_test.js` ALL PASS: 길이를 아는 datagram(49 / 1200 B)에 대해 rxBytes가 정확합니다.
- `ke_orchestration_test.ps1`은 r10의 22/22를 유지합니다. r11은 ke_avd만 바꿨고 slot·fixture는 그대로입니다. 새 watch와 조기 종료 경로는 기기 단계라 stub이 덮지 않으며, **기기에서는 아직 실행되지 않았습니다**.

## r3 보정 (Codex codex_apk_inbox_plan_src_r2.md; 이 절이 위 해당 문구를 대체)
- **하네스 r12**: snapshot `caplife/avd_guard_r12_snapshot` (SUMS `f4837b29…932a`), r11 대비 diff `caplife/avd_guard_r12.diff`. 바뀐 것은 ke_avd.py의 `_run` 하나와 ke_sample_test입니다.
  - 샘플 호출의 stdout을 **이 run의 OUT 아래 `sample_io/<pid>-<n>.out`**에 CreateNew로 받고 **보존**합니다. 삭제·덮어쓰기·%TEMP% fallback은 0입니다.
  - `ke_sample_test.py` **10/10**(새 case: 호출마다 이 run의 새 파일, 16개 보존). 시험 폴더도 tempfile 없이 `runs\ke_sample_test-<시각>-<pid>`를 새로 만듭니다.
  - 지난 9/9 실행(r11)은 `tempfile.TemporaryFile()` 기본값, 즉 %TEMP%에 임시 파일을 만들고 닫을 때 지웠습니다. 실행 환경 근거로 기록만 하며, 외부 %TEMP%를 탐색하거나 정리하지 않았습니다.
- **relay rate**: 구간 bytes/s = (구간 끝 쪽 snap의 rxBytes − 시작 쪽 snap의 rxBytes) / (두 snap의 **실제 시각 차**, 각 줄의 mono로 계산). 그리고 구간 경계와 snap 사이의 **경계 오차**(최대 5 s)를 함께 적습니다. close bucket이 있으면 1 s bucket 합과 그 경계 오차를 씁니다.
- **Host IDR 수 ≠ 요청 수**: `[keyframe]` 줄은 Host가 실제로 만든 IDR 전부입니다. selection 시작·bootstrap·주기 key 등 요청이 아닌 IDR도 포함하므로 `keyframe-request` 수와 같을 필요가 없습니다. 둘은 구간별로 따로 적고 대조만 합니다(r26의 176 대 174는 우연에 가까움).
- **유지**: sampled `wire` 줄은 합계에서 제외합니다. `[client-present]` 창별 p95는 평균하지 않고 분포만 적습니다.
