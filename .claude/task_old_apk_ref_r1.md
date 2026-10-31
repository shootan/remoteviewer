# task=rel-0164 / sub=old-apk-lane / stage=구·신 참조 대조 준비 / rev=r1 (2026-10-30, 검증용 remote#7fsfy22z)

정본 판정: `.claude/codex_old_apk_lane_peer_lost_decision_r1.md` (1b9d4930…). 이 지시서는 그 "(a) 준비" 절을 기존 old-apk-lane task 에 합친 것이다. 세부 범위·금지·결과 분기는 정본이 우선한다.

- 배경: c45fb9b S 실기 3w 는 진짜 Host peer-lost 기능 FAIL(SH-off). PC 0.2.164 게시 HOLD. 검증용이 지금 P 독립 1회를 돌리는 중(결과는 별도).
- 목표: 같은 구 APK(S fixture 50080b2b)로 **구 조합**(0.2.163 source Host fae0e1f 의 안전 시험 build + 게시 전 directory: fae0e1f apps/directory = main a0f991a, server.js LF fdc9cbe8…)과 **새 조합**(0.2.164 후보 Host 53fced4 계열 + 후보 directory 2742143d / trace 사본)을 같은 조건에서 각 1회 비교할 수 있게 준비한다.
- 구 Host 시험 build 허용 차이: 로컬 input/power OFF 효과 경계(생성자·초기화 이전부터 차단 + 실제 효과값 두 줄 guard), 같은 시험 target 격리, header-only 진단. UDP/TCP/Hello·Pong/ACK·retry/timing·auth·capture/codec 은 old source 그대로(backport 0). 게시 0.2.163 바이너리 실행 금지. 이 build 를 "게시 binary 실행" 이라 부르지 않는다.
- 진단(구·신 동일, test-only, bounded): Host 가 닫을 때의 outstanding control head(방향·streamId/message seq·attempts·시간), ACK 수신/수락 여부. relay 는 control data/ACK/NACK 방향·header 식별 보존, 종료 전/정기 snapshot 으로 bucket 이 Node 종료 전에 남게. socket send 성공과 ACK 수락을 구분. payload·password·credential·token 기록 0. 클라이언트 APK 수정 0.
- 운영 snapshot/기록 hash 와 source mapping 은 읽기 전용 대조, 다르면 먼저 명시.
- 범위: 구 조합은 login → ordinary/relay → 선택/first frame → 30 s 유지까지. 새 조합은 같은 구간 + 성공 시 ④~⑥ 이어갈 수 있게. 같은 SDK/AVD boot·SH-off·target/해상도/fps/bitrate·배경.
- 금지: c45fb9b verify tree 변경, 실제 구 Host/AVD 실행(고정 SOURCE 확인 전), 사용자 서비스·입력·전경·clipboard 변경, real4/prelogin 시험, 저장소 밖 삭제.
- 빌드 조율: 큰 빌드(구 Host MSBuild 등)는 검증용 실기(P 실행 중)와 겹치지 않게 — 검증용이 "슬롯 비었음" 을 알리기 전에는 읽기 전용 비교·소스 작업만 한다.
- 제출: 고정 commit(별도 tree)·안전 diff·진단-only diff·양성/거부 대조·구/신 각 1회 실행 계획(hash/runner)을 한 SOURCE 패킷으로. 실기는 하지 않는다.
- 우선순위: 이 작업이 C 후속보다 먼저(PC 0.2.164 blocker).
