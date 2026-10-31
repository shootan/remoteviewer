# APK X fence r4 — SOURCE 재검토

**SOURCE OK — 7f07f1aecfd33f9eb3e588c0ae40fe5008d11b42. r3 N1(미송신 down + 입구 거부 up)은 닫는다.** APK 0.2.33 전체·NDK/JNI·실 Host/AVD 또는 출하 승인이 아니다.

## 고정과 변경

- 비교 7c5d027..7f07f1a: ledger header, controller QueueInputEvent의 enqueue/NoteQueued 연결, 기존 fake wire 시험의 3파일 +183/-7. 제품 shared_core·JNI·session hpp는 변경 0이다.
- report_apk_xfence_r4.md SHA-256 064f6216241549fe66cf85900a41a0305a4f4c51e66a83cd5b456e50ca5bc54f 일치. tracked 변경 없음, diff --check 통과. worktree의 미추적 .claude/는 이 고정 소스 판정에 넣지 않았다.
- 소스는 git object로 읽고, 별도 D repository 안 검토 폴더에 고정한 6개 cpp와 의존 header로 component 시험을 빌드했다. 실제 GNLinkStream/Viewer/AVD·입력 주입·전원·clipboard API는 실행하지 않았다.

## N1 보수 수용

QueueInputEvent는 기존 parkMu_ 아래에서 입구 판정, 메시지 seq 생성, Enqueue, NoteQueued를 연결한다. sender의 ledger 판정과 연결 Reset도 같은 소유 lock을 사용한다. entry와 sender의 버튼 bool만으로 오래된 press의 결과를 추측하던 문제를 queue seq로 연결한 것을 수용한다.

- 아직 sender의 X fence에 도달하지 않은 down은 pending에 기록한다. 그 up이 no-X 입구에서 거부되면 해당 버튼의 pending seq를 cancelled로 옮긴다.
- sender는 X capability가 복귀해도 그 seq의 down을 폐기하고, 그 press의 X held bit도 숨긴다. 취소된 action에는 OnSentAction이 적용되지 않아 새 owed가 생기지 않는다.
- 입구를 통과한 up은 queue에 함께 들어가므로 정상 quick pair의 pending을 정리하며 down을 취소하지 않는다. entryHeld=false라는 이유만으로 정상 quick pair를 폐기하지 않는다.
- sender fence를 통과한 down은 pending에서 빠지고, 실제 송신·ACK 완료 뒤 기존 owed에 기록된다. cancellation은 기존 owed를 지우지 않는다. 연결 Reset은 두 신규 seq 장부도 비운다.
- no-X wire에 owed release나 X bit를 예외로 여는 변경은 없다. capability 감소 뒤 **owed 포함 park/drain의 bounded 실패 때 세션 종료**라는 기존 수용 범위도 유지한다.

## Codex 직접 확인 — 실제 controller와 loopback 가짜 Host

고정 시험의 phase 대조를 직접 실행했다. 가짜 Host는 loopback socket에서 wire 메시지를 기록하고 ACK/Pong을 제어하며 OS 입력을 수행하지 않는다. control sender가 앞선 key ACK를 기다리고 다음 Ping을 보류하는 사이 down은 queue에 남고, no-X Pong 뒤 up은 입구에서 거부되며 X Pong 뒤 sender가 진행하는 순서를 검사했다.

직접 출력:

```
phases: keysBeforeB=2, X1down=0, X1up=0
after X returns: wire=9, Xedges=0, Xbits=0
move: buttons=0x1 (left only)
park: Parked, parkReleased=0, X release=0
next quick pair: wire=2, Xedges=2
RESULT: ALL PASS (75 checks, 0 failed), rc=0
```

따라서 앞선 r3 반례의 **늦은 orphan down 없음**, 기존 owed 두 경로·unknown/no-X 전환·Reset·X2/left 및 다음 정상 pair 보존을 이 구성 단위에서 확인했다. test의 down=1/up=0 표기는 QueueInputEvent 반환값이며 wire down/up 개수가 아니다. wire 수치는 X1down=0/X1up=0으로 별도 확인했다.

빌드·로그:

- .claude/codex_test_injection_review/apk_xfence_7f07/xfence_7f07.exe
- exe SHA-256 674e192c1a5f18a757ec38261e6af13ec43c7b3dff2828f9de62413198098088
- 같은 폴더 build.log / run.log 및 고정 소스 27파일 보존.
- run.log SHA-256 4693ee4adb20c01a2c826615b30fa58a03faae7819433fc7427312df4b6dbbff

작업용의 75/75×6, NoteQueued 무동작 mutant 3 FAIL, 소비 target 21개 재빌드·role_park 13/13·prelogin_select 60/60은 보고 근거다. Codex는 위 별도 빌드 1회를 직접 실행했고 mutant/소비 시험을 재실행하지 않았다. NoteQueued 무동작 대조는 r3와 같은 pending 취소 부재를 모델링한 것이며 원래 r3 header 그대로 새 시험을 빌드한 것으로 부르지 않는다.

## 남은 검증과 다음 단계

검증용은 같은 APK X task의 이 SOURCE 고정본에 대한 독립 구성 단위 검사로 이어갈 수 있다. 별도 설계 변경이나 새 실행 go는 필요 없다. 이 시험은 제품 Host·GPU·AVD를 띄우지 않는 가짜 protocol peer 구성 단위이며, 실제 앱 시험의 공용 guard/slot 규칙을 완화하지 않는다.

NDK/JNI/4 ABI, 실제 APK 입력·macro/park 동작, PC cd9937a의 shared queue X up 보존 변경을 합친 통합 후보는 아직 미검증이다. 최종 통합에서 queue의 DropAllButReleases/backpressure/expiry와 X pending pair·owed 해제의 조합을 확인하고, source/component 성공을 앱 행동 성공으로 바꾸지 않는다. 다른 e 배선 미결과 APK 전체 출하 관문도 별도다.

제품 수정·전체 제품/NDK 빌드·설치·운영 변경·외부 삭제·commit/push는 없다. 이번에는 검토 문서와 자기 component 산출물만 보존했다. 검증용은 기존 구현/안전 기록에 취합한다. 사용자 현재 조치 없음.
