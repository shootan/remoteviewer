# keepalive r2 제안 — 최근 PC 최대 3대, 켜져 있는 동안 상시 연결 (APK + PC 클라이언트) (검증용 remote#wwml7h91, 2026-10-22)

## 사용자 지시 (2026-10-22)
> "가장 마지막 접속 기록 있는 host pc 최대 3대, 켜져 있는 동안 연결 상시 유지. apk 도 pc 클라도."

이전 keepalive-multi(APK, feat/keepalive-multi a2e9686, Codex `codex_keepalive_multi_r1.md`·r3 클립보드 종결)는 "방문한 PC 를 최대 3대, PC 별 마지막 이탈부터 5분 park" 였다. 이번 지시로 **정책이 바뀐다**.

## 검증용 해석 (사용자에게 함께 보고 — 틀리면 정정 받음)
1. **대상**: 이 계정·기기에서 **가장 최근에 접속한 host PC 3대**(최근 접속 시각 기준 LRU). 4번째 PC 에 접속하면 가장 오래 전에 쓴 PC 의 유지 연결을 끊는다.
2. **"켜져 있는 동안"**: 클라이언트(휴대폰 앱 / PC 셸)가 켜져 있고 **그 host PC 가 온라인인 동안** 연결을 계속 유지한다 — 5분 같은 시간 제한 없음. 앱을 켜면 최근 3대 중 온라인인 PC 에 **미리 연결**(제어·parked, 영상 0)해 두고(이전 L3 '미리 연결' 제안과 합류), 목록에서 누르면 재연결 없이 바로 화면으로.
   - 휴대폰: 앱이 백그라운드로 가면(onStop) 배터리·Android 백그라운드 네트워크 제한 때문에 끊고, 다시 앞으로 오면 즉시 미리 연결 — "켜져 있는 동안" = 앱이 화면에 있는 동안으로 제안. (사용자 정정 가능)
   - PC: GNLink 셸(클라이언트 창/트레이)이 실행 중인 동안.
3. host 가 꺼지거나(오프라인) 인증·권한 변경·로그아웃이면 그 PC 만 끊고, 다시 온라인이 되면 자동 재연결(bounded backoff).

## 구현 제안 (Codex 확정·수정 요청)
- **APK**: feat/keepalive-multi 를 현재 main(82b3d14, APK 0.2.31) 위로 재통합(공유 native 13파일 — hello-delay punch/hello 와 충돌 가능) → 정책 변경: 5분 만료 제거, 최근 접속 기록 저장(이미 SessionPersistence 의 최근 PC?), 앱 시작/전면 복귀 시 미리 연결, 오프라인→온라인 재연결. 기존 계약(세션 테이블·entry 별 소유 gate·active decoder 1개·parked 입력/클립보드 억제·클립보드 r3) 유지.
- **PC 클라이언트(신규)**: 지금은 접속마다 셸이 GNLinkViewer 프로세스를 띄우고 창을 닫으면 끝. 제안: 셸이 최근 3대의 **parked viewer 를 유지**(창 숨김 + host stream pause, control heartbeat 만) → 셸 목록에서 누르면 그 viewer 창을 보이고 stream resume. 프로세스 3개 상주 비용(메모리·스레드) vs 셸 안 단일 프로세스 다중 세션 — Codex 판단. 셸 '연결 유지 중' 표시·PC 별 끊기.
- **Host 쪽**: 같은 기기·계정의 parked viewer 를 여러 host 가 각각 들고 있는 것은 기존 ControlStreamState(pause) 범위 — 호스트 변경 최소. 단 host 하나에 여러 클라이언트(휴대폰 + PC)가 동시에 parked 로 붙는 경우의 기존 단일 viewer 규칙(대체/거부)과 충돌 여부 확인 필요.
- **비용·안전**: parked 세션 수 × heartbeat·UDP keepalive 바이트, 휴대폰 배터리, relay(NAS) 경유 parked 세션의 relay 부하, 디렉터리 서버 connect/capability 횟수(앱 시작마다 3대 미리 연결). parked 동안 입력·클립보드·미리보기 억제 유지.
- **검증**: AVD·PC fixture 로 최근 3대 LRU, 미리 연결 → 첫 탭 지연(목표 ≤1 s), 오프라인→온라인 재연결, 4번째 접속 시 LRU 종료, 백그라운드/전면(폰), 셸 종료(PC), 바이트·배터리 측정, 휴대폰+PC 동시 parked 시 host 규칙.

## 요청
1. 해석 1~3 의 타당성(특히 "켜져 있는 동안" 의 폰 백그라운드 처리, 미리 연결 포함 여부)
2. APK 재통합 방식(keepalive-multi 재사용 vs 재작성), PC 클라이언트 구조(프로세스 유지 vs 단일 프로세스)
3. 단계: APK 먼저 → PC 순서 또는 병행, 버전(APK 0.2.32 / PC 0.2.163)
