# maint r4 — 독립 EVIDENCE 판정

2026-10-28 / Codex remote#tt7fcvky → 검증용 remote#7fsfy22z / 기존 maint task.

**EVIDENCE OK — e58bb29의 배포 스크립트 maintenance 범위.** 기존 SOURCE OK와 연결해 교체 실패 진단·시험 fixture 보수의 기능 단위 검수를 통과한다. 실제 NAS 장애 주입·운영 게시 완료를 뜻하지 않는다.

## 후보와 독립성

- 고정 commit **e58bb2964b192ad6cf164b74887495c356f69277**. SOURCE 정본은 `codex_maint_src_r4.md`.
- 검증용 보고 `.claude/verify_maint/VERIFY_MAINT_e58bb29.md`와 원문 로그를 읽었다. 별도 sandbox는 **`D:/remote/remote/.claude/test-tmp/verify-maint`**이며 현재 해당 경로와 상위 `.claude/test-tmp`, `.claude`는 일반 디렉터리다.
- Codex가 commit의 `git archive`를 메모리에서 읽어 sandbox의 automation·apps/directory·apps/native_poc/src와 대조했다. **890개 파일 모두 bytes 일치, 줄끝 차이·누락·변경 0**이다. 작업용 worktree를 그대로 실행했다고 추정한 것이 아니라 실제 독립 사본의 후보 정합을 확인했다.
- 핵심 SHA-256도 SOURCE 후보와 같다:
  - deploy: **ecae5f38fcf105a3d7499745c1300b13245c705136b9f8923d4d2522be592155**
  - deploy_test: **67735713609c5c627b46466dc16244fb42883c3b862778978172f459198e2093**
  - verify CLI: **ca67846e3bcc87f0782d3e01f5d031b8d0006819c7a516bb293b77cff54c85aa**

## 유효 실행과 실제 확인한 동작

유효 로그의 실제 파일명은 **`.claude/verify_maint/verify_maint_run2.log`**다. Codex가 원문에서 **97 PASS / 0 FAIL / 0 SKIP**, 마지막 `gnlink_deploy_test: PASS`를 직접 집계했다. 보고된 rc0 및 시험의 PASS→exit0 경계도 일치한다. 로그 SHA-256 **8af4828b23ab80639d63c1c4d963286890c1c84e0c67c88494d62b94cb6df2c2**.

단순 합계 외에 다음 사례의 입력·동작·결과를 시험 원문과 로그로 대조했다. 실제 deploy 스크립트를 local transport로 실행하고, fault는 해당 경계에만 주입한다.

- 정상 게시: payload 선행, manifest/sig 게시, 재실행 불변, lock 해제. immutable version·해시/파일 누락·서명 오류는 게시 전 거부. verify-only는 변경하지 않는다.
- 기존 pair backup 및 같은 버전 재게시의 rollback point 보존. 안전한 backup 이름과 releaseId 경계도 유지한다.
- 13a: pair가 실제로 없을 때 sig rename 실패 → **exit4, manifest-only / incomplete**.
- 13b: 다른 release 위 sig rename 실패 → **does-not-verify**, 옛 sig 및 선행 backup 보존.
- 13c(M1): 같은 signed pair 재게시 중 sig rename 실패 → **verified-this-release**, 클라이언트 거부 오진 없음, 실제 검증 성공.
- 13d/e: manifest rename 실패 → **neither / verified-another-pair**, tmp 보존. read-back 실패 → **unknown**.
- 13g/h(M2-b): 조회255 및 find 내부 실패 → **swap/pair 모두 unknown**. fault 도달 표지를 확인하며 게시 pair bytes도 불변이다.
- 13i/j: verifier 실행 불가127·node1 → **unknown**, 거부로 확정하지 않는다. fault 도달과 pair 보존을 확인한다.
- 13k(M2-a): 실제 CLI와 공용 모듈의 crypto.verify 예외는 정상/불일치 pair 모두 **4**, 예외 도달 확인. 실제 거부3·정상0·서명 형식 오류3·파일/키 검사 불가4도 구분한다.
- 13f: 정상 게시에는 실패 진단 상태가 없고 재시작 문구는 코드·모듈·환경 변경 범위를 설명한다.

## 무효 실행·경계

- **1차 실행은 판정 제외**다. 실제 파일 `.claude/verify_maint/verify_maint_run1.log`에서 payload 이름을 읽을 `update_process_targets.cpp` 누락과 fixture 생성 실패를 확인했다. 제품 결함이나 유효 회귀 실행으로 세지 않는다. 로그 보존과 2차의 전체 파일 정합으로 준비 오류가 해소됐다.
- 보고의 run1/run2 축약 참조는 기록 취합 시 위 실제 파일명으로 맞추면 된다. 새 제품 revision이나 재시험을 요구하는 결함은 아니다.
- 이번 **전체 suite 실행 주체는 검증용**이다. Codex는 후보/시험/원문 로그·해시·archive 정합을 독립 확인했으며 97건을 다시 실행하지 않았다. 앞선 SOURCE 단계의 Codex 직접 경계 14건은 별도 근거다.
- 기본 fixture는 payload gate와 같은 이름 11개 및 실행별 일회용 시험 키를 사용한다. 제품 pinned key·운영 개인키를 바꿔 통과시킨 결과가 아니다. 시험은 local 모드이며 SSH·curl·실제 NAS 교체 장애는 이 실행의 검증 대상이 아니다. 원격 GNU find 전제는 앞선 검증용의 read-only 확인을 유지한다.

검증용은 **이 고정 후보까지** 정상 통합과 영향 검사·history/계획/Git MCP 기록으로 이어갈 수 있다. 운영 게시/외부 검증이나 다른 미검수 branch 전체를 승인한 것은 아니다. Codex 제품 수정·빌드·전체 suite 재실행·운영/NAS 변경·키 사용·삭제·commit/push 없음. 사용자 현재 조치 없음.
