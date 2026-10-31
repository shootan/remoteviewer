# old APK P — 시작 전제 INVALID와 다음1회 조건

2026-10-30 · Codex remote#tt7fcvky → 검증용 remote#7fsfy22z

## 판정

**이번 P는 INVALID로 보존한다. 제품/새 Host 호환 PASS·FAIL 근거가 아니다.** install/pm clear/실제 APK hash 확인 뒤, 앱의 auto_login NO_CREDENTIAL 전제에 도달하기 전에 시한이 끝났다. ②~⑦은 NOT RUN이다. signal9는 하네스가 중단한 시각과 맞으므로 이를 앱 자체 crash라고 기록하지 않는다.

Codex가 `oa-p-direct-check-20261030083149-29848`의 steps와 logcat 및 현재 p_first_connect를 읽었다. 30초가 wait_log에 고정돼 있고 진단 파일을 읽지 못한 채 끝난 것을 확인했다. class 검증이 늦게까지 진행된 정황은 수용하지만, 작은 accessor의9.66 bytecodes/s 한 줄만으로 전체 cold-start 원인·부하 원인까지 확정하지 않는다. 이번에는 부하 표본도 없다.

## 선택: (나) 유한 startup 전제 시한 보수

**하네스의 P startup 전제만120초로 넓히는 보수를 진행한다.** 부하가 낮아질 때까지 반복 대기한 뒤 같은30초를 다시 실행하는 (가)는 선택하지 않는다. 원문/첫 INVALID를 보존하며, 고정 보수본 Source 확인 뒤 **P 독립1회만** 다시 실행한다. 지금 수정 없는 c45로 다시 돌리지는 않는다.

같은 old-apk-lane 지시서 안에서 작업용1이 이 작은 보수를 먼저 고정한다. 현재 C prefix 설계나 구 stack 참조 전체 변경을 이 diff에 섞지 않는다.

### 동작 범위

- install 성공·pm clear 성공·되받은 게시 APK hash1a3eee63 확인은 그대로다. 대상은 기존 시험 AVD의 한 package뿐이다.
- **am start는1회**다. 앱을 먼저 데워 두거나, 사전 launch/재launch·dex/profile 강제 조작·추가 install/clear·adb 서버/AVD 재시작으로 우회하지 않는다.
- startup 전제의 전체 budget은 am start 시작 기준 **monotonic120초**다. am start 자체와 각 log pull도 남은 budget으로 제한한다. 현재 oa_adb.adb는 subprocess timeout이 없어 wait_log 인자만120으로 바꾸면 유한 상한이 되지 않는다. 이 P helper에서 per-call 상한(예: 최대5초와 남은 budget의 최소값)을 두고, timeout child를 회수한다. generic UI/stream/watch 시한을 전부 늘리지 않는다.
- 로그 파일이 아직 없거나 pull이 실패하면 **전제 미확인**으로만 기다린다. 빈 문자열/sign_in_store0만으로 성공시키지 않는다. 성공한 같은 snapshot에서 **NO_CREDENTIAL과 sign_in_store 부재**를 확인한 뒤에만 manual button/다음 단계로 간다.
- 다른 auto_login 결과나 sign_in_store가 나오면 deadline까지 기다리지 말고 즉시 INVALID/force-stop·tap0이다. log read 실패를 sign-in store가 비어 있다는 근거로 쓰지 않는다.
- deadline 만료도 INVALID/tap0·원문 보존·기존 finally 정리다. timeout을 여러 번 갱신하거나 loop마다120초를 다시 주지 않는다. 실제 elapsed·read 성공/실패/파일 미생성 구분과 cold-start 경과를 남기되 비밀 입력은 기록하지 않는다.
- snapshot은 소유 run 내부에서 읽기 성공이 구분되게 보존한다. 이전 local pull 파일을 새 성공으로 오인하거나 외부 TEMP를 만들고 지우지 않는다. 상위 job/run 시한이 이120초 준비를 먼저 끊지 않는지 확인하되 전체 run도 계속 유한하게 둔다.

### 필요한 기기 없는 대조

새 helper/실제 흐름으로 다음을 한 번 확인한다. 30초 뒤(예:60~90초)에 생기는 정상 NO_CREDENTIAL는 통과하고,120초를 넘는 것은 INVALID다. 파일 미생성·연속 pull 실패·늦거나 멈춘 pull도 유한 deadline/tap0이어야 한다. 잘못된 auto_login/sign_in_store는 즉시 거부하며, install/clear 실패의 기존 start0도 유지한다. 가상 clock/대역 adb로 하되 성공 조건을 낮추지 않는다. 실제 am start1회와 그 전후 다음 입력0을 확인한다.

고정 diff/commit·관련 대조·실제 helper의 deadline/회수 근거를 한 SOURCE 패킷으로 잇는다. 이 단계는 **보수 준비 승인**이며 새 시간 예산/흐름이 있는 바이너리를 검토 전 실제로 재실행하라는 승인이 아니다. 검토 후 runner의 정확 source/파일 hash gate를 갱신해1회로 이어 간다. 임시로 기존 gate를 없애지 않는다.

## 재실행의 환경·보고

새1회 전 고정30초 동안 SDK/boot/guest load와 PC 큰 build 유무를 읽고 기록한다. 수치가 선호하는 값이 될 때까지 반복하지 않는다. 측정 창에는 큰 build/다른 GPU·AVD 작업을 겹치지 않게 기존 검증용 창구가 조율하며 사용자 프로그램은 건드리지 않는다. runtime load도 별도로 기록한다.

NO_CREDENTIAL에 도달한 시간은 실제값으로 보고한다. 이 setup 여유를 old APK의 빠른 cold-start 성능 검증이라고 부르지 않는다. 그 뒤 SH-off/first frame/30초 유지/입력/P-5d/P-6r/standby 공존과 기존 input/power·EOF/job·SDK/slot 경계는 바꾸지 않는다. P는 TCP control 경로라 S UDP 종료를 단독으로 닫지 않는 앞 판정도 그대로다.

PC0.2.164/서버 출하 HOLD는 유지한다. 구 Host+구 server의 안전 참조 준비는 기존 scope로 계속하되, 이 startup 보수와 섞어 하나의 큰 후보로 만들지 않는다. 자동 retry/추가 S/물리 폰 설치는 승인하지 않는다. Codex 실제 Host/Viewer/adb/AVD·OS 입력·설치·배포·push는0이고, 제품/추적 파일 변경 없는 전문 검토라 제품 commit은 만들지 않았다. 사용자 현재 조치 없음.
