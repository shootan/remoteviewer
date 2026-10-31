# SH strength r6 — SOURCE 완료 확인

2026-10-27 / 검증용 remote#yj8mrmrx / 입력 A2A r6 보수 요청 및 고정 diff.

**제품 SOURCE OK — r5 N1-a/b 닫음. 기존 c2 터치 스트림·layout 복귀·picker/UA2·evidence A 구성 승인은 유지한다. 아직 새 APK 빌드/독립 EVIDENCE·게시 완료는 아니다.**

제품 **d29a0348affeb642c5072e6ca57718b4b868b466**, 시험 **b554e762613e5de82584564a96713589c1c28e06** 두 commit 전체 diff·OutputLog/qualifier·실제 Activity 호출·classifier/출력 gate 연결을 읽었다. 각각 whitespace rc0. 검사 시 worktree HEAD=b554e76·tracked clean. 기존 정본 report_sh_strength_r2.md는 확인 시 r5까지 있으므로 r6 시험 주장은 A2A 요청·저장 로그/시험 코드 기준으로 구분한다.

## N1-a — renderer 간 token 중복: 닫음

각 log의 seq0 대신 companion의 **프로세스 전역 AtomicLong NEXT**에서 token을 발급한다. concurrent reserve도 하나의 증가 순번을 사용하고 renderer 재시작/release 대기에서 초기화하지 않는다. 현재 renderer log로 옛 renderer token을 조회하면 존재하지 않아 NONE_SHOWN으로 닫히며 옛 종류/세대를 새 output으로 잘못 해석하지 않는다.

양쪽 log가 같은 순번 output을 보유한 VIDEO↔CLEAR 교차 사례 및4개 log 동시8,000개 token 중복0 대조가 추가됐다. 기존64개 bounded 보관·실 swap 전 metadata 게시·실패 swap withdraw도 유지한다. 새로운 외부 상태·권한·프로토콜은 없다.

## N1-b — REDRAW가 첫 실제 표시인 경우: 닫음

- VIDEO와 REDRAW를 모두 실제 picture buffer로 보고, **원 decoder frame timestamp + surface를 LatchClassifier에 전달**한다. 그 frame을 처음 보여준 output이 REDRAW여도 firstShow=true다.
- 실제 Activity는 같은 SharpenLatch.qualify를 호출하고 firstShow에서만 NativeNotifyFrameDisplayed·원 native handoff→실제 latch 지연·출력 gate를 처리한다. 이후 같은 frame의 VIDEO/REDRAW는 redraw로 분리하므로 새로운 decode/첫 표시로 중복 계수하지 않는다.
- CLEAR/unknown/evict/실패 output은 picture가 아니며 **NONE_SHOWN=-1**을 반환한다. Activity가 이를 shown generation에 실제로 반영한 뒤 frame 처리 전에 반환하므로, 이전 표시 proof를 남겨 입력 가드를 여는 r5 경계도 닫혔다.
- direct path의 원 decoder timestamp는 기존 의미를 유지한다. renderer output token으로 nativeHandoffInfo를 잘못 조회하지 않고 기록된 원 frameTs를 사용한다. metadata 누락을 정상 output으로 만드는 fallback은 없다.

시험은 F의 VIDEO 미latch→F REDRAW 첫latch→출력 gate 확인1회→같은F VIDEO/REDRAW 재latch는추가0→CLEAR0→다음G 첫표시를 실제 Activity와 공유하는 qualifier로 검사한다. source code의 해당 연결을 대조했다. 경합/드라이버를 실제 AVD로 재현했다는 근거는 아니다.

## 근거와 다음 후보

단위277/0 및 변이3(log별counter·REDRAW 첫표시거부·CLEAR 이전proof유지)의 실패는 **작업용 실행 귀속**이다. Codex는 저장 gradle_shr6 및 A/B/C 변이 로그와 해당 시험 코드를 읽었고 직접 Gradle/AVD를 실행하지 않았다. 보고 정본의 r6 절/최종 빌드 기록은 새 후보 보고 때 함께 갱신하면 되며 추가 SOURCE 승인 왕복은 필요 없다.

검증용의 현재 **1d4a05f4 APK 배치**는 그 해시의 결과로 보존한다. 끝난 뒤 같은 작업용1이 승인된 r6 제품·시험을 포함한 **새 noseam/shseam APK**를 빌드하고 새 hash/버전·ABI·동일 signer·시험 gate/양성 대조를 고정한다. 이전 APK의 해시나 PASS를 새 출력 token/qualification의 실행 근거로 쓰지 않는다.

새 후보에서 검증용은 실제 buffer token→원 decoder frame 상관·first-output/정지 화면·renderer 교체/A↔B/off·clear/fallback·관련 입력 차단을 독립 확인하고, A 시험 Host의 실제 APK prelogin UI 및 UA2/기존 회귀를 최종 후보의 증거 범위대로 연결한다. 제품과 동등 시험 빌드가 증명하는 것/실 Winlogon·폰 S4/발열·최종체감이 남은 것을 분리한다. 관련 없는 순수 행렬/이미 닫힌 소스를 매 commit 전량 반복할 필요는 없다.

**SOURCE OK이므로 빌드·독립 검사 준비를 계속한다.** 최종 고정 후보의 EVIDENCE가 통과하면 기존 승인된 서명/게시/외부 확인·관련 push/dist/사용자 안내로 잇는다. 별도 추상 GO나 삭제 권한 질문을 새로 만들지 않는다. 자기 생성 slot 한 인스턴스 예외와 deletion_candidates 운영 규칙을 따른다.

사용자 현재 조치 없음. Codex 제품 수정·빌드·시험 실행·삭제·서명/게시·commit/push 없음. 이 미추적 판정서/인계 외 기록·계획·Git MCP commit은 같은 작업에서 취합한다.
