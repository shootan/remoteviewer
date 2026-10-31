

### ② 진행 2 — relay 경로·세션 뒤 소켓 유지 (2026-10-27)
**고정: `spike/host-admission-r3` 75e37d1**. tracked clean.

| 커밋 | 범위 | 내용 |
|---|---|---|
| 60a6951 | 제품 | relay 경로 |
| 625b39f | 시험 | 실 server.js relay e2e |
| 30ef913 | 제품 | 소켓 유지 |
| 75e37d1 | 시험 | 소켓 유지 확인 |

- **relay 경로 (60a6951)**:
  - `directory_standby_support()`: `/api/hosts` 의 standby{supported, relay} 를 부작용 없이 읽는다. 옛 서버·timeout·오류는 미지원으로 본다.
  - `directory_connect(..., standby)`: `standbyRelay` 와 relay 후보 주소를 받는다.
  - `StandbyLease` 는 비연결 소켓을 쓴다. CapabilityFn 이 그 소켓으로 observe·standby connect 를 하고 **겨눌 주소**(reserved 면 relay, 아니면 Host)를 돌려준다. 그 주소에서 온 datagram 만 읽는다.
- **세션 뒤 소켓 유지 (30ef913)**: 세션에는 **복제 핸들**(WSADuplicateSocket/dup)을 준다. lease 는 자기 핸들을 유지하고 세션 동안 읽지 않는다. 세션이 끝나면 같은 끝점으로 Ping 을 이어 가므로, relay lease 가 끊기지 않는다. 앞 절의 "새 소켓·relay 재입장" 한계는 이로써 닫혔다.
- **e2e `--product-client --real-directory --old-host` 54/54** (`sh_run.ps1 -NativeExe`, e2e exe 662c8eab…):
  - 직결 35.
  - 실 server.js relay:
    - 제품 directory_login/list/standby support.
    - lease 소켓마다 standby connect 와 relay lease 예약.
    - A·B 가 relay 로 parked.
    - A 가 relay lease 위에서 activation(영상, relay 가 active 로 표시).
    - B ping 은 A 무간섭.
    - B takeover.
    - A 세션이 끝난 뒤 **같은 소켓으로 parked 유지**(loss 0, 새 connect 0), 같은 relay lease 로 회수(영상).
  - 구 Host 는 Unsupported.
- 변이:
  - standby 플래그 없이 connect → relay 사례 FAIL.
  - lease 가 소켓을 넘기고 새로 연다 → "loss 0" FAIL.
- 단위 46/46 그대로.
- 아직: e APK 배선(브랜치 결정 ①), Host 이동 뒤 proof 재증명의 실 relay 반례(서버 쪽 r4 시험에 있음 — 클라이언트는 Ping 16 s 무응답이면 Lost 로 단위 시험), §6/§7, 실 NAT.
