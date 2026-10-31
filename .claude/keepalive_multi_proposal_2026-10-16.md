# keepalive-multi 제안 — 여러 PC 의 연결을 동시에 유지 (검증용 remote#pvbntb3z, 2026-10-16)

## 사용자 요구 (2026-10-16, 0.2.30 게시 뒤 정정)
> "다른 pc 를 들어갔다 나와도 유지되어야지. A host pc, B host pc 가 있는데 지금은 B 연결하고 나가면 B 연결 유지되어 있다가 A 연결하면 B 가 끊기는데, 그게 아니라 B 를 계속 유지해야지. A 랑 B 랑 왔다갔다 하기 편하도록. 내가 원한 건 그거였는데."

## 현재(0.2.30) 와의 차이
- 설계 `codex_keepalive_r1.md` 는 "다른 host 선택 시 park 종료", 구현 `SessionPark.kt:13` "One parked connection at most". 사용자 요구와 어긋남(검증용이 요구 대조를 놓침).
- native 는 전역 단일 자원: `g_session_controller`(ClientSessionController 1개), `g_video_decoder_sink` 1개, `g_rendezvous` 1개, `g_connect_gate`(op/adopt 소유권, 한 세션 전제), `g_chosen_candidate`/`g_rendezvous_error`.
- host 쪽은 기존 `ControlStreamState`(stream pause)로 park 하므로 **PC 호스트 변경 없이** 각 host 가 자기 viewer 를 pause 상태로 들고 있으면 된다(0.2.158 지원).

## 검증용 제안 (Codex 가 확정·수정)
1. **동작**: PC 별로 연결을 하나씩 유지. 보고 있는 PC 는 1개(active), 나머지는 parked. 목록에서 parked PC 를 누르면 재연결 없이 그 연결로 복귀(현재 short park 재사용과 같은 경로), 방금 보던 PC 는 parked 로 내려간다.
2. **한도(사용자 확인 전 기본값 제안)**: 동시에 유지하는 PC **최대 3대**(active 포함). 넘치면 가장 오래 안 본 parked 부터 종료(LRU). parked 수명은 **PC 별로 마지막으로 떠난 시각부터 5 분**(현행 PARK_LIMIT_MS 유지). → 사용자에게 바꿀지 확인 예정(값만 바뀌는 상수).
3. **종료 조건(전부 종료)**: 로그아웃·계정/서버 변경·앱 백그라운드(onStop)·네트워크 경로 변경·명시적 '연결 해제'(그 PC) / '모두 끊기'(선택). **PC 별 종료**: 그 PC 의 peer lost·generation 변경·host 목록에서 사라짐·session_refused·만료·LRU.
4. **native 구조**: 전역 단일 → `hostId`(+account/directory identity) 키의 세션 테이블. 각 엔트리 = ClientSessionController + rendezvous socket + 연결 소유 gate(op/adopt/close) + park 상태. **디코더/Surface 는 active 1개에만 결합**(parked 세션은 영상 수신 0 이어야 하고, 들어온 AU 는 버림). 복귀 시 기존 selection/IDR 의무·D1 첫 입력 IDR·mobile-abr grace 재사용.
5. **소유권 계약 확장**: r3~r5 의 S1/S2/T1(compare-and-close, 프로세스 유일 op, CloseExplicit 직렬화)을 **세션별**로. 한 PC 의 늦은 callback 이 다른 PC 세션을 닫거나 채택하지 못해야 함. JNI 는 세션 핸들(또는 hostId)로 대상 지정, 전역 nativeDisconnect 는 '모두 끊기' 의미로 분리.
6. **비용·안전**: parked 세션마다 control heartbeat·UDP keepalive 만(영상 0) — 바이트/배터리/스레드 수 측정. parked 동안 입력·클립보드·미리보기 억제(현행). 동시 연결이 host 쪽에서 viewer 1 개만 허용하는 경우(다른 기기 접속 시 대체) 의 기존 규칙 유지.
7. **UI**: HOSTS 목록에서 유지 중인 PC 에 '연결 유지 중'(남은 시간) 표시, PC 별 '끊기'. 배너 문구 PC 별.
8. **검증**: 2 개 fail-closed 시험 host(서로 다른 hostId)로 AVD: A 보기 → 목록 → B 보기(A 유지) → 목록 → A 복귀(새 connect/observe/punch/hello 0, 첫 화면 시간) → B 복귀 → 3 대째로 LRU 종료 → 만료 → 백그라운드 전부 종료. 늦은 callback 교차(A 정리 중 B 채택 등) 결정적 반례. parked 중 영상 바이트 0·입력 0.
9. **릴리스**: APK 단독. 0.2.30(단일 park)은 그대로 두고 다음 APK.

## Codex 에 요청
설계 확정(특히 4·5 의 native 세션 테이블과 소유권 경계, 디코더 단일 결합, 한도·종료 조건), 범위 밖 판정, 작업용 지시 기준. 판정 파일: `D:/remote/remote/.claude/codex_keepalive_multi_r1.md`.
