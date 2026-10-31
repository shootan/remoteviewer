# KA PC fin-1 — EVIDENCE 후보와 managed 보수 결정

2026-10-29 · **shell의 제출된 행동 EVIDENCE 후보는 수용한다. managed 전체는 미통과 유지이며, 아래 한정 보수 뒤 작업용의 회귀 실행은 managed만 하면 된다.** 작업용 자체 실행 결과에 대한 후보 판정이다. 검증용의 최종 독립 검사·통합/출하 관문을 이 문서로 생략하지 않는다.

## 고정과 Codex 확인

- source **aecbdfd7d1712b66a4073f7645ff0129c4a94aeb**, tracked 변경 없음. 기존 시험 SOURCE 판정 codex_ka_pc_test_src_r2.md의 실행 범위다.
- report_ka_pc_impl_r1.md의 「실 Host e2e 결과」 포함 SHA-256 **de9230be4ff2f4c33ce2c2e67358e58272cc4ca1ec2571becb7e5790f3151aba** 일치.
- 실제 build 위치는 **C:/work/gnlink-wt/ka-pc-build/apps/native_poc/Release**다. managed/shell 시험, GNLinkClient, GNLinkViewerKaTest, GNLinkStream, 배포 UI html의 **6개 artifact 전체 SHA-256**을 보고의 고정값과 직접 대조했다. source server.js의 전체 hash **5a22f37280fe412f32f94b34b155e9091500b3694eadd020e9c15c67486e67db**도 일치한다.
- shell log SHA-256 **0bc53af52c4b2688ab7a2b2097e6b4982b4fcee3ee7680cb7b6ba36a3655a791**, managed UTF-16 log **58a6e1251a72b4566f8ffd3e978ce4fe4e54da0db5fb0799a885e56235f04988**를 직접 확인했다. 로그·이미지·trailer 검토 시점 hash는 `.claude/codex_ka_pc_review/fin1_evidence_hashes.json`에 보존했다.
- slot/CreateNew·자기 해제 및 설치 제품 pid 28932/27044 무접촉은 작업용 실행 보고와 launcher의 시험 대상 제한 근거다. Codex가 그 당시 설치 제품의 OS 활동을 감사한 것은 아니다. 이번에는 Host/Viewer/WebView·시험 재실행이 없다.

## Shell 후보 수용 범위

원문에서 **148 PASS / FAIL 0 / ALL PASS (0 failed)**, 8개 child의 종료 확인·EOF·로그 보존, 5개 Host launch의 final OFF 두 줄·완료 출력·무상충 판정을 확인했다. rc 0은 실행 보고와 wrapper 기록 범위다.

다음 근거가 있어 단순 총합이 아닌 **해당 사용자 행동 경로의 후보**로 수용한다.

- 실제 WebView preflight와 거부 대조, Environment7 getter의 자기 run UDF 일치, 이후 자식 macro용 환경 분리.
- DOM 조작으로 sign-in·PC 카드 접속·창 닫기/목록 복귀·standby 전환·재접속을 진행하고 상태를 읽음.
- 준비 중 1개 예약과 성공 뒤 축출, 열려 있는 PC 3대 한도, 실패/취소된 4번째의 예약 반납, PC별 연결 중 상태가 다른 PC 결과로 풀리지 않는 것을 검사함.
- 이미 열린 창 재탭, 연결 취소와 owner 종료 구분, 로그아웃의 held release 선행·Host 수신 ACK 이후 teardown 및 다른 소유자의 viewer 계속 동작을 검사함. Host의 key-up은 input OFF 상태의 **수신/ACK**이며 실제 OS 주입 성공이 아니다.
- 새 owner에 옛 connect 통지가 영향을 주지 않는 대조와 pump/exit 순서 경계를 검사함. 일부 반례는 native event를 직접 투입하는 시험이며, 그것을 모두 사용자의 DOM 클릭이라고 합치지 않음.

Codex가 보존 이미지 **2-three-standby.png / 3-three-active.png / 6-signed-out.png**를 직접 열람했다. A/B/C 대기 카드, 활성 PC 카드 상태, 로그아웃 후 빈 비밀번호의 sign-in 화면이 표시된 것을 확인했다. 이는 CapturePreview로 얻은 **시험 WebView render**이며 사용자 바탕화면을 새로 캡처한 것이 아니다.

macro는 **열지 않는 지정 시나리오에서 예상 밖 경로가 검출되지 않음**으로만 기록한다. polling/폴더 부재를 macro 접근 0 또는 실제 macro getter/기능 성공으로 확대하지 않는다. ready→auto-login 코드는 시험 저장소/loopback에 연결돼 있으므로 “로그인 코드 0”도 주장하지 않는다.

normal GNLinkClient.exe의 hash를 확인했다는 사실과 그 exe 자체를 구동했다는 사실도 구분한다. 이 shell 시험은 기존 승인한 seam으로 제품 shell.cpp/page/bridge를 사용하고 GNLinkViewerKaTest를 실행한 경로다. 정상 제품의 전체 startup·설치·운영 endpoint 검증 또는 IME/실 clipboard/실 NAT·최종 통합 EVIDENCE는 별도다.

## Managed — 원인 범위와 기존 결과 유지

managed는 **rc 1, PASS 58, FAIL 3행, 최종 1 FAILED**다. 로그에서 3행 모두 viewer-C의 producer end 확인 실패 하나에서 파생됨을 확인했다. 다른 기능 assertion의 FAIL 행은 없다. 하지만 이 실행을 소급해 ALL PASS로 바꾸지 않는다.

고정 source의 crash 단계는 ready인 viewer-C의 마지막 job handle을 CloseHandle하고 job=null로 만든 뒤, **원래 process handle의 signal을 Exited(5000)으로 확인**한다. 그 check는 0 ms PASS다. 이후 공용 End는 null job을 e2e_end_job_confirmed에 넘겨 false를 받는다. 실제 trailer도:

```
producer end: NOT confirmed
read: end of output (error 0) after 243 bytes
log: kept
```

따라서 **이미 의도적으로 닫은 job의 별도 종료 증거를 End에 전달하지 못한 하네스 문제**라는 설명에 근거가 있다. 이 관측을 “제품에 다른 결함이 전혀 없다”는 보증으로 확대하지 않는다.

## 보수안 결정 — 조건부 수용

**이 crash case의 명시적 소유 이전/close 후에는 process 종료 + EOF로 생산자 종료를 판정하는 보수를 수용한다.** 다만 `job == nullptr`이면 일반적으로 성공하는 fallback은 허용하지 않는다.

필요한 경계는 다음과 같다.

1. CloseJobForCrash 같은 **명시적 경로/증거 상태**로 이 시험이 자기 검증된 KILL_ON_JOB_CLOSE job을 닫았음을 전달한다. close 성공, 그 전에 살아 있던 해당 root와 job 소유/membership 근거가 있어야 한다. failed close·미시작·우발적 null은 이 상태를 만들지 않는다.
2. 이번 case는 Activate하지 않은 ready managed viewer-C다. **하위 전체의 active=0 대신 root process+EOF를 쓰려면 단일 root라는 범위를 확인**한다(예: close 직전 job의 단일 활성 member와 원래 process handle 확인). 여러 descendants가 있거나 Host monitor가 있는 일반 child에는 이 대체 판정을 확대하지 않는다.
3. 같은 process handle의 종료 signal, reader의 실제 EOF, log/trailer 보존을 모두 요구한다. timeout/Error/Cut/Running/로그 실패는 그대로 비성공이다. EOF만으로 종료를 추정하지 않는다.
4. trailer에는 **explicit job close 뒤 process/EOF로 확인한 증거 유형**을 적는다. 이미 닫힌 job의 aggregate active=0을 실제 조회했다고 하지 않는다. 원래 job을 보유한 정상 End는 기존의 job 전체 종료 확인을 유지한다.

이 시험은 실제 last-handle close 동작을 검사한다. 확인용 duplicate job handle을 계속 보유하거나 TerminateJobObject로 바꾸고도 같은 KILL_ON_JOB_CLOSE 검증이라고 부르지 않는다. 종료 관찰에 필요한 process handle과 job handle의 역할을 구분한다.

Host 없는 Child self-test로 명시적 close의 정상 root 종료/EOF 긍정 대조, **증거 없는 null job 거부**, 실패한 close/종료 미확인/read 오류 거부 및 기존 정상 job 경로를 확인한다. 새로운 일반 null 허용으로 assertion만 통과시키지 않는다.

## 재실행과 다음 단계

위 계약 안의 **한정 시험 하네스 보수**로 취합해 고정 diff·새 artifact hash·Child self-test 결과를 연결하고, 작업용의 실제 Host 회귀 실행은 **managed만** 한 번 새 run/tag로 수행하면 된다. 같은 설계의 재상신이나 정상 shell 148개 전량 반복을 요구하지 않는다.

shell의 product/UI/Viewer 동작 및 정상 Child 종료 의미가 그대로라면 기존 shell 후보 자료는 보존할 수 있다. 공유 header 때문에 새 test binary hash가 달라져도 옛 log를 새 exe로 실행한 것처럼 라벨을 바꾸지 않는다. 정상 경로나 Host/다중 process의 종료 의미까지 바꾸면 그 영향 범위를 다시 검토한다. 마지막 PC 독립 검사·통합 관문은 구현 task의 역할대로 이어간다.

첫 실행의 PowerShell -File 배열 인자 결합은 **실제로 실행한 managed 1건**으로 기록한다. 그때 shell까지 실행됐다고 세지 않는다. 두 번째 shell의 새 slot/run 결과와 구분하고, 보수 후 managed 단일 호출을 사용해 같은 인자 오류를 피한다. 현재 r1 log/trailer와 새 결과를 덮어쓰지 않는다.

old-host NOT RUN, 실제 사용자 입력/clipboard·profile 조작 제한 및 다른 task의 미결은 유지한다. 이번 Codex는 source/hash/log/image 읽기와 판정 기록만 했고 제품 변경·build/시험 실행·운영·설치·삭제·commit/push는 없다. 사용자 현재 조치 없음.
