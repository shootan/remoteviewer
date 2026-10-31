# keepalive-r2 e — AVD ke-r23 결과 (2026-10-29 18:40–18:44, r6 snapshot, 계획 plan_avd_ke_r22.md)

## 판정: **FAIL (E2) 뒤 중단** — bundle rc 3 (fixture 1, avd 1)
| 시나리오 | 결과 |
|---|---|
| E1 signed in; four PCs listed | **PASS** (18:41:52) |
| E1 OFF: the app prepares nothing on its own | **PASS** (`[preconnect_off] reason=login why=auto_prepare_off recent=0`) |
| E2 A (ordinary connect): selection → first frame within 25 s | **FAIL** (18:42:41) |
| 그 뒤 | `to_list('E2')` 의 `menu('viewerMenuHostsButton')` 가 3회 실패한 뒤 예외로 중단(`RuntimeError: menu button not reached`). **E3 이후는 실행되지 않았다.** NOT RUN(E7f/E8/E12)과 구분되는 "중단"이다. |

- `avd/steps.txt` 마지막 줄: `run end: 2/3 PASS, 0 NOT RUN`
- 보존 자료(run 폴더 `feat-apk-0233\.claude\e2e\runs\ke-r23-20261029184026-36368`):
  - `avd_stdout.txt`, **`avd_stderr.txt`(traceback 전문)**, `avd/avd_traceback.txt`, **`avd/exception.png`**
  - `avd/steps.txt`, `avd/_cur.log`(앱 로그), `avd/logcat.txt`
  - a/b/c 의 `directoryhost_host.log`, `harness.stdout.txt`
- 예외 시점 화면(`exception.png`): "My PCs" 목록. 상단에 "Connecting to ke-pc-a…", a·b·c 세 카드 모두 ONLINE. 즉 viewer 는 이미 닫혀 있었고, 그래서 viewer 메뉴 버튼을 찾지 못했다.
- `pm clear` 기록: `rc=0 stdout='Success' stderr=''`. 이것은 그 호출의 응답일 뿐이다. 앱 데이터 초기화나 ke-r22 의 원인을 이것으로 확정하지 않는다. 이번 실행에서는 저장 로그인 거부 기록 없이 로그인 화면에서 새 로그인이 정상으로 진행됐다.

## E2 에서 일어난 일 (양쪽 로그, 확인된 사실)
- 경로: 앱 `[directory_chosen] 10.0.2.2:18443|relay`, 즉 **relay 경로**다.
- 연결과 선택:
  - 09:42:04Z `directory_connect_committed`, `targets_ready`
  - 09:42:17Z `stream_state_request active=true`
  - 09:42:18.9Z `select_request`, 09:42:20.3Z **`select_ack`**(streamGen=2)
  - 화면 크기 480x270 과 surface bound 까지 정상이다.
- Host a(18:42:18.8 KST = 09:42:18.8Z):
  - `window-select applied=1 streamGen=2`, `selection first keyframe sent`
  - 이후 viewer 의 `keyframe-request reason=2` 를 **약 1초마다 받았고**(seq 1–5 이상), 매번 keyframe 을 보냈다(seq 49·51·73·99·118 …).
  - 18:42:31 까지 wire 송신이 계속됐다.
- 앱 `video_debug`: `in=0 out=0 stale=8 inGen=2 expectGen=2 readySel=0` 이 18:42:20–39 동안 바뀌지 않았다. **decoder 에 들어간 프레임이 0** 이다. keyframe 은 요청했지만 decoder 입력까지 이어지지 않았다.
- 세션 종료:
  - Host 18:42:31.227 `udp control session ended epoch=1 reason=peer-lost`
  - 앱 09:42:40.7Z `[keep_end] handle=1 host=ke-pc-a reason=peer_lost` → 목록 화면(`scene HOSTS`)
- **원인 미확정.** relay 경로에서 선택 뒤 영상이 decoder 에 도달하지 못했고, 양쪽이 peer-lost 로 끝났다. ke_r21 E7e(relay, peer_lost)와 비슷한 모양이다. 그러나 relay 의 패킷 계수가 없어 "relay 전달 / 클라이언트 재조립 / 폰 송신" 중 어디인지 가릴 수 없다. 이번 실행만으로 제품 결함이나 환경 문제로 단정하지 않는다.

## Host launch 별 OFF 최종 판정 (사실 그대로)
| Host | 시작 전 gate | OFF 최종 판정 | harness |
|---|---|---|---|
| a | PASS | `PASS input / power state: no contradiction during the run (1 launch(es): 1 judged on their complete output -- end, EOF and OFF lines confirmed, 514287 bytes --, 0 still running judged now)` | **RESULT: PASS** (exit 0) |
| b | PASS | 같음, 6307 bytes | exit 1 — `host captured the window … never said` (선택되지 않음) |
| c | PASS | 같음, 6305 bytes | exit 1 — 같음 |

- fixture: `job_run: child_exit=1 residual=0 forced=0 ended_confirmed=1`. 남은 GNLinkStream 은 사용자 제품(27044)뿐이다.

## 고정 (실행 직전 재확인, 모두 일치)
- HEAD `9f9a91c` clean
- job `DC4C5049…`, pinned Host `D806E375…`, 하네스 `722BF981…`
- APK off `8C6422FE…` / on `64A02118…`
- r6 snapshot 10파일 불일치 0
- 에뮬레이터 `device` / qemu 1 / boot 1

## 슬롯
- `sh_run.ps1` 이 CreateNew 로 잡았다. 실행 후 자기 파일만 제거했다(`slot removed=True`). **해제했다.**

## 정리
- ke-r22: INVALID 유지(기능 0, 유효 관측은 OFF 3 과 job residual 0).
- ke-r23: E1 2 PASS, **E2 FAIL**(relay, 선택 뒤 decoder 입력 0 → peer_lost, 원인 미확정), 이후 중단.
- E7h·E13/E13b 실 행동은 여전히 관측하지 못했다. 미결: 실 clipboard (c), old/E7f·E8·E12 NOT RUN, peer_lost(ke_r21·ke-r23) 원인.
