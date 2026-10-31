p = 'C:\\work\\gnlink-wt\\fix-capture-lifecycle\\docs\\history.md'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
anchor = '- 제품/테스트/문서: 제품(host 캡처 수명·backend 세션·상태 응답, capture lib DXGI 전체 프레임, shared client 상태 API) / 테스트(PS·UA e2e, 시험 빌드 전용 seam, kick 단위) / 문서(이 항목·구현계획).\n'
assert s.count(anchor) == 1
entry = anchor + '''### 2026-10-26 capture-lifecycle r2 — 경계 이전 영상의 출처(provenance)·backend 소유 epoch·조회/변경 분리·재획득 budget 의 source 경계 (fix/capture-lifecycle, 미게시 · R1 0.2.163 통합 대기)
- 근거: `codex_capture_lifecycle_src_r1.md` SOURCE NEEDS_CHANGES N1~N4(569059f). PS-B 는 SOURCE OK 그대로.
- N1: 프레임 출처 = 그 프레임을 만든 attachment(cookie, 프레임에 실림) + 그 attachment 가 만들어질 때의 desktop generation(`attachmentDesktopGen`, DetachCaptureSession 에서 찍음). PublishFrame 은 캐시(bootstrapCacheMu 안)·raw queue(res.frame.mu 안) **각 채택 시점에** 둘 다 현재일 때만 받는다 — callback 시각 규칙(`callbackUs >= desktopChangeDetectedUs`)은 폐기. 캐시에 attachment cookie 도 남겨 KickTryFill 이 현재 attachment 의 것만 재전송. 확인된 edge 에서는 **재구성 전에** media fence(inputEpoch 이동 → sender 가 그 전 AU 를 버림, raw frame 비움, key 요청)를 닫는다. 재구성은 backend 무관(WGC·DXGI·GDI) "실행 중 attachment 의 gen 이 현재가 아니면" — desktop generation 별 3회·backoff, 실패하면 닫힌 채(옛 attachment 의 프레임은 계속 거부). stream 없음 → 지금은 안 함(재개 때), idle detach → 재부착이 현재 gen 으로 만든다. Unknown 한 번·처음 본 다른 이름 한 번 = edge 아님(재시작·잠금 판정 없음)이지만 캐시는 버리고 확인된 이름을 다시 볼 때까지 캐시 금지(suspect). 첫 look 은 이름만 정하고 gen 을 올리지 않는다.
- N2: backend 소유자 판정을 한 곳(`host_backend_owner.hpp`, 순수)으로. tick 당 epoch 1회 읽기 → owner 확정 → 요청은 그 owner 기준으로 take(`TakeBackendRequestForOwner`: 옛 epoch 버림, **새 epoch 은 다음 tick 까지 남김**), 처리 표식은 요청 자신의 epoch. Serve 의 실제 종료(현재 epoch·resume 없음 — stream 을 끄는 같은 규칙)가 `backendSessionEndedEpoch` 를 남기면 그 owner 의 선택을 기본으로 1회 복귀, 이전 owner 의 늦은 종료는 무시. stream 비활성·idle detach 중 backend 요청은 저장만(캡처 재시작 0).
- N3: shared client `DesktopBackendControl` 의 변경 pending 과 조회 pending 분리 — 조회가 대기 중 변경을 덮지 않는다(변경과 조회가 같이 있으면 변경 하나가 나가고 그 답이 상태). 공개 API 는 그대로. host 는 세션이 올린 마지막 변경 key(`backendChangePostedKey`)와 상태를 게시한 owner(`backendStatusEpoch`)를 두고, 조회의 답은 그 세션 상태가 게시되고 변경이 처리된 뒤에만 applied(250 ms, 아니면 NotYetApplied) — 새 세션의 첫 조회가 이전 owner 의 선택을 '이번 연결' 답으로 받지 않는다.
- N4: full-frame 재획득 episode 는 한 source = (desktop generation, stream generation, control session epoch, 외부 (재)부착 경계 카운터) 에 묶인다. 다른 source → 새 유한 budget, 같은 source 의 자기 재획득(DXGI duplication 재생성·full-frame WGC 재시작)은 기존 budget 유지, episode 는 현재 출처로 채택된 프레임만 닫는다(거부된 프레임은 못 닫음). host ask(episode 당 3)과 library 재생성(ask 당 3)은 다른 상한 — 로그를 나눠 적는다.
- 추가 결함(시험에서 발견): r1 의 DXGI full-frame 응답이 **포인터 전용 acquire**(LastPresentTime 0)까지 '전체 화면'으로 넘겨, 새 duplication 의 첫 acquire 가 그것일 때 **검은 화면**(luma spread 0, `content=0`)이 캐시·전송됐다. 이제 이미지가 있는 acquire 만 요청을 닫는다.
- 시험 대역: library 의 정지 화면 흉내를 host handler 에서 capture lib 설정(`DxgiDesktopCaptureConfig::testStill`, 시험 빌드만)으로 옮겨 **실제 duplication 재생성 분기**가 돈다(`=nofirst`: 첫 프레임도 없음 → 유한 포기). `GNLINK_STREAM_TEST_DESKTOP_REBUILD_FAIL`(시험 빌드만): secure 쪽 재구성을 모두 실패시킨다.
- 시험: 아래 report r2 절. PS `--still-screen` 32/32(재생성 'duplication made anew' → 'delivered after 1 … content=1', luma 203~207), `--still-nofirst` 26/26(표시 0·재생성 9 = ask 3 × 3·포기 3·ask 3/3 뒤 정지·**새 source 의 새 budget 3**), UA 54/54(경계마다 새 attachment 의 첫 채택 프레임이 새 epoch 의 key 로 나가 디코드됨, synthetic 제외), UA `--rebuild-fails` 18/18(edge fence 가 재구성보다 먼저, 3회 실패 뒤 포기, provenance 거부, fence+500 ms 부터 그림 0, 복귀 edge 에서 그림 복귀), 단위 `host_backend_owner_test`(신규)·shared_core(조회/변경)·kick·policy·attachment·quarantine.
- 한계: PC 뷰어의 FrameBuffer `key` 는 디코더 **입력** 패킷의 표시, `captureUs` 는 **출력** 그림의 것(MF 가 한 호출 늦게 내보낼 수 있음) — 한 기록으로 묶어 'key 의 캡처 시각' 이라 부르지 않는다(시험은 host 의 첫 key 줄 + 디코드된 그림 시각으로 따로 확인). PS present thread 는 창 없이 FrameBuffer 를 읽는다(= 디코드/FrameBuffer 도착, 실제 화면 표시 아님). 실 UAC·실 정지 화면 하드웨어·실 SYSTEM 재부팅은 사용자 실기.
- 제품/테스트/문서: 제품(host 출처 채택·edge fence·재구성 규칙·backend owner·상태 답 판정·재획득 episode, capture lib 이미지 있는 acquire 만 응답, shared client 조회/변경 분리) / 테스트(PS nofirst·UA rebuild-fails 모드, owner 단위, shared_core 단위, 시험 빌드 전용 seam 2) / 문서(이 항목).
'''
s = s.replace(anchor, entry)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
