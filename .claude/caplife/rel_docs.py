import json
wt = 'C:\\work\\gnlink-wt\\rel-0163\\'

def edit(path, pairs):
    s = open(path, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for old, new in pairs:
        assert s.count(old) == 1, (path, s.count(old), old[:80])
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(path, 'wb').write(s.encode('utf-8'))

edit(wt + 'docs\\작업목록.md', [
    ('| **PS** | 🔴 **로그인 전 화면 영상 0',
     '| **PS** | **PC 0.2.163 후보 dry-run(10-26, 게시 전) — capture-lifecycle r1~r4 구현·자동 검증·Codex SOURCE OK(adc9482), 실기 대기** · 🔴 **로그인 전 화면 영상 0'),
    ('| **LK** | **PC 0.2.162 게시(10-22)·실기 대기** · **LK-UX 재시도: 구현·자동 검증 완료(fix/lk-retry, 10-26) → R1 0.2.163 통합 대기**',
     '| **LK** | **PC 0.2.162 게시(10-22)·실기 대기** · **LK-UX 재시도: 구현·자동 검증 완료(fix/lk-retry 8d8655e, Codex SOURCE OK) → PC 0.2.163 후보 dry-run(10-26, 게시 전)**'),
])

edit(wt + 'docs\\구현계획.md', [
    ('- [ ] PS·UA 실기 — 실제 로그인 전 재부팅·실 UAC 승인 (R1 0.2.163 게시 뒤 사용자)',
     '''- [ ] PS·UA 실기 — 실제 로그인 전 재부팅·실 UAC 승인 (R1 0.2.163 게시 뒤 사용자)
- [x] R1 PC 0.2.163 통합·릴리스 후보 dry-run — rel/0.2.163 = main a1c551c + capture-lifecycle adc9482(LK-UX 포함), 버전 90192f4 (2026-10-26)
- [ ] R1 PC 0.2.163 서명·NAS 게시·push (검증용 독립 검사 + Codex EVIDENCE 뒤 검증용)
- [ ] R1 APK 0.2.32 — SH r2 + UA-2 APK 뒤 별도 게시 (작업용2)'''),
])

h = wt + 'docs\\history.md'
s = open(h, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
if not s.endswith('\n'):
    s += '\n'
s += '''### 2026-10-26 PC 0.2.163 릴리스 후보 dry-run (게시 전 — 서명·업로드·push 없음)
- 범위: R1 을 PC 먼저 / APK 나중으로 분리(작업용2 미착수 — PC 뷰어는 backend 요청을 보내지 않아 UA-2 UI 없이 완결, 구 APK 0.2.31 의 wgc 요청은 새 Host 에서 그 세션에만 적용). 정본 `task_rel_0163_r1.md`.
- rel/0.2.163 = main a1c551c + merge --no-ff **f0a388c**(fix/capture-lifecycle adc9482: capture-lifecycle r1~r4 + LK-UX 8d8655e, Codex SOURCE OK `codex_capture_lifecycle_src_r4.md`·`codex_lk_ux_retry_src_r4.md`; 충돌은 `docs/작업목록.md` 한 곳 — PS 행 + LK 최신 행 보존) + 버전 **90192f4**(product_version.hpp 0.2.162→0.2.163 한 줄; UTF-8 BOM·CRLF 17·1020 B 유지). webview2 는 robocopy 실복사(90 파일, 원본 불변, junction 없음).
- `gnlink_release.sh --dry-run` **rc 0**: parity·payload-set·checksums PASS, installer payload 10/10. `.claude/rel/0.2.163`: windows.manifest(미서명) `5ea1ac74…`, SHA256SUMS.txt `9b1a21dd…`(11 항목 OK), sdk set `703a6a41…`.
- 산출물: UTF-16 `0.2.163` Viewer·Stream·Host·Client 각 1·Setup 5, `0.2.162` 잔존 0. 출하 gate 에 LK·capture 시험 표식 추가(`][test-seam]`·`ForTest`·`pointerfirst` + 새 환경 이름 9개 단독 대조) — payload 9/9, self-test 51, 시험 빌드 host 를 넣은 부정 대조 FAIL(새 표식 검출). 출하 10 타깃 시험 define 0(`REMOTE60_ENCODER_POLL_DIAG=0` 만). 제품의 시험 hook 호출 0(LK hook 선언·기본 empty, capture testStill 은 시험 빌드 #ifdef 안에서만). shell.html `8fac49cd…`(소스·빌드·payload 동일).
- smoke(같은 commit 의 시험 빌드, 콘솔 세션, SLOT CreateNew→삭제): LK lock 68·prelogin 79, PS still 32, UA 54, UA fullframe-fails 18, real-recv-smoke(payload GNLinkStream) PASS, host_punch_reply 102, hello_backlog 28, punch_any PASS, **dxgi_wedge_reconnect_runner ALL PASS**(DW 에서 미뤄 둔 1회). 잔여 시험 프로세스 0.
- 상태: **로컬 실기 후보(게시 전)**. 서명·NAS 게시·push 는 검증용(독립 검사·Codex EVIDENCE 뒤). 실기 미검증: 실 잠금 화면 틀린 비밀번호 재시도, 실 로그인 전 재부팅, 실 UAC 승인/거부, 회사 PC DW. APK 0.2.32 는 별도.
- 제품/테스트/문서: 제품(버전 한 줄) / 테스트(출하 gate 표식·self-test) / 문서(이 항목·작업목록·구현계획).
'''
if crlf:
    s = s.replace('\n', '\r\n')
open(h, 'wb').write(s.encode('utf-8'))

json.dump({"repo_path": "C:/work/gnlink-wt/rel-0163",
           "files": ["automation/gnlink_check_fixed_server.py", "docs/history.md", "docs/작업목록.md", "docs/구현계획.md"]},
          open('D:\\remote\\remote\\.claude\\caplife\\rel_add2.json', 'w', encoding='utf-8'), ensure_ascii=False)
json.dump({"repo_path": "C:/work/gnlink-wt/rel-0163",
           "message": "release gate + docs: 0.2.163 candidate -- the LK-UX and capture-lifecycle test markers in the ship gate, the dry-run recorded\n\n"
                      "gnlink_check_fixed_server.py: TEST_MARKERS += '][test-seam]', 'ForTest', 'pointerfirst'; the nine new test-build\n"
                      "environment names each checked alone (ascii and wide) by the self-test (51 checks). A test-build host in the\n"
                      "payload's place fails it on the new markers.\n\n"
                      "Product / test / docs: product (none) / test (ship gate markers) / docs (history, task list, plan).\n\n"
                      "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"},
          open('D:\\remote\\remote\\.claude\\caplife\\rel_commit2.json', 'w', encoding='utf-8'), ensure_ascii=False)
print('ok')
