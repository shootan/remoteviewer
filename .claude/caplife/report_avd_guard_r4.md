# keepalive-r2 e — AVD guard r4: J1 (Codex codex_avd_guard_src_r3.md), 실행 0

## 고정
- feat-apk-0233 **4f04415cc30e2eb94fe5a662c7a560edd542556a** (J1, 부모 de2d60a)
- HEAD **9f9a91c852419a2d2848be73aab077d779b0550c** (merge ka-r3 af49c26 = T1)
- 4파일: `e2e_job_run_tool.cpp`, 새 `e2e_job_run_start.hpp`, 새 `e2e_job_run_start_test.cpp`, CMake
- `e2e_job_run.exe` sha256 `DC4C5049E8E9D32A8C9096005A2702C95EF30CCE2D3F1998BD4005DA8C89462F` (최종 재빌드)
- 스크립트는 r3 snapshot 그대로다(`avd_guard_r3_snapshot`, 변경 없음).

## J1
- start 단계를 `e2e_job_start(JobStartApi, job, pi)` 로 분리했다. Win32 호출(assign, IsProcessInJob, TerminateProcess, wait, ResumeThread, TerminateJobObject, active)은 API 표로 받는다.
- **assign/membership 실패**:
  - suspended command 를 terminate 한다. **wait 가 그 종료를 본 경우에만** `ended_confirmed=1`, exit 2 다.
  - 그 밖에는 `ended_confirmed=0`, exit 98 이다.
  - 출력에 assign, in_job, error, terminate, wait 값을 남긴다.
- **ResumeThread 실패**:
  - job 을 terminate 한다. command 종료를 봤고 **그리고** job 이 비었을 때만 confirmed, exit 2 다. 아니면 98 이다.
  - 실행 중으로 넘기지 않는다.
- 정상 경로(편입 → 확인 → resume → 대기)는 그대로다.

## 대조
- `e2e_job_run_start_test` **8/8 ×3, exit 0**. Win32 대역이고, 프로세스·job 은 만들지 않는다. test exe `223EA84B…`. 경우:
  - Codex 경우: assign false + terminate false + WAIT_TIMEOUT → 98, `ended_confirmed=0`
  - confirmed 인 경우
  - 멤버가 아님
  - 멤버십을 물을 수 없음
  - resume 실패, 세 가지: confirmed / 종료 미확인 / job 이 안 빔
  - 전부 성공
- **보수 전 등가 mutant**(결과 무시 + resume 결과 무시 = de2d60a): **5 FAIL**. 복원 후 해시가 일치하고 8/8 이다.
- 오케스트레이션(stub, 보수된 runner 사용) **18/18**, 잔여 0. 로그 해시 `74c23fd7…` 는 r3 와 같다.

## 그대로
- 실 AVD 실행은 SOURCE 판정 전까지 보류한다.
- 실 clipboard (c), old/E7f/E8/E12 NOT RUN, E7h·Activity 교체·raw AUTO, ke_r21 peer_lost 는 미결이다.
- dxgi-oslink-q9(작업용2)와는 별개다.
