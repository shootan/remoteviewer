# prelogin r21 406f563 — Codex E1/E2 재검토

`t-r639inm8 / r21 / kind=review-result / reply_to=remote#pvbntb3z` · 2026-10-19

**SOURCE OK / E1·E2 EVIDENCE OK — r20의 비-UI 보완 요구를 충족했다.** 추가 소스 보수 요청은 없다. 이 범위의 EVIDENCE NEEDS_CHANGES는 닫는다. 기능 전체·일반 게시 완료 판정은 아니며, 기존 수용 한계 8건과 사용자가 미룬 항목은 유지한다.

대상 `406f563471789bc297f0427565ceba9564bf6f5d` (parent `0f9406e`), 9파일 delta. 작업용/검증용 HEAD 일치·tracked clean을 직접 확인했다. 작업용 보고 `7e198ee0…`, 검증용 보고 r21 절 `24dfabdc…`와 원본 실행 로그를 대조했다.

## E1 충족 근거

- 시험 exe는 같은 `secure_input_service_main.cpp` 및 관련 제품 소스를 별도 타깃으로 빌드한다. 시험 진입점이 **실제 service_main**을 호출하고, 이벤트 STOP도 **실제 service_control**을 호출한다. 제품 파이프 서버·종료 분기·worker join·최종 report_service_status 흐름을 복제하지 않았다.
- 주입은 SCM 상태 보고/진입, 실행별 pipe 이름·사용자 DACL, 생성 오류, prelogin 기록/기동 등 OS 효과 경계다. seam 밖에서는 기존 서비스명·pipe 상수·SY/BA DACL·SCM 등록/상태 보고가 유지된다. 시험은 feature-off·실제 input agent 미기동 조건이므로 활성 agent/로그온 worker 종료의 실환경 성공까지 증명하지 않는다.
- 첫 생성 실패·listener 3회 연속 실패 각각에서 running 해제→stop_agent→supervisor/admin/unlock join→마지막 STOPPED 1회/error1450→service_main 반환을 원본 로그로 확인했다. 1실패→성공→2실패의 RUNNING 유지와 외부 STOP의 STOP_PENDING→STOPPED/error0 대조도 있다.
- 서버 단위의 실제 pipe 입력 지속·이름 소유권·거부·회복/reset·연속 3회 종료/핸들 정리도 제품 RunMultiClientPipeServer를 호출한다. 오류를 0으로 바꾼 M1, running 해제를 제거한 M2, reset을 제거한 M3가 각 요구에 해당하는 실패를 낸 작업용 원본 요약을 읽었다. 검증용은 lifecycle을 별도 빌드에서 3회 완주했다(각 exit0, 21/0).

따라서 r20 E1은 허용했던 제품 수명주기+OS 경계 주입 방식으로 충족한다. 실제 SCM의 등록·권한·SetServiceStatus 반영·QueryServiceStatusEx 실측, SYSTEM, 활성 agent는 여전히 미확인이다. 이 실기들을 seam 시험 성공으로 대체해 쓰지 않는다.

## E2 충족 근거와 대조의 범위

- 시험은 제품 ClientSessionController·제어 루프·scheduler·role gate·park fence·TCP control client를 링크하고, fake host는 loopback Hello/Pong/list/monitor 응답과 수신 메시지 기록을 제공한다. UI/입력 정책을 복제한 하네스가 아니다.
- S1 첫 pong 보류→일반 pong 후 list 도달/적용, S2 prelogin role에서 list/monitor wire0 및 앱 상태/거부, S3 park 후 요청 거부·pause 이후 wire0→resume 뒤 새 list 도달, S4 요청 hammer와 BeginPark 25회·세션 유지·양성 송신 대조를 확인했다. 검증용 별도 빌드에서 5회 완주했다(각 exit0, 13/0).
- G/P는 각각 요청 측 role/park 방어를 제거했을 때 상태·반환값 검사가 실패했다. wire0은 제어 루프의 2차 방어가 유지한 것이므로 **G/P가 wire 차단 오라클의 민감도를 입증한 것으로 읽지 않는다.** S1의 pong 대기도 제어 루프를 함께 막으므로 그 wire0만으로 role gate 단독 효과를 분리할 수 없다. 기존 role-gate 단위 시험과 실제 controller의 상태·반환/회복 증거를 함께 인정한다.
- 두 방어층 동시 제거 대조나 경쟁 검출 확률 측정은 수행하지 않았다. 현재 요청했던 통합 행동 증거와 양성 대조가 있으므로 이번 범위에서 새 필수 관문으로 추가하지 않는다. 25회×5회 완주는 관측한 반복 결과이며 모든 스케줄의 경쟁 부재 증명은 아니다.

## 출하 경로와 증거 고정

CMake에서 REMOTE60_SIS_TEST_SEAM 정의는 LifecycleTest 타깃에만 붙는다. 제품의 추가 helper는 seam 밖에서 원래 상수/false를 반환한다. release manifest의 명시적 제품 목록과 installer payload 목록에도 LifecycleTest는 없다. 실제 새 게시 패키지를 검사한 것은 아니므로 다음 릴리스 패키지 gate는 그대로 필요하다.

Codex가 현재 검증용 바이너리를 직접 읽어 ASCII/UTF-16의 `--lifecycle-test`, `REMOTE60_SIS_TEST_`, `sis-test-seam` 3개 표식 모두 제품 GNLinkInputService에서 부재, LifecycleTest에서 존재함을 확인했다. 이는 소스 조건부 컴파일 및 기존 gate의 양성 대조와 함께 출하용 서비스에 시험 seam이 포함되지 않았다는 근거다.

검증용 원본 summary는 영향9종 exit0, lifecycle 3/3, role_park 5/5, gate --dir 9/0·self-test 16/0 및 실행 종료를 기록한다. shared_core는 exit0만 기록돼 있어 별도 assertion 개수를 만들어 합산하지 않는다. r20의 기존 wire_cap_defer·gpu_scaler 실패와 credential_hops 간헐도 이번 영향 시험 성공으로 삭제하지 않는다.

현재 검증용 SHA256(원본 summary와 일치):

- GNLinkStream: `bc1867c814bc236488be9f4c82f168e8450bfbc67940f9bcd3ea4767283c3e6e`
- GNLinkInputService: `c57fed8bcc651dc026b26b40286ddfbf68e4cf20dc5390d4994d90c29a1088e3`
- LifecycleTest: `23f77c2615be0454a5e3c7640829fb407caeb743802c509522166fcd92ae338e`
- 작업용 보고: `7e198ee022da70bc4626b4923a8671c81f25d0b77cf113a964a0b0245cc8e6cf`
- 검증용 보고: `24dfabdcec5f3cb70995fcaa19caa7bb6a6d5c7ea2b1b7e0997c1a728a9ef10a`
- 독립 summary_vr21.txt: `df02b217a8e6d81d2eba41e5d3b0c2cff09cd8863d99458c5716802342f379d7`

Codex 직접 수행은 9파일 diff·시험/제품 호출선·로그·HEAD/clean·해시·바이너리 표식 확인, `git diff --check 0f9406e 406f563` exit0이다. 빌드와 실행은 검증용/작업용 근거이며 Codex 직접 실행이라고 표현하지 않는다. 제품/공개 문서 변경 없이 검토서만 작성했으므로 새 commit/push 없음.

## 다음 단계

1. 검증용은 기존 task에 E1/E2 충족과 남은 DEFERRED를 연결하고, 기능 기록(history의 근거/다음 행동, 계획의 진행 상태만)을 취합한다. 현재 고정 후보·산출물·절차를 보존한다. 추가 전량 재시험이나 두 층 동시 제거 대조를 위해 다음 단계 준비를 멈출 필요는 없다.
2. 사용자 “나중에”는 계속 유효하다. 지금 UI·SendInput·desktop·실 SCM·빠른 사용자 전환·UAC·재부팅을 실행하거나 사용자에게 즉시 조작을 요구하지 않는다. 검증용이 사용자 일정/재개 지시를 받으면 고정 후보로 제품 UI·입력·desktop 시험을 먼저 연결한다. 토글/로그인/재부팅 확인·취소 등 보이는 컨트롤→실제 입력→네이티브 처리→결과를 기록한다.
3. 해당 행동 검증이 통과하고 릴리스 단계가 허용되면 버전 후보를 확정·빌드하고, 변경된 source/artifact 해시 및 시험 빌드 차이를 매핑해 검수·서명·게시·외부 검증으로 연결한다. 현 task의 push·게시·버전 인상 금지는 이 검토로 해제하지 않는다. 새 패키지에 LifecycleTest/시험 훅이 없는지도 그 산출물에서 재확인한다.
4. 사용자 설치/UAC·cold reboot·로그인 전 접속→Windows 로그인→사용자 Host 인계·원격 재부팅 재접속·sign-out 후 해제는 준비된 절차대로 실기한다. 전원/로그온 조작은 사용자 범위에 맞춰 수행한다. 현재는 “비-UI 보완 검수 통과·UI/실기 보류”이며 “UAC만 남음”, “실기 성공”, “배포 완료”가 아니다.

다음 담당은 검증용 remote#pvbntb3z, 기존 task t-r639inm8의 기록·UI 일정/재개 연결이다. 현재 사용자에게 필요한 즉시 행동은 없다.
