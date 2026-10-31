# 검증용 독립 Android 관측 — APK inbox C r3 (2b492ef), SH-off 120 s (2026-10-30)

## 고정
- APK: 검증용 독립 clean 빌드 `plain-debug-2b492ef-verify.apk` 9f7b0bd0…e07b (`BUILD_RECORD.md`)
- batch: `verify_c2/c3_shoff_batch_verify.ps1` df030bee (작업용 c1_shoff_batch f7b3dda6 에서 arm 만 변경), sh_run.ps1 -NativeExe 슬롯(CreateNew, removed=True, leftover 0)
- 하네스: feat-apk-0233 e2e = r12 snapshot 일치(검증용 대조), server.js 372e6eb8, KE_PLAN=e2-shoff, KE_WATCH_SEC=120, relay trace on
- run: `C:\work\gnlink-wt\feat-apk-0233\.claude\e2e\runs\c3-shoff-v-20261030073538-28856` (07:35:38–07:41:24)
  - sha256(앞 16): slot aa55aade, fixture_result 7ef9cfec, steps 782394a5, result.json 8f2b450e, app_final 594f0681, logcat a6ac6ca3, host d7898b43, server.stdout 826985eb
- 측정: `caplife/c_measure.ps1` fefd6c08 → `verify_c2/c3_measure.txt`

## 결과 (원문 직접 확인)
- RESULT PASS 8/8 (18 NOT RUN: plan e2-shoff), E2 select → first frame 5442 ms, **watched=120534ms**, host peer-lost 0, job residual 0 / ended_confirmed 1, Host OFF 2줄.
- SH: `[sharpness_select] level=off origin=settings` → `[sharpen_path] requested=off effective=off fallback=none`.

| 지표 (watch SEGMENT) | C r3 (이번) | B1 | B2 |
|---|---|---|---|
| SEGMENT 길이 | 122.928 s | 123.007 s | 122.675 s |
| Host keyframe-request /s | **0.822** (101) | 1.862 (229) | 1.329 (163) |
| relay h2c B/s | 30,077 | 44,250 | 44,071 |
| watch fps (같은 구간 video_debug out 차) | **10.88** | 14.04 | 17.04 |
| app PSS (watch) | 63–67 MB | 68–73 MB | 66–71 MB |
| app CPU % (watch 표본) | 86–165 | 63–162 | 64–153 |
| emulator loadavg (watch) | **9.9–12.2** | 7.4–8.7 | 7.6–8.7 |

C 고유 계수(watch 처음→끝): episodes +97, recSent +99, recClosed +97, recExhausted 0, keyTaken +101, chainSkipped +3, chainTrim +10, **inboxDrops +1420, gapDrops +580** (out +1333 / in +1333), covered/coveredExh 0, lossRecovered 0.

## 검증용 판독 (판정 아님)
- 요청은 원인당 1회로 줄었다(0.82/s, B 대비 −38 ~ −56 %) — episode +97 이 recClosed +97 로 닫힘, exhausted 0.
- 그러나 **같은 구간 fps 10.88 은 B1/B2(14.04/17.04)보다 낮다.** Codex C 채택 조건(영상 갱신·fps 유지/개선)을 이 1회로는 충족한다고 쓸 수 없다.
- 교란: 이번 run 의 emulator loadavg 가 B 보다 높다(약 10–12 vs 7.4–8.7). fps 저하가 C 정책 때문인지 부하 때문인지 이 1회로 가를 수 없다.
- 설명 필요: inboxDrops +1420 이 out +1333 보다 크고 episode 가 120 s 에 97개 — 손실 episode 가 거의 연속이며, inbox 가 표시 가능한 것보다 많은 프레임을 버린 것으로 읽힌다(정의는 B 와 다름: chainTrim 포함). 무엇을 세는지 소스로 확인 필요.
- 미계측: peakPre(C 에서 갱신 안 함), 실제 표시 latency.
