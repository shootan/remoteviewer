# 검증용 B→C pair — SH-off 120 s 각 1회 (2026-10-30 08:12–08:25)

- wrapper: `codex_decoder_liveness_review/bc_shoff_pair_reviewed.ps1` e338ad87 (Codex 검토본), `caplife/sh_run.ps1 -NativeExe` 한 슬롯(removed=True, leftover 0), batch log `caplife/sh_r2/bc-shoff-pair-verify.log` 2a78a145
- 실행 전 PC: CPU 4 %, 큰 빌드 프로세스 없음
- APK: B plain-debug-r5 32bb788f, C 검증용 독립 빌드 9f7b0bd0 (`BUILD_RECORD.md`); 같은 boot 37551b86, adb listener pid 25608, r12 하네스, server 372e6eb8, relay trace on, app trace off
- 측정: `caplife/c_measure.ps1` fefd6c08 → `verify_c2/<run>.measure.txt`; Host 공급 = watch SEGMENT 안 `encodedFrames/sentFrames` 줄 합(검증용 집계)

| | B (bc-b-shoff-20261030081349-43476) | C (bc-c-shoff-20261030081943-25112) |
|---|---|---|
| 결과 | PASS 8/8, watched 120990 ms | PASS 8/8, watched 120414 ms |
| select → first frame | 4316 ms | 4929 ms |
| pre-arm guest loadavg(1분) | 1.15 → 0.76 (휴지) | **11.20 → 6.91** (직전 B 의 여파) |
| watch 중 loadavg | 8.8–12.6 | 6.0–9.0 |
| Host 공급(watch, sent) | 2735 (22.08/s) | 2691 (21.81/s) |
| 같은 구간 fps (out) | **16.12** (+1979) | **14.63** (+1798) |
| 전달률 out/sent | 72 % | 67 % |
| Host keyframe-request /s | 1.147 (142) | **0.657** (81) |
| relay h2c | 33,755 B/s | 30,477 B/s |
| inboxDrops / gapDrops (watch 증가, 정의 B≠C) | +151 / +512 | +887 / +187 |
| C: episodes / recSent / keyTaken / chainTrim | – | +78 / +79 / +80 / +5, covered 0, exhausted 0 |
| app PSS / CPU (watch 표본) | 64–66 MB / 57–124 % | 64–66 MB / 44–133 % |
| peer-lost / job | 0 / residual 0, ended_confirmed 1 | 0 / residual 0, ended_confirmed 1 |
| SH | requested=off effective=off | requested=off effective=off |
| Host OFF 줄 | 2 | 2 |

원문 sha256(앞 16): B slot 04750418, fixture_result d98720e2, steps 09db9a24, result 10974617, app_final 855c4b77, host 400aa24c, server 78f0292b / C slot f151879b, fixture_result 9e5561a9, steps e2a0eea0, result bdaf997f, app_final d2222e44, host 194e4dd7, server d9abe2cc

## 검증용 판독 (판정 아님)
- 같은 boot·같은 공급(약 22/s)에서 C 의 같은 구간 fps 가 B 보다 약 1.5/s(−9 %) 낮다. watch 중 부하는 오히려 C 쪽이 낮았다(전처치 pre-arm 은 C 가 높음 — 직전 B 의 여파).
- 요청은 C 가 −43 %(0.657 vs 1.147/s), h2c −10 %.
- 앞 C 단독(c3, fps 10.88, load 10–12)과 이번 C(14.63)의 차이는 부하 영향이 컸음을 시사하지만, pair 안에서도 C < B 가 남는다(N=1 pair).
- Codex 기준("비슷한 공급에서도 C 저하가 남으면 유효 prefix/최신성 정책 설계로")에 해당하는 관측으로 보인다. 원인(C 의 전체 폐기 vs 표시 가능 prefix)은 미확정.
- 미계측: 실제 표시 latency/age, peakPre.
