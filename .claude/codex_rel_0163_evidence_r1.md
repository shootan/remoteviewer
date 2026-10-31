# PC 0.2.163 r1 — 통합·산출물·EVIDENCE 검토

2026-10-26 / 검증용 remote#yj8mrmrx / 기존 릴리스 task **t-bpwzezfg** / build **90192f49ffee4d6dd56ea1db1552399de8999258** / gate·문서 tip **66bb5e92b0225f7cefbb38a89a702bf9247ebe65**.

**통합 SOURCE·산출물 정합은 OK. EVIDENCE는 자동 가능한 실제 renderer 출력 한 묶음 보완 전까지 보류한다.** 이미 승인된 게시/push 권한을 다시 묻는 절차가 아니다. AGENTS.md 최우선6/배포 완료 기준과 기존 capture SOURCE r1~r4가 정한 사용자 행동 검증 중 **정지 첫 화면 및 desktop 전환 뒤 render**가 아직 제출되지 않았다. 현재 자료로 '사용자 UAC만 남았다' 또는 '서명·실게시 GO'라고 판정하지 않는다. 아래 E1만 보완하고 이미 통과한 전량 시험을 반복하지 않는다.

## 직접 확인한 통합·산출물

- merge f0a388c의 parents는 main a1c551c + SOURCE OK adc9482다. **adc9482→merge의 apps/libs diff0**을 직접 확인했다. merge 뒤90192f4는 product_version.hpp의0.2.162→0.2.163만 바꿨고,90192f4→66bb5e9 apps/libs diff0이다. 미승인 KA spike/SH/APK 제품은 이 PC 후보에 없다.
- product_version.hpp의 git blob은 BOM을 보존하고 버전 치환만 일치한다. checkout은1020B/BOM efbbbf/CRLF17로 보고와 일치한다(git blob의 LF 정규화와 checkout CRLF를 혼동하지 않는다). rel worktree tip66bb5e9/tracked clean.
- 미서명 manifest **5ea1ac7453465829a369498f795929e11dd7050dcc409b5f96e92e484f60a476**, SHA256SUMS **9b1a21dd98b831152f342a8e29c9b7be53e81a0872c5664b16affccbc3c172b5**를 직접 계산했다. manifest11개 artifact의 **크기·SHA256 전부 로컬 payload와 일치**한다. 현재windows.sig는 없다(서명 전).
- payload Viewer/Stream/Host/Client의 UTF16 0.2.163은 각1개, Setup5개,0.2.162는0. 나머지4exe는 원래 버전 literal이 없고0/0이다. Stream **ddc5b39988eed4052e51d05073d329a075c056adf3767cc3ec1aebaf883838e8**, Viewer **13d9467cd1e5242cf9e9f7acf7d56111c420e360fd83e71cdf8f0807a9e90381**, Setup **4999bfaeb0765c18bcdffcd48ff29bac2c81c8e6b5bff5eae7902ffd7dfcf265**로 고정한다.
- 새 gate diff는 기존 TEST_MARKERS의 LK/capture 표식을 추가하며 기존 검사를 제거하지 않는다. **Codex가 --dir payload를 직접 실행해 rc0/ALL PASS9** 확인했다. self-test51·실 시험 Stream 부정 대조는 검증용/작업용 보고 귀속이다(읽기 전용 gate만 재실행했고 self-test는 반복하지 않음).
- build-0.2.163의 **출하10 vcxproj + capture library** PreprocessorDefinitions를 직접 읽어 시험/seam/mutant define0 확인했다. TestStill Off·시험 hook 제품 호출0은 기존 SOURCE 검토와 apps/libs 동일성에 귀속한다. ForTest 문자열0만으로 hook 부재를 증명했다고 쓰지 않는다.
- 검증용 hashes_before.txt의5개 exe를 현재 파일과 다시 대조해5/5 일치했다. 시험 exe와 출하 exe가 다르다는 설명은 적절하다. 같은 product source+시험 대역의 근거로 보며 fixture를 SYSTEM/Winlogon/실 UAC 성공으로 확대하지 않는다.

## 수용하는 독립 실행 근거와 남은 E1

검증용의 PS32·UA54·FullFrame18·LK68/79·출하 Stream real-recv PASS 원문을 대조했다. 최초 SKIPPED를 PASS로 세지 않고 허용 env 설정 뒤 실행한 자료로 판정한 것도 맞다. 이들은 **검증용 독립 실행**, Codex는 로그/산출물 검토자다.

LK는 실제 제품 window procedure/보이는 모달창에서 입력·취소·Busy·교체·재시도·결과 문구와 잘림 여부까지 검사하고 서비스 대역의 바이트 교체/옛 요청 종료를 연결한다. 실제 LogonUI는 아니지만 현재 자동 계약의 유효한 행동 근거다.

반면 PS/UA의 present thread는 FrameBuffer를 읽고 lastPresented를 직접 갱신한다. real-recv-smoke도 실제 renderer를 호출하지 않는다(제출 telemetry의 d3dPresentSuccess/gdiFallbackPresented/paintEnter도0). 따라서 PS486/154/185ms는 **첫 디코드/FrameBuffer 도착**이지 첫 실제 표시 지연이 아니다. UA도 source/IDR/수신·decode 계층의 근거이며 **보이는 창이 전환 뒤 새 내용을 표시하고 복귀했다는 근거는 아니다**. 이는 이미 SOURCE r1~r4와 릴리스 정본의 한계로 명시했던 항목이다.

**E1 — 같은 후보에 실제 renderer를 붙인 최소 자동 행동 증거를 추가한다.** 설치본·서비스·실 UAC 조작 없이 격리된 product-equivalent host/Viewer로 다음을 한 묶음 검증한다.

1. PS still/prelogin 이름 대역: picker10초→사용자와 같은 desktop 선택→**실제 presented 원본 화면≤1초**·눈으로 확인 가능한 캡처/marker, 이후 정지 화면의 최소 갱신·워커 생존. 현재32항목 전부를 다시 돌릴 필요는 없다.
2. UA 이름 대역: 이미WGC인 같은 control 연결에서 secure→Default를2회 왕복하고, 전환별 새 source/IDR과 **렌더된 구별 가능한 내용/스크린샷**을 연결한다. 과거 raw/AU를 내보내지 않는 기존 source fence 근거도 함께 대조한다. 단순 lastPresented setter/디코드 총합/현재 AU header를 지연 출력 그림에 붙인 것만으로 판정하지 않는다.

기존 renderer/하네스를 재사용하고 제품 소스를 바꾸지 않는 제한 실행을 우선한다. 시험 배선 보강이 필요하면 시험 변경·exe 해시를 별도로 고정하고 **product source와 릴리스 payload 해시가 그대로**임을 명시한다. 자동화 가능한 표시를 '실 UAC여서 사용자만 가능'으로 묶지 않는다. actual Winlogon/UAC 픽셀 자체는 여전히 사용자 실기다. E1 결과는 기존 VERIFY_0163.md와 한 추가 결과 packet에 취합해 Codex로 회신한다. 새 추상 승인 요청·전체 빌드/전량 재검사는 불필요하다.

## DW·문서 범위의 정정

작업용 DW runner의 복원 screenshot `4-restored.bmp`를 Codex가 열어 실제 stream이 표시된 것은 확인했다. 해당 runner의 UI 재접속·복원 사슬 근거로 수용한다. **이는 PS still/UA 같은 연결 edge의 renderer 증거를 대신하지는 않는다.** runner는 작업용 실행이며 검증용 독립 실행으로 표기하지 않는다.

runner의 `not recorded reason=folder-unresolved err=3`는 **영속 격리 기록 실패**다. 현재 runner는 recorded/not recorded 둘 다 '시도함'이면 PASS하도록 작성돼 있다. 따라서 UI 복원/이번 실행의 WGC 재기동 PASS와 영속 store 쓰기·다음 프로세스/재부팅 유지 미확인은 분리한다. 이를 '격리 영속화 PASS'라고 요약하지 말고 fixture 경로 문제의 원인·다음 담당을 원장에 둔다. 기존 DW store source가 이번 릴리스에서 바뀐 것은 아니므로 이 관찰만으로 제품 회귀를 단정하거나 DW 설계 전체를 다시 열지 않는다. 자동으로 해결할 수 있는 fixture 기록 검사는 가능한 범위에서 좁게 보완한다.

PC 먼저/APK 나중 분리는 수용한다. 이번0.2.163에 SH r2/새 APK UI/KA/NP는 미포함이며 APK0.2.32는 작업용2의 별도 고정 후보·검증·게시다. PC 게시 단계에서는 Android manifest/sig pair가 그대로인지 확인하고, 나중 APK 게시와 원자적 동시 배포였다고 주장하지 않는다. 구 APK0.2.31의 WGC 요청은 새 Host의 해당 세션에만 적용되는 호환 한계와 폰 sealed 잠금 입력 미구현도 그대로다.

## 다음 담당·게시 경계

검증용이 **기존 rel-0163/t-bpwzezfg 흐름**에서 E1을 보완하고 Codex의 이 판정을 업데이트받는다. 후보를 위한 로컬 준비·독립 시험·기록 취합은 계속 진행한다. **실 NAS 게시/main push는 이번 EVIDENCE 보류 해소 뒤** 기존 사용자 승인대로 서명→signature/변조 거부→deploy dry-run→실게시→외부 HTTPS11개hash·manifest서명/버전·Androidpair불변→main/docs일반push/실remotehash→dist/FINAL_PACKET으로 잇는다. 반복적인 사용자 게시 허가를 묻는 것이 아니다.

현재 실제 SYSTEM/Winlogon·실 UAC 예/아니오·틀린 암호 후 진짜 LogonUI 재시도·회사 PC/폰은 미검증이며, 위 자동 렌더 증거와 분리한다. 사용자의 설치/재시작/서비스 교체·UAC승인·재부팅은 에이전트가 임의 수행하지 않는다. E1 전에는 '에이전트 작업 완료·사용자 UAC만 대기'로 닫지 않는다.

이번 Codex 변경은 미추적 판정서/인계뿐이고 제품·공개 문서 편집/빌드/설치/서명/게시/commit/push 없음. hash/정적define/읽기 전용 gate 실행 및 보존 이미지 열람을 직접 수행했다. 지금 사용자 조치는 없다.
