# apk-decoder-inbox 역순 B→A SH-off 120 s — 결과 (2026-10-30, 정본 codex_apk_inbox_ab_r3_result_review.md)

## 실행 전 확인 (실제 파일로 확인)
- 배치 `codex_decoder_liveness_review/ab_shoff_batch_reverse_r4.ps1` = 4b3f274c…6df9입니다.
  - reviewed r3(6c519f11…)와 비교하면, 줄끝을 정규화한 뒤 차이는 **추가 2줄**뿐입니다: 주석 1줄, `$arms = @($arms[1], $arms[0])`.
  - 그냥 `git diff`로 보면 파일 전체가 바뀐 것으로 나옵니다. 줄끝 형식이 달라서입니다.
- E r12 snapshot 대비 불일치 0, server.js 372e6eb8…, A 8c6422fe…, B 32bb788f…, adb 7035cf5e…. 모두 일치합니다.
- 슬롯은 비어 있었고, GNLinkStream pid 27044(사용자 것)만 돌고 있었습니다.

## 판정
| 순서 | arm | run 폴더 (`C:\work\gnlink-wt\feat-apk-0233\.claude\e2e\runs\…`) | rc | 판정 |
|---|---|---|---|---|
| 1 | B (ac587dd plain-debug-r5) | `ab-b-shoff-20261030021750-10004` | 0 | **PASS** watched=120746ms, 8/8 PASS |
| 2 | A (9f9a91c ke-off) | `ab-a-shoff-20261030022255-36552` | 1 | **FAIL** watch +3.0 s에 `keep_end reason=peer_lost`(watched=3049ms), 5/6 PASS |
- batch는 A의 rc 1에서 끝났습니다. 남은 arm은 없었습니다. 지시대로 추가 반복은 하지 않았습니다.
- 정리:
  - 두 arm 모두 `job_run residual=0 forced=0 ended_confirmed=1`, node ended, a exit 0입니다.
  - 지금 남은 테스트 프로세스는 0개입니다(pid 27044만 있음).
  - 슬롯은 `slot removed=True`로 해제했습니다. 해제 시각은 02:25:21(sh_run 출력 mtime 02:25:20.925)입니다. SLOT_REQUEST.txt는 없습니다.

## A (2회째) 경과 — 로그 사실
- Host: `window-select … applied=1` 02:25:03.067, `keyframe-request seq=1` 03.378, `udp control session ended … reason=peer-lost` **02:25:12.181**.
  - 1장 절의 판정 규칙(Host의 맨 앞 control 메시지 하나에 약 6 s 동안 ACK 없음)을 적용하면, 그 메시지의 첫 송신은 약 17:25:06.2Z로 추정됩니다(NACK가 없었다는 가정).
- 앱 logcat (pid 26437). sink 호출 thread 26822는 A 구조상 receive thread입니다(`peer_lost_source_r1.md` 3절).
  - 17:25:03.862 `updated codec config`
  - 04.467 `CCodec allocate`(codec thread 27100)
  - 04.830 `Created component`
  - 06.108 `MediaCodec started`(26822): codec 생성 안에 약 2.2 s 있었습니다.
  - 06.505 `queued … count=1` → 12.983 `count=31`: 생성 뒤 6.5 s 동안 30 frame(약 4.6 fps)이 들어갔고, Host 송신은 30 fps였습니다.
- relay trace(connect f0059a54):
  - 17:25:07.3Z→12.3Z에 h2c +195166 B / +307 pkt, c2h +884 B / +17 pkt.
  - 17:25:17.324Z snap의 ageMs는 h2cRx=5154, c2hRx=4345입니다. 그러므로 h2c 마지막 도착은 약 12.17Z(Host 판정 시각과 같음), c2h 마지막 도착은 약 12.98Z(Host 판정 **뒤**)입니다.
  - relay 수치는 relay가 받은 양입니다. relay send callback은 Host 수신 증거가 아닙니다.
- 1회째(01:50)와 같은 모양입니다: receive thread에서 codec 생성, 이후 영상 소비가 송신보다 낮음, Host peer-lost, 판정 뒤에도 c2h 도착.
- 해석 범위: A 경로(receive thread의 codec 처리)와의 **연관성 강화**까지만 말할 수 있습니다. APK 단독 원인이나 relay 무관은 확정하지 않습니다. A는 2회 모두 B와 같은 boot(boot_id 37551b86…)의 같은 하네스에서 실패했습니다.

## B 측정 (trace off; 구간 길이 = SEGMENT epoch 차, watched와 구분)
| run | 구간 | SEGMENT 길이 | Host `[keyframe]` 수 / bytes | `keyframe-request seq=` 수 | 요청 율 (수 / SEGMENT 길이) |
|---|---|---|---|---|---|
| 1회째 B 01:52 | startup | 124.567 s | 19 / 30664 | 15 | 0.120/s |
| | watch (watched=120398ms) | **123.007 s** | 229 / 380286 | 229 | **1.862/s** |
| | list | 53.757 s | 93 / 154061 | 93 | 1.730/s |
| 2회째 B 02:17 | startup | 107.736 s | 11 / 17883 | 8 | 0.074/s |
| | watch (watched=120746ms) | **122.675 s** | 163 / 269846 | 163 | **1.329/s** |
| | list | 60.867 s | 99 / 163865 | 99 | 1.626/s |
- relay watch 구간(5 s snap, 실제 mono 차로 나눔):
  - 1회째: 125.012 s, 경계 오차 −2335 / −330 ms, h2c 44250 B/s, c2h 625 B/s.
  - 2회째: 125.012 s, 경계 오차 −868 / +1468 ms, h2c 44071 B/s, c2h 596 B/s.
- **watch 구간 안의 마지막 video_debug**:
  - 1회째 16:57:10.123 `in=1750 out=1747`.
  - 2회째 17:21:50.520 `in=2089 out=2087`.
  - **정정**: 지난 보고(result_ab_shoff_r3.md)의 `in=2231 out=2227`은 list 구간(16:57:51)의 값이었습니다. watch 품질 값이 아닙니다.
- B는 두 번 모두 120 s를 유지했고 Host peer-lost는 0입니다. keyframe 요청 폭주(watch 1.3–1.9/s)는 두 번 모두 있었습니다.

## 측정 방법
- `caplife/ab_r4_measure.ps1` (7f020f17…)
  - Host 로그 시각(현지)을 epoch로 바꿔 SEGMENT [begin, end)에 넣어 셉니다.
  - relay는 main connect에서 경계에 가장 가까운 snap 두 개를 고르고, 그 mono 차로 나눕니다.
  - video_debug는 앱 로그(UTC) 시각이 watch [begin, end] 안에 있는 것만 씁니다.

## 결과 파일 sha256
B (`ab-b-shoff-20261030021750-10004`)
```
67e0943eeac510858e8e623a697287f258db04756d043150c8358d0909412e73  slot.txt
06bbec02d1a86bf377a369d9dd229fc31066326ec7a8949fa69bb52b312b3b40  fixture_result.txt
e1ef59d0afced8895dde03cf838a79d6b9049476809c8eec653cf1cafbc9f9fc  avd\steps.txt
e264179f7c53bfe7bbeb4ff2797a8c22445a9bc3a6811c41965653da8c726790  avd\result.json
8176514f402bccd6a3f8d6984d9eaed461bf9c730c58e443736a0d1171db9ca6  avd\app_final.log
00c959dcc3eb22285d4a076b9da0dea951d746dbf0c459a0c762d99ec3fec916  a\directoryhost_host.log
ff15bcb244af8741b18a248ec670c26a50030998ac032b820a62e4f3caad2feb  dir\server.stdout.txt
6c90abde67d1fcadf7b63c368f2b328b4e43b1809fe50b9ced8d9d176fb8a728  avd\logcat.txt
```
A (`ab-a-shoff-20261030022255-36552`)
```
e88131b8471e31e1ce0f700e78c9b846c0fcdd1111c095d274a512b42f366f58  slot.txt
eb991766308b3e215583510db8d62a4e7b9b789ba621ab2e94010417fcbb263f  fixture_result.txt
21cea4c001da8d08d02acc18c00a0758fc91c4cb51c92f4b519df8be664b1e8f  avd\steps.txt
d9035dd6425f28db1b38b51afaf27212cd90b5f7f301d34a7123b4458b4fa205  avd\result.json
64261dc92b14812be57fae2d3abaa64192ed2763029a70c8fa2c8d3cee9e70da  avd\app_final.log
521cb701f5f151c0bb944001c47060263efcd984f7e0bdc3e130b538b4ac196e  a\directoryhost_host.log
58bad4e36f3adf2580043e52f036061a89da774c209a959f1999569e13ed04e6  dir\server.stdout.txt
f8de4c93d8b55f1315eb34bcdcf7a3bd59a2c48586d6ac1d61ed85d86f9ca16d  avd\logcat.txt
```
```
c79eeeb05be77e371b2480963a8cebf426373b5d3a07ea785706450dddf11656  .claude\caplife\sh_r2\ab-shoff-r4-reverse.log
7f020f17e957dd43068c958dca9f214aba8c6b88d15e7532dfb85c66aeb7773b  .claude\caplife\ab_r4_measure.ps1
```

## 계획 정합 (2026-10-30, Codex codex_apk_inbox_next_run_decision_r4.md (b)안)
- (b)안은 역순 B→A 대신 B 단독 1회입니다. 그 지시가 왔을 때 이 역순 batch는 **이미 끝나 있었습니다**(02:17:15–02:25:21). 지시대로 다음과 같이 셉니다.
  - 02:17 B(`ab-b-shoff-20261030021750-10004`)를 **B2**(두 번째 trace-off 120 s 표본)로 셉니다. B1은 01:52 B 단독입니다.
  - 같은 batch의 A(02:22)는 (b)안 기준으로는 계획 밖에서 실행된 것입니다. 결과는 기록으로만 남깁니다. 추가 A/B 실행은 0입니다.
- B1과 B2는 C 이전 기준선으로만 씁니다. 완전 정상이라는 뜻도, 출하 판단의 근거도 아닙니다.
- 이 파일의 "A 구조상 receive thread"는 ACK 경로를 말합니다. 정정된 범위는 `peer_lost_source_r1.md`의 정정 절에 있습니다(Tick은 worker Read 대기에서도 돎, A/B 차이는 27d5966 하나가 아님).