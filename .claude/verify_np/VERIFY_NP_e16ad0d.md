# 검증용 독립 검사 — NP ⓪+①b (feat/np-early-relay e16ad0d) (remote#7fsfy22z, 2026-10-28)

- 고정: worktree `C:/work/gnlink-wt/np-impl` HEAD e16ad0d(tracked clean). Codex SOURCE OK(`codex_np_impl_src_r2.md`).
- **독립 빌드**(작업용 build 와 분리): `C:/work/gnlink-wt/np-verify-build`(VS2022 x64 Debug, 시험 타깃만) — probe `7D25FC0C…`, punch_any `91CF5054…`. configure/build rc 0(`configure.log`·`build.log`).
- 구 서버 사본: `git archive 0a5e71d apps/directory` → `.claude/test-tmp/verify-np-old`.

| 시험 | rc | 결과 |
|---|---|---|
| directory suite `node test/run.js`(REMOTE60_TEST_TMP=.claude/test-tmp/verify-np) | 0 | RESULT: ALL PASS — PASS 845 / FAIL 0(r2 시험 추가로 832→845) |
| relay_reply_fence_test | 0 | ALL PASS |
| np_early_bootstrap_e2e ×3(새/구 서버 × 새/구 뷰어 × Host 응답/무응답) | 0 ×3 | 26/26 ×3 — 새+새 무응답: relayEarly=accepted, relay 라벨, **chosenMs=1001**; 새 서버+구 뷰어: legacy, 1501; Host 응답: direct, 0 ms; 구 서버+새 뷰어: legacy |
| punch_any ×3 | 0 ×3 | 61/61 ×3 |

## 미검증 (분리)
- **제품 창 relayPath 표시 변화**(중계 세션이 이제 '중계'로 보임): 실 GNLinkViewer 창 렌더 증거 없음 → R2 통합 후보에서 격리 창 렌더(E1 방식)로 확인 예정.
- 실 NAT 필드 KPI(회사→집), APK(0.2.33 뒤 JNI·Kotlin 공동 배선), 운영 서버 배포(server.js — 디렉터리 서버 재시작 필요), KA 기반(0a5e71d)의 별도 미결.
