import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit

ROOT = r'D:\remote\remote'
REPORT = ROOT + r'\docs\field_test_2026-09-07_0.2.100.md'
HIST = ROOT + r'\docs\history.md'
PLAN = ROOT + r'\docs\구현계획.md'

edit(REPORT, [
# 0. evidence scope: upload failure wording + timeline
("""- **NAS 로그에 이 구간이 없다.** `/opt/gnlink/remote60-directory/logs/shotan/` 의 host.log(8ec6ecb1) 는 13:47:14, viewer.log/client.log(68f79d01) 는 13:47:27/13:48:38 이 마지막 줄이다. 14:35 재확인, 15분 뒤 재확인에서도 동일. 원인은 5절의 업로드 401 이며, 실패 배치는 폐기되므로(`log_upload.cpp`: "Failures are counted, not retried") NAS 로는 복구되지 않는다.""",
 """- **NAS 로그에 이 구간이 없다.** `/opt/gnlink/remote60-directory/logs/shotan/` 의 host.log(8ec6ecb1) 는 13:47:14, viewer.log/client.log(68f79d01) 는 13:47:27/13:48:38 이 마지막 줄이다. 14:35 재확인, 15분 뒤 재확인에서도 동일. 호스트 쪽은 5절의 업로드 401(13:48:39~)이 직접 확인됐고, 업로더는 실패 배치를 자동 재송신하지 않아(`log_upload.cpp`: "Failures are counted, not retried") NAS 사본은 생기지 않는다. 회사 쪽(viewer/client)은 13:56 서버 재시작 **이전**인 13:47/13:48 에 이미 멈췄으며 viewer 측 401 직접 로그는 미확보다. 로컬 원본(이 PC `host_app.log`, 회사 노트북 `%LOCALAPPDATA%\\GNLink\\viewer.log`)은 수동 회수가 가능하다."""),
# 1. relay fact vs P2P cause
("""- **P2P 직접 경로는 한 번도 성립하지 않았다.** 매 접속에서 relay 가 2.5s 유예 후 응답해 bound 됐다. 후보 원인: 새 공유기의 UDP 43000/3478 → 192.168.0.76 포트포워딩 미설정 또는 NAT 유형. 이 문서로는 미확정(라우터 설정 확인 필요).""",
 """- **P2P 직접 경로는 한 번도 성립하지 않았다**(사실: 매 접속에서 relay 가 2.5s 유예 후 응답해 bound). **원인은 별개이며 미확정**이다. 후속 분석 계획: `/api/connect` 가 뷰어에 준 후보 목록과 관측 포트, 호스트 punch 송신(`directory punch -> 211.218.222.1:…`)에 대한 회사 측 수신 여부(뷰어 로그 필요), relay 2.5s 유예 안에 직접 응답이 있었는지, 양측 NAT 유형. 포트포워딩 설정 변경은 이 분석 뒤에 판단한다."""),
# 2. event table rows (sample vs population)
("""| 14:15~14:24 정적 1분 + 타이핑, 14:23 GMux 채팅/백스페이스 | 문제없음, 반응 ~100ms 허용 | wire 1,388 프레임(key 55), 송신 간격 med 492ms(정적 refresh cadence), 큐 대기 med 16µs, 송신 med 5.3ms, encUs med 11.5ms, readback captureToQueue med 0/p95 2.4ms, `d3dMt=off`. 호스트가 받은 keyframe-request **0건**(reason 1~6 모두 0). 입력 inject 실패 0 | present gap, 입력→표시 지연, 제어 RTT | 호스트 측 정상. 체감 통과 |""",
 """| 14:15~14:24 정적 1분 + 타이핑, 14:23 GMux 채팅/백스페이스 | 문제없음, 반응 ~100ms 허용 | 전수(`udpTxFrames` 누적차): 14:15:01~14:24:31 2,548 프레임/570s ≈ 4.5fps(정적 refresh·kick 포함), `udpTxFail=0`. wire 로그 **표본**(키프레임 또는 송신 간격 >1.5×목표인 프레임만 기록, `host_encoded_sender.cpp:218`) n=1,388(key 55): 큐 대기 med 16µs, 송신 med 5.3ms. user-feedback **표본**(경고 임계 이상만, n=56): encUs med 11.5ms, readback captureToQueue med 0/p95 2.4ms, `d3dMt=off`. 호스트가 받은 keyframe-request **0건**(reason 1~6 모두 0). 입력 inject 실패 0 | present gap, 입력→표시 지연, 제어 RTT | 호스트 측 정상. 체감 통과 |"""),
("""| 14:25~14:26:10 고화질 영상 + 영상↔정적↔타이핑 전환 3회 | 멈춤 없음 | 송신 중 간격 med 29.1ms(≈34fps) p95 41.2ms, 프레임 bytes med 45.9KB p95 233KB, key 78/989, 큐 대기 p95 30.4ms(max 50.7ms), 송신 p95 23.6ms; 호스트 sender 큐 트림(senderQueueDrops) 85→146(60초간 61 프레임 hold/drop, 12Mbps 목표 대비 pacing); keyframe-request 0건, 혼잡 신호 호스트 도달 0 | decoded fps, present gap, congestionState, catchup/stale 복구 | 호스트 측 정상. 체감 통과. "멈춤 없음" 을 뷰어 로그로는 입증 못 함 |""",
 """| 14:25~14:26:10 고화질 영상 + 영상↔정적↔타이핑 전환 3회 | 멈춤 없음 | 전수(`udpTxFrames`/`udpTxBytes` 누적차): 14:25:01~14:25:31 1,746 프레임(58.2fps)·136MB(≈36Mbps), 14:25:31~14:26:01 1,758(58.6fps)·144MB(≈38Mbps), 14:26:01~14:26:31 704(영상 종료 14:26:10 포함) — runtime-config 목표 12Mbps 의 약 3배(P7 의 VBR 피크 양상), `udpTxFail=0`. wire **표본** n=989(key 78, 지연 이벤트 위주라 전체 분포 아님): 큐 대기 max 50.7ms, 송신 max 44.9ms. `senderHeldFrames` 85→146(60초간 61 프레임 hold, 12Mbps 목표 대비 pacing); 호스트 도달 keyframe-request 0건, 혼잡 신호 0 | decoded fps, present gap, congestionState, catchup/stale 복구 | 호스트 측 정상. 체감 통과. "멈춤 없음" 을 뷰어 로그로는 입증 못 함 |"""),
# 3. latency section rewrite
("""호스트 내부(프레임당, `wire seq` / `user-feedback`):

| 구간 | 정적+타이핑(A) | 영상(B) | UAC 뒤(C2) |
|---|---|---|---|
| readback captureToQueueUs med / p95 | 0 / 2,406 | 2,843(n=1) | 8,472 / 10,300 |
| encUs med / p95 | 11,491 / 12,907 | 13,813(n=1) | 11,716 / 12,742 |
| sender queueWaitUs med / p95 | 16 / 25 | 20 / 30,357 | 17 / 26 |
| sendDurUs med / p95 | 5,287 / 8,852 | 7,019 / 23,585 | 6,358 / 21,365 |

- 호스트 capture→wire 는 정적·타이핑에서 ≈15~20ms, 영상 p95 ≈50~65ms(큐 대기+송신). UAC 뒤 readback 이 +6~8ms 더해진다.
- `user-feedback` 의 `pipeUs` 180~190ms(`bottleneckStageName=encode_au_enqueue_jitter` 486ms 등)는 **희소 정적 스트림에서 비동기 인코더가 다음 AU 를 내놓기까지의 간격**이지 입력→표시 지연이 아니다. 사용자 지연으로 읽지 않는다. 합성/보정 stamp 의 0 값도 0ms 로 해석하지 않았다.""",
 """호스트 내부 — **모두 표본**이며 서로 다른 프레임의 표본을 더해 구간 합을 만들지 않는다. wire 표본 = 키프레임 또는 송신 간격 >1.5×목표인 프레임(`host_encoded_sender.cpp:218`); user-feedback 표본 = `pipeUs`(= `hdr.sendQpcUs − hdr.captureQpcUs`, 캡처→와이어, 인코더 보유 시간 포함; `host_stage_encode_send_h264_au.cpp:563`) 가 경고 임계 이상인 AU 만, 1초 rate-limit. 비동기 인코더에서는 user-feedback 의 입력 단계 필드(captureUnmapWait 등)와 AU 헤더의 capture 가 서로 다른 프레임일 수 있어 단계별 합산에 쓰지 않는다.

| 표본 | 정적+타이핑(A) | 영상(B) | UAC 뒤(C2) |
|---|---|---|---|
| wire: sender queueWaitUs med / max | 16 / 41,805 | 20 / 50,729 | 17 / 26,972 |
| wire: sendDurUs med / max | 5,287 / 41,742 | 7,019 / 44,885 | 6,358 / 26,926 |
| user-feedback: pipeUs(capture→wire) med / max | 184,769 / 369,968 | 120,799 (n=1) | 174,688 / 392,405 |
| user-feedback: encUs med | 11,491 | 13,813 (n=1) | 11,716 |
| user-feedback: captureToQueueUs med | 0 | 2,843 (n=1) | 8,472 |

- **호스트 capture→wire 의 전체 분포는 미측정**이다(전수 per-frame 계측 없음). 위 표본으로 "≈15~20ms" 같은 구간 합 추정은 하지 않는다.
- user-feedback 표본의 `pipeUs` 180~190ms(정적·타이핑 구간 실제 프레임)는 캡처→와이어 전체이며 인코더 보유 시간을 포함한다. 희소 입력에서 비동기 인코더가 다음 입력(150ms trailing kick)까지 프레임을 쥐고 있는 설계(`host_kick.hpp`)와 크기가 부합하므로, 타이핑 마지막 프레임의 호스트 몫 지연 후보로 본다 — 단 경고 임계 이상 표본만 기록되므로 대표값이 아니며 확정하지 않는다. UAC 뒤에는 readback 이 표본상 +6~8ms 더해진다.
- 합성/보정 stamp 지표(kick 시각 등)는 실제 입력→표시와 별개이며 0 값을 0ms 로 해석하지 않았다."""),
# 4. fix proposal wording
("""수정 제안(별도 task, 제품 변경): (1) 캡처 (재)시작·기하 확정·`WM_DISPLAYCHANGE` 시 `SetTargetRect` 재호출 + `secure-input target rect=` 로그 추가, (2) 전체 데스크톱 캡처일 때는 rect 0 을 넘겨 에이전트가 live 가상 화면(virt)을 쓰게 하는 것도 대안. 검증: secure_input.log 에서 `target==virt`, `mapped==in`.""",
 """수정 제안(별도 task, 제품 변경): 캡처 (재)시작·기하 확정·`WM_DISPLAYCHANGE` 시 **캡처 대상의 현재 물리 rect+원점**(선택 모니터·창 모드 포함)으로 `SetTargetRect` 를 재호출하고 `secure-input target rect=` 를 로그한다. rect 0(에이전트의 가상 화면 fallback)은 단일 모니터 전체 데스크톱에서만 성립하는 조건부 대안이며 일반 해법으로 권장하지 않는다. 검증: 단일 모니터 전체 데스크톱은 `target==virt`·`mapped==in`; 다중/선택 모니터·창 모드는 해당 대상 rect 기준으로 `mapped` 가 기대 좌표와 일치하는지 확인."""),
# 5. upload 401 timeline
("""- 서버 `handleLogs`: Bearer 세션이 없으면 `x-host-token` 을 `hostTokens`(sha256→hostId map) 에서 찾고 없으면 401. `hostTokens` 는 재등록(`/api/register`) 시 이전 해시를 삭제하고 새 해시를 넣는다(server.js 657/676). 호스트 앱 업로더는 시작 시 `g.cache.hostToken` 으로 1회 구성(`host_app_main.cpp:797-800`)되고, 재등록(sign-in, 1335 에서 새 토큰 저장) 뒤 업로더에 토큰을 갱신하는 API 가 없다 → 재등록 이후 영구 401. 13:56 서버 재시작은 `hostTokens` 를 store 에서 재구성하므로 x-host-token 경로엔 영향 없지만, 클라이언트 셸·뷰어의 Bearer 세션(`sessions = new Map()`, in-memory)은 소멸해 회사 쪽 업로드도 13:48 이후 끊긴 것으로 보인다(직접 로그는 없음).""",
 """- 서버 `handleLogs`: Bearer 세션이 없으면 `x-host-token` 을 `hostTokens`(sha256→hostId map) 에서 찾고 없으면 401. `hostTokens` 는 재등록(`/api/register`) 시 이전 해시를 삭제하고 새 해시를 넣는다(server.js 657/676). 호스트 앱 업로더는 시작 시 `g.cache.hostToken` 으로 1회 구성(`host_app_main.cpp:797-800`)되고, 재등록(sign-in, 1335 에서 새 토큰 저장) 뒤 업로더에 토큰을 갱신하는 API 가 없다 → 재등록 이후 영구 401(재등록 시각 자체는 로그로 미확인).
- 회사 쪽 타임라인: viewer 13:47:27, client 13:48:38 이 NAS 마지막 줄 — 13:56 서버 재시작 **이전**이므로 재시작만으로는 최초 누락을 설명하지 못한다. Bearer 세션(`sessions = new Map()`, in-memory)의 소멸은 13:56 이후 업로드가 계속 실패했을 후보일 뿐이며, viewer 측 401 직접 로그는 미확보. host 401 시작(13:48:39)과 client 업로드 중단(13:48:36 로그인 직후)이 3초 차로 겹치지만 공통 원인은 미확인."""),
("""- 다음 조치: ① 업로드 401 수정(5절) 후 실기 재실행 — 그 전엔 회사 뷰어 로컬 `%LOCALAPPDATA%\\GNLink\\viewer.log` 를 수동 회수해 뷰어 측 판정 보완, ② UAC target rect 갱신(4절) 제품 수정, ③ 호스트 30s 통계에 NACK 카운터 출력 추가, ④ 라우터 UDP 43000/3478 포워딩 확인 후 P2P 재시도, ⑤ acquire timeout A/B(6절).""",
 """- 다음 조치: ① 업로드 401 수정(5절) 후 실기 재실행 — 그 전엔 회사 뷰어 로컬 `%LOCALAPPDATA%\\GNLink\\viewer.log` 를 수동 회수해 뷰어 측 판정 보완, ② UAC target rect 갱신(4절) 제품 수정, ③ 호스트 30s 통계에 NACK 카운터 출력 + 전수 per-frame capture→wire 계측 추가, ④ P2P 미성립 원인 분석(후보 목록·관측 포트·punch 송수신·유예·NAT) 뒤 조치, ⑤ acquire timeout A/B(6절)."""),
])

edit(HIST, [
("""- 근거 한계: NAS 의 host/viewer/client 로그가 13:47~13:48 에서 끊겨 이 구간이 **없음**(호스트 업로드 401 — 실패 배치 폐기라 복구 불가; 뷰어 측은 13:56 서버 재시작으로 Bearer 세션 소멸 추정). 1차 근거는 로컬 `host_app.log`(GNLinkStream stdout 사본)·`secure_input.log`·NAS 디렉터리 저널. 뷰어 로그 미확보 → nackNegotiated/keyResync/keyRetries/present gap/제어 RTT 판정 불가.""",
 """- 근거 한계: NAS 의 host/viewer/client 로그가 13:47~13:48 에서 끊겨 이 구간이 **없음**(호스트: 13:48:39 부터 업로드 401, 업로더 자동 재송신 없음·NAS 사본 없음 — 로컬 원본은 수동 회수 가능; 회사 쪽 viewer/client 는 13:56 서버 재시작 **이전**에 이미 멈춰 재시작만으로는 설명 안 됨, Bearer 세션 소멸은 이후 지속 실패 후보, viewer 측 401 직접 로그 미확보). 1차 근거는 로컬 `host_app.log`(GNLinkStream stdout 사본)·`secure_input.log`·NAS 디렉터리 저널. 뷰어 로그 미확보 → nackNegotiated/keyResync/keyRetries/present gap/제어 RTT 판정 불가."""),
("""- 확인: (1) 본 세션 14:05:26~14:30:44 와 재접속 4회 모두 **릴레이 경유**(저널 `bound/active`, 호스트 `endpoint translated actual=175.207.45.151:29181`); P2P 는 매번 2.5s 유예 후 relay 로 넘어가 미성립(새 공유기 UDP 43000 포워딩 미확인). (2) 호스트 송신 경로: 정적 큐 대기 med 16µs·송신 med 5.3ms·encUs 11.5ms; 영상 송신 간격 med 29ms p95 41ms, 큐 대기 p95 30ms; 25분간 호스트 도달 keyframe-request 는 재접속 시 reason=7 ×5 뿐(reason=2 0).""",
 """- 확인: (1) 본 세션 14:05:26~14:30:44 와 재접속 4회 모두 **릴레이 경유**(저널 `bound/active`, 호스트 `endpoint translated actual=175.207.45.151:29181`); P2P 미성립은 사실이나 원인은 별개·미확정(후보 목록·관측 포트·punch 송수신·NAT 분석 뒤 판단). (2) 호스트 송신 경로(전수 `udpTxFrames`/`udpTxBytes` 누적차): 정적·타이핑 ≈4.5fps, 영상 58fps·36~38Mbps(목표 12Mbps 의 약 3배, P7 양상), `udpTxFail=0`; wire/user-feedback 로그는 키·지연 이벤트 표본이라 전체 분포 산출에 쓰지 않음(**정정**: 최초 보고의 "med 29ms≈34fps"·p95·"capture→wire ≈15~20ms" 철회, 전체 capture→wire 분포는 미측정; 표본상 정적 구간 실제 프레임 pipeUs 180~190ms 는 비동기 인코더 보유+150ms trailing kick 설계와 크기 부합, 확정 아님); 25분간 호스트 도달 keyframe-request 는 재접속 시 reason=7 ×5 뿐(reason=2 0)."""),
("""- 다음 액션: 업로드 401 수정·재실기(그 전 회사 뷰어 로컬 viewer.log 수동 회수), UAC target rect 재갱신 제품 수정, 호스트 30s 통계에 NACK 카운터 추가, 라우터 UDP 포워딩 확인, acquire timeout A/B.""",
 """- 다음 액션: 업로드 401 수정·재실기(그 전 회사 뷰어 로컬 viewer.log 수동 회수), UAC target rect 재갱신(캡처 대상의 현재 물리 rect+원점; rect=0 가상화면 fallback 은 단일 모니터 전체 데스크톱 한정) 제품 수정, 호스트 30s 통계에 NACK 카운터·전수 per-frame capture→wire 계측 추가, P2P 미성립 원인 분석 뒤 조치, acquire timeout A/B. (문구 정정: Codex 감독 검토 5건 반영)"""),
])

edit(PLAN, [
("""  - 완료조건: [ ] 캡처 (재)시작·기하 확정·WM_DISPLAYCHANGE 시 target rect 재갱신 + 로그 [ ] secure_input.log 에서 `target==virt`, `mapped==in` 실기 확인""",
 """  - 완료조건: [ ] 캡처 (재)시작·기하 확정·WM_DISPLAYCHANGE 시 캡처 대상의 현재 물리 rect+원점(선택 모니터·창 모드 포함)으로 target rect 재갱신 + 로그 [ ] 실기: 단일 모니터 전체 데스크톱은 `target==virt`·`mapped==in`, 다중/선택 모니터는 해당 대상 rect 기준 `mapped` 일치 (rect=0 fallback 은 단일 모니터 한정 조건부)"""),
])
