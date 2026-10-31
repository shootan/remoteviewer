import re
ROOT = r'D:\remote\remote'
def rw(path, fn):
    raw = open(path, 'rb').read(); t = raw.decode('utf-8')
    nl = '\r\n' if '\r\n' in t else '\n'
    t2 = fn(t, nl); open(path, 'wb').write(t2.encode('utf-8')); print('ok', path, 'crlf=', nl == '\r\n')
def ledger(t, nl):
    old = '남은 항목: 없음 — F-01~F-21 전부 처리(2026-08-31, 위 "Phase 4 처리" 절 참조).'
    assert t.count(old) == 1, 'ledger anchor'
    new = ('## F-22 (2026-09-07, S12 하네스 단정 위치 — 범위 밖·미수정)' + nl
           + '| 위치 | 원인 | 처리 |' + nl + '|---|---|---|' + nl
           + '| `apps/native_poc/src/viewer_udp_recovery_test.cpp:1294` S12 `CHECK(host.corrupted_keys() == 1)` | `CorruptNextKey()` → `SendFrame(true)` 직후 단정하지만 비동기 인코더 호출에서는 강제 IDR 이 그 호출에 안 나올 수 있어 corrupt(zero) 가 뒤 `pump` 의 SendFrame 에서 일어남 → 단정만 비동기 인코더와 경쟁(시나리오 의미는 유지: SendAu 는 AU 단위 key 로 corrupt). 0.2.100 부터 있던 하네스 타이밍, 제품 회귀 아님(1005ebe 는 pending 규칙만 변경, S12 는 held 프레임 없음). 릴리스 sweep 1회 FAIL `.claude/release_0.2.103_sweep_fail_remote60_viewer_udp_recovery_test.log:162` | task t-5ahw0t5x 범위 밖 — **미수정**, Codex 범위 판단 대기. 수정안: 키가 나갈 때까지 SendFrame 반복 또는 pump 뒤 단정 |' + nl + nl
           + '남은 항목: F-22(범위 밖, Codex 판단 대기). F-01~F-21 전부 처리(2026-08-31, 위 "Phase 4 처리" 절 참조).')
    return t.replace(old, new)
def history(t, nl):
    assert '### 411)' not in t
    block = (nl + '### 411) 2026-09-07 검증용 NEEDS_CHANGES(경미) 반영 — T-P11d 문구(e08de14)·S12 하네스 발견사항 F-22 기록 (A2A task t-5ahw0t5x)' + nl
        + '- 검증용 최종 검사(seq 1115 계열, 우편함 지연 수신): 40b836e/d980ff0/1005ebe/acd8233 diff 대조 OK, 검증용 직접 실행 frame_gate·recovery PASS, 설치본 c263758b… payload 6개 = Release 빌드 sha256 동일. NEEDS_CHANGES 1건 = T-P11d 문구(이미 **e08de14** test(viewer) 로 반영: printf·주석 "decoded keyframe" → "fresh keyframe (anchored at admit)", 동작 변경 없음).' + nl
        + '- 이 커밋: `docs/뷰어_리팩터_발견사항.md` F-22 — S12 `viewer_udp_recovery_test.cpp:1294` 단정이 비동기 인코더와 경쟁(하네스, 제품 회귀 아님, 범위 밖·미수정, Codex 범위 판단 대기). `docs/history.md` #411.' + nl
        + '- 설치본: 제품 소스는 acd8233 과 동일(e08de14·이 커밋은 테스트/문서만) → `dist/GNLinkSetup-0.2.103.exe` 재생성 불필요, build-local 제품 바이너리 미변경. 테스트 exe(`remote60_viewer_frame_gate_test`)만 재빌드해 1회 실행(로그 `.claude/tp11d_frame_gate_e08de14.log`).' + nl
        + '- 상태: task working 유지, 검증용 재검사 대기. 설치·push·서버 배포 없음.' + nl)
    return t.rstrip('\r\n') + nl + block
rw(ROOT + r'\docs\뷰어_리팩터_발견사항.md', ledger)
rw(ROOT + r'\docs\history.md', history)
