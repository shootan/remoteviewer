import io, sys
p = "docs/작업목록.md"
s = io.open(p, encoding="utf-8", newline="").read()
crlf = "\r\n" in s
s = s.replace("\r\n", "\n")
assert "\r" not in s

edits = []
def rep(old, new):
    edits.append((old, new))

rep("> 2026-09-22 재작성. 기준 `main` = `a6cba13` (0.2.133). **남은 작업만** 적는다.",
    "> 2026-09-22 재작성, 2026-09-30 갱신. 기준 `main` = `e274c37` (0.2.145). **남은 작업만** 적는다.")

rep("> 현재 배포: NAS `windows=0.2.133` · `android=0.2.20`(versionCode 19). 디렉터리 서버 배포본 = repo HEAD 에서\n"
    "> `27d9e74` 하나만 뺀 것. Codex 검토를 거친 우선순위 근거는 `.claude/priority_2026-09-22.md`.",
    "> 현재 배포(2026-09-30 NAS manifest 직접 확인): `windows=0.2.145`(e274c37) · `android=0.2.23`(versionCode 22, 09-28 —\n"
    "> 고정 서버·자동 로그인·계정 상태는 **아직 폰에 없음**, V11). 디렉터리 서버 배포본 = `e274c37` 의 `apps/directory/` 6개 파일과\n"
    "> sha256 동일(09-29 21:22 재시작, NRestarts 0). 우선순위 근거 `.claude/priority_2026-09-22.md`(09-22 기준).")

rep("| V0 | 🔴 **이 PC 호스트·클라이언트 설정 복구**",
    "| V11 | **APK 0.2.24 게시 → 폰 실기** — 고정 서버 `gnlink.shotan.net`(입력란 없음, 옛 자격 이행)·자동 로그인(Keystore)·계정 상태 문장. PC 0.2.145 와 같은 트리 `e274c37`. 빌드·패키징 작업용2 task `t-kufxcnae`(지시서 `.claude/task_apk_0.2.24_r1.md`, 09-30), manifest·서명·게시 검증용 | 검증용 게시 → 사용자 폰 1회 | 업데이트 알림→설치 · 재로그인 없이 호스트 목록 · 로그아웃 뒤 재로그인 · 승인 대기 계정이면 그 문장 |\n"
    "| V12 | **0.2.145 PC 자동 로그인 실기** — 클라 재시작·서버 재시작·12 h 뒤 입력 없이 목록, 로그아웃이 서버에도 반영 | 사용자 | `login.cred` 존재 · 재시작 뒤 목록 · 로그아웃 뒤 폼 |\n"
    "| V13 | **관리 창구(Account Admin API v1) 운영** — 서버 코드는 배포됐으나 별도 포트 리스너는 NAS env 미설정으로 꺼져 있음 | 사용자(NAS env·노출 범위 결정) → 검증용 | 승인 대기/정지 전환이 클라 문장·Host 백오프로 보임 · disable 시 릴레이 즉시 끊김(미검증) · 설치 GNLinkHost(관리자)의 403 문장(미검증) |\n"
    "| V0 | 🔴 **이 PC 호스트·클라이언트 설정 복구**")

rep("C1~C3 의 ✅ 는 **구현 범위 승인**이다. 아래 RV-C 가 끝나기 전에는 C3 를 릴리스하지 않는다.",
    "✅ **2026-09-30 정리**: RV-A~G 는 검증용 재검수 OK(`feat-connect-bandwidth/.claude/rv_verify.md`, 09-23~24)·main 병합·0.2.140(68edf58) 이후 게시본에 포함. 아래 표는 그 기록이고 남은 것은 실기(V4~V6 계열)뿐이다.\n"
    "C1~C3 의 ✅ 는 **구현 범위 승인**이다. 아래 RV-C 가 끝나기 전에는 C3 를 릴리스하지 않는다.")

rep("| **C0 (핵심)** | 🔧 **① 관찰 단계 작업용 구현·자체검증 완료, 검수 대기**",
    "| **C0 (핵심)** | ✅ ① 관찰 단계 `c0fdcba` 검증용 OK(`rv_verify.md`)·main 병합(0.2.140 포함) · ② cap/pacing 적용 **미착수**")

rep("| C16 | **PC 클라 마우스 뒤로/앞으로(X1/X2) 버튼**", "| ~~C16~~ | **PC 클라 마우스 뒤로/앞으로(X1/X2) 버튼**")
rep("🔧 **작업용 구현·자체검증 완료 (feat/mouse-xbutton, 2026-09-28 mouse-xbutton r1) · 검수 대기**",
    "✅ **병합 dd65942 · 게시 0.2.141(7386ded, 09-28)**")

rep("| C17 | **영상 FEC 패리티 부가분 A", "| ~~C17~~ | **영상 FEC 패리티 부가분 A")
rep("🔧 **작업용2 구현·자체검증 완료 (feat/fec-single-chunk-stride, 2026-09-28 fec-single-chunk-stride r1) · 검수 대기**",
    "✅ **병합 · 게시 0.2.142(080f442, 09-28)**")

rep("| C11-H | **file-copy-helper r1", "| ~~C11-H~~ | **file-copy-helper r1")
rep("🔧 **작업용2 구현·자체검증 완료 (feat/file-copy-helper, 2026-09-28, r2: pipe 수신 경합 수정 + 승격 호출자 Launch 거부, r3: pending I/O 소유·호스트 생존 정책·STA fence·원본 경로 handle 정책) · 검수 대기**",
    "✅ **병합 7fb4c5e(efe7968, r3 까지) · 0.2.143 부터 포함, 네트워크 배선은 C11-N**")

rep("| C6 | NACK 대기·손실 지표 분리 (R07)",
    "| C11-N | 🔧 **file-copy 네트워크 — 탐색기 Ctrl+C/V 파일 복사**(작업용1, `feat/file-copy-net`, task `t-zdmsd4gb`) — 도우미 출하 c25508d → r3 05ee458 → r4 `c32e736`(epoch 판정·전환·기록을 한 잠금에, 세션을 잃은 요청은 아무것도 적용 안 함; Codex: 역행은 닫힘, 세션 격리 P1 3건 남음) → **r5 진행 중**(늦은 종료 훅·옛 Offer 게시/해제·bulk 열기/닫기를 세션 세대 소유권으로; 지시서 worktree `.claude/task_file_copy_network_r5.md`) | 파일 복사 | 큼 | 검수 remote_claude. 출시: c32e736 은 0.2.146 에 넣을 수 있으나 업데이트본에는 도우미가 아직 안 깔려 실제 동작은 그 다음 버전부터, r5 는 0.2.147 |\n"
    "| C6 | NACK 대기·손실 지표 분리 (R07)")

rep("| D1 | **서버 `27d9e74` 배포** (store durable·인증 비동기 KDF). 배포본 = repo HEAD − 이 커밋 | 라이브 store 파일 스키마 검사 · 백업 · 기동 확인 뒤. journal 오류 0 · 재시작 0 (09-22 확인) |",
    "| ~~D1~~ | ✅ **서버 `27d9e74` 배포 완료** — 09-29 19:57(d34f6e5)·21:22(c5de3cf) 배포에 포함. 09-30 확인: NAS `remote60-directory/` 6개 파일 = `e274c37` sha256, NRestarts 0. 절차 `feat-fixed-server/.claude/server_deploy_plan_2026-09-29.md`(gnlink user service, sudo 불필요) | — |")

rep("지금 `refactor/viewer-split`(main 보다 118 커밋 뒤)", "지금도 `refactor/viewer-split` 659f788(09-30 확인, main 보다 훨씬 뒤)")

rep("| R3 | 133 실기 사실",
    "| R4 | `history.md` 에 0.2.140~0.2.145 · APK 0.2.21~0.2.23 의 **게시 기록이 없다**(release 커밋 메시지·`.claude/rel/<버전>`·`release-*/.claude/msg.txt` 만). 0.2.145 게시 근거(manifest·sig·artifact 해시)는 `feat-fixed-server/.claude/state_fixed_server.md` |\n"
    "| R3 | 133 실기 사실")

for old, new in edits:
    n = s.count(old)
    assert n == 1, (n, old[:60])
    s = s.replace(old, new)
if crlf:
    s = s.replace("\n", "\r\n")
io.open(p, "w", encoding="utf-8", newline="").write(s)
print("ok", len(edits), "edits", "crlf" if crlf else "lf")
