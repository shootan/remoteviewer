# Record/fatal r10 — SOURCE

2026-10-29 · **SOURCE OK: ccaf7453f366e2baa4045a426f33978bb27c61a7.** D1/D2의 고정 보수로 두 시험만 독립 재실행할 수 있다. r1의 record FAIL/fatal INVALID를 EVIDENCE 성공으로 바꾸는 판정은 아니다.

## 범위와 근거

- af49c26..ccaf745는 os_input_record_e2e_test.cpp와 host_fatal_exception_test.cpp **두 시험 파일 +50/-3**이다. 고정 HEAD·tracked 변경 없음·diff --check를 확인했다. 미추적 .claude/scratchX는 판정에 넣지 않았다.
- ba2eaf2 이후 관련 누적 변경은 시험/helper·CMake/automation 보수이며, 이번 제품 OS 효과 경계·Host session·libs/Android 제품 코드는 바꾸지 않았다. record-only build 및 기존 fatal input/power·출력 완결 gate의 SOURCE 수용을 유지한다.
- Host의 초기 epoch, 첫 접속 경로, 이후 session 전환 로그 및 child env 생성 순서를 실제 고정 source와 대조했다. Host/Viewer/AVD/adb 및 두 실제 하네스를 실행하지 않았다.

## D1 — 첫 세션 epoch 근거와 자료 보존 수용

제품의 clientSession.epoch 초기값은 **1**이다(host_session.hpp). 첫 token-less UDP Hello는 host_startup_connect의 접속 경로에서 처리되며, client connected transport 로그를 낸다. 이후 startsSession 경로는 AdmitHello의 BeginEpoch 및 host_startup_control의 session epoch 로그와 연결된다.

따라서 **이 새 Host/단일 loopback client fixture**에서 다음 구분을 수용한다.

- 명시된 이후 session epoch 로그가 있으면 그 값을 쓴다. 값이 0/판독 불가면 초기값으로 fallback하지 않는다.
- 그 로그가 없고 client connected 로그가 정확히 1이며 첫 standby activation 로그가 없으면, source의 초기 epoch 1을 출처와 함께 사용한다.
- 어느 쪽에도 해당하지 않으면 INVALID이고 staging 보존이다.

이것은 record effect의 epoch 1을 보고 expected 값을 맞추는 방식이 아니다. **고정 제품의 첫 세션 불변식과 별도 startup 로그 조건**에 근거한다. epoch 로그가 없는 임의 실행이나 다른 fixture의 일반 parser 규칙으로 확대하지 않는다.

expected epoch 미획득 시 입력을 queue하기 전에 중단하고, assertion 실패 때도 set_keep를 적용해 Host 원문을 보존하는 보수를 수용한다. INVALID의 보존 자료는 실패 조사 자료다. 그것을 producer 종료/EOF 또는 완전한 효과 매칭의 성공 근거로 쓰지 않는다.

## D2 — 정상 startup 대조 수용

run은 encoded-experiment 값을 **host_env의 child env 블록을 만들기 전**에 설정하고 해당 run 뒤 해제한다. 기존 run_with_client도 자기 env 생성 전 같은 값을 설정하는 경로가 있음을 확인했다. none만 고치고 이후 client case를 codec 거부 상태로 두는 변경은 아니다.

none은 이제 codec refusal/FATAL 부재뿐 아니라 **waiting client bindPort 도달**과 정상 종료 또는 시험 deadline 경로를 요구한다. 최종 whole-output의 input/power OFF 두 줄·무상충·출력 완료 판정도 그대로다. startup-fault case의 input 줄 부재 허용과 정상 none의 조건을 섞지 않는다.

따라서 r1의 exit 11/no-FATAL false-positive assertion을 보수하는 방향을 수용한다. 실제 재실행에서는 정상 도달·종료/deadline 시각·전체 출력과 각 case 결과를 남겨 확인한다.

## Codex 제한 직접 대조

고정 cpp의 판정 블록을 추출하고 Host log 읽기를 문자열 입력으로 바꾼 **Host 없는 순수 대조**를 별도 빌드했다. **14 checks, 0 failed / rc 0**이다.

- epoch: 첫 ordinary 1, metadata 없음, connected 두 번, standby first, 이후 epoch, 명시 0·판독 불가에서 초기값 fallback 금지, 마지막 명시 epoch 선택.
- none: startup ready + 정상/deadline 경로, codec 거부, FATAL, ready 부재, 예상 밖 exit의 거부.

자료: `.claude/codex_test_injection_review/r10_ccaf/parse_probe.cpp/exe`, 고정 matcher header 및 build.log/run.log. 이것은 실제 Host/child 종료를 대역으로 실행한 시험이 아니라 **문자열 판정의 제한 확인**이며 두 실제 하네스 PASS의 대체물이 아니다.

작업용 제공 두 시험 exe의 현재 hash도 읽기만으로 직접 확인했다.

| 산출물 | SHA-256 |
|---|---|
| record e2e | c1efeac3347ded6bb030514b1b8734c5d22569a94e129d5fd4d5d94266dd928e |
| fatal test | da09e9833195b39a158e4291575b9d03a2a2e5550d324155b107a0ddfa260012 |
| Codex 순수 판독 probe | f9b2e1fd84e7967aaba0ae751b6a43d666114278bd4bf0333965b53e0379d805 |

검증용은 자신의 ccaf745 독립 빌드의 Host/helper/test 전체 hash를 새 실행 기록에 연결한다.

## 재실행 범위

**기존 ka-pc 우선 작업 뒤 slot이 해제되면**, ccaf745 독립 빌드로 아래 두 시험을 새 r2 자료 폴더에서 진행할 수 있다. 같은 범위의 별도 실행 go를 다시 받을 필요는 없다.

1. ordinary os_input_record_e2e: record-only target·loopback·clipboard OFF·자기 staging/job, declaration 이전 client 0. 첫 세션 epoch 출처와 같은 session의 dispatch/effect 순서·fragment 매칭을 확인한다.
2. host_fatal_exception: 기존 b8에서 허용된 startup-fault/std/winrt/none 및 지정 client case 범위. normal env·각 launch gate·종료·최종 출력 판정을 유지한다. 이 허용은 금지된 viewer_prelogin_login_e2e를 포함하지 않는다.

r1의 원문/실패와 새 결과를 덮어쓰거나 합산하지 않는다. FAIL/INVALID의 원문·staging은 보존하고 새 로그에서 원인을 판정한다. 일반 8종의 구성 단위 EVIDENCE OK는 그대로이며 이 변경을 이유로 전량 반복하지 않는다. 실 OS 입력 성공·실 clipboard·prelogin UI·최종 제품 통합/출하는 여전히 별도다.

이번에는 source·artifact hash 읽기와 자기 순수 대조·판정 기록만 수행했다. 제품 수정·실제 Host 시험·사용자 입력/clipboard·설치·운영·삭제·commit/push는 없다. 사용자 현재 조치 없음.
