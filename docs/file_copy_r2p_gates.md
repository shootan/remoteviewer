# file copy R→P — 영상 우선 gate (t-zdmsd4gb r1 2단계, 측정 전 고정)

작성 2026-09-29, 측정 전. 이 파일의 문턱은 **결과를 본 뒤 완화하지 않는다**(합의 D1 gate). 결과가 문턱을
넘으면 송신기를 고치거나 실패로 보고한다. ABR·제어 정책을 바꾸거나 ABR 을 얼려서 통과시키는 것은 실패다.

## 무엇을 보는가
호스트가 파일을 보내는 동안(R→P) 영상이 **영상만 보낼 때보다** 나빠지는지. 비교는 항상 같은 경로·같은 길이·같은
측정 구간의 **영상 단독(A) 대 영상+파일(B)** 이다. 병목 경로에서는 A 도 강등될 수 있으므로 "강등 0" 이 아니라
**B 의 추가분**을 본다.

## 측정 장치 (고정)
- `remote60_abr_client_evidence_e2e_test --file-bench` (REMOTE60_ALLOW_HOST_E2E=1).
- 호스트: 시험 빌드 `GNLinkStreamClipSink` — GNLinkStream 과 같은 소스, 차이는 파일 원천만: 호스트 클립보드
  대신 `REMOTE60_FILE_COPY_TEST_SOURCE_DIR` 폴더의 파일을 트리거 시각에 `OnHostClipboard` 로 넘긴다(시험 빌드
  전용, `clip_image_build_gate_test` 가 GNLinkStream.exe 에 없음을 확인). 도우미는 사설 window station 에서
  이 사용자로 기동. 클립보드 동기화 끔(사용자 클립보드 무접촉). `--input-injection-mode none`.
- 뷰어 쪽: 시험 프로세스 안의 제품 `FileCopyClient`(700 ms 질의·게시·Prepare·청크 검증)와 제품 제어 스케줄러,
  사설 window station 의 도우미 + 탐색기 복사 엔진 소비자(`--consumer --mode drop`). 파일은 끝까지 실제로 붙여넣는다.
- 영상: 이 시험 창(60 Hz 재그리기) 실캡처·실인코딩, 30 fps, 기본 bitrate.
- 파일: 64 MiB 1개, 측정 구간 시작(10 s)에 복사 트리거. 송신기 설정은 기본값(`host_file_bulk_rate_config_from_env`:
  시작 256 kbps, 상한 8 Mbps), 영상 큐가 차 있으면 양보(videoBusy).
- 실행 길이 60 s, 측정 구간 10–60 s. 경로마다 A/B 각 2회(순서 A,B,A,B).

## 경로 (udp_impair_proxy, 실행 중 고정)
| id | RTT | 손실 | 용량 | 성격 |
|---|---|---|---|---|
| P1 | 40 ms | 0 | 20 Mbps | 건강한 고정 용량 |
| P2 | 40 ms | 0 | 6 Mbps | 병목(영상만으로도 빠듯) |
| P3 | 40 ms | 1 % | 8 Mbps | 손실 원거리(clip-image WAN 과 같은 조건) |

## 지표 (정의)
- `abrDowns`: 측정 구간 안 호스트 `[abr] profile=` 줄 중 한 단계 내려간 것의 수.
- `recoverS`: B 에서 강등이 있었다면, 파일 전송 종료부터 전송 전 profile 로 돌아올 때까지 초(구간 끝까지 못 돌아오면 ∞).
- `capToRecvP95`: 실프레임의 캡처→이 쪽 수신(같은 PC QPC) p95, ms.
- `pingP95`: 제품 제어 ping RTT p95, ms(입력 지연의 대리값 — 입력 에코 RTT 는 이 장치에 없다).
- `gaps250`: 수신 실프레임 간격 > 250 ms 인 횟수. `maxGapMs`.
- 순서: 호스트 로그에서 B 의 각 강등 줄 앞 4 s 안에 파일 송신기의 감속/정지(`RATETRACE ... rate=a->b`, b<a 또는
  yield=1) 줄이 있는가.
- 판정 불가(통과 아님): cadence 부족(ABR 이 그 초를 판단하지 않음) · `metricsFresh=0` · 호스트 비정상 종료.

## 문턱 (고정)
근거: ABR 은 지연 ≥150 ms 가 2 s(severe) 또는 ≥125 ms 가 4 s(moderate)일 때 한 단계 내리고, 좋은 초가
5 s(low→mid)·8 s(mid→high) 이어지면 올린다(`host_abr.hpp:268-270,364-369`). 아래 여유는 그 문턱보다 훨씬 작게 잡았다.

| 경로 | 조건 (B 가 A 두 회의 나쁜 쪽 대비) |
|---|---|
| P1 | `abrDowns(B) = 0` 이고 최종 해상도·fps 가 A 와 같다 · `capToRecvP95` +15 ms 이하 · `pingP95` +15 ms 이하 · `gaps250` +1 이하 |
| P2, P3 | `abrDowns(B) ≤ abrDowns(A)` · 강등했다면 `recoverS ≤ 8 + (A 의 회복초, A 강등 없으면 0)` · `capToRecvP95` +30 ms 이하 · `pingP95` +30 ms 이하 · `gaps250` +2 이하 |
| 전부 | B 의 모든 강등(파일 전송 중)은 그 앞 4 s 안에 송신기 감속/정지가 먼저 있다(시간순 로그) |

파일 처리량·완료 시간은 **기록만** 한다(판정 아님 — 합의 D1: 양보 때문에 파일이 느려지거나 멈출 수 있음을 허용).
단 B 에서 파일이 붙여넣기 완료(소비자 EndOperation 성공 + 바이트 동일)되지 않으면 그 실행은 "파일 미완료" 로 따로 적는다.

## 이 gate 가 증명하지 않는 것
- 실제 원격 회선(여기는 루프백 + 프록시). 비관리자 시험 빌드라 실제 연결 토큰 도우미 기동·UAC 는 4단계 실기.
- 다른 PC·다른 인코더·다른 해상도의 영상. 4 GiB 급 장시간 전송.
