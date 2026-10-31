import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
ROOT = r'D:\remote\remote'
SWEEP = open(ROOT + r'\.claude\edits\v14b_sweep_placeholder.txt', encoding='utf-8').read().strip()

def rw(path, fn):
    raw = open(path, 'rb').read(); t = raw.decode('utf-8')
    nl = '\r\n' if '\r\n' in t else '\n'
    out = fn(t, nl).encode('utf-8')  # build first: an exception must not truncate the file
    open(path, 'wb').write(out); print('ok', path)

def history(t, nl):
    assert '### 419)' not in t
    # (4) correct #417: the sweep failure was a product defect surfacing, not a harness fault
    old = "두 번째 FAIL 은 S23 의 복구 IDR 단정이 부하 중 뷰어 자체 재요청으로 발행된 더 새 IDR 을 못 받아들여 생긴 하네스 문제"
    assert t.count(old) == 1
    t = t.replace(old, "두 번째 FAIL 은 **하네스 문제가 아니라 제품 결함이 드러난 것**이었다(#419: 포기한 AU 의 늦은 청크가 조립을 재생성해 같은 head 를 2~3회 포기시키고 뒤의 정상 IDR 배달을 막았다). 당시 함께 넣은 복구 단정 완화")
    L = [
"### 419) 2026-09-08 A04 후속 — 포기한 AU 의 늦은 청크가 head 를 재차단하던 제품 결함 (검증용 remote#srb0qig4 NEEDS_CHANGES, Codex 구현 주의 4건)",
"- 결함(검증용 지적, 제 오진 정정): #417 의 `GiveUpIncomplete` 는 조립만 지우고 그 identity 를 남기지 않아, 호스트가 계속 보내던 그 AU 의 늦은 data/FEC 청크가 `PushDatagram` 의 stale 가드(`native_video_client_shared_core.cpp` 의 `deliveredAny_ && !sequence_is_newer(...)`)를 통과해 **같은 seq 의 조립을 재생성**하고 head 를 다시 막았다. 그 결과 같은 (gen,seq) 가 2~3회 포기되고(로그 age 가 다시 처음부터 계산되는 것이 증거: `.claude/n3_rule2_recovery_run2.log` hard-cap 1,000,893 → hard-cap 1,000,880 → nack-spent 425,432), Codex 조건 \"hardCap 초과는 한 episode\" 가 깨졌으며, 뒤따르는 정상 IDR 이 배달되지 못해 sweep2 의 S23 이 FAIL 했다(`.claude/v14_a04_sweep2_fail_remote60_viewer_udp_recovery_test.log`). 단독 실행은 PASS 였지만 같은 결함을 담고 있었고, 회귀가 이를 못 잡은 이유는 단정이 keyframe 요청 수만 보았고 그 수는 KeyframeRequestState 리미터(120 ms 간격·토큰 3)가 눌러 1~2회로 보였기 때문이다(감사 T2 와 같은 부류).",
"- 수정: 포기한 identity 를 **tombstone** 으로 기억한다(`abandoned_`, `IsAbandoned(gen,seq,nowUs)`). `GiveUpIncomplete(gen,seq,nowUs)` 성공 시 그 (generation, seq) 를 기록하고, `PushDatagram` 은 stale 가드 앞에서 그 identity 의 청크를 Ignored 로 버린다. `lastDeliveredSeq_` 를 전진시켜 \"배달한 것처럼\" 꾸미지 않는다(Codex 주의 1: 배달 gap·IDR 복구 계약과 다음 배달의 `droppedPreviousIncomplete` 의미가 바뀌면 안 되고, 판정하지 않은 AU 를 삼켜서도 안 된다). 다른 generation 은 seq 공간이 새로 시작하므로 절대 매칭되지 않는다. 상한·수명(Codex 주의 2): 최대 16개(오래된 것부터 폐기)·시계를 받은 경우 5 s TTL·`Reset()`(새 세션/디코더 리싱크)에서 소거 — 헤더 주석에 명시.",
"- 회귀(Codex 주의 3, 요청 수가 아니라 **포기 횟수**로 직접 단정): 수신기의 포기 카운터를 `RecvStats.udpStuckHeadGiveUps` 로 올리고 `VideoReceiver::stats()` 로 노출, rig `give_ups()`. S17·S18·S21·S23·S24 각각 \"그 head 의 포기 == 1\" 을 단정. 신규 **S24** — 포기 뒤 그 AU 의 늦은 청크가 도착(`FakeHost::ResendChunks`)해도 재차단·재포기 없음(포기 1회 유지), 이어진 복구 IDR 이 정상 배달. shared_core 단위: 늦은 청크 Ignored·다음 AU 정상 배달·같은 seq 라도 다른 generation 은 미차단·TTL 만료·`Reset()` 소거. S23 은 청크 간격을 30 ms 로 바꿔 rule ③(무진행)이 아니라 **hardCap 이 실제 발동**하도록 정정(로그 reason=hard-cap 1건). `published_key_at_or_after` 완화는 유지하되 해소 근거로 쓰지 않는다(Codex 주의 3).",
"- 검증: 수정 후 recovery 단독 ×5 `.claude/v14b_recovery_run1..5.log` 전부 PASS(실행당 포기 6회 = 시나리오마다 정확히 1회, reason=hard-cap 은 S23 1건), shared_core `.claude/v14b_shared_core.log` PASS. 수정 전 재현 로그 보존: `.claude/n3_prerule_recovery.log`(초기안), `.claude/n3_rule2_recovery_run1..3.log`(포기 2~3회, PASS 였으나 결함 포함), `.claude/v14_a04_sweep2_fail_remote60_viewer_udp_recovery_test.log`(S23 FAIL).",
"- " + SWEEP,
"- 변경 파일: `apps/native_poc/src/native_video_client_shared_core.hpp`, `native_video_client_shared_core.cpp`, `native_video_client_shared_core_test.cpp`, `viewer_recv_stats.hpp`, `viewer_video_receiver.hpp`, `viewer_video_receiver.cpp`, `viewer_udp_recovery_test.cpp`, `docs/history.md`(이 항목 + #417 정정), `docs/작업목록.md`. 설치·push·서버 배포 없음.",
"- 다음: 검증용 검사(#418 c77b01d 와 함께) → 구현 OK → Codex 최종 확인 → 그 뒤에만 0.2.104 릴리스 빌드·설치본 1회.",
    ]
    return t.rstrip('\r\n') + nl + nl + nl.join(L) + nl

def tasks(t, nl):
    anchor = "1.6 [x] **A02+A06+HN07 인코더 provenance**"
    assert t.count(anchor) == 1
    line = "1.7 [x] **A04 후속(포기 identity tombstone)** — 포기한 AU 의 늦은 청크가 조립을 재생성해 같은 head 를 2~3회 포기시키던 제품 결함 수정, 포기 횟수 직접 단정·S24 신규 (history #419)"
    idx = t.index(anchor); end = t.index(nl, idx)
    return t[:end] + nl + line + t[end:]

rw(ROOT + r'\docs\history.md', history)
rw(ROOT + r'\docs\작업목록.md', tasks)
print('V14 follow-up docs ok')
