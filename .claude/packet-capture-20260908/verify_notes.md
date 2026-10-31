# 호스트 패킷 캡처 1차 회차 — 검증용 최종 검사 노트 (remote#865h2pvp, 2026-09-08, Codex 정정 반영본)

범위: 호스트 NIC(NDIS 계층, pktmon) 캡처 1회차 + NAS viewer/host 로그 대조. 새 캡처 없음. 제품·설정 변경 없음.
산출: gnlink_host_20260908_112450.etl 10,681,934 B sha256 46e9460c… / .pcapng 23,680,496 B 895b4d76… (작업용 해시와 일치). 분석기 .claude/probe/gnlink_pcap.py, 보고 verify_pcap_report.txt, verify_persec.csv, viewer_assembly_1124_1131.txt(NAS 발췌), viewer_clock_1127.txt.

## 1. 캡처 자체의 무결성 (검증용 직접)
- pcapng: 링크타입 1(Ethernet), 인터페이스 1개, EPB 106,256개. 모든 EPB 의 wire 길이 == IP total length + 14 (불일치 0) → 데이터그램은 온전히 관측됐고 잘림은 snaplen 192 뿐(103,521개 잘림, 2,735개 전체). IP 단편화 0. etl2pcap 삭제 0, ETL 손실 이벤트 0. 128MB 순환 미도달(11:24:51.648~11:31:41.675 연속).
- 파싱: 106,256 전부 GNLink(포트 43000), VideoChunk 헤더(pack(1) 88B, streamGeneration @48) 절단 0. gen 6/7 = host.log streamGen 6/7 일치.

## 2. 캡처 불완전 — 같은 gen+seq 로 입증 (시계 무관)
- 뷰어가 `stage=assembly … fecRecovered=0` 로 **완성**(= 모든 data 청크 수신)을 기록한 AU 120개(11:24:51~11:31:41)에 대해 pcap 의 H>P 고유 data chunkIndex 집합을 대조: **83개가 불완전** — 그중 약 40개는 pcap 에 패킷이 0개(예 gen6 seq 38775·38842·40015·40152·40308, gen7 40823·41003·41622·42120·43143…), 나머지는 부분(예 40318 20/249, 42419 12/253, 43724 19/263). 37개만 100%.
- 패리티(flags 0x10)는 chunkIndex 가 그룹 번호라 별도 집계(gen6 4,618·gen7 7,868). (gen,seq,idx) 중복 그룹 6,024개의 구성을 검산: **data+parity 6,020 / data+data 4**(전부 40359 idx 27~30 = 원송신+NACK 재전송). 모든 그룹이 sendQpcUs 를 공유하는 것은 sender 가 AU 단위로 찍기 때문이라 "재송출" 근거가 못 됨. 진짜 재전송은 seq 40359 idx 27..56 30건뿐(원송신도 캡처된 것은 27~30).
- 결론: 이 캡처는 호스트 송신의 일부만 담았다. **누락 원인은 미확정**(NDIS/pktmon 경로·`--comp nics` 관측 지점·버스트 등 후보만; "드라이버 누락" 이나 "오프로드 아님" 을 기각 근거로 쓰지 않음). 작업용 capture_coverage.md 의 50.6%(헤더 선언 chunkCount 대비 고유 청크)는 방향만 참고하며 **전체 NIC 누락률로 단정하지 않음**(선언 수 = 실제 송출 수 가정 불가: 중도 abort·범위 끝·원송신/FEC/재전송 혼재). 캡처 불완전의 근거는 위 같은 AU 대조(뷰어 완성+FEC 0 vs pcap 인덱스 집합)만으로 충분.
- 따라서 이 캡처로 판정 **불가**: AU 단위 호스트 송신 완전성, NACK round 1·2 응답 유무, pcap 상 seq 공백(461건)의 송신자 귀속. 판정 **가능**(하한/존재 증명): 특정 시각에 특정 seq/idx 가 NDIS 계층에서 관측됐다는 사실, 저속 역방향(NACK·제어·Hello) 존재, peer 튜플 전환.
- NIC 관측은 NDIS 계층(pktmon) 이며 "하드웨어 밖으로 나갔다" 를 보장하지 않는다(NIC 통계 OutboundDiscarded 0 은 작업용 보고, 검증용 미확인).

## 3. 시계 정렬 (같은 이벤트 대조)
- 같은 AU 의 pcap 마지막 청크 시각 vs 뷰어 `stage=assembly` 벽시계: n=78, viewerWall − pcapLastSeen = −378 ~ −298ms, p50 **−374ms**, 100% 캡처된 AU 들(38695·40304·40772 등) −372 ~ −375ms → 뷰어 벽시계 ≈ 호스트 벽시계 − 375ms(편도 지연 ~1.5ms 포함). 보조: 뷰어 ping seq 3108 송신(11:27:36.311) ↔ pcap P>H ControlData 11:27:36.687(len 52) 도 +376ms.
- 초별 표는 같은 시계가 아니므로 "동시 구간" 으로 주장하지 않음; 아래 경계는 위 오프셋으로 변환.

## 4. 11:27 경계 — 검증된 것
1) 뷰어 마지막 수신: viewer.log 11:27:39.886 link-silent datagramAgeUs 3,571,588 → 마지막 datagram 뷰어 11:27:36.314 ≈ **호스트 11:27:36.689**. pcap 에서 그 시각은 gen6 seq 40359 idx 27 송신(11:27:36.688) — 뷰어 NACK 이 40359 의 idx 27..56 을 요구한 것과 정확히 일치. → **관측 경계 ≈ 호스트 11:27:36.69**(뷰어 마지막 수신에 대응하는 송신 패킷 = seq 40359 idx 27). "최초 물리 손실이 정확히 36.688" 로 강화하지 않음(Codex 최종 확인 조건) — 이는 관측된 경계이지 물리 손실 시점의 확정이 아님.
2) 호스트 송신 지속(하한): pcap 에 구 peer 60698 로 11:27:43.808(seq 40769)까지 H>P 청크 관측(초당 547~1,554, 캡처 누락 고려 시 실제는 더 많음). host.log wire seq 도 같은 구간 진행.
3) 역방향 도달: 뷰어→호스트 ControlData 11:27:37~40 초당 2~4개, VideoNack 3건(36.813/.843/.874) 호스트 NIC 에서 관측 → 뷰어 송신은 호스트에 닿음.
4) 재전송: NACK round 0(missing 27..56, 30개) 직후 11:27:36.8135~.8137 에 idx 27..56 **30/30 재전송 관측**. round 1·2 응답은 캡처에 없으나 §2 때문에 무응답으로 판정 불가(관측 하한). 코드: dispatch 는 NACK 마다 RetransmitAu(host_startup_control.cpp:361-370, 라운드 중복 제거 없음), 예산 토큰 버킷(rate = pacePeakBps/8×15%, cap = rate/2+64KB; 11:27:37 pacePeakBps 45M → ≈844KB/s·cap ≈486KB, 요청 ≈33KB) 초과 아님, sendto 반환 무시(host_net_io.cpp:272). 재전송 27..56 도 뷰어 미수신(round 1·2 목록 동일).
5) 뷰어 종료·재접속: viewer.log 11:27:40.580 control action failed(udp-tunnel closed **reason=shutdown**) → 11:27:40.589 done — 뷰어 프로세스 측 종료(shutdown 사유; 사용자/클라이언트 동작)이며 호스트의 11:27:43.793 peer-lost(host.log)와는 별개 사건. 재접속 Hello 11:27:44.622(pcap, 새 소스 포트 56584) → 호스트 ControlAck 3ms 뒤 → host.log 11:27:44.620 punch·epoch 6 → gen7 IDR 40772 11:27:46.409(pcap)/뷰어 11:27:46.031 수신.
6) 재접속 성공에도 pcap 에 H>P HelloAck(301) 0건: 코드상 같은 미디어 소켓으로 송신(host_startup_control.cpp:291) → 필터(IP 동일)·엔드포인트·파싱 원인 아님. §2 의 누락 부류로 추정(확정 아님). 호스트가 Hello 를 처리한 증거는 3ms 뒤 ControlAck 과 뷰어의 "udp hello ack features=0x1e".

## 5. 미확정 / 판정 범위
- 미수신 위치(공유기·ISP·회사 NAT·뷰어 PC 수신)는 호스트 단독 캡처로 확정 불가. 확정: 호스트는 NDIS 계층에서 계속 송신했고(하한), 뷰어는 40359 idx 27 이후 받지 못했으며, 역방향은 정상. 새 소스 포트로 즉시 정상.
- 직전 11:27:36 송출 급증(pcap 하한 1,554 청크/s, IDR 40367 342청크 36.856)과의 인과는 미확정.
- 캡처 누락 원인 미확정. 2차 캡처 방법(구성요소 지정·counters --drop-reason·Npcap 등)은 Codex 결정.

## 6. 정정 이력
- 1차 노트 "4청크만 재전송" → 30/30 관측(원송신의 27~30 만 캡처돼 중복으로 집계된 오류). "round 1·2 무응답" → 미확정. "pktmon 드라이버 누락 추정/오프로드 아님" → 원인 미확정으로 격하. 초별 표 비교 → 같은 gen+seq 증명으로 대체. "뷰어 종료" → shutdown 사유(뷰어 측)로 명시, host peer-lost 와 분리. 시계 오프셋 ±0.4s 가정 → 같은 AU 대조 −375ms 로 추정.
- 작업용 중간 파일 정정 필요: pcap_freeze_window 의 gen(오프셋 44 에서 u64 를 읽어 하위 32비트 = chunkStride 1112, 상위 = gen; 올바른 위치는 @48..55), capture_coverage.md 의 "중복 6,024 = NACK 재전송" (실제는 FEC 패리티 chunkIndex 충돌; 재전송은 30건), pcap seq 공백을 송신자 드롭으로 해석 금지.

## 7. 핵심 결론(Codex 합의 문구)
호스트 송신 관측(NDIS 계층 하한)·요청 30청크 재전송 관측(round 0), 뷰어 무수신(11:27:36.314 뷰어 ≈ 11:27:36.689 호스트, seq 40359 idx 27 부터), 역방향 도달 정상, 새 소스 포트 재접속으로 회복; **물리 드롭 위치는 미확정**. 2차 캡처·제품 수정·push 없음.

## 8. 종결 (Codex 최종 확인)
fd1f220 / 4a92d5e / abd136e diff 확인·수용, 캡처 분석·문서화 승인, 작업용 task 완료 처리 가능. 확정 범위: 호스트 NDIS 송신 + 첫 NACK 30 재전송 관측 / 뷰어 무수신 / 역방향 도달 / 새 port 재접속 회복. 미확정: 물리 드롭 위치·전체 손실률. 제품 수정·2차 캡처·설치·push 승인 없음(다음 수정 계획은 별도 task).
