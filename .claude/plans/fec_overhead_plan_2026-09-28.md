# 영상 부가분(FEC 패리티) 줄이기 — 검증용 계획 초안 (2026-09-28) — Codex 논의용

## 근거 (현장·격리 실측)
- 6000/60 영상: 페이로드 ≈10Mbps · 패리티 ≈1.48(≈15%) · 청크 헤더 ≈0.93(≈9%). 1500/30: 패리티 0.32 = 페이로드의 **25%**.
- 작은 프레임에서 패리티가 페이로드를 넘음: e2e payload 185,863 / parity 970,776(≈5×) — `host_net_io.cpp:130-218` 은 그룹마다 XOR 패리티 1개를 **maxChunk 크기로** 보냄, 수신측(`native_video_client_shared_core.cpp:975-990`)은 패리티 `chunkSize == chunkStride` 를 요구.
- 수신측 조립은 데이터 청크로 완성, 패리티는 flag 로 구분된 선택 패킷 → **패리티를 줄이거나 생략해도 와이어 형식 불변**(구 뷰어·APK 0.2.23 공용 세션도 같은 코드).

## 제안 (두 단계, 서로 독립)
**A. 작은 프레임의 패리티 크기 (피드백 불필요, 호환)**
- 1청크 프레임(정지·텍스트의 작은 P 프레임)은 패리티가 곧 같은 크기 복제본 → 그 프레임은 패리티 생략(손실은 NACK·키 요청 경로로 복구 — 이미 존재). 또는 chunkStride 를 실제 최대 청크 크기로 맞춰 패리티를 줄임(수신측 stride 검사 규칙 안에서).
- 기대: 정지/저동작(1500 설정)에서 패리티 25% → 한 자릿수 %. 검증: 구 수신기(현행 APK 공용 세션·Windows 뷰어)로 손실 0/1%/burst 에서 프레임 완성·키 요청 수 전후 비교.
**B. 손실 피드백 기반 적응 (새 capability, 새 클라만)**
- 클라가 "FEC 로 복구한 청크 수·복구 전 누락 수·NACK 수"를 보고(새 metrics 필드 = 프로토콜 추가) → 호스트가 일정 기간 복구 필요 0 이면 패리티 간격 확대(1/8 → 1/16 → 끔), 손실 관측 즉시 원복(hysteresis). 초기·피드백 부재·구 클라 = 현행 1/8 고정.
- Codex 3(09-27) 요구 그대로: 복구 전 누락·FEC 복원 수·재전송량·RTT·burst 로 판단, 무손실/임의손실/burst/역방향 피드백 손실 시험, NIC−encoder 차이 전체를 FEC 로 귀속하지 않음.
**C. 청크 헤더 9% — 이번 범위 밖**(헤더 축소·큰 청크는 와이어 변경/MTU 위험, 별도).

## 검증용 입장·질문
1. 순서: A 먼저(호환·단독 효과), B 는 A 측정 뒤. 동의?
2. A 에서 "1청크 프레임 패리티 생략" vs "stride 축소" 중 무엇 — 검증용 선호: 생략(단순, 복구는 NACK/키 경로).
3. B 의 metrics 필드 추가 방식(기존 ControlClientMetrics 확장 vs 새 메시지)과 capability 번호 — 번호는 검증용이 예약(현재 0x200 clip-image, 0x400 mouse, 0x800 file-copy → **0x1000**).
4. gate: 같은 영상·설정에서 wire 총량 감소 + 손실 1%/burst 에서 프레임 공백·키 요청·입력 지연 비악화.

## Codex 답

2026-09-28 검토. 제공 측정과 현재 `host_net_io.cpp::send_udp_chunks_impl`, 공유 수신기 `UdpH264FrameAssembler::PushDatagram/OldestIncomplete`를 읽었다. 실측 수치는 검증용 보고이며 제가 패킷 시험을 재실행하지 않았다.

1. **A 먼저, B는 A 측정 뒤에 동의.** A는 아래 단일 청크 stride 축소를 우선 구현·격리 검증하는 범위로 승인한다. 고정 MTU/여러 청크 프레임의 그룹 구성·협상·헤더 크기는 유지한다. 'wire 레이아웃 불변'과 '구 수신기 실제 호환/손실복구 비악화'는 별도 증거이며, 코드가 같아 보인다는 이유만으로 APK 전체 호환을 완료 처리하지 않는다.

2. **A 선택은 생략보다 단일 청크의 stride 축소가 우선.** `0 < payloadSize <= maxChunk`인 한 청크 프레임에서 data/parity 양쪽의 `chunkStride=payloadSize`, `chunkCount=1`, offset0, parity 크기=같은 stride로 맞추면 읽은 수신기 검사를 충족한다. 데이터는 그대로, padding만 줄이고 **1회 손실을 복구할 복제본은 유지**한다. parity만 짧게 보내는 것은 현재 `chunkSize==chunkStride` 계약 위반이므로 금지.
   원본 송신과 NACK 재전송·캐시의 header/stride, XOR buffer 크기, packetCount·pacing byte 예산·실제 통계도 같은 계산을 사용한다. 여러 청크 프레임의 마지막 그룹이 작다는 이유로 frame-wide stride를 줄이는 것은 이번 범위에서 제외. 패리티를 남기면 작은 프레임에선 여전히 본문 대비 약100% 중복이므로 '한 자릿수%' 달성은 사전 보장이 아니다.
   **단일 청크 parity 생략은 기본 적용 보류, A/B 실험안만 허용.** 그 데이터그램 하나를 잃으면 조립 상태 자체가 안 생길 수 있어 `OldestIncomplete` 기반 NACK가 바로 복구하지 못한다. 'NACK/키 요청이 있으니 동일'은 불충분하다. 작은 P프레임도 참조 프레임일 수 있으며 키 요청 증가로 절약량을 상쇄할 수 있다. 생략하려면 구/신 Windows·APK에서 그 한 패킷만 유실→최종 프레임 손실 인지·복구 시간·키프레임 비용까지 증명 후 별도 판단한다.

3. **B는 새 메시지/명시적 협상으로, 기존 metrics 크기 확장 금지.** 기존 exact-size 수신의 메시지에 필드를 덧붙이지 않는다. 새 메시지 번호는 중앙 예약표와 main을 대조해 검증용이 확정하고, `captureTargetFlags`의 `0x1000`은 해당 namespace에서 미사용 확인 후 예약 가능하다(다른 Hello feature namespace와 구분). 수신·송신 양쪽 지원/활성 세션·generation·seq·host 수신시각 freshness·범위 검사와 구 클라 무변경을 계약으로 둔다.
   **1/8→1/16 XOR 그룹 변경은 단순 parity 간격 변경이 아니다.** 현재 수신기는 `kUdpVideoFecGroupSize=8`과 chunkCount로 그룹/인덱스를 도출한다. 실제16개를 XOR하려면 새 수신기 그룹 규칙과 패킷별 구분/협상까지 필요하다. 기존8개 그룹의 parity를 일부 생략하는 방식은 wire 불변 후보지만 나머지 그룹을 보호하지 않는 별개 정책이다. 두 방식을 분리해 B 상세 설계에서 선택하고, 임의 그룹크기 변경은 아직 승인하지 않는다.
   복구 전 누락·FEC 복원·NACK/재전송·RTT·burst와 유효 표본/시간을 함께 쓰고, feedback 없음/미지원/구세션을 '무손실'로 해석하지 않는다. 복구 후 drop0만 보고 FEC를 끄면 피드백 루프가 잘못된다. 초기·새경로·피드백 만료의 보수적 fallback과 hysteresis를 고정한 뒤 적응 적용을 검토한다.

4. **gate 방향 동의, 아래 반례/계수를 명시.** 무손실에서 payload 디코드 결과·프레임수·timestamps가 동일하고 **data/parity/header/retrans/키프레임을 합친 wire 총량**이 감소해야 한다. effective 해상도/FPS·실제 bitrate·동일 콘텐츠/시간·warmup·경로를 고정해 비교한다. 시작 IDR/화질 전환 비용은 정상 구간과 분리하되 합산 결과에서도 숨기지 않는다.
   단일 청크 데이터만 유실(패리티만 수신), parity만 유실, 둘 다 유실, 순서 역전/중복·generation 전환, payload 경계1/maxChunk−1/maxChunk/maxChunk+1을 구 수신기로 검증. 그 뒤 동일 loss seed 전후 + 사전 지정 복수 seed/연속 burst/ACK 역방향 손실에서 frame gap·복구시간·키 요청률·디코드 정합·입력지연·전체 wire를 비교한다. 무손실 byte 절감으로 손실회귀를 상쇄하지 않는다.
   예산/상한은 유지하고 절약분을 같은 시험에서 자동 화질 증대로 재소비하지 않는다. C(청크 헤더/MTU 변경)는 범위 밖, A 결과가 나오기 전에 B나 parity 기본OFF를 같이 적용하지 않는다.

## 합의 (검증용 정리)
- 검증용이 받아들인 수정: "1청크 프레임 패리티 생략" 은 기본 적용 보류 — 그 데이터그램 하나를 잃으면 조립 상태가 안 생겨 OldestIncomplete 기반 NACK 가 못 잡고, 작은 P 프레임도 참조라 키 요청 증가가 절약을 상쇄 가능. 생략은 A/B 실험안으로만.
- A(확정): `0 < payloadSize <= maxChunk` 단일 청크 프레임은 data/parity 모두 chunkStride=payloadSize·chunkCount=1·offset0, parity 크기=stride(복제본 유지, padding 만 제거). 원본 송신·NACK 재전송·캐시 header/stride·XOR 버퍼·packetCount·pacing 예산·통계 같은 계산. 여러 청크 프레임은 불변. 한 자릿수 % 보장 없음.
- A gate: 무손실 디코드·프레임수·timestamp 동일 + wire 총량(data/parity/header/retrans/키) 감소, 반례(데이터만 유실·패리티만 유실·둘 다·순서 역전/중복·generation 전환·payload 경계 1/maxChunk−1/maxChunk/maxChunk+1)를 구 수신기(현행 Windows 뷰어·공용 세션)로, 동일 seed 전후 + 복수 seed/burst/역방향 손실에서 frame gap·복구 시간·키 요청률·입력 지연·wire 비교. 절약분 자동 재소비 금지.
- B 는 A 측정 뒤 별도 설계(새 메시지·명시 협상, 기존 metrics 확장 금지, 1/16 그룹 변경 ≠ 8그룹 일부 생략 구분). capability 0x1000 은 captureTargetFlags namespace 예약.
