# task rel-0162 r1 — PC 0.2.162 릴리스 후보 준비 (dry-run 까지) (검증용 remote#wwml7h91, 2026-10-22) — 담당 작업용1

**이 파일이 정본이다. 끝까지 읽고 CLAUDE.md 대로 진행한다.** 근거: Codex `.claude/codex_integ_0162_r1.md`(9f54912 통합 SOURCE/EVIDENCE OK, rel/0.2.162 로컬 후보 준비 승인).

## 범위
- 입력: `integ/0.2.162` @ **9f54912**.
- 하는 것: rel 브랜치·버전 commit·기록 상태 → `automation/gnlink_release.sh --dry-run` → 산출물 검사. **여기까지.**
- 하지 않는 것: NAS 업로드·manifest 교체(gnlink_deploy 실게시)·git push·사용자 PC 설치/재시작/잠금/재부팅/서비스 교체. 서명은 검증용 판단 뒤(지금 하지 않음).

## 할 일
1. worktree `C:/work/gnlink-wt/rel-0162`, 브랜치 `rel/0.2.162` = 9f54912. **webview2 는 처음부터 실제 복사**: `robocopy D:\remote\remote\third_party\webview2 <wt>\third_party\webview2 /E`(junction 을 만들지 않는다 — 릴리스 스크립트가 junction 을 해시하지 못하고, repo root 밖에서 junction 제거도 하지 않기 위해). 원본 파일 수 전후 불변 확인.
2. 버전 commit(Git MCP, `.claude/mcp_git_client_wt.py <worktree>`): `product_version.hpp` 등 버전 원천을 0.2.162 로 — **UTF-8 BOM·줄바꿈 유지**(바이트 대조). 이전 릴리스의 버전 commit(예: 6be579b)과 같은 파일 집합을 바꾼다. history/구현계획에 0.2.162 후보 상태(게시 전) 기록.
3. `automation/gnlink_release.sh --dry-run`(Bash tool, POSIX PATH). rc·rel 디렉터리·manifest(미서명) 해시·SHA256SUMS.
4. 산출물 검사:
   - 4 exe(Viewer/Stream/Host/Client) UTF-16 `0.2.162` 존재·`0.2.161` 잔존 0, payload 버전 carrier 수.
   - gate `--dir`(payload/릴리스 산출물 기준) + self-test, 생성 vcxproj 기준 출하 타깃 시험 define 0(defines 검사 스크립트가 있으면 그것), payload 에 시험 exe 0, seam 문자열 0.
   - `ui/shell.html` 해시가 9f54912 소스·payload 와 일치.
   - 영향 smoke: real-recv-smoke 등 기존 릴리스 smoke(이전 rel 보고와 같은 것).
5. 측정 슬롯 CreateNew, **repo root 밖 삭제 금지**(새 worktree 생성만), 사용자 설치본·서비스 무조작.

## 보고 (1회)
`.claude/report_rel_0162_r1.md` + `remote#wwml7h91` pin: rel 브랜치·버전 commit·BOM 확인, dry-run rc·rel 디렉터리·미서명 manifest 해시·SHA256SUMS, exe/Setup 해시, 버전 문자열 검사, gate/defines/seam/HTML/smoke 결과.
