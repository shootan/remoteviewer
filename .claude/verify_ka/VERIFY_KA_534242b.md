# 검증용 독립 검사 — KA r2 공용 534242b (punch thread 보수 포함) (remote#7fsfy22z, 2026-10-28)

- 고정: worktree `C:/work/gnlink-wt/ka-r3` HEAD **534242b**(제품 6c3cb5a SOURCE OK `codex_ka_punch_thread_src_r2.md`, 시험 534242b SOURCE OK `..._r4.md`). 독립 빌드 `ka-r3/build-verify`(Release) 재빌드: GNLinkStream BAC75DC0…, e2e A9D16925…, punch 5C73DC73…, entry CC1B119F…, lease 7B20B785….
- 구 Host: 게시본 0.2.163 payload GNLinkStream. REMOTE60_TEST_TMP=`.claude/test-tmp/verify-ka`(실행 뒤 비어 있음). 측정 슬롯 05:11~ 자기 생성분만.
- **유효 원문 로그 `.claude/verify_ka/r3/`**:

| 시험 | rc | 결과 |
|---|---|---|
| host_agent_punch ×3 | 0 ×3 | 9/9 ×3 |
| standby_entry_client | 0 | 72/0 |
| standby_lease | 0 | PASS(66 checks) |
| host_standby_admission_e2e `--product-client --real-directory --old-host <0.2.163>` ×2 | 0 ×2 | **ALL PASS 78/0 ×2** — 기존 흐름 + **같은 기기 crash 직후 즉시 재입장 627/629 ms, release 직후 1619/594 ms**(각 새 relay lease, 10 s 이내 — 보수 전 5 s 무응답 → Unsupported 였던 경로) |

- 미검증: PC 즉시 재유지(작업용2 feat/ka-pc 에 534242b 병합 뒤), 앱 e r2 AVD, 실 NAT·비용, 통합 시 dirretry cycleCv 와 punch worker 동시 보존(병합 뒤 재확인).

## 판정
- Codex 구성 단위 **EVIDENCE OK**(`codex_ka_punch_thread_evidence.md`). e2e_1 의 PASS 접두 출력 손상 1곳은 본문·최종 counter·2회차로 대조 기록. e r2·PC 즉시 재유지·dirretry 병합·출하는 별도.
