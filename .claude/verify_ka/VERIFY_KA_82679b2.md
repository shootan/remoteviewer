# 검증용 독립 검사 — KA r2 공용(shared) 82679b2 (remote#7fsfy22z, 2026-10-28)

- 고정: worktree `C:/work/gnlink-wt/ka-r3` HEAD **82679b2**(Codex SOURCE OK `codex_keepalive_r2_src_r3b.md`). 작업용 build 와 분리된 **`ka-r3/build-verify`**(VS2022 x64 Release; 시험 타깃 4 + GNLinkStream). 해시: GNLinkStream 3C02BCE3…, e2e 397AC86E…, table 86B7F3F2…, entry CC1B119F…, lease 26273D41….
- 구 Host: **실제 게시본 PC 0.2.163 payload GNLinkStream.exe**(`rel-0163/.claude/rel-final/0.2.163/payload`, standby 없음).
- **유효 원문 로그 = `.claude/verify_ka/r2/`** (table·entry·lease_1~3·e2e_1·e2e_2). 상위 `verify_ka/*.txt` 는 무효 1차(보존만).
- 환경: REMOTE60_ALLOW_HOST_E2E=1, REMOTE60_TEST_TMP=`.claude/test-tmp/verify-ka`(미리 생성), node(PATH), 측정 슬롯 자기 생성분만 01:32~ 해제.

| 시험 | rc | 결과 |
|---|---|---|
| host_standby_table | 0 | ALL PASS 56/0 |
| standby_entry_client | 0 | ALL PASS 72/0(파일 저장소 비ASCII 경로 포함) |
| standby_lease ×3 | 0 ×3 | PASS(52 checks) ×3 |
| host_standby_admission_e2e `--product-client --real-directory --old-host <0.2.163>` ×2 | 0 ×2 | **ALL PASS 74/0 ×2** — A·B parked(각자 entry) → A activation → B 무간섭 → B takeover(새 epoch) → A 같은 소켓 재parked → Release(StandbyRelease 1) / 실 server.js relay: /api/hosts 지원 확인·standby connect 각자 relay lease·relay parked·activation·영상 / **구 Host 0.2.163: 무응답 → Unsupported(자동 연결 0)** |

## 무효 처리한 1차 실행 (기록)
- 1차는 빌드를 저장소 밖 `C:/work/gnlink-wt/ka-verify-build` 에 두어 e2e 의 scratch 가드가 실행 자체를 거부(51 FAIL)했고, entry 의 파일 시험은 REMOTE60_TEST_TMP 루트를 미리 만들지 않아 10 FAIL — **둘 다 검증용 환경 실수, 제품 결함 아님**. 결과 폐기, 2차가 판정 근거. 가드가 저장소 밖 scratch 를 거부하는 동작 자체는 확인됨(부수 근거).

## 미검증
- 앱 배선(e: feat/apk-0.2.33 4e88a0b~ 진행 중)·AVD 행동, PC managed Viewer(③ 미착수), 실 NAT, 비용·교차 기기, 운영 relay/NAS(무변경).

## 판정
- Codex 공용 구성 단위 **EVIDENCE OK**(`codex_keepalive_r2_shared_evidence.md`). 출하 승인 아님 — 남음: 앱 e/AVD·PC ③·실 NAT/비용·실기 교차·Host 이동/proof 손실 통합 실패 경로.
