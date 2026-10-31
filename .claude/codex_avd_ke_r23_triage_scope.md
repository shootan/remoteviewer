# ke-r23 — 실패 분류와 읽기 조사 범위

2026-10-29 · **E2 FAIL 뒤 중단, APK 0.2.33 출하 보류 유지에 동의한다. 재실행 없는 1단계 조사를 수용한다.** 원인 판정이나 제품 변경 지시는 아니다. 기존 작업용1 task 정본에 아래 구분을 취합하면 된다.

## 확인한 자료와 상태

- report_avd_ke_r23.md SHA-256 **cce27d1a7e8757b8fcfffbd33c9d2012b2d4888696ef611f203f4bd41dd412f3** 일치.
- 실제 run의 slot/steps와 앱/Host 원문에서 E1 통과, E2 first-frame 25초 실패, 이후 메뉴 미도달 예외, 앱·Host peer_lost 및 job `residual=0 forced=0 ended_confirmed=1`을 직접 대조했다. pm clear 명령의 rc 0/Success도 기록돼 있다.
- E3 이후는 **중단으로 미실행**이며 계획상 NOT RUN(E7f/E8/E12)과 구분한다. 2/3 PASS를 KA 기능 관문 통과라고 하지 않는다.
- Host OFF/출력 완료와 정리는 안전 실행 관측으로 유지한다. b/c의 미선택에 따른 capture assertion 실패를 별도 제품 기능 결함으로 단정하지 않는다.

E2는 **standby activation 이전의 ordinary 접속**이다. 기본 선택→첫 영상이라는 필수 행동이 실패했으므로 이 경로를 해결하지 않은 채 APK 출하를 진행하지 않는다. 반대로 ke-r21과 증상이 비슷하다는 이유만으로 같은 root cause나 relay 결함이라고 확정하지 않는다.

## 조사에 포함할 핵심 구분

### video_debug는 수신 패킷 계수가 아님

고정 9f9a91c의 android_video_decoder.cpp를 읽었다.

- `in`의 inputFrameCount_는 **AMediaCodec_queueInputBuffer 성공 뒤** 증가한다(:861). `in=0`은 해당 집계/초기화 구간의 codec queue 성공이 없다는 값이지 UDP 수신/조립 전체 0의 증거가 아니다.
- OnEncodedH264FrameAs의 `stale` 증가는 owner 검사 뒤 selection gate가 거부한 frame의 값이다(:181).
- `inGen`은 같은 sink callback에서 gate를 통과한 frame의 generation을 기록한다(:199). 관측된 `inGen=2`는 일부 gen2 frame이 그 지점까지 왔다는 단서이며, 최근 IDR이 완전하게 도착했거나 계속 수신됐다는 증거는 아니다.

따라서 원래 계획의 Host 송신→relay→AVD 수신→조립→decoder를 **sink callback→선택 gate→IDR/codec 준비→실제 input queue→출력/present**까지 나눈다. counter의 reset 시점·세대/바인딩 범위를 먼저 맞추고, stale=8의 시각과 현재 정지 구간을 구분한다. 이미 수신된 delta/CSD와 필요한 IDR의 누락 가능성도 데이터로 가르며, 추정으로 어느 쪽을 채택하지 않는다.

### 제어와 영상의 종료를 분리

Host keyframe 생성/송신 로그는 relay 전달·AVD 수신의 증거와 다르다. frame/packet 단위로 보존 자료에서 확인 가능한 마지막 지점을 적는다. control ACK/probe/retransmit의 마지막 정상 시각과 video의 마지막 정상 시각도 별도로 본다. 공유 receive/dispatch thread가 있다면 그 소유·대기 경계도 source에서 확인한다. peer_lost는 종료 이유이며 영상 단절의 원인을 그 자체로 설명하지 않는다.

같은 흐름을 비교할 식별자는 Host run/session epoch·peer tuple/relay leg·stream ID/streamGeneration·frame/chunk ID다. Host KST와 앱 UTC 표시는 변환하고 실제 clock offset/각자의 monotonic 시각을 구분한다. 다른 세션/방향의 로그를 한 타임라인으로 잘못 이어 붙이지 않는다.

### 통과/실패 후보 차이

ke_r16–19와 ke_r21/23의 source commit뿐 아니라 **실제 Host/APK/server.js·fixture·하네스 hash/flag·emulator 상태·선택 경로**를 구분해 표로 비교한다. 제품/fixture/하네스 중 어느 범주인지 기존 증거로 좁히고, relay 계수가 없어서 구분 불가한 구간은 그대로 표시한다.

## 다음 단계

이미 지시한 읽기 조사 한 건을 계속한다. 현재 자료의 한계와 가능한 최소 추가 계측을 보고한 뒤, 필요하면 해당 경계의 고정 source/측정 계획을 판단한다. 이 통지만을 이유로 새로운 AVD/Host 실행, emulator reset, 계측·제품 수정을 시작하지 않는다. 기존 다른 독립 작업이나 안전 하네스 EVIDENCE를 전량 보류하지 않는다.

이번 Codex는 보고·일부 원문·counter source 읽기와 이 기록만 수행했다. 실행·기기 조작·제품 변경·삭제·commit/push는 없다. 사용자 현재 조치 없음.
