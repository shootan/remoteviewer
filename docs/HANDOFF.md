# HANDOFF — 원격 세션 안정화 사이클 (2026-08-25 기준)

다음 세션이 이 파일 + `history.md`(250~) + `구현계획.md`(원격 세션 안정화 사이클 체크리스트)만
읽으면 이어서 작업할 수 있도록 정리한 인수인계 문서.

## 지금 상태 한 줄
"게임 중 멈춤/끊김" 문제를 원인별로 분해해 P0·P1·P1.5·필드안정화까지 배포(0.2.54~0.2.57). **현재 최신
설치본은 `dist/GNLinkSetup-0.2.57.exe`.** 남은 것은 실기 검증 대기 + P2/P3.

## 배포 이력 (상세는 history.md 해당 번호)
- 0.2.50 (250) 인코더 output-starvation 계측
- 0.2.51~53 (251~253) 로그 번호 로테이션 + viewer.log 신설 + 로그 폴더 열기 버튼
- 0.2.54 (255) P0: 피커가 스트림 안 끊게 / 정적 1Hz 리프레시 / (원격 커서 링 — 이후 기본 OFF)
  / UDP 컨트롤 기동 레이스 배리어 (e2e 30%→6/6)
- 0.2.55 (257) P1: force-key 래치(IDR 연발 제거) + keyint A/B 오버라이드
- 0.2.56 (258) P1.5: 유령 top-left 버튼 제거(+툴바 "대상 선택" 복원) / 입력 실패 stage 계측 /
  default-desktop SetCursorPos 실패 시 SYSTEM 브로커 폴백
- 0.2.57 (261) 필드 안정화: 클라 참조체인 key-anchor IDR 재동기(텍스트 스크롤 깨짐) / DXGI 워커 웨지
  독립 워치독(5s kill, exit44, supervisor 동일 recovery 클래스) / readback-slow 로그 1s rate-limit / GOP 계측

## "멈춤/끊김"의 확정된 원인 4갈래와 상태
1. 정적 화면이 안 갱신됨 → **해결**(0.2.54 1Hz 리프레시).
2. 유령 버튼(지도 좌상단 클릭이 몰래 피커/매크로 토글) → **해결**(0.2.56).
3. 입력이 안 먹음(설정앱 등 UWP 포그라운드에서 `SetCursorPos` 실패) → **부분**: 자동복구(브로커
   폴백) + 진단 계측 넣음. **실기 로그로 실효성 미검증** — 아래 실기 게이트 참조.
4. 주기적 한-프레임 히치 = 매초 대형 키프레임 + 호스트 공급 ~52fps(60 미달) → **P1로 빈도 절감,
   근본(공급)은 P2 미착수**.

## 다음 액션 (순서 — Codex 합의)
**A. 0.2.56→0.2.57 실기 판정 (다음 세션 최우선, 이게 나오기 전엔 아래를 쌓지 말 것)**
   판정선 4개:
   1) 게임 지도 좌상단/브레드크럼 클릭이 picker/macro 무발화 + 게임에 정상 전달
   2) 입력 실패 시 host 로그 `stage=set_cursor_pos` → `inputDefaultBrokerQueued` 증가
   3) `%ProgramData%\GNLink\secure_input.log`에 `inject=ok`/landed (폴백이 실제로 클릭을 넣었나)
   4) stuck button/drag 0건
   5) (0.2.57/261 추가) 텍스트 스크롤 garbage 소멸 + viewer `staleRefRecoveries` 관찰 / 게임 프리즈 시
      host_app 로그 `dxgi-worker watchdog code=44` 후 수초 내 자동복구 / readback-slow 로그 ≤1줄/초
   로그 수집: 클라 "로그 폴더" 버튼 → `viewer.log*`,`client.log*`; host "Open log" → `host_app.log*`;
   그리고 host의 `%ProgramData%\GNLink\secure_input.log`.

**B. 판정 결과에 따라**
   - 폴백이 먹으면: sticky input routing 착수(아래 부채) → 그 다음 P2.
   - 폴백이 안 먹으면: secure_input.log의 attach/journal/inject 단계로 다음 원인 특정.

**C. P2 readback (호스트 60fps 공급)** — 착수 전 **SyntheticRefresh flag 선행 필수**(아래) + 호스트 분할 리팩터 Phase 1~3 선행(구현계획 참조).
**D. P3 클라 paced playout(도착 버스트 평탄화), P4 해상도 프리셋 UI.**

## 미해결 부채 (Codex 합의, 반드시 유지)
- **sticky input routing**: 브로커로 간 mouse-down은 대응 up도 브로커 고정, key down/up 짝 고정,
  drag(버튼 held) 동안 broker, hover는 coalesce. 실기에서 stuck 1회라도 = 즉시 blocker.
- **SyntheticRefresh flag (P2 착수 전 완료 게이트)**: 정적 1Hz 리프레시 프레임이 client의
  latency/catchup/anomaly/real-fps 집계를 오염(avgLatency 0.6~0.8s). EncodedFrameHeader.flags
  bit1 + UDP chunk flags 보존(안드로이드 공유 assembler 관통) → client에서 present는 하되 위
  집계에서 제외. 별도 커밋+리뷰.
- (비차단) same-generation 크기변경 시 원격 커서 capW/H 동시 스탬프. (커서는 현재 기본 OFF)

## 테스트 환경 주의 (중요)
- **호스트 PC에 RDP 접속 중이면 DXGI 캡처가 0x80070005로 막혀 WGC 저품질 폴백** — 실측 확인됨.
  테스트 시 RDP 끊을 것. ("다른 PC 3~5fps" 사건의 유력 원인이기도.)

## 유용한 env 스위치 (host PC 시스템 환경변수, GNLinkHost 재시작 필요)
- `REMOTE60_NATIVE_KEYINT_OVERRIDE=120` — 키프레임 2초 간격 A/B (0=off, 기본 60)
- `REMOTE60_NATIVE_STATIC_REFRESH_MS=1000` — 정적 리프레시 주기 (0=off)
- `REMOTE60_NATIVE_REMOTE_CURSOR=1` — 원격 커서 링 재활성 (기본 off, 사용자가 "불필요" 판정)

## 검증 파이프라인
- 로컬 회귀: `cmake --build build-local --config Release --target remote60_native_video_host_poc
  remote60_native_video_client_poc remote60_mf_h264_codec_test remote60_capture_cadence_gate_test`
- live-host UDP e2e: `GNLinkStream.exe --transport udp --codec h264 --bind-port 43000,3478
  --control-port 43001` 띄우고 `remote60_udp_control_e2e_test.exe 127.0.0.1 43000`
  (REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1, ENCODER_TUNE_MODE=low_latency 필요). 기대 6/6.
  주의: 로컬 LAN e2e는 무인증이라 브로커 폴백 경로는 자동검증 불가(=필드 게이트).
- 릴리스: product_version.hpp 범프 → installer 빌드 → **payload GNLinkHost.exe 임베드 버전
  확인 후** dist/GNLinkSetup-x.exe 복사(직전 설치본만 함께 보존). 이번 세션에서 미범프(0.2.55)를
  0.2.56로 낼 뻔한 것을 임베드 검증으로 잡음 — 이 확인 단계 유지할 것.

## 교차검증 (a2a)
- 이 사이클 전 구간을 GMux a2a 버스의 "검증용Codex"(remote#codex 세션)와 리뷰하며 진행했다.
  다음 세션도 큰 변경은 a2a-collab 스킬로 그 세션과 교차검증 권장.

## 코드 지도 (거대 파일 주의)
- `apps/native_poc/src/native_video_host_main.cpp` 9,896줄(main() 7,005줄, 지역변수 502개, 람다 62개),
  `native_video_client_main.cpp` 5,318줄. **분할 리팩터 설계 완료 → `docs/호스트_분할_리팩터_계획.md`**
  (구간 지도 §1.2, 결합도 §1.3). Phase 0(순수 이동)은 실기 판정과 무관하게 진행 가능, Phase 1~3은
  P2 착수 전 완료. 이 리팩터는 사용자 결정으로 **Codex 교차검증 없이** 진행한다(위 "교차검증" 권고의
  예외). 지금은 특정 코드 찾을 때 grep/sed로 좁혀야 함.
- **브랜치**: 리팩터 코드는 `refactor/host-split`(main에서 분기, Phase 0-9 완료 = 2f09ec5). 계획·이력·체크리스트
  문서는 **main에서만** 갱신하고 브랜치는 `git merge main`으로 동기화한다. Phase 0 실행 순서·의존성 정정은
  계획 문서 상단 2026-08-26 갱신 블록 참조.
