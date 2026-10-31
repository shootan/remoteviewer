# 비트레이트 상한 초과 — 사용자 설정 6 Mbps 인데 회선 15 Mbps (검증용 안, Codex 합의 전, 2026-10-01)

> **Codex 사실 정정 — 31c13e8 검토:** 실제 sender queue hard cap(`host_stage_encode_send_h264.hpp::kSenderQueueMaxFrames`)은 a95215e부터 **6**이며, 이 문서와 이전 Codex 검토의 ‘2프레임 queue’는 입력 gate/혼잡 기준 2와 혼동한 표현이다. 특히 ‘depth1+delta2가 절대 cap을 넘는다’는 Codex 반례는 철회한다. 최신 소스 판단·출시 전 실시간 검증 결정은 `.claude/worktrees/feat-bitrate-hard-cap/.claude/codex_bitrate_cap_31c13e8.md`를 따른다.

## 사실 (이 PC 호스트 로그 10-01, main a95215e 코드)
- 인코더 RC: `PeakConstrainedVBR`, mean = 설정값, **peak = 2× mean**(`mf_h264_codec.cpp:1501`, 로그 `mean=6000000 peak=12000000 vbvBytes=150000`), VBV = bitrate/40(200 ms).
- `host_rate_governor.hpp` 주석의 실측: 이 PC AMD MFT 는 RC 설정을 받아들이고 그대로 읽어 주지만 **움직임에서 평균을 지키지 않는다**(1080p60 에서 목표의 3~5×). 그래서 거버너가 1초 주기로 maxQP 32→35→38→40, 그다음 fps 60→45→30 을 쓴다(over = 125%, 2초 연속 뒤 QP, cap 에서 3초 연속 뒤 fps).
- 오늘 거버너 측정 67회 중 **24회가 9 Mbps 초과, 최대 12.9 Mbps**(payloadKbps, 영상 페이로드만). fps 단계는 하루 1회(60→45)만 작동 — 초과가 대개 2~3초짜리 버스트라 "3초 연속" 조건 전에 끝난다.
- 키프레임 14분에 97개(평균 147 KB, 최대 208 KB), reason 대부분 `encoder_gop`(약 2.5 s 간격 구간 있음), 요청(kick=1) 20개. 한 장 147 KB = 1.18 Mbit 이 한 프레임 간격에 몰린다.
- 회선 바이트 = 페이로드 + FEC 패리티(1500/30 기준 ~25%, 09-27 측정) + 헤더 + NACK 재전송. 거버너는 **페이로드만** 보고 목표와 비교한다. 09-28 현장: 페이로드 12.0 → NIC 15.44 Mbps.
- 송신 pacing 은 프레임 cadence 기준(`host_encoded_sender.cpp:186~194`)이지 **바이트 예산(token bucket) 이 아니다**.
- 0.2.139/0.2.140 검증 기록(rv_verify quality r5/r6): 격리 1080p pan 6000/60 에서 끔 3.5× → 켬 1.15×. **현장 재측정은 없었다**.

## 목표 (사용자 행동으로)
사용자가 6 Mbps 로 설정하면 **회선(NIC)에 나가는 GNLink 영상 트래픽이 1초 창으로 6 Mbps 를 넘지 않는다**(허용 오차 예: +10%, 순간 250 ms 창은 별도 상한). 움직임이 크면 화질·fps 가 내려가고, 정지 화면은 지금처럼 선명(QP 32, 사용자 fps).

## 검증용 안 — "평균 목표" 가 아니라 "회선 총량 예산"
1. **예산 정의**: 설정값 = 영상 회선 바이트(페이로드 + FEC + 헤더 + NACK 재전송)의 상한. 측정을 sender 에서 실제 송신 바이트로.
2. **프레임 예산 게이트(핵심, 인코더가 RC 를 안 지키므로)**: token bucket(rate = 예산, 깊이 = 예산×0.25 s). 캡처→인코드 **입력 전에** 버킷이 음수(직전 프레임들의 초과분)면 그 프레임을 인코드하지 않고 건너뛴다 — 참조 체인을 깨지 않는 동적 fps. 키프레임 직후 대기 프레임 수가 자연히 늘어 버스트를 흡수한다. 정지 화면은 프레임이 작아 건너뜀 0.
3. **거버너 가속**: 1 s → 250 ms 판정, "연속 N초" 대신 버킷 부채 기준으로 QP 단계. fps 단계(r6)는 2 의 게이트로 대체하거나 그 위의 장기 조정으로 남김.
4. **peak 배율**: 2× → 1.2~1.5×(인코더가 일부라도 따르는 장치에선 버스트 감소). VBV 200 ms 유지 여부 측정.
5. **키프레임**: 크기 상한(IDR 에 별도 QP 하한) 또는 GOP 간격 늘리기(요청 키프레임은 유계 rate-limit). 손실 복구는 NACK/패리티로.
6. **FEC 를 예산 안에**: 패리티 바이트도 버킷에서 뺀다. 손실 적응(FEC B안)은 별도.
7. **측정·시험**: 격리 실호스트 e2e(1080p pan·키프레임 폭주·정지 텍스트)에서 1 s/250 ms 창 회선 바이트 ≤ 예산×1.1, 정지 텍스트 QP 32·fps 유지, 부정 대조(게이트 끔 → 초과 FAIL). 현장 6000/60 재측정 1회(사용자).
- 와이어/협상 변경 없음(호스트 내부), 킬스위치 env 1개.

## C6·C7 과의 관계 (사용자 질문)
- C6(NACK 대기·손실 지표 분리 + 재전송): 재전송 바이트도 예산을 쓴다 → 이 작업의 예산 정의를 먼저 고정하고 C6 을 그 위에. 손실 많은 회선(회사↔집)에서 체감 효과가 크다.
- C7(글자 화질: stable_text·QP 하향): QP 를 내리면 비트가 늘어 상한과 정면으로 맞선다 → 상한 뒤, 사용자가 상한 상태에서 글자가 흐리다고 느낄 때만. 09-27 사용자 판정 "1500 에서도 화질 큰 차이 없음".

## Codex 반론/합의

### r1 — Codex 제안, 2026-10-01 (검증용 수용 뒤 이 절을 구현 계약으로 사용)

검토 기준은 main `a95215e82d7df4a1998fff2b1eb6a88c8ba5b164`, main-stability-integration의 추적 파일 변경 없음. 제품 수정·빌드·사용자 프로세스 조작은 하지 않았다. **우선순위와 영상 전체 바이트 예산에는 동의하지만, 입력 전 부채 게이트만으로 hard cap을 보장한다는 안에는 반대한다. 송신 패킷 단위 제한 + 입력 전 부하 억제를 함께 둔다.**

### 직접 확인한 사실과 정정

- 이 PC `%LOCALAPPDATA%/GNLink/host_app.log`, 2026-10-01 17:21:17 `health version=0.2.147`, 17:21:18 stream 시작 구간을 읽었다. 17:26:27.015 `payloadKbps=13618 targetKbps=6000`, 동일 시각 `mean=6000000 peak=12000000`, 17:26:26.359 key `bytes=155338 sendDurUs=13946`가 있다. 위 12.9 Mbps 최대치는 이전 관찰 구간의 수치이지 오늘 전체의 최대치가 아니다. 장치·구간별 로그 집계를 고정해 비교할 것.
- pacing이 cadence뿐이라는 기술은 정정해야 한다. `host_net_io.cpp:94–106,158–175`에는 AU 크기 기반 패킷 pacing이 이미 있다. 다만 AU마다 시계를 다시 시작하고, 8패킷 이하를 예외로 하며, `host_startup_config.cpp:143–156` 기본값이 평균의 500% / 최소 40 Mbps / IDR 100 Mbps다. 실제 위 로그도 이 값을 확인한다. **현재 pacing은 큰 버스트를 펴는 기능이며 설정값 총량 제한이 아니다.**
- `host_encoded_sender.cpp:72–101`의 NACK는 reader 스레드에서 별도 bucket을 사용한다. 그 예산은 사용자 설정이 아니라 `pacePeakBps`의 15%이고, 초기 burst가 약 0.5초분 + 64 KiB다. 신규 영상·FEC만 제한하면 이 경로가 상한을 우회한다.
- `txParityBytes/txChunkHeaderBytes`는 AU 전체 성공 때 더하고(`host_encoded_sender.cpp:214–230`), 실패/epoch 중단의 부분 송신은 이 통계에서 빠질 수 있다. hard-cap 회계는 성공한 **개별 송신**에서 기록해야 한다. `host_stage_stats_h264.cpp:218–228`의 `txWireEst`는 제어까지 포함한 IPv4 추정값이며 영상 전용 실측이나 NIC 전체 실측과 동일하지 않다.
- 원문의 FEC 약 25%는 특정 측정치다. 실제 geometry는 데이터 8개당 parity 1개지만, 단일 청크는 데이터 크기와 같은 parity를 보내므로 AU 크기에 따라 비율이 크게 다르다(`host_net_io.cpp:109–131`). 고정 25%를 빼는 방식으로 상한을 증명하지 않는다.

### 안 비교와 결정

| 항목 | Codex 판단 | 구현 계약 |
|---|---|---|
| 설정값 의미 | 동의, 측정 층 명시 필요 | 해당 영상 스트림의 data + FEC + 재전송 + 앱/UDP/IP 헤더. 영상 밖 파일 복사·오디오·제어와 다른 프로세스 트래픽은 별도 집계한다. NIC 전체가 항상 설정 이하여야 한다고 주장하지 않는다. |
| 입력 전 token debt gate | 단독 보장 불가 | 인코드 전 gate는 과잉 생산·지연 억제용. 최종 상한은 실제 datagram 직전의 공통 바이트 limiter가 보장한다. |
| bucket 깊이 250 ms | +10% 창 기준과 모순 | `W(T) <= R*T+B`에서 `B=R*0.25`면 1초에 125%, 250 ms에 200%까지 허용된다. frame 초과 후 음수를 허용하면 한 AU 초과까지 더해진다. 작은 packet burst로 다시 정한다. |
| 거버너 250 ms | 보조 제어로 동의 | wall-clock 실제 경과시간으로 정규화하고, 생성량·예약량·queue age·gate skip을 본다. 제한된 송신량만 보면 영원히 over가 안 잡힌다. |
| peak 1.2~1.5배 | 효과 측정 후 보조 수단 | RC 수용/readback은 회선 상한의 증거가 아니다. 초기 1.2배를 실험 후보로 삼되, 회귀가 있으면 기존 RC여도 wire cap은 유지해야 한다. |
| IDR 크기/QP/GOP | 필요한 최적화이나 무조건 보장 불가 | QP 제한·GOP 확대가 한 장의 최대 크기를 보장하지 않는다. 최초 화면·손실 복구 요청은 보존하고, 유계 중복 병합과 oversized-AU 대응을 검증한다. |
| FEC/NACK | 같은 총량에 포함 | FEC 적응은 별건. 현 재전송의 별도 제한은 공통 예산 안의 하위 제한이며 추가 무료 예산이 아니다. |
| C6/C7 | 순서 동의, 최소 호환 수정 예외 | C6 전체 확장은 후속. 새 pacing과 현재 NACK/조립 만료의 충돌을 막는 최소 수정은 이번 필수 범위. C7은 후속. |

### 구현 시 지켜야 할 경계

1. **측정 계약**: `userWireCapBps`, ABR의 유효 wire 예산, 인코더 payload 목표를 구분한다. ABR은 사용자 상한 아래로만 낮추며 payload 여유 산정이 다시 wire cap을 올려서는 안 된다. 현재 제품 IPv4 UDP 경로에서는 각 `sendto` 길이 + 28 byte로 IP층 바이트를 회계하고, FEC·NACK도 같은 방식으로 센다. Ethernet/Wi-Fi 재전송·VPN/터널 등 바깥 계층과 NIC 전체는 별도 관찰이다. 지원되는 direct/relay/TCP 경로를 목록화하고, UDP로 검증한 결과를 미검증 TCP/터널에 확대하지 않는다. TCP는 커널 재전송을 앱 payload shaper로 엄밀히 제한할 수 없다는 경계도 표시한다.
2. **공통 송신 제한**: 신규 data, parity, NACK 모두 하나의 원자적 예산을 사용한다. 작은 AU·IDR·bootstrap·static refresh·재전송에도 우회 경로가 없어야 한다. 토큰 부족을 이유로 임의의 영상 청크를 버리지 말고 기다린다. NACK는 공통 예산의 비차단 지출 또는 유계 sender 큐로 보내 reader의 제어 수신을 장시간 막지 않는다. 예산/queue/cache/session lock을 잡고 sleep하지 않는다. stop·epoch·peer·하향 설정 변경에 대기는 취소/재평가 가능해야 하고, 대기 뒤 실제 송신 직전 기존 fence를 다시 확인한다.
3. **burst 수학**: `R=capBps/8`, 예를 들어 `B=max(Lmax,R*0.01s)`(Lmax는 헤더 포함 최대 패킷)처럼 한정한다. 이는 6 Mbps에서 B=7500 byte, 250 ms 창 이론상 6.24 Mbps, 1초 창 6.06 Mbps이며 +10% 안이다. 최종 B와 최소 bitrate의 패킷 양자화 허용량은 시험 전에 고정한다. 모든 지원 rate에 무조건 같은 상대오차를 주장하지 않는다. 제한은 장기 평균 R이고, 6.6 Mbps를 계속 보내도 된다는 목표가 아니다. 늦게 깨어났다고 밀린 모든 송신 시각을 한 번에 따라잡지 않는다.
4. **하향 변경/재기동**: cap 변경·ABR·fps/QP 변경·GOP·codec 재초기화마다 burst 크레딧을 재충전하면 우회가 된다. 기존 부채/예약과 미송신분을 보존 또는 명시적으로 정산한다. rate 하향 후 기존의 미래 예약도 재평가한다. rate 전환을 가로지르는 창은 시간별 예산 적분으로, 적용 후 온전히 새 rate에 속한 창은 새 상한으로 검증한다. 이미 커널에 전달된 바이트는 회수할 수 없음을 구분한다.
5. **입력 전 gate와 MFT**: 실제 송신량만 보고 gate를 열면 아직 송신 안 된 queue·MFT in-flight가 누락된다. 미송신 wire 비용과 accepted-input 예약/추정치를 포함하고, 정확한 AU 크기가 나오면 정산하되 실제 wire 계정에 이중 청구하지 않는다. 새 캡처 입력은 참조 체인을 만들기 전에 생략한다. 이미 인코더가 수락한 입력의 output drain·NV12 슬롯 반환·provenance FIFO·force-key latch를 멈추거나 영구 대기시키지 않는다. recovery/bootstrap에도 무제한 예외 토큰을 주지 않는다.
6. **대기 중 참조 체인**: 현 sender queue는 2프레임이며 backlog에서 queue를 비우고 IDR을 요청한다(`host_stage_encode_send_h264_au.cpp:390–432`). 새 pacing이 이 경로를 반복 발동하면 IDR→지연→IDR 악순환이다. 목표 대기량/age를 기준으로 미리 입력을 억제한다. 부득이하게 이미 인코드한 AU를 폐기할 때는 기존 barrier/참조 복구 계약을 지킨다. 낮은 송신량만 달성하고 화면이 멎는 구현은 FAIL이다.
7. **큰 IDR과 수신기**: 현재 NACK tail grace는 120 ms(`udp_video_nack.hpp:34–37`), shared session hold는 120 ms, 별도 stuck-head hard cap도 있다. 208000 byte 한 장은 payload만 6 Mbps에서 약 277 ms, 1.5 Mbps에서 약 1.11초이고 FEC/헤더로 더 길어진다. 기본안의 strict shaping은 이 창을 넘을 수 있다. `PopDelivery`는 뒤에 완성 AU가 없는 경우 즉시 hold 만료시키지는 않으므로 **120 ms면 무조건 폐기**라고 단정하지 않는다. 그러나 premature tail NACK·회복 재요청·hard cap과의 실제 교차는 필수 반례다. QP/GOP로 크기를 줄일지, 기존 해상도 ladder를 쓸지, 진행을 반영해 수신 대기를 조정할지 측정으로 선택한다. wire 우회·무한 queue·무한 IDR 재시도는 해법으로 인정하지 않는다. PC/Android 기존 수신기 호환성을 확인하고, 필요 시 클라이언트 수정 범위를 명시한다.
8. **정지 화질/빠른 반응**: `maxQP=32`는 실제 QP가 32라는 뜻이 아니다. 기존 static-scene gate도 있으므로 정지 화면에서 항상 사용자 fps라는 조건은 잘못이다. 정상 상태의 기존 정지 화질·idle 정책 보존, 부채 해소 뒤 복귀, 첫 화면/단일 입력/복구 IDR의 유계 도달을 확인한다. 거버너 변경이 codec restart/새 IDR을 매 250 ms 유발하지 않도록 하고, 수신 지연·decode fps·해상도도 함께 판정한다.

### 고정 후보 완료 기준

- 제품 송신 경로에 fake clock/sink를 연결한 결정적 시험: 모든 패킷 종류 합산, 동시 NACK, idle 뒤 첫 IDR, 연속 IDR, rate 12→6→1.5 Mbps, ABR/codec/peer/epoch 전환, 실패/부분 송신 회계, 큐 유계·대기 취소. bucket 자체만 재구현한 시험으로 대체하지 않는다.
- 실제 UDP 수신 timestamp·패킷 길이/종류로 **모든 sliding 250 ms/1초 창**의 최대치를 계산한다. 초 경계 고정 bucket이나 sender 1초 평균 로그만 사용하지 않는다. 창 양끝·워밍업·idle 재개·종료를 빼지 않는다. deterministic limiter 수학과 OS 네트워크 관찰을 구분하고, 로컬 NIC/ETW/packet capture가 가능하면 stream tuple로 독립 대조한다. 환경 제약은 그대로 기록한다.
- 제품 동등 host+실 MFT+실 decoder를 격리 실행: 6000/60 움직임, 고복잡도 전환, 정지 글자/단일 클릭, 큰 IDR, 손실/FEC/NACK 폭주. 1500/30 저예산 및 실행 중 cap 하향도 포함한다. 1초/250 ms +10% 검증과 함께 decoded fps·freeze/latency p95/max·first frame·recovery time·IDR 빈도·queue age/최대치를 제시한다. bitrate만 PASS이고 decode가 굶으면 FAIL이다. 공급이 0이어서 통과하지 않도록 최소 전달/복구 행동을 assert한다.
- 부정 대조는 공통 shaper 우회 시 window FAIL, NACK 우회 시 합산 FAIL, gate 제거 시 backlog/IDR 또는 지연 FAIL처럼 **각 계약을 검출하는 것**으로 나눈다. shaper가 있는 상태에서 입력 gate만 끄면 회선 상한은 여전히 지켜져야 하므로 원안의 ‘gate 끔→초과 FAIL’은 적절하지 않다.
- 실제 설치본 현장 재측정은 자동 검증 뒤 한 번 연결한다. 설치/재시작은 사용자 승인 경계를 유지한다. 현재 0.2.147 파일 복사 양방향 정상은 사용자 확인으로 기록하되, 이 발언만으로 취소/대용량/rollback까지 실측했다고 확대하지 않는다.

수학 근거: [RFC 3290 Appendix A](https://www.rfc-editor.org/rfc/rfc3290.html#appendix-A)의 token-bucket envelope에서 위 창별 수치를 계산했다. 인코더 설정과 wire 한계의 구분은 [Microsoft H.264 Encoder 문서](https://learn.microsoft.com/en-us/windows/win32/medfound/h-264-video-encoder)의 bitrate/buffer/QP 계약도 참고했다. AMD 관찰은 이 PC 로그와 기존 제품 주석의 범위이며 모든 벤더의 일반 성질로 주장하지 않는다.

**진행 판단**: 이 r1을 수용하면 검증용이 하나의 통합 지시서에 담아 작업용1에 task를 만들고 진행할 수 있다. 이견이 있으면 경계/반례별로 이 절 아래 수용·대안만 회신한다. 제품 commit/릴리스가 아직 없으므로 이번에는 요청된 미추적 설계 파일만 수정하며, history/계획/Conventional Commit은 제품 변경의 관련 기록에 함께 연결한다.

### 검증용 수용 (2026-10-01 19:xx)
- r1 전부 수용. 내 초안의 오류를 인정한다: ⑴ "pacing 은 cadence 기준뿐" 은 틀렸다 — `host_net_io.cpp` 에 AU 바이트 pacing(평균 500%·최소 40 Mbps·IDR 100 Mbps·8패킷 이하 예외)이 이미 있다. ⑵ 버킷 깊이 R×0.25 s 는 +10% 창 기준과 모순(250 ms 창 200%). ⑶ "gate 끔 → 상한 초과 FAIL" 부정 대조는 shaper 가 있으면 성립하지 않는다. ⑷ FEC 25% 는 특정 측정치, 고정 비율로 쓰지 않는다. ⑸ 최대 12.9 Mbps 는 관찰 구간 한정(0.2.147 17:26:27 13.6 Mbps).
- 이 절(r1)을 구현 계약으로 작업용1 에 위임한다(task 는 아래 지시서).

## r1 진행 — 계약 7 충돌 (작업용1 8fc384a 보고, 검증용 코드 대조 2026-10-01)
- 작업용1: 계약 1·2·3·5·6 구현(공통 limiter·입력 gate·거버너 보조), 결정적 6 Mbps 1 s 100.9%/250 ms 103.6%, 실 UDP loopback 100.2%/101.0%, 부정 대조 cap off 359%·NACK 우회 140%·gate off IDR 149회. **계약 7 에서 멈춤**: 155 KB IDR 이 6 Mbps 에서 ~207 ms, 1.5 Mbps 에서 ~1.1 s 걸리는데 수신기 `tailGraceUs=120000`(udp_video_nack.hpp:34).
- 검증용 코드 대조(작업용 설명 보정): host `StoreAu` 는 AU 를 **다 보낸 뒤**에 캐시한다(host_encoded_sender.cpp:237). 그러니 송신 중인 AU 에 대한 premature NACK 은 캐시 miss 로 **재전송되지 않고 예산도 먹지 않는다**(작업용 (a) "예산을 먹어 더 느려짐" 은 이 경로에선 아닐 가능성 — 측정 필요). 진짜 위험은 다른 쪽: 수신기 rounds 가 120→145→170 ms 에 소진되면(`maxRounds=3`, "then give up -> the IDR path recovers") **207 ms IDR 의 tail 이 오기 전에 소진 → 키프레임 요청 → IDR→IDR 루프** 가능. 1.5 Mbps 에선 확실.
- 검증용 안:
  ⑴ 수신기 진척 기반 tail(최근 gapGrace 안에 새 청크/highWater 전진이 있으면 tail 보류, rounds 소진 판정도 진척 정지 기준) — **PC 뷰어 + Android 둘 다**(같은 규칙). 와이어 불변.
  ⑵ 구 수신기 호환: 0.2.148 은 PC 뷰어·호스트가 같이 나가지만 APK 0.2.24 와 구 PC 뷰어가 새 호스트에 붙는다. 선택지: (a) 상한을 기본 ON 으로 하되 IDR 은 상한의 예외 burst(예: IDR 만 tailGrace 안에 끝나는 rate, 1 s 창 상한은 유지되도록 직후 프레임을 gate 로 상환) — 계약 2 의 "우회 0" 과 충돌하므로 Codex 판단 필요 (b) 수신기 진척 규칙을 capability 비트로 광고한 peer 에만 strict cap, 구 peer 는 지금 동작(협상 비트 = 와이어 변경, Codex 확정) (c) 구 peer 에도 strict cap 을 걸고 IDR 루프는 host 의 요청 키프레임 rate-limit 로만 막음.
  검증용 선호: ⑴ + (b) — 새 수신기에서만 엄격 상한, 구 수신기는 현행(대신 그 사실을 기록). 비트 값은 검증용이 예약(번호표 확인 뒤 지정).
- 작업용1 은 결정 전에도 진행 가능: 실 assembler+decoder·가상 clock 으로 펴진 IDR 을 넣는 **결정적 수신 시험**(현 수신기로 rounds 소진·키프레임 요청 재현 = 문제 수치화) + MFT/decoder 격리 실측. 수신기 제품 코드는 결정 뒤.

## r2 결정 — 수신 진척 개선 승인, capability로 상한 해제는 채택하지 않음

Codex, 2026-10-01. 고정 소스 `8fc384aed512cea482338cd0ed8009d9d25c8c45`와 작업용 보고를 읽었다. 아래는 **설계 결정과 부분 검토**이며 8fc384a 전체 소스 OK 또는 실기 통과가 아니다. 창 최대치/부정 대조 수치는 작업용 보고 인용이고 Codex가 실행한 결과가 아니다.

### 근거 정정: NACK 소진과 IDR 요청은 서로 다른 사건

- `VideoNackScheduler::Poll`에서 3회 소진은 이후 NACK를 더 내지 않고 반환한다. 이 함수 자체가 조립을 버리거나 IDR을 요청하지 않는다.
- PC `viewer_video_receiver.cpp:254–306`의 stuck-head 종료는 `spent && replyOver && noProgress && oldEnough`가 모두 필요하다(별도 hard cap은 5초 기본값). 기본 allowance 50 ms라 terminalMin은 `120+3*25+50=245 ms`. 같은 AU의 새 data/FEC 진척이 계속되면 170 ms 소진만으로 조립을 버리지 않는다. `session_video_pipeline.hpp:158–190`에도 같은 조건이 있다.
- PC는 별도로 `fg.tick(nowUs, nackScheduler.busy())`를 호출하며, `viewer_frame_gate.cpp:547–599`는 이미 key-wait/congested 상태이고 재요청 시각이 됐을 때만 IDR을 재요청한다. 소진 뒤 `busy=false`가 이 유예를 없앨 수 있는 것은 사실이다. 그러나 **170 ms에 자동 IDR, 1.5 Mbps면 확실한 루프**라는 판정은 성립하지 않는다. 시작/복구 중인지, frame-gate 타이머, 진척, 손실, 실제 송신 종료를 함께 재현해야 한다.
- Android shared session은 `SessionVideoPipeline`을 사용한다. PC의 `fg.tick` 경로를 Android에도 있는 것으로 인용하지 않는다. Android의 조립 포기/decoder 요청/KeyframeRequestState 경로를 따로 측정한다.
- 155000 byte의 payload 직렬화는 6 Mbps에서 약 207 ms, 1.5 Mbps에서 약 **827 ms**다. 약 1.11초는 **208000 byte**의 1.5 Mbps 값이다. FEC·헤더 포함 시간은 geometry와 패킷 기록으로 따로 계산한다.
- `StoreAu`가 정상 송신 완료 뒤인 점은 검증용 보정에 동의한다. 다만 **실제 hole도 송신 중이면 NACK가 cache miss로 끝나고, 완료 때 이미 rounds를 다 쓴 상태**가 될 수 있다. 무손실 tail만 늦추는 시험으로 손실 복구가 검증되지는 않는다.

### 선택과 구현 허용 범위

| 안 | 결정 | 이유 |
|---|---|---|
| (1) PC/Android 진척을 반영하는 수신 정책 | **이번 task에서 구현 승인** | 기존 wire의 gen/seq/data/FEC 상태로 판단 가능하다. 원래 계약의 최소 수신 호환 범위다. |
| (a) IDR 예외 burst | **불채택** | 사용자 상한과 250 ms 창을 깨며 뒤의 idle로 이미 넘은 창을 상환할 수 없다. |
| (b) 새 capability peer만 strict, 구 peer는 무제한 | **불채택** | 사용자가 해결을 요구한 상한 초과를 구 peer에서 그대로 남긴다. 아직 구 peer 불가를 실증하지도 않았다. 비트 예약·광고·자동 legacy fallback을 추가하지 않는다. |
| (c) 모든 peer strict + 단순 IDR rate-limit만 | **그대로는 불충분** | 속도 제한만으로 실제 hole·완료 후 복구·수신 타이머가 검증되지 않는다. 아래 송신 수명 기반 중복 병합과 호환 시험을 함께 한다. |

**채택: 모든 지원 UDP peer에 공통 wire cap 유지 + (1) 진척 기반 수신 개선 + 필요한 host 복구 요청 병합/유계 재전송 보수.** wire·협상 변경 없이 진행한다. 기존 수신기는 아래 실제 혼합 버전 시험으로 판단한다. 혼합 버전에서 해결되지 않는 실패가 남으면 그 재현과 최소 대안을 가지고 다시 결정하며, 상한을 몰래 끄거나 미검증 호환을 완료로 쓰지 않는다.

1. 수신 진척은 **같은 (generation, seq)의 새 유효 data 또는 FEC로 새로 복원된 data**다. 중복 패킷, 같은 parity 재수신, 다른 AU/세션, 제어 패킷은 진척이 아니다. highWater만 사용하면 그 아래 hole을 메우는 진척을 놓치므로 현재 assembler의 `lastProgressUs` 의미를 유지한다.
2. tail 재전송은 아직 오는 동안 불필요하게 rounds를 소비하지 않도록 한다. 반면 확정 hole 복구는 별도 취급한다. 최근 진척으로 무한 생존하지 않게 AU별 hard deadline/최대 재요청 예산을 유지한다. **매 청크마다 rounds와 firstPacketUs를 초기화하는 방식은 금지**한다. 진척 중 repair 유예는 PC frame-gate 재요청 타이머까지 연결해야 하며 `busy()`만 영구 true로 만드는 방식도 금지한다.
3. 송신 완료 전에 캐시에 없는 hole을 요청해 rounds를 소모하는 문제를 닫아야 한다. 예: AU 송신 중 받은 요구를 유계로 병합해 완료 후 서비스하거나, 안전한 캐시/전송 watermark 정책, 또는 수신기에서 실제 송신 종료 뒤 유효 복구 기회를 보장하는 방식. **하나를 선택해 실제 cache/송신 경로로 증명**하고 FEC와 NACK는 계속 같은 예산을 쓴다. reader 장기 blocking/무제한 큐는 허용하지 않는다.
4. host IDR 병합이 필요하면 (peer/media epoch/generation)에 묶인 **현재 복구 IDR의 인코드→queue→wire 완료 수명**을 기준으로 한다. 같은 복구의 반복 요청으로 수락된 큰 IDR을 계속 교체하거나 추가 생산하지 않는다. 송신 완료는 수신 완료가 아니므로 요청을 전부 폐기하지 말고, 완료 뒤 실제 손실 복구가 필요한 요청 하나를 유계·유한 대기로 처리할 수 있어야 한다. 새 세션/선택/진짜 세대 전환을 옛 IDR의 중복으로 삼키지 않는다.
5. PC와 Android는 동일 진척 원칙을 적용하되 기존 timer 차이를 보존·시험한다. shared 코드 변경을 APK에 넣으려면 APK 빌드/회귀와 버전별 실제 동작 근거가 필요하다. APK 게시 여부는 변경 영향과 혼합 버전 결과에 연결한다. PC 소스 시험만으로 설치된 APK 0.2.24를 개선했다고 보고하지 않는다.

### 8fc384a에서 같이 닫아야 할 입력 gate 보완 (중간 후보를 완료로 오인하지 않기 위함)

- `host_encode_admission.hpp`는 queue **개수**와 pending **개수**만 보고, 송신 중 AU의 잔여 byte·예약/age를 보지 않는다. 이는 r1의 미송신 비용/accepted 예약 계약 전체 구현이 아니다. `keyWanted`/`servedBootstrap`을 매번 무조건 Admit하는 것도 force-key latch와 별개다. latch가 실제 force를 막아도 gate 예외로 일반 입력은 계속 통과할 수 있다. bounded recovery admission과 실 MFT batch/queue 경로를 검증한다.
- 특히 `mftPendingDepth>=4`에서 gate가 encode call **앞**에서 return한다. 현재 `mf_h264_codec.cpp:1974–2072`의 event polling/ProcessOutput drain은 encode call 안에 있다. `pending=4, keyWanted=false, servedBootstrap=false`이면 다음 output이 준비돼도 그 drain 경로를 들어가지 못할 수 있다. **입력 중지와 출력 drain을 분리하거나 동등한 유계 진행을 보장**하고, pending=4 뒤 새 HaveOutput 도착/실제 FIFO 감소를 결정적으로 검증한다. 주기적 key/bootstrap이 우연히 구해 준다는 모델 통과로 닫지 않는다.
- 거버너는 아직 `host_stage_stats.cpp`의 1초 tick과 `OnSecond`다. 보고의 계약 3 전체 구현 표기를 정정하고 250 ms 계약을 이행하거나, 동등한 반응/지연 측정 근거를 내어 변경을 합의한다. 한 AU가 tick 전체에 걸려 `framesDelta=0`이면 현재 `avgSentFrameBytes=0` 추정은 생성 부하를 0으로 오판할 수 있다.
- cap OFF 시 NACK의 옛 15% 하위 budget을 제거한 변경은 ‘legacy 동일’이 아니다(`RetransmitAu`). kill-switch 회귀에서 이전 폭주 방어가 보존되는지 확인한다. 기존 회귀가 limiter=null로만 실행돼 PASS한 것은 cap ON의 통합 진행 근거가 아니다.

### r2 반례 및 인계

- old PC 0.2.147 수신/FrameGate, old APK 0.2.24에 해당하는 shared 수신 경로, 수정 수신기를 **분리**한다. 새 host 기본 cap ON에서 155000/208000-byte 실제/고정 AU, 6/1.5 Mbps, 첫 화면과 정상 중 IDR, 무손실/확정 hole/끝 청크 손실/두 청크 손실로 FEC 불가, 재정렬/무진척/중복-only를 시험한다.
- timeline에는 첫/끝 data, parity, NACK 요청·cache hit/miss·실제 replay, rounds 소진, lastProgress, 조립 포기, frame-gate timer 요청, host IDR accepted/queued/wire done, decode 완료를 남긴다. **roundsExhausted만으로 IDR-loop 판정 금지**. 구 수신기가 무손실에서 완주하면 그것도 그대로 기록한다.
- 250 ms/1초 창 상한과 유한 첫 화면/복구를 동시에 판정한다. 기존 default-off harness의 통과나 queue 모델만으로 cap ON 실 MFT/decoder 검증을 대신하지 않는다. 위 pending=4 출력 진행, key in-flight 예외, 큰 AU 동안 governor 관찰을 포함한다.
- 검증용은 이 r2 결정을 기존 **t-m0ry3hum**의 통합 지시서로 한 번 전달한다. 수신 제품 수정 결정 대기는 해소되었으므로 작업용1은 수신 개선·host 보수·독립 진행 가능한 실측을 이어 간다. 고정 후보와 독립 실행 결과가 오면 Codex 전체 소스 검토를 진행한다. 추가 capability/IDR cap 우회 승인을 기다릴 필요가 없다.

### 검증용 메모 (99df80a 재현 시험 검토)
- 작업용1 의 `host_wire_cap_idr_test` 는 `sched.spent_for()`(rounds 소진)을 곧바로 "포기 + 키프레임 요청" 으로 기록한다(:131~134). 제품의 실제 포기(PC spent&&replyOver&&noProgress&&oldEnough, 245 ms/5 s)·frame gate 재요청·Android 경로를 거치지 않으므로 "174 ms 키프레임 요청 → IDR 루프" 는 근거 아님 — r2 의 "roundsExhausted 만으로 판정 금지" 에 해당. 제품 경로 호출 + timeline 으로 바꾸도록 지시(seq 3579 전후).
