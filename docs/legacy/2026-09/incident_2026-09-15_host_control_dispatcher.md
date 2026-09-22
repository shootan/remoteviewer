# 사건 2026-09-15 — 집 Host 재접속 실패 (0.2.126) · 제어 디스패처 무기한 차단

조사 전용. 제품 수정·배포·설치 앱 조작 없음. 상세 근거·철회 이력은 `.claude/urgent-126-20260915/incident.md`(미추적, rev12).

## 원본 스냅샷 / 해시
| 대상 | 원본 | sha256(앞 16) |
|---|---|---|
| 집 Host 로그(NAS) | `logs/shotan/8ec6ecb1-…/host.log` | `a9942d4a7c247b0c` (9,371,215B, 14:09 기준) |
| 회사 Viewer 로그(NAS) | `logs/shotan/68f79d01-…/viewer.log` | `696bd05a77d5846d` (9,760,530B) |
| 요청측 읽기전용 스냅샷 | `.claude/urgent-126-20260915/host.log` · `viewer.log` | `bb4a09485616c557` · `d390077149f630a4` |
| 로컬 감독자 로그 | `%LOCALAPPDATA%\GNLink\host_app.log`(+회전본 .1~.10) | 스탬프 없는 감독자 줄 포함 |
| wait-chain 조회 | `.claude/urgent-126-20260915/wct-result.txt` / `wct_probe.ps1` | `d12d0b0b64691b7f` / `d42a5837545f0ded` |
| 조사 정본 | `.claude/urgent-126-20260915/incident.md` | `908c467ad1dac3a2` (rev12) |

기기: 집 Host = 이 PC(`f6e8e54a7d5aff7c`, AMD Radeon 31.0.21924.61 + Parsec/Virtual Display). 게임 Host 세션(13:45:10~13)은 분리·제외. 호스트의 `localVersion=0.2.125`는 REL-V01 표찰 결함이며 구버전 설치가 아님.

## 사건 시각 (확정)
| 시각 | 사실 |
|---|---|
| 13:45:37 / 13:46:44 | 정상 세션 2건(`active=1`, HW 디코더). 4K HDR 고화질 테스트 구간 |
| **13:47:19.864~31.206** | OS GPU 워치독(`Component WATCHDOG`) 라이브 커널 덤프 요청 7건 (`WerKernel/Operational`). 오늘 그 외 0건 |
| 13:47:20.545 → 31.914 | Stream(pid 31552, 20h02m 가동) DXGI 획득 `0x887A0027` 타임아웃 → 제품 dxgi-worker 워치독 **code=44 자진 종료** → 감독자 WGC 고정 후 재기동 |
| 13:47:47~ | 이후 모든 세션 실패: 연결·첫 Pong·창목록·picker 까지 되고 15초 뒤 `control-lost` |
| 13:50:28 / 14:01:52 | 사용자가 GNLink 두 번 재시작, 둘 다 동일 실패 |
| **15:52:29.052** | 같은 프로세스(27860)에서 14:02:08부터 막혀 있던 Serve(epoch1) 반환 → 같은 ms 대에 영상 정상. 이후 전부 정상 |
| 15:52 전후 | window-list 10→9. 사라진 창 소유 프로세스 = `dnplayer.exe`(LDPlayer) 하나 (전후 열거 추론, 이벤트 아님) |

## 판정
**확정**
- **UDP 제어 경로의 디스패처(`udpControlThread`)는 1개**이며, 첫 세션 이후 `udp control session ended`가 세 프로세스 모두 0건 → 세션을 끝내지 못했다. 사고 전(12:01~13:46)과 2일치 회전본에서는 전부 짝이 맞았다 — 오늘 처음 생긴 상태. ⚠️ "프로세스당 디스패처 1개"는 **틀린 서술**이었다: `controlPort>0`이면 TCP `controlThread`가 별도로 존재하고(host_startup_control.cpp:127/:163 vs :397/:431) 운영 호스트는 둘 다 띄운다(`control waiting port=43001` + `udp bound port=43000`). 사건 세션은 전부 UDP 터널이라 관측된 차단은 UDP 디스패처의 것이며, 그 시간 TCP 경로의 가용 여부는 관측하지 않았다.
- 감독자 재기동 트리거는 자식 종료뿐이라 막힌 디스패처는 감시되지 않는다(DXGI 워커 워치독은 있고 13:47에 동작했다).
- `AwaitControlReady`가 1500ms 상한이라 디스패처가 막혀도 hello ack는 나간다 → 뷰어에는 "연결됨"으로 보인다.
- 코드: `Serve` → `ControlWindowThumbnailRequest` → `send_window_thumbnail` → `capture_window_thumbnail` → `PrintWindow`(동기 SendMessage, deadline 없음, 사전 가드 `IsHungAppWindow`뿐). `host_bgra_scale.cpp`는 0.2.127과 바이트 동일.

**강한 추정** (직접 스택·HWND 요청 로그 없음)
- 차단 지점 = LDPlayer 창 썸네일용 `PrintWindow`. `Close(SessionRollover)` 2회·10초 읽기 타임아웃이 1h50m 동안 통하지 않았고(→ `link.Read` 아님; 풀린 직후 Serve는 10.7초에 정상 타임아웃), 반환 시각이 LDPlayer 소멸과 맞물린다.
- ⚠️ 구현 task ④(검증용 재현 `d10ce4a`): 이 OS에서 `PW_RENDERFULLCONTENT`는 DWM 리다이렉션 표면에서 렌더되어 **창은 WM_PRINT를 받지 않는다**(fixture 수신 0건, 정체 창에서도 ~24ms에 완료). 이것이 말하는 것은 **"이 fixture/OS/flags로는 기존 코드의 무한 대기를 재현하지 못했다"**까지다. "LDPlayer가 WM_PRINT에 응답하지 않았다"는 약화되지만 DWM/GPU 내부 대기가 사건 원인이라고 **확정하지도 못한다** — 두 설명 모두 미확정 후보. 같은 시각 GPU 워치독·TDR(프로세스 문맥 GNLinkStream)은 정황. LDPlayer 소멸 시각은 전후 열거 추론이며 "소멸=회복"은 연관으로만 유지. 제품 수정 방향(별도 프로세스 격리+deadline)은 원인 확정과 무관하게 유효하다(user mode에서 취소 불가한 지점을 회수하려면 프로세스 경계가 필요).
- WCT 조회(15:53:43)는 해소 74초 뒤라 장애 당시 근거로 쓰지 않는다.

**미확정**
- 13:47 GPU 워치독의 원인(4K HDR·LDPlayer·캡처 동시 부하는 후보). **커널 미니덤프 분석(사용자 지시, 승격은 복사 1회)으로 TDR 책임 드라이버는 `amdkmdag.sys`(0x141 P2 = amdkmdag+0xFA6D0)로 모듈 단위 특정**됐다. 스택 메모리 영역의 주소 후보도 amdkmdag·dxgkrnl·dxgmms2·ntoskrnl 만이나 **unwind 한 프레임이 아니므로 호출 순서로 읽지 않는다.** Codex 가 공식 WinDbg(CDB, Authenticode 확인)로 공개 심볼 unwind: `LKD_0x141_IMAGE_amdkmdag.sys`, `VidSchiWorkerThread→VidSchiCheckHwProgress→ResetEngines→TdrCollectDbgInfoStage1`(로그 `codex-windbg-symbol-analysis.log`). **TDR 추가 데이터의 PROCESS_OBJECT(P4=0xffffe68cad8f1080)가 파일 오프셋 0x40BF8 에 있고 바로 뒤 0x40C05 에 `GNLinkStream.e`** — 검증용이 사본 바이트를 독립 대조해 일치 확인. 즉 **타임아웃된 엔진 작업의 프로세스 문맥은 GNLinkStream**(연관). ⚠️ 연관 ≠ 원인: amdkmdag 내부·GPU 과부하(4K HDR·LDPlayer 동시)·GNLink 워크로드 중 근본은 **미확정**이며 GNLink 잘못 확정 금지. TDR 의 "책임 드라이버" 도 드라이버 버그 확정이 아님. 사본·산출물(재현 가능): `.claude/urgent-126-20260915/WATCHDOG-20260915-1347.dmp`(sha256 `912c5fde…8115`), `parse_watchdog_dump.py`, `dump-analysis.txt`, `dump-modules.txt`.
- LDPlayer가 WM_PRINT에 응답하지 않은 이유. 13:47:47 창 7→8의 정체. LDPlayer의 정확한 종료 시각·사용자 조작 여부(답변 대기).
- **GNLink가 GPU를 멈추게 했다는 원인 증거는 없다**(무관 증명도 아님). 단 TDR 기록의 프로세스 문맥은 GNLinkStream(연관, 위 참조).

## 재발 시 안전한 우회 (조건부)
증상이 "연결됨 → 화면 선택 창 → 15초 내 끊김, 재시작해도 반복"이면 GNLink 재시작 대신 **호스트 PC의 응답 없는 창(GPU 렌더링 앱·오류 대화상자·이번엔 LDPlayer)을 닫는 것**을 시도한다.
- ⚠️ **보편적 보장이 아니다.** 15:52:29 의 **자연 회복 1회 관측**에 근거한 조건부 우회다. 같은 기전이 아니면 효과가 없을 수 있다.
- ⚠️ **창을 닫기 전에 그 창의 미저장·실행 중 작업(에뮬레이터 세션, 편집 중 문서 등)에 미치는 영향을 먼저 고려한다.** 우회를 위해 사용자 작업을 잃지 않는다.
- 어느 창인지는 로그가 알려주지 않는다. 재부팅·0.2.127 업데이트는 이 결함을 고치지 않는다.

## 수정 방향 (제품 수정은 이 사건 범위 밖 — 별도 task)
1. 썸네일 캡처를 **deadline 있는 격리 작업**으로, 초과 시 그 창은 건너뛰고 세션 계속.
2. ⚠️ 별도 스레드를 띄우고 타임아웃에 버리면 막힌 PrintWindow가 살아남아 **누수·UAF·스레드 무제한**이 된다 → **bounded worker(상한·재사용) 또는 별도 프로세스 소유권**까지 설계.
3. **제어 디스패처 liveness** — 마지막 진행 시각을 감독자가 감시, 상한 초과 시 세션 회수/재기동(DXGI 워커 워치독과 같은 급).
4. `IsHungAppWindow`는 유지하되 충분조건으로 보지 않는다.

## 조사 중 철회한 판정 (요약; 상세는 정본 §0-R·§4-A·§4-F·§4-G)
"프로세스당 연결 1회/재시작 1회 보장" · "`stream restored` 부재=Serve 미진입" · "커널 사고 14건 동시" · "host→viewer 경로 단절" · "DXGI 복귀=GPU 회복" · "로그 없이 사망" · "재시작 안 함" · "WCT 결과가 PrintWindow 반증".
