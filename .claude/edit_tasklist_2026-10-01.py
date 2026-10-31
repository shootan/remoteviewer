import io, sys
p = "docs/작업목록.md"
s = io.open(p, encoding="utf-8", newline="").read()
crlf = "\r\n" in s
s = s.replace("\r\n", "\n")
def rep(old, new):
    global s
    n = s.count(old)
    if n != 1:
        print("ANCHOR", n, old[:80]); sys.exit(1)
    s = s.replace(old, new)

rep("> 2026-09-22 재작성, 2026-09-30 갱신. 기준 `main` = `e274c37` (0.2.145).",
    "> 2026-09-22 재작성, 2026-10-01 갱신. 기준 `main` = `df274be` (0.2.146).")
rep("> 현재 배포(2026-09-30 NAS manifest 직접 확인): `windows=0.2.145`(e274c37) · `android=0.2.23`(versionCode 22, 09-28 —\n"
    "> 현재 게시 APK 에는 고정 서버·자동 로그인·계정 상태 **미포함**, V11).",
    "> 현재 배포(2026-10-01 NAS manifest 직접 확인): `windows=0.2.146`(df274be, 10-01 04:2x) · `android=0.2.24`(versionCode 23,\n"
    "> 09-30 17:1x — 고정 서버·자동 로그인·계정 상태 포함).")
# V11: APK published -> phone field test only
rep("| V11 | **APK 0.2.24 게시 → 폰 실기** — 고정 서버 `gnlink.shotan.net`(입력란 없음, 옛 자격 이행)·자동 로그인(Keystore)·계정 상태 문장. PC 0.2.145 와 같은 트리 `e274c37`. 빌드·패키징 작업용2 task `t-kufxcnae`(지시서 `.claude/task_apk_0.2.24_r1.md`, 09-30), manifest·서명·게시 검증용 | 검증용 게시 → 사용자 폰 1회 |",
    "| V11 | **APK 0.2.24 폰 실기** — ✅ 게시 09-30 17:1x(manifest ede573eb…, APK c39b2434…, 인증서 = 0.2.23, 백업 pair 0.2.23). 내용: 고정 서버(입력란 없음, 옛 자격 이행)·자동 로그인(Keystore)·계정 상태 문장(트리 `e274c37`) | 사용자 폰 1회 |")
# V12 done
rep("| V12 | **0.2.145 PC 자동 로그인 실기** — 클라 재시작·서버 재시작·12 h 뒤 입력 없이 목록, 로그아웃이 서버에도 반영 | 사용자 | `login.cred` 존재 · 재시작 뒤 목록 · 로그아웃 뒤 폼 |",
    "| ~~V12~~ | ✅ **0.2.145 PC 자동 로그인 실기** — 사용자 확인 09-30 16:2x(\"자동 로그인 잘 되는 거 확인했다\") | — | 완료 |")
# V14 new: file copy field test
rep("| V13 | **관리 창구(Account Admin API v1) 운영**",
    "| V14 | **0.2.146 파일 복사 실기** — 두 PC 사이 탐색기 Ctrl+C → 원격 데스크톱 Ctrl+V(양방향), 취소·대용량·전송 바. 도우미 `GNLinkClipHelper.exe` 첫 출하(Setup 에 포함, 인앱 업데이트 payload 11개). 바로 시험하려면 **Setup 설치 권장**(기존 0.2.145 업데이터도 11개 payload 를 받는지는 이 실기가 첫 확인). P2 한계: 원격 도우미가 세션 중 재기동되면 source PC 에서 다시 복사해야 붙여넣기 됨 | 사용자 PC 2대 | 복사→붙여넣기 완료, 취소 시 원격 클립보드 정리, 실패 문구 |\n"
    "| V13 | **관리 창구(Account Admin API v1) 운영**")
# C11-N done
rep("| C11-N | 🔧 **file-copy 네트워크 — 탐색기 Ctrl+C/V 파일 복사**(작업용1, `feat/file-copy-net`, task `t-zdmsd4gb`) — 도우미 출하 c25508d → r3 05ee458 → r4 `c32e736`(epoch 판정·전환·기록을 한 잠금에; Codex 정적 검토: epoch 역행은 닫힘, **세션 격리 P1 3건 미해결**) → **r5 진행 중**(늦은 종료 훅·옛 Offer 게시/해제·bulk 열기/닫기를 세션 세대 소유권으로; 지시서 worktree `.claude/task_file_copy_network_r5.md`) | 파일 복사 | 큼 | 검수 remote_claude. 출시(계획, 미확정): **r5 검수 전에는 출시하지 않는다** — c25508d 부터 Setup 이 helper 를 담으므로 새 설치·helper 보유 환경에서는 바로 켜진다. r5 OK 뒤 다음 PC 버전에 묶음 |",
    "| ~~C11-N~~ | ✅ **file-copy 네트워크 — 게시 0.2.146(df274be, 10-01)** — `feat/file-copy-net` r4 c32e736 → r5~r10(세션 epoch·헬퍼 instance 소유권: 늦은 훅·옛 Offer·bulk 열기/닫기·응답 소비·Gone 이 소유한 모든 상태 종료) → r11 dacee8e(V3 시험) → r12 9140604(main 병합 + 시험 빌드 링크 1줄). 검수: 검증용 독립 실행 r5~r12(net e2e 325/0, 부정 대조가 정확히 자기 반례만 FAIL) + Codex 소스 OK(3b68b5f). 기록 `verify-c2-ef533f2/.claude/rv_r5~r12/verdict_*.md`, 통합 결함 1건(`GNLinkStreamMigrationTest` 링크) 은 병합 통합 빌드에서 발견·보수 | 파일 복사 | 큼 | 실기 V14. P2 한계(도우미 재기동 뒤 재복사)는 구현계획 K 절에 기록 대상 |")
# R4 publish records: extend range
rep("| R4 | `history.md` 에 0.2.140~0.2.145 · APK 0.2.21~0.2.23 의 **게시 기록이 없다**(release 커밋 메시지·`.claude/rel/<버전>`·`release-*/.claude/msg.txt` 만). 0.2.145 게시 근거(manifest·sig·artifact 해시)는 `feat-fixed-server/.claude/state_fixed_server.md` |",
    "| R4 | `history.md` 에 0.2.140~0.2.146 · APK 0.2.21~0.2.24 의 **게시 기록이 없다**(release 커밋 메시지·`.claude/rel/<버전>`·`release-*/.claude/msg.txt` 만). 0.2.145 근거 `feat-fixed-server/.claude/state_fixed_server.md`, 0.2.146·APK 0.2.24 근거 `D:/remote/remote/.claude/state_2026-09-30.md`(manifest·sig·artifact 해시·백업 pair) |")
if crlf:
    s = s.replace("\n", "\r\n")
io.open(p, "w", encoding="utf-8", newline="").write(s)
print("ok")
