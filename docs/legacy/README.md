# legacy — 끝났거나 대체된 문서

읽지 않아도 현재 작업을 할 수 있는 문서들이다. `docs/작업목록.md` 의 항목이 여기를 가리킬 때만 연다.
상대 링크는 옮기기 전 위치 기준이라 일부 깨져 있을 수 있다. 내용은 옮길 당시 그대로다.

## 2026-09/ — 2026-09 안정화·업데이터 라운드 (2026-09-22 정리)

| 문서 | 무엇 | 상태 |
|---|---|---|
| `HANDOFF.md` | 2026-08-25 기준 인수인계 (0.2.54~0.2.59) | 대체됨 → `작업목록.md` |
| `windows_viewer_recovery_work_order.md`, `stream_freeze_diagnosis_2026-09-06.md` | 뷰어 정지·지연 작업 지시와 원인 분석 | 0.2.100~0.2.104 에 반영 |
| `field_test_2026-09-07_0.2.100.md`, `field_test_2026-09-08_0.2.103.md`, `field_test_2026-09-08_0.2.104.md`, `field_test_2026-09-08_0.2.104_720p.md` | 09-07~08 실기 기록 | 미확정 잔여는 `작업목록.md` V10(재발 시) |
| `stabilization_audit_2026-09-08.md`, `full_code_audit_2026-09-08.md` | 코드 전수 감사 (A01~A17, HN·SV·AN·I 원장) | 처리분은 history #416~#431, 잔여는 `작업목록.md` 5절 |
| `업데이트_배선_계획.md` | 업데이트 배선 W1~W8 | 완료 (history #453~#459) |
| `nas_release_handoff_0.2.109.md`, `nas_release_handoff_2026-09-10.md` | 0.2.108/0.2.109 NAS 최초 구축·게시 요청서 | 완료. 평시 절차는 `CLAUDE.md` 릴리스 절 |
| `a2a_stall_diagnosis_2026-09-12.md` | 반복 중단 원인 (턴/task 혼동) | 정책 반영 완료 |
| `host_pc_thread_recovery_audit_2026-09-12.md`, `..._inventory_...`, `..._probes_...` | Host/PC 스레드·복구 전수 점검 (Host13/PC13/공유10) | 구현은 `host_pc_recovery_implementation_2026-09-12.md`, 커밋 분리는 `host_pc_recovery_commit_split_2026-09-14.md` |
| `incident_2026-09-12_remote_game.md`, `incident_2026-09-12_code_review.md`, `incident_2026-09-12_evidence_sha256.txt` | 09-12 게임 원격 멈춤 사건 | 수정은 `remote_game_stability_implementation_2026-09-12.md` |
| `release_readiness_2026-09-12.md` | R01~R12 수정안 | R01·R03~R06·R08·R10·R11 반영, R02·R07·R09·R12 는 `작업목록.md` C4·C6·C10·C9 |
| `remote_game_stability_implementation_2026-09-12.md`, `remote_game_stability_review_2026-09-13.md`, `remote_game_stability_merge_verdict_2026-09-13.md` | 안정화 후보 구현·검토·머지 판정 | main 반영 (0.2.124~0.2.127) |
| `host_pc_recovery_implementation_2026-09-12.md`, `host_pc_recovery_commit_split_2026-09-14.md`, `main_recovery_integration_2026-09-15.md` | 복구 구현·커밋 분리·main 통합 | 0.2.127 |
| `release_0.2.124_field_trial.md`, `release_0.2.127_final_review.md`, `diagnostic_latency_2026-09-14.md`, `field_test_1755_2026-09-14.md` | 0.2.124~0.2.127 게시·실기 | 완료 |
| `gpu_timeout_2026-09-15_codex_analysis.md`, `incident_2026-09-15_1715_stream_dropout.md`, `incident_2026-09-15_host_control_dispatcher.md` | 09-15 GPU TDR 덤프 분석, 영상 끊김, 제어 디스패처 차단 | 썸네일 격리는 0.2.128, liveness 감독은 `작업목록.md` C8 |
| `updater_abandon_race_crash_2026-09-21.md` | 업데이터 abandon race 시험 크래시 규명 | 0.2.133, G gate 닫힘 |
| `클라이언트_뷰어_분할_리팩터_계획.md`, `호스트_분할_리팩터_계획.md` | 뷰어·호스트 분할 리팩터 설계 | 완료 (0.2.58/0.2.59) |
| `뷰어_리팩터_발견사항.md`, `호스트_리팩터_발견사항.md` | 리팩터 중 발견 원장 F-01~F-23 · H-01~H-28 | 전부 처리 |

## 그 밖의 파일 (2026-04~08)

- `history_old.md`, `history_old2.md` — 옛 history 분할. 기본으로 읽지 않는다.
- `구현계획_old.md`, `android_구현계획.md`, `계정_호스트등록_홀펀칭_설계.md` — 옛 계획·설계.
- `Host_Client_최적화_UI_감사_20260730.md`, `Host_Client_최적화_UI_상세계획_20260730.md`, `h264_코드리뷰_20260409.md`, `버퍼링_GPU경합_검토_20260409.md`, `버퍼링_GPU경합_분석_20260409.md` — 옛 감사·리뷰.
