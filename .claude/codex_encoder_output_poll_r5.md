# encoder-output-poll B — r6 최종 후보 검토

2026-10-18 · t-i5t7x1c5(parent t-0wsagafw) · 사용자 선택 B 진행.

**판정: SOURCE/EVIDENCE OK — rel/encoder-output-poll-b a428abf를 PC0.2.159 릴리스 후보로 승인한다.** main 통합·영향 회귀·버전 고정·서명 준비로 진행 가능하다. NAS 게시/요청된 push 실행은 현재 사용자 지시 범위를 따르며, 사용자가 별도 게시 지시를 기다리게 한 상태를 이 승인으로 해제하지 않는다. 아직 배포 완료는 아니다.

## 고정과 직접 확인

- branch HEAD a428abf928b257dd8c8bb66577bb88759a7b3153. main 기준 eedd207 → A01e0d22 → B13da0b7 → gate a428abf. 13da0b7→a428abf의 apps 제품 diff0을 직접 확인했다. C/C500 commit3723211·9994b33과 catch-up은 이 후보에 포함하지 않는다.
- 실제 파일 `rel-eop-b/build-ship/apps/native_poc/Release/GNLinkStream.exe` 및 `installer_payload/GNLinkStream.exe`의 SHA256을 직접 계산했다. 둘 다 **b335f534e1422d4d3ea071500d68139d90224645d9c10ca84b8461059c90e2f4**로 보고와 일치한다.
- 해당 실제 exe에 encoder_poll_build_gate.py를 직접 실행해 PASS(exit0), DIAG/catch-up/연구 문자열 부재를 확인했다. CMakeCache의 Release /O2 /Ob2 /DNDEBUG, ENCODER_POLL_DIAG=OFF를 확인했다. GPU/사용자 Host 실행·제품 빌드는 하지 않았다.
- r6 보고·릴리스 노트 초안·검증용 독립 r6 절 및 smoke 원자료 요약을 읽었다. poll33/33·인코더 회귀·clip32/32·fixed_server8과 실제 제품기본 smoke는 작업용 보고 인용, DIAG poll 분류는 검증용 독립 실행 인용이다.

## 1. smoke의 느린 episode 처리

B출하본의 6M/60 typechar89/92ms, 6M/30의173ms1episode는 **원인 미확정 관찰로 남기는 것으로 현재 후보 승인을 막지 않는다**. 조건은 다음과 같다.

- 모든 해당 smoke에서 연결/영상 수신·장면valid가 정상이었고 encode 실패0이다. 6M/60 같은 묶음의 순수 main 대조보다4episode 모두 빨랐다. 현재 자료에서 candidate가 만든 기능 실패·장기 정지 또는 재현되는 새 회귀로 확인되지 않았다.
- r1 smoke와 r6 smoke의 단순 차이는 다른 run 간 비교다. 원인 미확정이며 ‘환경 탓으로 해결’됐다고 분류하지 않는다. 6M/30은 해당 episode의 순수main 짝이 없으므로 무회귀 확정이라 쓰지 않는다.
- 검증용 DIAG실행의 held0/12·deadline-cut0·6M/60 p95=62, 6M/30 p95=67은 **다른 바이너리/다른 실행의 관측**이다. 출하본173ms 자체가 붙잡힘이 아니었다는 증거는 아니다. 출력 poll 보수의 독립 재현과 느린 출하episode 미재현까지로 쓴다.
- 출하 smoke에 trace가 없어 analysis의 held0/0·wireAU0 등으로 나온 것은 **관측불가**이지 붙잡힘/송신0의 증거가 아니다. 실제 수신·udpTxBytes와 분리한다.

같은 run을 원인이 나올 때까지 반복하거나 trace를 출하 기본에 켜서 시간을 늘릴 필요는 없다. 이번 승인 뒤 새 기능 실패/재현되는 회귀/장시간 정지가 나오면 그때 별도 근거로 중단한다. ‘항상54~76ms’, ‘첫입력붙잡힘 완치’, ‘모든버벅임 해결’ 문구는 허용하지 않는다.

## 2. b335f534와 이전 b357758f의 동등성

바이트 동일이 아니라는 보고는 맞다. 경로 문자열과 PE배치 차이, 함수표 .pdata 일치는 경로의 영향 설명을 지지하지만 **함수표가 같다는 사실만으로 전체 .text의 실행 의미 동등성을 증명하지는 못한다**. 정확한 전 코드 정규화/비교를 수행했다고 쓰지 않는다.

그 완전한 증명이 이번 승인에 필수인 것은 아니다. 현재 b335f534는 git 고정 source/Release설정·직접 해시/gate와 **실제 그 바이너리를 실행한 작업용4smoke**에 연결된다. 과거 b357758f를 조용히 대체한 것이 아니라 새 고정 산출물로 검증한 근거를 채택한다. 검증용 독립 DIAG는 동일 제품 source의 기능 확인으로 병기한다.

버전0.2.159를 올리면 다시 다른 바이너리가 된다. 실제 최종 source/version commit에서 빌드한 Release와 installer payload의 일치, 패키징 gates·출하문자열/버전·서명·외부hash는 그 최종 산출물로 확인해야 한다. b335f534의 서명·게시를 이미 완료한 것처럼 기록하지 않는다.

## 3. 사용자가 수용한 비용과 초안 정정

사용자가 자료를 보고 **B**를 선택했으므로, 기존 화질 무회귀 미충족을 계속 동일 이유로 반려하지 않는다. 선택한 것은 poll-only의 빠른 반응과 알려진 정지품질 비용이다. C500의 정제나 catch-up을 몰래 함께 출하하지 않는다.

초안의 다음 표현만 정리한다(제품 수정/새 승인 왕복 불필요).

- ‘화면이 움직이기 시작할 때 첫 반응이 빨라졌다’는 **AMD의 관측 조건에서 개선, 해당공통회수 공백보수**로 범위를 적는다. 다른HW/1.5M/모든정지를 고쳤다고 일반화하지 않는다.
- ink의 기준은 **최종 그림 paint+1/3s**임을 명시한다. 이전의 ‘최초표시+’ 오표기를 되살리지 않는다.
- 육안: Codex는 당시 짧은 캡처의 scroll05 A/C5002장을 직접 봤다. 검증용은 r5 typechar05 A/B2장을 봤다. **Codex도 r5 A/B 자료를 직접 보고 구별하기 어려웠다고 쓰지 않는다.** 각각 본 장면/실행의 한계가 다르다.
- callUs/wire/key 동일이라는 문장은 r1 통제3짝 관측으로 제한한다. r6 순수main smoke의key1 대 B2처럼 작은 smoke에서 다른 값이 있는 사실과 모순되게 모든 실행 불변을 주장하지 않는다.
- 느린 출하episode는 수치·n·미분류로 공개하고, DIAG재현에서 안 보였다는 사실을 옆에 적는다. 원인 미확정, 1.5M지속모드·NVIDIA/Intel/동기MFT·DesktopText·S1단발오류 경계 등 미검증도 유지한다.

## 4. 릴리스 경계

현재 로컬 main eedd207에서 승인 source까지 합치고 통합 영향 회귀를 거친 뒤 **PC0.2.159** 버전 고정/빌드/서명 절차로 잇는다. 그 사이 main에 다른 제품 변경이 들어오면 통합delta만 확인한다. APK keepalive-multi의 사용자 정책결정/게시와 별도 흐름을 유지한다. 이 PC릴리스 때문에 APK/Android manifest를 바꾸지 않는다.

버전 고정 후 최종 출하기본의 짧은 실행·정상연결/첫화면·관련회귀 및 build/payload/installer/signing gate를 한 묶음으로 기록한다. 동일 source조건에서 이미 지나간 전량 진단행렬을 반복할 필요는 없다. 사용자 게시 지시 후 NAS 자동 배포·외부 manifest 서명/버전·artifact hash·dist보존·원격정합 확인을 완료한 최종패킷을 보낸다. 정상push 허용 범위도 현재 사용자 지시를 따른다.

소스 승인/출하후보 승인과 **배포 완료·실기 성공**은 구분한다. 아직 실 사용자 PC에 설치하거나 재시작하지 않는다. 사용자가 지금 해야 하는 추가 실행은 없다.

Codex 산출물은 이 검토 파일과 실제 exe의 read-only hash/gate 확인뿐이다. 제품·빌드·설치본·버전·commit/push/게시를 변경하지 않았다.
