import io, re
ROOT = r'D:\remote\remote'

def rw(path, fn):
    raw = open(path, 'rb').read(); t = raw.decode('utf-8')
    nl = '\r\n' if '\r\n' in t else '\n'
    t2 = fn(t, nl); open(path, 'wb').write(t2.encode('utf-8')); print('ok', path, 'crlf=', nl == '\r\n')

def history(t, nl):
    assert '### 415)' not in t
    L = [
"### 415) 2026-09-08 11:27 고화질 정지 — 호스트 UDP 패킷 캡처 1차 회차 분석·한계 기록 (A2A task t-gmwzjgy1, Codex t-2z9fjuqu 마무리, 검증용 remote#865h2pvp 변경 없이 OK)",
"- 목표: 0.2.103 실기 중 11:27 고화질 영상 정지(재접속 후 정상)를 호스트 NIC 패킷 캡처와 NAS 로그로 대조한 결과와 판정 범위를 기록한다. 제품 코드·설정 변경, 2차 캡처, 설치, push, 서버 배포 없음(문서만).",
"- 근거 원본(모두 `.claude/` 미추적, 저장소에 넣지 않음): 검증용 최종 노트 `.claude/packet-capture-20260908/verify_notes.md` §1~§7(Codex 정정 반영본), 작업용 `report_1127.md`·`capture_coverage.md`·`pcap_freeze_window_v2.txt`(검증용 지적대로 정정한 뒤 인용: streamGeneration 은 헤더 @48..55 u64 — 이전 파일은 @44 에서 읽어 하위 32비트가 chunkStride 1112 였음; \"중복 6,024 = 재송출\" 은 FEC 패리티(flags 0x10) 의 chunkIndex 가 그룹 번호라 data 인덱스와 충돌한 오류 — 분리하면 data+parity 6,020 / data+data 4, 실제 재전송은 gen6 seq 40359 idx 27..56 30건; 50.6% 는 전체 NIC 누락률로 쓰지 않음), NAS 사본 `.claude/field-20260908-1127/`(viewer/client/host.log + 디렉터리 서버 저널, sha256 기록). 캡처 절차 `capture_plan.md`·`start_output.txt`.",
"- 회차 식별: 2026-09-08 11:24:50.7~11:31:41.7 KST, 관리자 pktmon(사용자 UAC 승인, `--comp nics --pkt-size 192 --file-size 128 --log-mode circular`), 필터 UDP 43000 + IP 211.218.222.1(양방향), snaplen 192 B, 128 MB 순환 미도달(연속 보존), 106,256 패킷·etl2pcap 삭제 0·ETL 손실 이벤트 0. `gnlink_host_20260908_112450.etl` 10,681,934 B sha256 46e9460c64c9090dc0d9f3cab406002a9935247f70d10e4e2464005cdb6b9c25 / `.pcapng` 23,680,496 B sha256 895b4d76d5f8affcc9166c7d34f6ab3b706d3ccf5885de1830e74c0409158ba7. 종료는 사용자 11:27 정지 통지 뒤 stop.request 로 정상 종료(자기 etl 확인).",
"- 검증된 경계(확정): 시계는 같은 AU 의 pcap 마지막 청크 vs 뷰어 assembly 벽시계 n=78, p50 −374 ms → 뷰어 ≈ 호스트 −375 ms. 뷰어 마지막 수신 11:27:36.314(뷰어 시계) ≈ 호스트 11:27:36.689 = pcap gen6 seq 40359 idx 27 송신(11:27:36.688) → 순방향(호스트→뷰어) 손실 시작. 호스트는 NDIS 계층(pktmon)에서 구 peer 211.218.222.1:60698 로 11:27:43.808(seq 40769)까지 송신 관측(관측 하한, NIC 하드웨어 밖 보장 아님). 뷰어 NACK 3건(11:27:36.813/.843/.874, missing 27..56) 호스트 NIC 도착 → round 0 직후 idx 27..56 30/30 재전송 관측; round 1·2 응답은 캡처에 없으나 캡처 불완전 탓에 판정 불가. 역방향 ControlData 11:27:37~40 초당 2~4개 도달. 뷰어 11:27:40.580 `udp-tunnel closed reason=shutdown`(뷰어 측 종료) 은 호스트 11:27:43.793 peer-lost 와 별개 사건. 11:27:44.622 새 소스 포트 56584 Hello → epoch 6 → 11:27:46 gen7 IDR(seq 40770) → 정상. 호스트 앱 wire seq 로그와 pcap seq 범위는 초 단위로 일치.",
"- 한계·미확정: 캡처는 호스트 송신의 일부만 담음 — 같은 gen+seq 대조로 입증(뷰어가 fecRecovered=0 으로 완성한 AU 120개 중 pcap 불완전 83개, 그중 패킷 0개 약 40개); 누락 원인 미확정(드라이버/오프로드 단정 금지) → AU 단위 송신 완전성·NACK round 1·2 응답·pcap seq 공백(461건)의 송신자 귀속은 판정 불가. H>P HelloAck(301) 미캡처(같은 미디어 소켓 송신, 누락 부류로 추정·확정 아님). 물리 드롭 위치(공유기/ISP/회사 NAT/뷰어 PC 수신)는 호스트 단독 캡처로 미확정. 직전 11:27:36 송출 급증(캡처 하한 1,554 청크/s, IDR 40367)과의 인과 미확정. 초별 표는 열마다 시계가 달라 동시 구간으로 읽지 않음.",
"- 핵심 결론(Codex 합의 문구): \"호스트 송신·요청 30청크 재전송 관측, 뷰어 무수신, 역방향 도달, 새 port 재접속 회복; 물리 드롭 위치는 미확정.\"",
"- 변경 파일: `docs/history.md`(이 항목), `docs/구현계획.md`(0.2.103 실기 항목 상태만), `docs/작업목록.md`(1.0 아래 한 줄). 2차 실행용 helper(`Start/Stop-GnlinkCapture.ps1`, chcp 65001·`.etl` 줄 판별, 검증용 재검토 통과)는 `.claude/` 에 준비만, 실행 없음.",
"- 다음: 2차 캡처 방법(구성 요소 id 지정·`pktmon counters --drop-reason`·Npcap 등)은 Codex 결정. 뷰어 측 캡처 없이는 드롭 위치 확정 불가.",
    ]
    return t.rstrip('\r\n') + nl + nl + nl.join(L) + nl

def plan(t, nl):
    old = "- [~] 0.2.103 실기 — 정적 정지 없음·UAC 클릭/복귀 부분 확인, 고화질·재접속 정지 미해결"
    assert t.count(old) == 1
    return t.replace(old, old + " — 11:27 정지 호스트 캡처 1차 분석 완료(history #415), 2차 캡처 방법 Codex 결정 대기")

def tasks(t, nl):
    anchor = "(history #413). 제품 수정 미실행."
    assert t.count(anchor) == 1
    return t.replace(anchor, anchor + nl + "1.0.1 [~] **11:27 고화질 정지 패킷 캡처** — 1차 캡처 분석 완료·2차 캡처 방법 결정 대기 (history #415)")

rw(ROOT + r'\docs\history.md', history)
rw(ROOT + r'\docs\구현계획.md', plan)
rw(ROOT + r'\docs\작업목록.md', tasks)
print('field capture docs ok')
