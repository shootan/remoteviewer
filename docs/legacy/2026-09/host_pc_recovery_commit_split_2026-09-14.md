# Native 커밋 분리 결과 — 2026-09-14

- 기존 Native 커밋: `daacbb2`, 백업 `backup/host-pc-recovery-daacbb2`. 서버 커밋 `27d9e74`는 변경하지 않음.
- 작업 범위: 기존 변경을 재구성한 것. 새 기능 수정·메인 머지·push·배포·설치 없음.
- 검증 작업 폴더: `D:/remote/remote-worktrees/host-pc-recovery-split`. 기존 작업 폴더의 브랜치도 완료된 이력으로 연결함.
- 각 단계 소스에서 x64 Release 빌드 후 관련 테스트 종료0을 확인하고 커밋함. 최종 소스만 빌드하고 중간 커밋을 통과 처리하지 않음.
- 최종 제품 소스·테스트·CMake 설정은 Git blob 비교로 기존 `daacbb2`와 동일. 기록용 문서만 분리 이력·검증 결과를 추가함. 재빌드한 실행 파일의 바이트 동일성을 뜻하지 않음.
- 기존 GPU/다중 모니터/원격 입력/재연결 종단/UAC·설치·soak 미검증과 업데이트 UI 서명 manifest pin 실패는 유지. 이번 커밋 분리는 그 미완료를 해결한 작업이 아님.
- bisect는 이 순서의 커밋에서 가능. 의존하는 후속 커밋이 있으므로 모든 커밋을 임의 순서로 단독 cherry-pick/revert할 수 있다는 보장은 아님.

실행기 보정: 디렉터리 재시도 시험의 첫 실행은 전체120초 제한에 걸렸다. 원본 시험에 잘못된 응답8종×최대15초 대기와 추가 사례가 있어 제한이 부족함을 확인했고, 시험 소스/판정은 유지한 채 실행기만300초로 조정해 재검증했다. 원본 커밋에서의 보조 실행은 진행 상황 확인 후 중단했으며 PASS 근거로 사용하지 않았다.

| 순서 | 커밋 | 수정 단위 | 이번 단계 검증 |
|---|---|---|---|
| 01 | `4059a0e` | QPC overflow와 MSVC 빌드 옵션 | Release build + 실행 0개 exit0; `.claude/split-01-results.json` |
| 02 | `58c5d45` | pipe/진단 종료 및 HTTP 오류 경계 | Release build + 실행 2개 exit0; `.claude/split-02-results.json` |
| 03 | `76fcf85` | UDP control 총량·TTL·조각 검증 | Release build + 실행 1개 exit0; `.claude/split-03-results.json` |
| 04 | `504684d` | Viewer 입력 큐와 영상 밖 버튼 해제 | Release build + 실행 2개 exit0; `.claude/split-04-results.json` |
| 05 | `34ed0c3` | Viewer 수신·표시·세션 수명 복구 | Release build + 실행 2개 exit0; `.claude/split-05-results.json` |
| 06 | `5fee4ab` | Host 초기화·종료·캡처 수명/대상 보존 | Release build + 실행 1개 exit0; `.claude/split-06-results.json` |
| 07 | `0196ceb` | encoder pending·출력 복구와 tune epoch | Release build + 실행 2개 exit0; `.claude/split-07-results.json` |
| 08 | `e302c05` | Host 자식 Job·반복 DXGI fallback | Release build + 실행 1개 exit0; `.claude/split-08-results.json` |
| 09 | `866efd8` | observe 재시도·주소 snapshot·소켓 영구 오류 | Release build + 실행 2개 exit0; `.claude/split-09-results.json` |
| 10 | `d768ef8` | Host/Client 인증·로그·업데이트 작업 수명 | Release build + 실행 3개 exit0; `.claude/split-10-results.json` |
| 11 | `16d4d6b` | Client 같은 Host 재연결·취소 UI | Release build + 실행 1개 exit0; `.claude/split-11-results.json` |
| 12 | `a38b1e9` | Shell/macro WebView 복구 및 종단 UI fixture | Release build + 실행 1개 exit0; `.claude/split-12-results.json` |

공유 파일도 부분 분리했다. `host_app_main.cpp`는 자식 관리와 인증 작업 수명을, `client_shell_main.cpp`는 소유 작업/인증·재연결·WebView 재생성을 나눴다. `viewer_window_proc.cpp`는 입력 해제와 렌더/UI 감시를, `host_stage_time_limit.cpp`·`host_stage_selection.cpp`는 캡처와 인코더 재시도를 분리했다. CMake의 새 테스트 타깃도 의존 파일이 생기는 단계에 추가했다.

검증 키·시험 코드의 성공 조건을 약화시키지 않았고 설치된 사용자 앱을 종료하거나 교체하지 않았다. 새 UI 시험은 전용 프로필의 WebView browser에만 장애를 주입했다.
