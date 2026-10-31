from pathlib import Path
import difflib
import hashlib
import json

repo = Path('D:/remote/remote/.claude/worktrees/main-stability-integration')
out = Path('D:/remote/remote/.claude/inj_closure_review')
files = ['docs/history.md', 'docs/작업목록.md', 'docs/구현계획.md']
before = {name: (repo / name).read_bytes().decode('utf-8') for name in files}
after = dict(before)
marker = '### 2026-10-29 시험 안전 하네스 보수·독립 검증 완료'
assert before['docs/history.md'].count(marker) == 1
head = before['docs/history.md'].split(marker, 1)[0]
entry = '''### 2026-10-29 시험 안전 경계·하네스 보수·독립 검증 완료 (안전 Finding 후속 — 제품 출하 아님)
- 범위: 2026-10-28 안전 Finding(시험 Host 입력 주입 기본 ON·stream-start wake·전원 유지)의 **제품 안전 경계·시험 하네스 보수**. 정본 `.claude/codex_test_injection_finding.md`, 구현은 `spike/host-admission-r3`(ka-r3) d9a3a91 → ccaf745 (R2 통합 때 main 으로 온다).
- 제품 변경(같은 브랜치, Codex SOURCE OK): `--host-power-management on|off`(명령줄 전용, 기본 on, 첫 효과 전 bind) — off 면 실행 상태 요청·display wake·monitor-power broadcast 0. OS 입력·전원 API(SendInput·입력 PostMessage·SetCursorPos·IME·AttachThreadInput·UIA SetValue·SetThreadExecutionState)를 `os_input_effects.hpp` 경계로 모음. 시험 전용 build `GNLinkStreamInputRecordTest` 는 해당 API 효과를 기록만 하고 수행하지 않으며 출하 gate 가 그 marker 를 거부.
- 일반 시험 Host 표준: `--input-injection-mode none --host-power-management off`, 해당 launch 의 두 OFF 줄 전 연결 0, 실행 중 재판정, 종료→EOF→최종 snapshot 판정, kill-on-close job, 저장소 test root 안 소유 fixture. record-only·fatal 은 별도 검수한 선언/gate·효과·수명 조건을 적용한다. 실제 입력이 주제인 4개 시험은 기본 REFUSED.
- 검증용 독립 실행(`.claude/verify_inj/VERIFY_RECORD_ba2eaf2.md`): 일반 8종(admission 2모드)의 **9개 명령 ALL PASS**, 19 Host launch 전부 OFF 최종 판정 확인(r1, b8dcc93) · 기록 전용 e2e **12/0**, fatal **26/0**(r2, ccaf745). r1 FAIL/INVALID 이력은 보존하고 판독·환경 보수 후 r2 통과로 구분한다(record r1 Host 원문은 삭제돼 세부 재구성 불가). Codex 구성 단위 EVIDENCE OK(`codex_inj_general_evidence_r1.md`, `codex_inj_record_fatal_evidence_r2.md`).
- 경계 사고 기록(코드 보수, 과거 미확인 유지): 저장소 밖 %TEMP% 생성·cleanup 경로(runtime fixture 작업용 9회 실행 보고·생성/삭제 시도 27회, gate self-test 도구 호출 43회/8 세션; 실제 전체 경로·개별 생성/삭제 결과 미확인) → 저장소 test root 의 새 run 폴더로 바꾸고 junction 은 진입 전에 제외한다. 도구 호출 수·check 수·실제 실행/정리 결과를 구분한다.
- 남은 것(이번 관문 밖): 실제 입력 4개 시험의 record-sink 전환, `viewer_prelogin_login_e2e` 재설계(사용자 재개 전 실행 금지 유지), 게시본 구 Host(--old-host) 실행 미검증, PS/LK/APK 의 안전 재확인과 PC·AVD 실기 EVIDENCE, R2 통합·출하.
- 변경 파일: docs/history.md·docs/작업목록.md·docs/구현계획.md(진행 체크만). 제품·시험 코드는 기존 브랜치에 있고 main 반영은 R2 통합에서.
'''
newline = '\r\n' if '\r\n' in before['docs/history.md'] else '\n'
after['docs/history.md'] = head + entry.replace('\n', newline)
plan = '''
## 시험 안전 경계·하네스 — 진행
- [x] INJ 브랜치 안전 경계·하네스 보수 및 독립 구성 단위 검증(일반 8종·record·fatal)
- [ ] INJ 실제 입력 4개 시험의 record-sink 전환
- [ ] INJ viewer_prelogin_login_e2e 재설계(사용자 명시 재개 전 실행 금지)
- [ ] INJ R2 통합·출하 및 남은 실기 안전 재확인
'''
assert '## 시험 안전 경계·하네스 — 진행' not in before['docs/구현계획.md']
newline = '\r\n' if '\r\n' in before['docs/구현계획.md'] else '\n'
after['docs/구현계획.md'] = before['docs/구현계획.md'].rstrip('\r\n') + newline + plan.replace('\n', newline)
patch = ''
hashes = {}
for name in files:
    final = out / 'final' / name
    final.parent.mkdir(parents=True, exist_ok=True)
    final.write_bytes(after[name].encode('utf-8'))
    canonical = lambda s: s.lstrip('\ufeff').replace('\r\n', '\n')
    hashes[name] = {
        'before_utf8_lf_no_bom': hashlib.sha256(canonical(before[name]).encode('utf-8')).hexdigest(),
        'after_utf8_lf_no_bom': hashlib.sha256(canonical(after[name]).encode('utf-8')).hexdigest(),
    }
    if before[name] != after[name]:
        patch += ''.join(difflib.unified_diff(before[name].splitlines(True), after[name].splitlines(True),
                                            fromfile='a/' + name, tofile='b/' + name))
(out / 'apply_review.patch').write_bytes(patch.encode('utf-8'))
(out / 'approved_hashes.json').write_text(json.dumps(hashes, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print('Prepared final contents for 3 docs; review patch changes history and plan only.')
print('Main worktree untouched.')
