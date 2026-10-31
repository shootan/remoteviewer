# 1차 캡처(gnlink_host_20260908_112450) 커버리지 정량 — 검증용 지적 "송신의 약 절반만 담김" 확인 (2026-09-08 11:45)

방법: pcapng 의 TX VideoChunk(kind 302) 헤더에서 seq·chunkIndex(offset 40)·chunkCount(offset 42)·flags(offset 14) 를 읽어, seq 별 "헤더가 선언한 chunkCount" 대비 "캡처된 서로 다른 chunkIndex 수" 를 셈. 호스트 로그 `wire seq=N … chunks=K` 와도 대조.

## 결과
- seq 4,722개(양 세션), 선언 chunk 190,439 중 캡처된 고유 chunkIndex 96,312 (50.6%) — **이 값은 패리티 인덱스 충돌을 포함한 방향 참고치이며 전체 NIC 누락률로 쓰지 않는다.** 캡처 불완전의 근거는 검증용의 같은 gen+seq 대조(verify_notes.md §2): 뷰어가 `fecRecovered=0` 으로 완성한 AU 120개 중 pcap 에서 data 청크가 불완전한 것 83개, 그중 패킷 0개 약 40개(예 gen6 seq 38775·38842·40015·40152·40308). 완전 캡처 seq 457, 부분 4,265. 호스트 로그 seq 3,157개 중 캡처 0청크 672.
- 초 단위 커버리지(11:27:25~50): 36~66% 범위(11:27:31 97% 예외). 정지 구간 11:27:36~43 도 44~59% → "NIC TX 지속" 결론은 유지되지만 **절대 청크 수는 실제 송출의 약 절반**.
- 빠진 chunkIndex 는 연속 구간 단위(예: seq 38773 chunks=43 중 캡처 idx 11-14, 19-26, 31-34; seq 38701 22 중 idx 1-8 결손).
- 프레임 길이: 최대 1,242 B(orig == 캡처 전 길이), 1,600 B 초과 0 → 캡처된 프레임에는 세그먼트 오프로드로 합쳐진 징후가 없다(관측 사실일 뿐, 누락 원인을 '드라이버/오프로드 아님' 으로 단정하지 않음). 호스트 로그 chunks=K 는 parity 포함 wire 청크 수라 헤더 chunkCount(데이터 청크 수)와 다름(정상).
- (정정, 검증용 pcap 재검산) "중복 6,024 = 재송출" 은 오류. flags 0x10 인 FEC 패리티 패킷은 chunkIndex 가 **패리티 그룹 번호**라 data 인덱스와 충돌한다. (gen,seq,chunkIndex) 중복 그룹 6,024개의 구성 = **data+parity 6,020 / data+data 4**. data+data 4건은 전부 gen6 seq 40359 idx 27~30(원송신 11:27:36.688 + NACK round 0 재전송 11:27:36.813). sendQpcUs 는 sender 가 AU 단위로 찍어 같은 AU 의 data·parity 가 같은 값을 가지므로 구분 근거가 아니며, IP ident 는 어떤 두 패킷이든 다르다. 실제 재전송은 **seq 40359 idx 27..56 30건**(그중 원송신도 캡처된 것은 27~30 4건)뿐.
- NIC 통계(비관리자): Realtek OutboundDiscardedPackets 0, OutboundPacketErrors 0. 바인딩: ms_tcpip·ms_pacer 등 직접 바인딩(외부 vSwitch 미사용). LSO v2 사용, RSS 큐 4.
- pktmon stop: "손실된 이벤트 없음", etl2pcap 패킷 삭제 0 → **pktmon 자체 통계로는 손실이 보고되지 않았는데 절반이 비어 있음.**

## 가능한 원인(미확정, 2차 실행 설계용)
1. pktmon 캡처 경로의 버스트 드롭(ETW 버퍼 외 내부 큐) — 송신은 pacing 버스트(순간 수천 pkt/s). 확인: 2차에서 `--comp all` 로 구성 요소별 카운트를 비교하고 `pktmon counters`(있으면)로 컴포넌트별 통과/드롭을 본다.
2. `--comp nics` 가 잡는 지점이 Realtek 미니포트가 아닌 다른 NIC 구성 요소이거나, 다중 TX 큐(RSS 4)에서 일부 큐만 관측. 확인: 관리자 실행 시 `pktmon list` 로 Realtek 구성 요소 id 를 얻어 `--comp <id>` 지정, 또는 `--comp all` 비교.
3. 대안 도구: npcap/Wireshark(미설치) 또는 `netsh trace start capture=yes`(ETW, 같은 한계 가능).
- 어느 쪽이든 1차 결론("11:27:36~43 호스트→뷰어 방향 송출이 NDIS 계층에서 계속 관측됨(하한), 뷰어 수신 0")은 청크의 절반이 보였다는 사실만으로 성립하나, 청크 단위 손실률·재전송 여부·AU 완전성 판정에는 1차 캡처를 쓰지 말 것. pcap 의 seq 공백을 호스트 송신자 드롭으로 해석하지 말 것(뷰어가 완성한 AU 약 40개가 pcap 에 0패킷 — 검증용 대조). 누락 원인은 미확정.
