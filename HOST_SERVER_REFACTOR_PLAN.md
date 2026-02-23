# HOST_SERVER_REFACTOR_PLAN

## 목적
`remote60_host`를 "짧게 실행되는 테스트 프로세스"가 아니라, **상시 대기형 멀티스레드 네트워크 서버**로 개편한다.

핵심 목표:
- host 프로세스는 한 번 기동되면 유지된다.
- signaling 연결/역할 등록(host)은 유지된다.
- client 접속/종료는 세션 단위로 처리한다.
- 클라이언트가 끊겨도 host는 종료되지 않고 다음 연결을 기다린다.
- 로직(캡처/인코드/송출)과 네트워크(signaling/협상)를 분리한다.

---

## 현재 문제 요약
- 로그상 host가 `registered(role=host)` 직후 수십 초 내 `role offline(reason=close)` 되는 케이스가 반복됨.
- runtime 경로에서 client 종료/연결 상태 변경이 host 프로세스 종료로 이어지는 경로가 존재함.
- 결과적으로 실사용 요구사항(지속 대기 + 재접속 허용)을 만족하지 못함.

---

## 요구사항 (완료 조건)

### 기능
1. host는 signaling 서버에 연결 후 host role 등록 상태를 유지한다.
2. client가 없으면 `WAITING` 상태로 대기한다.
3. client 접속 시 협상 후 스트리밍을 시작한다.
4. client 종료/오프라인 시 세션만 정리하고 다시 `WAITING`으로 복귀한다.
5. 다음 client 접속 시 재협상 후 정상 스트리밍이 가능해야 한다.

### 안정성
1. client disconnect가 host process exit 원인이 되면 안 된다.
2. signaling 일시 단절 시 자동 재연결(backoff)한다.
3. abort/crash dialog 없이 회복 가능해야 한다.

### 품질
1. 10초 이상 연속 AV(runtime) 송출 통과
2. video: `videoTrackOpen=1`, `encodedFrames>0`, `videoRtpSent>0`
3. audio: runtime audio path active(무음 실패 없음)

---

## 아키텍처

### 스레드 구성
1. **SignalingThread**
   - ws connect/register 유지
   - offer/answer/ice/peer_state 수신
   - 이벤트를 Control queue로 전달

2. **ControlThread (상태머신 오케스트레이션)**
   - 상태 전이 관리
   - 세션 생성/종료 트리거
   - 재연결/에러 복구 정책 수행

3. **MediaThread**
   - 캡처/인코드/송출 루프 수행
   - `STREAMING` 상태에서만 활성

### 상태머신
- `BOOTING`
- `WAITING` (host 등록 완료, client 대기)
- `NEGOTIATING` (offer/answer/ice 교환)
- `STREAMING` (AV 송출)
- `RECOVERING` (세션 정리/재시도)
- `SHUTDOWN` (명시 종료 시)

전이 규칙:
- `WAITING -> NEGOTIATING`: 유효 offer 수신
- `NEGOTIATING -> STREAMING`: peer connected + track ready
- `STREAMING -> WAITING`: peer offline/disconnect
- `* -> RECOVERING`: recoverable 에러
- `* -> SHUTDOWN`: 명시 종료/치명적 초기화 실패

**금지 전이:** `STREAMING -> SHUTDOWN` (client disconnect만으로)

---

## 구현 계획 (단계별)

### Phase 1: 종료 경로 차단 (핫픽스)
- client offline/close 이벤트에서 process 종료로 이어지는 `break/return/stop` 경로 제거
- disconnect 시 세션 정리 함수만 호출하도록 변경

산출물:
- host가 client 종료 후에도 살아있고 WAITING으로 복귀

### Phase 2: 상태머신 도입
- runtime 흐름을 명시적 enum 상태로 치환
- 조건 분기를 상태 전이 중심으로 정리
- 상태 전이 로그 추가 (`[host][state] A->B reason=...`)

산출물:
- 재현 시 상태 전이가 로그로 추적 가능

### Phase 3: 네트워크/미디어 분리
- signaling 콜백에서 미디어 직접 조작 금지
- signaling 이벤트는 queue push, control thread에서 처리
- media thread는 session context와 stop token 기반 실행

산출물:
- 콜백-스레드 경합 축소, 조기종료/레이스 완화

### Phase 4: 세션 라이프사이클 정립
- `SessionContext` 도입 (pc/track/encoder handles)
- 세션 생성/종료 API 분리
- 재접속 시 새 SessionContext로 clean start

산출물:
- 접속/종료 반복 안정화

### Phase 5: AV 실사용 검증 루프
- 10s 이상 연속 AV 검증 시나리오
- 접속-종료-재접속 3회 반복 테스트
- 영상/오디오 지표 확인 및 로그 아카이브

산출물:
- 실사용 가능한 서버 동작 확인

---

## 코드 수정 대상 (우선순위)
1. `apps/host/src/realtime_runtime.cpp`
   - runtime 주 루프 종료 조건
   - signaling 콜백 -> queue 분리
   - connected/trackOpen 기반 상태 전이

2. `apps/host/src/main.cpp`
   - `--runtime-stream` 진입점이 서버형 런루프를 사용하도록 정리

3. (필요 시) `libs/common/src/ws_rtc_signaling_client.cpp`
   - reconnect/recover에 필요한 상태 callback 보강

4. `apps/signaling/server.js` (추가 개선 시)
   - role 상태 로그 확장, peer_state 신뢰성 확인

---

## 로깅/관측 포인트
필수 로그 키:
- signaling: connected/registered/peer_state/routed/offline
- state transition: `prev->next`, reason
- media: capturedFrames, encodedFrames, videoRtpSent, audio packets
- session id: connect/disconnect/cleanup 경로

로그 파일:
- `automation/logs/host-*.out.log`
- `automation/logs/client-*.out.log`
- `automation/logs/sig-*.out.log`

---

## 테스트 시나리오

### 시나리오 A: 기본 연결
1. signaling 기동
2. host 기동 (server mode)
3. client 접속
4. 10초 AV 유지 확인

### 시나리오 B: client 종료 내구성
1. 시나리오 A 후 client 종료
2. host가 WAITING으로 복귀하는지 확인
3. host process 계속 생존 확인

### 시나리오 C: 재접속 반복
1. client 접속/종료 3회 반복
2. 매회 streaming 복구 확인

### 시나리오 D: signaling 일시 단절
1. signaling 재시작
2. host reconnect 및 재등록 확인

---

## 리스크 및 대응
1. **콜백-스레드 경쟁 상태**
   - 대응: queue 단일 소비자 모델 + atomic 최소화
2. **세션 자원 누수**
   - 대응: SessionContext RAII + 종료 시 단일 cleanup 경로
3. **종료 조건 오판**
   - 대응: 종료 사유 enum 도입(명시 종료/치명 오류만 process exit)

---

## 결정 사항 (팀 합의)
- host는 멀티스레드 네트워크 서버 모델로 전환한다.
- client 연결은 세션 단위이며, client 종료는 host 종료 사유가 아니다.
- 이 문서를 기준으로 코드 리팩터링 및 검증을 수행한다.
