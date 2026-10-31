# GNLink 호스트 UDP 패킷 캡처 계획 — 2026-09-08 (task t-2occguzn, 작업용 remote#nt5lxb96)

상태: **STOPPED 11:31:41.7 (stop.request, 사용자 11:27 정지 통지) — 결과 report_1127.md**. 이 파일은 미추적(.claude/).

## 0. 권한 확인 (11:21 KST)
- 작업용 셸 무결성: MEDIUM (`whoami /groups` 에 S-1-16-12288 없음, IsInRole(Administrator)=False, 사용자 shotan\shotan)
- `pktmon status` / `pktmon list` / `pktmon filter list` 모두: `PktMon 드라이버와 통신하지 못했습니다.: 액세스가 거부되었습니다.`
  → 기존 캡처·필터 존재 여부를 **확인할 수 없음**(아래 시작 명령이 먼저 status 를 보고 활성 캡처가 있으면 시작하지 않음).
- tshark/dumpcap: PATH 에 없음, `C:\Program Files\Wireshark` 없음. pktmon: `C:\WINDOWS\system32\PktMon.exe`.
- 이 빌드(Windows 11 10.0.26200.9168)의 pktmon 변환 하위 명령은 `etl2pcap` 임(`etl2pcapng` 는 "알 수 없는 명령").
  start 옵션: `--capture`, `--comp {all|nics|id...}`, `--pkt-size`, `--file-name`(-f), `--file-size`(-s, MB), `--log-mode`(-m).

## 1. 현황 (읽기 전용, 11:21 KST)
| 항목 | 값 |
|---|---|
| GNLinkHost | PID 29104 (Console) |
| GNLinkStream | PID 18844 (Console) — `UDP 0.0.0.0:43000` 소유(netstat -ano -p UDP). 다른 GNLink UDP 소켓 없음 |
| GNLinkInputService | PID 15632 (Services), 20436 (Console) |
| 사용 NIC | `이더넷` ifIndex 12, Realtek PCIe GbE, 192.168.0.76 (1 Gbps). 그 외 Up: vEthernet(Default Switch) ifIndex 15 172.30.192.1, vEthernet(WSL) ifIndex 25 172.30.80.1, Tailscale ifIndex 6 |
| 활성 peer | **211.218.222.1** (회사 뷰어 공인 IP). NAS host.log.1: `09:59:46 punch -> :64652`, `10:24:04 punch -> :50337`, `10:35:08 punch -> :60698` + `udp peer updated; media barrier armed epoch=5`. 그 뒤 새 punch 없음(현재 host.log 10:36:55~, 11:22 까지 wire seq 진행 중 = 같은 세션). 로컬 `%LOCALAPPDATA%\GNLink\host_app.log` 는 11:12:04 부터 회전돼 peer 줄 없음 |
| 미디어 포트 | 호스트 43000/UDP (peer 포트는 세션마다 바뀜: 필터는 IP 만 한정) |

## 2. 필터
```
pktmon filter add GNLinkUDP -t UDP -p 43000 -i 211.218.222.1
```
(UDP, 포트 43000 어느 쪽이든, IP 211.218.222.1 어느 쪽이든 → 호스트↔뷰어 송수신 양방향)

## 3. 캡처 파라미터
- 출력: `D:\remote\remote\.claude\packet-capture-20260908\gnlink_host_<yyyyMMdd_HHmmss>.etl`
- `--comp nics` (NIC 구성 요소만; 개별 component id 는 `pktmon list` 가 관리자 전용이라 미확인)
- `--pkt-size 192` (Ethernet 14 + IPv4 20 + UDP 8 + UdpVideoChunkHeader ≈88 → seq/gen/chunk 식별 충분, payload 불필요)
- `--file-size 128` (MB) · `--log-mode circular`
- watchdog: 같은 관리자 PowerShell 안의 `Start-Job { Start-Sleep 600; pktmon stop }` → 최대 10분. **창을 닫으면 job 도 죽으므로 창 유지.**

## 4. 관리자 PowerShell 에서 붙여넣을 명령
### 시작 (1줄) — status 에 활성 캡처가 있으면 시작하지 않음
```
$s=(pktmon status|Out-String); if($s -match '(?<!In)Active|(?<!비)활성'){"EXISTING CAPTURE ACTIVE - NOT STARTED`n$s"}else{$ts=Get-Date -Format yyyyMMdd_HHmmss; $f="D:\remote\remote\.claude\packet-capture-20260908\gnlink_host_$ts.etl"; pktmon filter list; pktmon filter remove | Out-Null; pktmon filter add GNLinkUDP -t UDP -p 43000 -i 211.218.222.1; pktmon start --capture --comp nics --pkt-size 192 --file-name $f --file-size 128 --log-mode circular; Start-Job -ScriptBlock { Start-Sleep 600; pktmon stop } | Out-Null; "START $ts stopBy=$((Get-Date).AddMinutes(10).ToString('HH:mm:ss')) file=$f"; pktmon status}
```
### 중지 (1줄) — 사용자 멈춤 시각 통지 시 (타임아웃 전이면)
```
pktmon stop | Tee-Object -FilePath "D:\remote\remote\.claude\packet-capture-20260908\pktmon_stop_$(Get-Date -Format HHmmss).txt"; pktmon filter remove; pktmon status
```
- 중지 뒤 변환·해시·패킷 수·시간 범위는 작업용이 수행: `pktmon etl2pcap <etl> -o <pcapng>` (드라이버 불필요, 비관리자 가능 여부는 실행 시 확인).

## 5. 기록
- 11:21 준비 완료. 시작 시각 / 필터 / 파일명은 관리자 실행 결과("START …" 출력)를 받아 여기에 추가.
- 11:24:50 사용자 UAC 승인 → 관리자 PowerShell 에서 `start_capture.ps1` 실행(출력 `start_output.txt`). status-before: 캡처 실행 중 아님, 필터 없음. 필터 GNLinkUDP(UDP, 211.218.222.1, 43000) 추가. **START 20260908_112450 stopBy=11:34:51** file=`gnlink_host_20260908_112450.etl` (--comp nics, --pkt-size 192, --file-size 128MB, circular). 중지: `stop.request` 파일 생성 시 즉시, 아니면 타임아웃 → 스크립트가 stop 출력 기록·filter remove·etl2pcap 변환.
- CAPTURE_RUNNING pin: Codex remote#8cotzdve, 검증용 remote#865h2pvp (11:25).
- 11:31:39 Codex 경유 사용자 통지("11시27분에 멈췄어… 다시접속했을때는 정상영상") → stop.request 생성 → 11:31:41.7 스크립트 정상 종료(로그 파일 = 자기 etl, 손실 이벤트 0) → filter remove → etl2pcap 106,256 패킷(삭제 0). etl 10,681,934 B(순환 미회전) / pcapng 23,680,496 B. 해시 capture_hashes.txt. 분석·대조: report_1127.md, NAS 원본 사본 .claude/field-20260908-1127/.
