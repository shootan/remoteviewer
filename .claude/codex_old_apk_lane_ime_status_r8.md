# old APK lane — S ④와 연결 종료의 분리

2026-10-30. **S①②③의 부분 관측은 보존하고,④는 하네스의 영상 입력 전제 미충족으로 구분한다. 별도 NOT_CONNECTED/error 종료는 원인 미확정 호환 blocker다. 추가 S/P는 r7d와 기존 원인 조사 확인 전까지 보류한다.**

Codex가 검증용 worktree의 `oa-s-20261030025800-36232` result.json/steps/Host 원문과 `3_window_first_frame.png`를 직접 읽었다. 현재 pinned73206964·WINDOWS·Host applied/ack/streamGen2·select_ready/out의 chain은 기록돼 있고 이미지에도 pinned 영상 일부가 보인다. 다만 **IME가 하단과 입력 지점을 덮는다.** 이것은 전체 영상이 입력 가능한 상태라는 증거가 아니다.

steps의02:59:54 tap1200,540 뒤02:59:57 input-trace0은 실제 영상 영역 tap을 시도한 근거가 아니다. raw④FAIL은 유지하고 이를 입력 wire/Host 수신 호환 실패로 확정하지 않는다.⑤⑥⑦/P NOT RUN과 정리 residual0/ended_confirmed·OFF 근거는 보존한다. 현재 P를 같은 전제에서 실행하지 않은 판단을 수용한다.

한편 그 뒤 앱의 NOT_CONNECTED/error→HOSTS는 **IME 문제와 별도**다. Host의03:00:01.723 reason=none도 직접 확인했으나, 이는 Host가 PeerLost를 기록하지 않은 종료이며 클라이언트의 정상 종료 원인까지 증명하지 않는다. IME에 들어간 key/editor action이 무엇인지, native 실패·사용자 종료 호출/세대 변경·backlog가 무엇을 일으켰는지는 조사 전 확정하지 않는다.

이미 위임한 b554e76 source 조사는 같은 task로 계속한다. 최초tap·추가 하네스 호출·IME/editor action·native 종료·connection/sign-in 세대·Host 종료를 clock과 함께 맞추고, password/key text 같은 비밀을 추가 로그로 남기지 않는다. 이 종료까지 “하네스 INVALID” 안에 넣어 제품 가능성을 지우지 않는다.

## r7d의 범위

검증용이 이미 위임한 **IME 숨김 확인 + 영상 view의 실제 bounds/IME 비겹침 때만 tap**은 승인된 하네스 보수 범위다.

- IME 존재를 확인하지 않은 BACK/Enter 반복이나 keyboard/시스템 서비스 비활성화를 하지 않는다.
- 숨김 뒤 같은 viewer/연결 상태·대상 영상이 유지되는지 재확인한다. 다른 scene으로 돌아가 버린 상태를 정상 준비로 세지 않는다.
- tap 지점이 실제 영상 view 안·IME 밖이라는 사전 조건을 기록한다. overlay와 layout의 부재를 고정1200,540만으로 가정하지 않는다.
-④ 전제/입력이 실패하면 그 상태로 메뉴 wake tap/후속 시나리오를 계속하지 않고 원문을 보존해 정리한다.③ 이후 screenshot의 키보드/overlay 상태도 판정에 포함한다.
- 이것은 정확한 view→picture pixel mapping·실 OS 주입이나 별도 monitor 선택을 새로 요구하는 범위가 아니다. pinned window·Host input none/power off와 기존 guard/slot/job·SDK·단일 시험 package 경계는 그대로다.

현재 게시 APK 자체를 하네스 목적에 맞춰 수정하지 않는다. IME가 남는 원인이 제품 UI/하네스 입력 방식 중 무엇인지 구분하며, 기존 제품 UI의 한계도 시험 보수로 소급 해결됐다고 하지 않는다.

다음 패킷은 **고정 r7d diff/기기 없는 대조 + 이미 맡긴 연결 종료 조사**를 함께 잇는다. 현재 새 Source 후보가 없는 상태에서 P나 S를 반복하지 않는다. rel-0164 호환/출하 gate와 APK0.2.33·push 보류는 각각 유지한다. 사용자 조치 없음.

이번 Codex는 보존 로그·이미지 읽기와 판정만 했다. 실제 Host/Viewer/AVD/adb·GPU/OS 입력·앱 설치·운영·삭제·제품 변경·commit/push는 없다. 미추적 검토 기록만 작성했으므로 제품 commit은 만들지 않았다.
