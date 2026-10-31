# apk-decoder-inbox A/B SH-off 120 s — 결과 (2026-10-30, 정본 codex_apk_inbox_plan_src_r3.md 실행 승인)

## 실행 전 확인 (실제 파일로 확인)
- E 하네스 13개 파일 = r12 snapshot, server.js 372e6eb8…, A ke-off 8c6422fe…, B plain-debug-r5 32bb788f…, Host d806e375…, harness 722bf981… 모두 일치.
- 배치: reviewed `codex_decoder_liveness_review/ab_shoff_batch_reviewed_r3.ps1` (6c519f11…).

## 판정
| arm | run 폴더 | rc | 판정 |
|---|---|---|---|
| A (9f9a91c ke-off) | `C:\work\gnlink-wt\feat-apk-0233\.claude\e2e\runs\ab-a-shoff-20261030014717-45932` | 1 | **FAIL** E2-SHOFF 120 s 유지: watch +5.0 s에 끝남(`watched=4969ms`). 5/6 PASS, 19 NOT RUN. 배치가 첫 nonzero rc에서 멈춰 B는 그 배치에서 NOT RUN |
| B (ac587dd plain-debug-r5), 단독 1회 | `…\runs\ab-b-shoff-20261030015253-2608` | 0 | **PASS** 120 s 유지(`watched=120398ms`). 8/8 PASS, 18 NOT RUN |

- B 단독 배치 `caplife/ab_shoff_batch_b_only_r3.ps1` (75fb1cd1…): reviewed r3에서 **arm A 항목 2줄만 삭제**하고 주석 1줄을 더했습니다. 나머지(adb 30 s 관찰, pin, env, rc 정지)는 같습니다. 이 파일은 Codex 검토를 받지 않았습니다.
- **A/B 비교는 성립하지 않습니다.** A의 watch가 5 s뿐이라 120 s 구간 수치를 대조할 수 없습니다. 각 arm은 1회씩만 돌렸고, 반복하지 않았습니다.

## A 실패 경과 (로그 사실만, 원인은 미확정)
- Host `[control] udp control session ended epoch=1 resumed=0 reason=peer-lost` 01:50:17.775. 그 뒤 stream inactive, capture detached.
- relay trace를 보면 h2c 마지막 rx는 약 16:50:17.76Z, c2h 마지막 rx는 약 16:50:18.40Z입니다. Host가 peer-lost를 판정한 **뒤에도** c2h가 relay에 0.6 s 더 도착했습니다. 둘 다 같은 PC 시계입니다.
- 앱 `[keep_end] reason=peer_lost` 16:50:19.756(UTC), 직전 video_debug는 `in=47 out=47`입니다.
- 이전에도 c2h 공백이 있었습니다. 16:50:09.670 snap의 c2hRx age가 2724 ms라, select 직후 약 2.7 s 동안 c2h가 없었습니다.
- A 전체 Host `[keyframe]` 4개(initial / stream_active / selection / viewer 1)와 keyframe-request 1개. A에서는 요청 폭주가 일어나기 전에 끝났습니다.
- 에뮬레이터 부하: watch+0s에 loadavg 8.16(4 vCPU), app CPU 46.1%.
- 정리: `job_run residual=0 forced=0 ended_confirmed=1`, node ended, a exit 0. pid 40812/17136/35012/45968 모두 종료. adb listener pid 25608 그대로, boot_id 37551b86… 그대로. 이것을 확인한 뒤 B를 단독으로 돌렸습니다.

## B 측정 (trace off, 구간 = SEGMENT epoch)
| 구간 | Host `[keyframe]` 수 / bytes | `keyframe-request seq=` 수 |
|---|---|---|
| startup 01:53:02.675–01:55:07.242 | 19 / 30664 | 15 |
| watch 01:55:07.467–01:57:10.474 | 229 / 380286 | 229 |
| list 01:57:11.175–01:58:04.932 | 93 / 154061 | 93 |
- watch 중 요청은 약 1.86/s입니다. r26의 요청 폭주가 B(C 전)에서도 그대로 보입니다.
- relay(connect d59caff9) watch 구간:
  - 사용한 snap은 16:55:05.132Z(경계 오차 2.335 s)와 16:57:10.144Z(경계 오차 0.330 s)이고, mono 차는 125012 ms입니다.
  - h2c Δ 5531773 B → 44250 B/s, c2h Δ 78160 B → 625 B/s (UDP payload rx).
- 앱(마지막 video_debug 16:57:51): `in=2231 out=2227 gateQueueDrops=0 ptsReanchor=312 ptsFallback=311 stale=25`.
- sample: 13개(watch 10 s 간격 + 끝), miss 0. app CPU 62.8–162%, PSS 68–73 MB, loadavg 7.4–8.7. `sample_io` 파일 70개 보존.
- 정리: `job_run residual=0 forced=0 ended_confirmed=1`, node ended, a exit 0. Host peer-lost 0.

## 슬롯
- 두 슬롯 모두 sh_run.ps1 CreateNew로 잡았고 `slot removed=True`입니다. 해제 시각(sh_run 출력 mtime 기준)은 A 배치 01:50:27.3, B 단독 01:58:07.8입니다. 지금 SLOT_REQUEST.txt는 없습니다.

## 결과 파일 sha256
A (`ab-a-shoff-20261030014717-45932`)
```
fc748012dbdecc464fba85fb10522e0731e19af82f9098e965dd17b440b5cd4d  slot.txt
a1b4a098da554559966376fdc13a8c01d0f6eeaca4b9af6a31e984e367e2786c  fixture_result.txt
465338bc7010c6880d19ca4c863b4c064c14145de397c7ebdfe45d2bc62f6e96  avd\steps.txt
4d0d49f02fee2248fed495b668073244e95b7817c79f026bd7a011e0aa9cf7bf  avd\result.json
9645e489e4602b8ae72b3a794ee34b350200e6fdfa4ccf2a837e7ae1aedf5806  avd\app_final.log
40cf83cfccb6fd877f026284308735db6ea8cf80b1cbbf88644af1aa3ce4476b  a\directoryhost_host.log
8c195f3674ea12762a840e5737ff2e9b39dc2bd02655437ac290c51bbfdeca26  dir\server.stdout.txt
c028315e16b68901c577e787062378f86dc92f599b831fd5109b62745999b94a  avd\logcat.txt
9e7d6afbf0a5307b038b7ec3fc04af0464a8c18600d6da555b26a2860ba403a4  .claude\caplife\sh_r2\ab-shoff-r3.log
```
B (`ab-b-shoff-20261030015253-2608`)
```
3c11918fff55587cfa1f3af85ef04c0b3e014fa9d64a265982619c2d39ea85f9  slot.txt
d245e390807f1a21b31e773df16b7cea961998bad527fda20896a67349d42476  fixture_result.txt
cf42d37f75a2510166105ef51e8ad1aa78d1db222f69c4fb9d1f225f43315801  avd\steps.txt
d84882684aab5ad695971233954087fcf7064777ce7d4211eba7e070349abc01  avd\result.json
9c6bb4b91d0bcf69212fe03eda94570e6e1f692e7f94973c75888580128ca1bb  avd\app_final.log
d092d212e8cb63717054aba8e5aff62ec487d8fe398ee8a0aa779b2c24404b00  a\directoryhost_host.log
5824fc78431186f297b2a294747e815d1fe83222cc69474c6e974b6ed7bd2d77  dir\server.stdout.txt
2198b521644be889ebe68a51945f3bd5403a7b1fe8a5a8289cf807012fd310de  avd\logcat.txt
955599180a2cc51d42967a6f394e8f1f4733e2e66a74b0bdb03e58f0b77b8b3b  .claude\caplife\sh_r2\ab-shoff-r3-b-only.log
75fb1cd1488aacd06e488833ec043173a56b7e5276874693539b0ba52fca5e6d  .claude\caplife\ab_shoff_batch_b_only_r3.ps1
```

## 다음 결정 (Codex/검증용)
- A의 peer-lost가 A APK 고유인지, 부하에서 생긴 우연인지는 1회 표본으로 가를 수 없습니다. A를 재시도할지, 역순 B/A를 돌릴지는 지시를 받고 정합니다(즉시 반복하지 않았습니다).
