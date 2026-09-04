# CLAUDE.md

## 응답 규칙 (필수)

- **모든 응답의 마지막에는 "수행된 작업" 요약을 반드시 명시한다.**
  - 무엇이 변경/실행되었는지: 파일, 커밋 해시, 빌드/테스트 결과, 산출물(dist 등).
  - 분석·조사만 한 턴이면 "수행 작업: 분석만, 파일 변경 없음"처럼 명시한다.
  - 진행 중(빌드 대기 등)이면 "진행 중 작업"과 "완료된 작업"을 구분해 적는다.

## 테스트 규칙 (필수)

- **테스트를 돌리기 전에 RDP 접속 상태인지 먼저 확인한다.**

  ```
  qwinsta
  ```

  `rdp-tcp#N` 세션이 `Active` 이면 RDP 접속 중이다. (`$env:SESSIONNAME` 은 내 셸이 속한
  세션만 보여주므로 부족하다 — 콘솔에서 돌고 있는데 다른 곳에서 RDP로 붙은 경우를 놓친다.)

- **RDP 접속 중이면 테스트를 진행하지 말고, 사용자에게 RDP 종료를 요청한다.**
  사용자가 "종료했다"고 알려준 뒤에 테스트를 진행한다.

- **이유** (실측):
  - RDP 접속 중에는 DXGI 캡처가 `0x80070005`로 막혀 WGC 저품질 폴백으로 떨어진다.
    DXGI/readback 경로(NV12 lease, frozen-ring, 캡처 cadence)는 아예 판정이 안 된다.
  - `Microsoft Remote Display Adapter`의 갱신률이 낮다(2026-08-29 실측 **32Hz**).
    GDI 캡처는 화면 갱신률에 묶이므로 `remote60_gdi_capture_process_test`가
    `fps >= 50` 요구를 **물리적으로** 만족할 수 없어 항상 FAIL 한다.
    이걸 코드 회귀로 오진하기 쉬우니, 실패 시 먼저 `qwinsta`부터 볼 것.

- 즉 RDP 상태에서 나온 테스트 결과는 **PASS든 FAIL이든 판정 근거로 쓰지 않는다.**

## 기타

- 워크플로우·커밋 규칙은 `AGENTS.md`를 따른다.

## 로그 위치 (필수 인지)

- **사용자가 증상/현상을 보고하면, 추측하지 말고 항상 먼저 NAS 로그를 열어 근거로 판단한다.**
  (viewer/host/apk(폰) 3종 모두. 로그로 확인 전에는 원인·수정 방향을 단정하지 않는다.)
- **host 로그도 무조건 NAS 것을 본다.** 이 PC 가 호스트여도 로컬 `%LOCALAPPDATA%\GNLink\host_app.log`
  사본을 보지 말 것 — NAS 의 host.log 를 봐야 viewer/apk 와 같은 시각·같은 출처로 대조된다.
  ("NAS 부하 줄이려고 로컬 본다" 같은 예외 금지. NAS 는 idle 이고, 일관성이 우선이다.)
- **host / client / viewer 로그는 전부 NAS(디렉터리 서버)로 업로드된다.** 뷰어가 회사 등 원격에
  있으면 그 `viewer.log` 는 이 PC 에 없다 — **NAS(192.168.0.6, LAN)** 에서 봐야 한다.
  - 접속: `plink`/`ssh claude@192.168.0.6` (읽기전용 계정, pw 는 메모리 참조).
  - 저장 경로: 디렉터리 서버(`apps/directory/server.js`) `<LOG_DIR>/<account>/<device>/<stream>.log`
    (stream = host.log / client.log / viewer.log). 계정: software30@chronostudio.net.
  - 이 PC `%LOCALAPPDATA%\GNLink\` 의 `client.log`·`viewer.log` 는 로컬 뷰어 것뿐이라 원격 테스트 땐 낡음.
  - 퍼블릭 175.209.236.194:29180 은 호스트 업로드용 NAT 주소일 뿐, 접속은 LAN 192.168.0.6 으로.
