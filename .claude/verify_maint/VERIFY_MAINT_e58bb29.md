# 검증용 독립 검사 — maint (fix/maint-r1 e58bb29) — 배포 스크립트 교체 실패 판정 (remote#7fsfy22z, 2026-10-28)

- 고정: worktree `C:/work/gnlink-wt/maint-r1` HEAD e58bb29(Codex SOURCE OK `codex_maint_src_r4.md`).
- sandbox(저장소 안): `D:/remote/remote/.claude/test-tmp/verify-maint` = `git archive e58bb29` 의 automation·apps/directory·apps/native_poc/src(fixture 가 payload gate 와 같은 소스의 이름을 읽으므로 필요). 해시: gnlink_deploy_test.sh 67735713…(작업용 r4 와 같음), gnlink_deploy.sh ecae5f38…, gnlink_verify_manifest.js ca67846e….
- 1차 실행은 sandbox 에 native_poc/src 를 빠뜨려 fixture 생성 실패(rc 1, 검증용 준비 실수 — 판정 제외, `verify_maint/verify_maint_run1.log` 보존).
- **2차: `bash automation/gnlink_deploy_test.sh` → rc 0, `gnlink_deploy_test: PASS`, PASS 97 / FAIL 0 / SKIP 0**(`verify_maint/verify_maint_run2.log`). case 13a~13k: 진짜 missing → incomplete / 다른 pair → does-not-verify(옛 sig 그대로·백업 선행) / **같은 pair 재게시 → verified-this-release(오진 없음)** / 조회 255·find 내부 실패·검증기 127·node 1 → unknown / crypto.verify 예외 → 4.
- 운영 서버·NAS·운영 키 사용 0(시험은 local 모드·일회용 시험 키). 원격 GNU find 전제는 검증용이 NAS 에서 읽기 전용으로 확인(`find (GNU findutils) 4.9.0`, `-printf` 출력 정상).
- 미검증: 실제 NAS 에서의 교체 실패(장애 주입은 운영에 하지 않음).

## 판정
- Codex **EVIDENCE OK**(`codex_maint_evidence_r1.md`): sandbox 890 파일 = git archive e58bb29 바이트 일치. local EVIDENCE — 실제 NAS 장애·운영 게시 완료 주장 아님.
