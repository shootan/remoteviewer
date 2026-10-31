p = 'C:\\work\\gnlink-wt\\fix-capture-lifecycle\\docs\\history.md'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
anchor = '- 제품/테스트/문서: 제품(host 출처 채택·edge fence·재구성 규칙·backend owner·상태 답 판정·재획득 episode, capture lib 이미지 있는 acquire 만 응답, shared client 조회/변경 분리) / 테스트(PS nofirst·UA rebuild-fails 모드, owner 단위, shared_core 단위, 시험 빌드 전용 seam 2) / 문서(이 항목).\n'
assert s.count(anchor) == 1
entry = anchor + '''### 2026-10-26 capture-lifecycle r3 — 재시작 실패의 소유·GDI/연속성 출처·끝난 Serve 의 요청·상태 snapshot (fix/capture-lifecycle, 미게시 · R1 0.2.163 통합 대기)
- 근거: `codex_capture_lifecycle_src_r2.md`(6fa709a: N4·client Query·N2 대부분·N1 fence 닫힘, 잔여 N1-a/N1-b/N2-end/N3-host).
- N1-a: 실제 재시작은 Detach(gen 을 현재로 찍음) 뒤 실패할 수 있다 — 그러면 "stale" 이 사라져 desktop 재구성이 끝난 것처럼 보였고 일반 retry(8 s backoff, 무제한)가 이어받았다. 이제 재시작 호출자가 owner(General / DesktopRebuild / FullFrame)를 넘기고 실패에 남는다(`CaptureState::restartOwner`). 재구성은 "stale 이거나 이 gen 의 재구성이 실패한 채 아무것도 붙지 않음" 동안 열려 있고, (gen, 세션)당 3회·backoff(재시작 wrapper 의 backoff 도 존중). 일반 retry 는 DesktopRebuild 실패를 넘겨받지 않고, FullFrame 실패의 retry 는 그 episode 의 것(새 source 경계 아님). 재시작 실패 중엔 full-frame ask 도 없다.
- N1-b: GDI 도 시작 때의 attachment cookie 를 프레임에 싣고, PublishFrame 은 cookie 0 을 거부한다. 캐시 연속성 토큰(`cacheContinuity`)을 모든 캐시 무효화가 올리고 프레임은 찍힐 때의 값을 싣는다 — Unknown/후보 이름 전에 제출되어 늦게 끝난 readback 은 인코더로는 가되(edge 아님) 캐시엔 안 들어간다.
- N2: 각 Serve 가 자기 sequence 를 갖고 실제 종료는 (epoch, 그 sequence)로 남는다. 끝난 Serve 의 요청(남아 있던 것·늦게 온 것)은 버리고, 같은 epoch 의 **이후** Serve(종료 뒤 resume)는 다시 live.
- N3-host: owner epoch·packed 상태·변경 수·처리 key 를 한 lock 의 snapshot 으로 게시/읽기(`host_backend_status_board.hpp`). 다른 owner 의 snapshot 은 이 세션의 답이 아니다(0 + NotYetApplied).
- M9 대조(결정적): 시험 빌드 `GNLINK_STREAM_TEST_STILL_SCREEN=pointerfirst` — 열린 요청마다 한 번, 새 duplication 의 첫 acquire 를 포인터 전용으로 넘긴다. 그것이 요청을 닫지 않고 그 뒤 이미지 acquire 가 닫는지 본다(첫 시도: 재생성마다 넘겨 정지 화면에서 영원히 미전달 — 요청당 1회로 고침).
- 시험: report r3 절. UA `--rebuild-fails` 를 실제 Detach 뒤 실패(`GNLINK_STREAM_TEST_FAIL_RESTART_FILE`)로 다시 짬 — (1) 1회 실패 뒤 같은 gen 2회차 성공·그림 복귀 (2) 3회 실패·포기, 10 s 동안 4회차·일반 retry 0, 그림 0, 다음 edge 에서 복귀. r2 의 `GNLINK_STREAM_TEST_DESKTOP_REBUILD_FAIL`(함수 호출 전 실패 — 이 경로를 못 탐)은 제거.
- 제품/테스트/문서: 제품(재시작 owner·재구성 열림 규칙·GDI cookie·연속성 토큰·Serve sequence·상태 board, lib pointerfirst seam) / 테스트(UA rebuild-fails 재작성, PS pointer-first, owner 단위 확장, status board 단위 신규, attachment [D] 계약 반전·[E][F] 신규) / 문서(이 항목).
'''
s = s.replace(anchor, entry)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
