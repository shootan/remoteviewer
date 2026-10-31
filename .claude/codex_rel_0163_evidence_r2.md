# PC 0.2.163 최종 후보 — EVIDENCE 판정 r2

> **추가 정정(2026-10-28 Finding-3): 실 GNLinkStream 스트리밍 행동에는 입력 OFF와 별개인 wake/전원 효과도 적용된다.** PS/LK 및 해당 실 Host capture/render 근거는 안전한 재확인 대기이며, 관측 원문·SOURCE/산출물 정합은 보존한다. [입력·전원 효과 Finding-3](codex_test_injection_finding.md) 참조.

> **안전 판정 정정(2026-10-28 Finding-2): viewer_prelogin_login_e2e_test를 사용한 PS/LK 및 공유 행동 EVIDENCE는 관문 사용 보류/안전한 재확인 대기다.** 과거 기능 관측·이미지·SOURCE/산출물 정합은 보존하되 아래 최초 OK를 안전한 자동 검증 전체 완료로 인용하지 않는다. [입력 주입 Finding-2](codex_test_injection_finding.md) 참조.

2026-10-26 / 검증용 remote#yj8mrmrx / 기존 rel-0163·t-bpwzezfg / 최종 build **fae0e1fc787456e852af37af35de7b19f87f01ac** = SOURCE OK 제품eefac09 + 시험0854ac4 + docs.

**SOURCE/EVIDENCE OK. 기존 r1의 E1 보류를 해소한다. 이 고정 산출물의 서명·gnlink 게시·외부 검증·관련 main/docs 일반push·dist 진행 GO.** 사용자10-26 게시/push 승인 범위이며 별도 추상 go를 기다리지 않는다. 현재는 게시 전 판정이고 실제 게시 완료·실 UAC/Winlogon 성공 선언은 아니다.

## Codex 직접 확인

- 승인 제품과 최종 후보의 차이는 시험/문서다.0854ac4→fae0e1f apps/libs diff0, HEADfae0e1f/tracked clean을 확인했다. 기존 LK/capture 및 picker SOURCE r7 판정을 유지한다.
- 미서명 manifest **6d89ae487a125ebc620e46e6be5396cb3131dc6380878b596a85b47edf64dfd3**, SUMS **2a3f5163279d3e07d67b1ad6051cf59dc966f30548970c69961bdda1c49d6c0d**를 직접 계산했다. manifest의11개 artifact 크기·SHA256이 모두 payload와 일치한다.
- 고정 Stream **825feb97a067b7b7841a5317fca220c7b6fa59bb917412dc9cc96ed8a5791b35**, Viewer **0d47c4ba9daf038034c5fcf396a8dd5215e4aca41972e5c118ba6a8d0bd1d81f**, Setup **907e5702ac744c558e023f11d9751a8f7e2a3d6d5795123cbb2729ab8979cc1d**. shell8fac49cd/macro380c2326도 기존과 일치한다. 이전90192f4의 hash를 쓰지 않았다.
- Viewer/Stream/Host/Client의 UTF16 0.2.163은 각1개, Setup5개, 이전0.2.162는 모든 payload에서0. 버전 literal이 없는4exe는 기존처럼0/0이다.
- Codex가 읽기 전용 gate를 직접 실행해 rc0/ALL PASS9를 확인했다. fresh build-0.2.163-final의 출하10 vcxproj+capture library에서 시험/seam/mutant define0을 확인했다. self-test51은 검증용 독립 실행 보고에 귀속한다. 새 hook은 제품 소스에 선언·empty 검사 뒤 호출만 있고 설정 대입0임을 다시 확인했다.
- 검증용 hashes_before.txt의 시험6exe를 현재 파일과 대조해6/6 일치했다. 원문 요약 gate PASS/window proc55/PS39/UA-render28/LK68·79/UA54/FullFrame18/출하 Stream real-recv PASS를 확인했다. **실행 주체는 검증용이며 Codex는 증거 검토자다.**

## E1 보류 해소 근거

검증용 VERIFY_0163_FINAL.md와 원문을 대조했다.

- **PS 실제 renderer**: picker10초→실제 카드 클릭→D3D present **287ms**(success6→7), 다시10초 뒤 선택은 **297ms**다. 이후3초에6present, 역할 확정 전 시험에서는4present였다.500ms는 제품 목표 설정이며 모든 present 간격이 정확히500ms였다고 주장하지 않는다. 역할 전 클릭3494ms는 pong을4초 늦춘 fixture 결과로,1초 이내 측정에 합산하지 않는다. 해당 intent는1회 실행됐다.
- R3 채택 전 취소는 version75→75, permission→write는 blocked=1/version79→80으로 A write 선행, ticket 직후 취소는 **84→84/refused1→2**이며 B는 자기 ticket/key로 완료했다.39/39, present 실패0이다.
- **UA 실제 renderer**: WGC 같은 연결에서 secure↔Default를2회 왕복했다. 각 edge의 adoptedCaptureUs와 renderer presentedCaptureUs가 일치한다. green/blue/yellow/magenta의 각 스크린샷은 새 색100%·이전 색0%다.28/28이다.
- Codex가 보관된 ps_picker.png/ps_presented.png/ua_green.png를 직접 열어 picker→제품 뷰어 창 안의 원격 화면 및 초록색 출력을 확인했다. 원본 BMP와 전체 색 계측/host·present 연결은 검증용 독립 기록에 귀속한다. 이미지는 로컬 증거로 보관하며 공개 문서에 옮기지 않는다.

r1에서 빠졌던 decode/FrameBuffer→실제 renderer 구간이 채워졌다. 제품 picker의 ack 대기 결함을 시험에서 우회하지 않았다. 실 Winlogon/UAC 픽셀은 아니지만 자동 가능한 사용자 조작·표시 증거로 충족한다. 같은 승인을 위한 전량 재시험/추가 SOURCE 보수 요청은 없다.

## 남은 범위

실제 SYSTEM/Winlogon 재부팅 화면, 실제 UAC 예/아니오 픽셀, 실제 LogonUI에서 틀린 비밀번호→재시도→해제, requireAdministrator/SCM 업데이트, 회사 PC/NVIDIA, 실제 무변화 하드웨어·실폰은 미검증이다. 현재 자동 결과를 그 성공으로 확대하지 않고 게시 뒤 사용자 실기 묶음으로 연결한다.

PC0.2.163 먼저/APK0.2.32는 별도 후보·검증이다. 이번 PC에 SH r2/KA/NP/폰 sealed 입력까지 포함됐다고 쓰지 않는다. PC 게시 단계에서 Android manifest/sig pair는 변경하지 않고 최종 전후 hash로 확인한다.

이전 DW runner의 UI 복원·이번 실행 WGC는 유효하지만 folder-unresolved err3은 영속 격리 기록 실패로 남긴다. 해당 fixture 원인·후속 담당은 ledger로 연결한다. 이번 릴리스에서 DW store 제품은 바뀌지 않았으며 이 결과를 영속화 성공으로 바꾸어 쓰지 않는다.

## 다음 실행·최종 보고

검증용은 승인된 최종 rel 현물에 보호 키로 서명한다. manifest 본문 hash 불변·sig self/C++/JS 검증과 변조 거부→gnlink deploy dry-run→실게시→외부 HTTPS11개 hash·manifest 서명/버전·Android pair 불변→관련 main 반영/docs Git MCP commit·일반push·실 remote hash→dist를 계속한다. 키는 PC 밖으로 내보내지 않고 사용자 설치/재시작/UAC 승인·실서비스 조작은 자동 수행하지 않는다.

**한 FINAL_PACKET**에 build commit·서명 pair/hash·게시 URL·외부11개 검증·Android 전후 pair·main/remote hash·dist·남은 실기/ledger·사용자 다음 행동을 묶어 Codex에 보고한다. 실패한 단계만 구체 blocker로 구분하고 미실행을 성공으로 보고하지 않는다. Codex가 최종 증거를 확인해 사용자에게 **배포 완료(마일스톤)·실기 테스트 대기**를 안내한다. manifest 서명과 Setup Authenticode는 별개라는 기존 안내를 유지한다.

Codex는 이번 hash/정적 define·source/로그·보관 이미지와 읽기 전용 gate를 직접 확인했다. 제품 빌드/편집·서명/게시·commit/push는 직접 하지 않았다. 산출물은 미추적 판정서/인계뿐이고 공개 history·계획·Git MCP commit은 기존 릴리스 task에 취합한다. 지금 사용자 조치는 없다.
