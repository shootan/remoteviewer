# task rel-apk-0231 r1 — APK 0.2.31 (선명화 필터) 릴리스 후보 준비 (검증용 remote#wwml7h91, 2026-10-22) — 담당 작업용2

**이 파일이 정본이다. 끝까지 읽고 CLAUDE.md 대로 진행한다. directory-retry-fix(t-iw0926r5)보다 먼저** — 지금 단계 커밋 후 멈추고 이것부터.

사용자 지시(10-22): "이번에 apk 에 필터도 같이 넣어" → "내가 시킨 것 중 작업 다 한 거 다 게시해야지. 마무리해서. apk 에 넣으라니까." → **APK 0.2.31 에 SH 포함해 PC 0.2.162 와 함께 게시**.
범위 결정(검증용): SH 만 넣는다. keepalive-multi 는 base 가 오래돼(eedd207) 공유 native 13파일 재통합·사용자 정책값(3대·5분) 확인이 남아 이번에 넣지 않는다. 휴대폰 잠금 화면 비밀번호(sealed APK)는 미구현 — 알려진 한계 유지.

## 입력
- PC 쪽: `integ/0.2.162` @ 9f54912(Codex 통합 OK). 작업용1 이 `rel/0.2.162`(9f54912 + PC 버전 commit) 를 dry-run 중 — 그 브랜치가 생기면 그 위로 옮긴다(PC 버전 파일만 다르므로 충돌 없음 예상).
- SH: `feat/sh-filter` @ c494dc6(Codex SOURCE/기능별 EVIDENCE OK, `.claude/codex_sh_filter_impl_r3.md`).

## 할 일
1. worktree `C:/work/gnlink-wt/rel-apk-0231`, 브랜치 `rel/apk-0.2.31` = rel/0.2.162(있으면) 또는 9f54912 → `git merge --no-ff feat/sh-filter`(git CLI). 제품 충돌은 보고 후 대기. webview2 는 robocopy 실복사(junction 만들지 않음).
2. APK 버전 commit(Git MCP): `apps/android_direct_client/app/build.gradle.kts` versionName **0.2.31** / versionCode **30**(이전 릴리스 b4fd204 와 같은 방식), history 에 APK 0.2.31 후보 상태. PC 버전은 건드리지 않는다.
3. 출하 APK 빌드: 지난 APK 게시와 같은 파이프라인(게시는 assembleDebug, debug 키 — 인증서 SHA-256 **dcc806ae…** 와 같아야 함, `.claude/state_2026-09-30.md` 'APK 게시 준비 메모'). seam 없는 빌드. APK sha256·크기·apksigner 인증서.
4. 검사: `gnlink_check_fixed_server.py --apk`(rc0), seam 표식 0, versionName/Code 확인(aapt/apkanalyzer), 4 ABI .so 포함.
5. 공유 native 영향 smoke(AVD, 측정 슬롯 CreateNew): 이 APK 는 PC 0.2.162 의 공유 native 변경(hello-delay 의 PunchAny/hello, DW 의 TakeMonitorListRequest 등)도 함께 싣는다 → AVD 에서 접속·영상·선명도 0.20 표시·재연결 1회·모니터 목록이 있는 호스트 접속 1회. SH 핵심(compare) 1회.
6. **금지**: 서명된 manifest 작성·게시·push·사용자 폰 설치, repo root 밖 삭제(생성만).

## 보고 (1회)
`.claude/report_rel_apk_0231_r1.md` + `remote#wwml7h91` pin: 브랜치·merge·버전 commit, APK 해시·크기·인증서, gate·seam·버전 확인, AVD smoke 결과, 남은 위험.
