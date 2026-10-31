import io, sys, re
p = "docs/작업목록.md"
s = io.open(p, encoding="utf-8", newline="").read()
crlf = "\r\n" in s
s = s.replace("\r\n", "\n")

def rep(old, new):
    n = s.count(old)
    if n != 1:
        print("ANCHOR", n, old[:80]); sys.exit(1)
    return s.replace(old, new)

# 5) header wording
s = rep("고정 서버·자동 로그인·계정 상태는 **아직 폰에 없음**, V11)",
        "현재 게시 APK 에는 고정 서버·자동 로그인·계정 상태 **미포함**, V11)")
# 5) V11 acceptance split
s = rep("| 검증용 게시 → 사용자 폰 1회 | 업데이트 알림→설치 · 재로그인 없이 호스트 목록 · 로그아웃 뒤 재로그인 · 승인 대기 계정이면 그 문장 |",
        "| 검증용 게시 → 사용자 폰 1회 | 업데이트 알림→설치 · ⑴ 기존 유효 세션 이행(0.2.23 평문 세션은 origin·유효기간 확인 뒤 그 실행에서만 사용) ⑵ 최초 명시 로그인 1회 뒤 기기 자격 발급 → 재시작 자동 로그인 · 로그아웃 뒤 재로그인 · 승인 대기 계정이면 그 문장 |")
# 4) V0 row: replace whole line
lines = s.split("\n")
idx = [i for i, l in enumerate(lines) if l.startswith("| V0 |")]
assert len(idx) == 1, idx
lines[idx[0]] = ("| V0 | **이 PC 클라이언트 설정 잔여** — Host 는 복구됨(사용자 재로그인 09-23 15:45 `rv_verify.md`, 0.2.144 이행이 09-29 18:38 `host.json` 을 `gnlink.shotan.net`·`shotan` 으로 재기록, 09-30 직접 확인). "
                 "**Client 의 `client.txt` 는 아직 시험 값**(`127.0.0.1:52762`·`account-b`, 09-23 03:25, `client_recovery_ui_test` 가 덮어씀 — RV-00). 0.2.144 부터 서버 입력란이 없으므로 아이디·비밀번호 로그인 1회면 된다 — V12 와 함께 확인 "
                 "| 사용자: GNLinkClient 로그인 1회 | `client.txt` 에 시험 주소·계정 없음 |")
s = "\n".join(lines)
# 3) RV summary + row statuses
s = rep("✅ **2026-09-30 정리**: RV-A~G 는 검증용 재검수 OK(`feat-connect-bandwidth/.claude/rv_verify.md`, 09-23~24)·main 병합·0.2.140(68edf58) 이후 게시본에 포함. 아래 표는 그 기록이고 남은 것은 실기(V4~V6 계열)뿐이다.",
        "✅ **2026-09-30 정리**: RV-A~G 는 검증용 재검수 OK(`feat-connect-bandwidth/.claude/rv_verify.md` 그룹 A·B·C~G, 09-23~24)·main 병합·0.2.140(68edf58) 이후 게시본에 포함. RV-10 서버 patch 는 09-29 서버 배포(= `e274c37`)에 포함. "
        "**남은 것**(rv_verify 기록): RV-07 실행 검증 없음 · 보안 데스크톱(SYSTEM 에이전트) 경로 키는 해제 대상 아님 · 설계 결정 대기 RV-03 제안·RV-09·C3 범위 확장 · 실기(V4~V6 계열).")
s = rep("29a4da1 · 검수 대기**", "29a4da1 · 검수 OK(rv_verify A)**")
s = rep("4cf7be0 · 검수 대기**", "4cf7be0 · 검수 OK(rv_verify B)**")
s = rep("ae92a26 + 1d93a23 · 검수 대기**", "ae92a26 + 1d93a23 · 검수 OK(rv_verify C~G)**")
s = rep("65fcee7 · 검수 대기**", "65fcee7 · 검수 OK(rv_verify C~G)**")
s = rep("974c06a · 검수 대기**", "974c06a · 검수 OK(rv_verify C~G, 코드)**")
s = rep("G 커밋(rv_report.md G 절) · 검수 대기**", "G 커밋(rv_report.md G 절) · 검수 OK(rv_verify C~G)**")
m = re.search(r"278e072 · 검수 대기[^*]*\*\*", s)
if not m:
    print("RV-F anchor:", [l for l in s.split("\n") if "278e072" in l][:1]); sys.exit(1)
s = s.replace(m.group(0), "278e072 · 검수 OK(rv_verify C~G), patch 는 09-29 서버 배포(e274c37)에 포함**")
# C2 row
s = rep("**미완**: 서버 patch 배포·재시작, 게시(C1+C2 묶음), 실기 첫 클릭 성공률 — 실기 대기",
        "**2026-09-30**: 서버 patch 는 09-29 서버 배포(e274c37)에 포함, C1+C2 는 0.2.140 이후 게시본에 포함. **남은 것**: 실기 첫 클릭 성공률 — 실기 대기")
# 1) C11-H
s = rep("✅ **병합 7fb4c5e(efe7968, r3 까지) · 0.2.143 부터 포함, 네트워크 배선은 C11-N**",
        "✅ **소스 병합 7fb4c5e(efe7968, r3 까지)** — helper 의 출하(installer·update payload)는 0.2.143 에 없음(`c25508d` 가 처음 추가, feat/file-copy-net) → 출하·네트워크 배선은 C11-N")
# 2) C11-N
s = rep("(epoch 판정·전환·기록을 한 잠금에, 세션을 잃은 요청은 아무것도 적용 안 함; Codex: 역행은 닫힘, 세션 격리 P1 3건 남음)",
        "(epoch 판정·전환·기록을 한 잠금에; Codex 정적 검토: epoch 역행은 닫힘, **세션 격리 P1 3건 미해결**)")
s = rep("검수 remote_claude. 출시: c32e736 은 0.2.146 에 넣을 수 있으나 업데이트본에는 도우미가 아직 안 깔려 실제 동작은 그 다음 버전부터, r5 는 0.2.147 |",
        "검수 remote_claude. 출시(계획, 미확정): **r5 검수 전에는 출시하지 않는다** — c25508d 부터 Setup 이 helper 를 담으므로 새 설치·helper 보유 환경에서는 바로 켜진다. r5 OK 뒤 다음 PC 버전에 묶음 |")

if crlf:
    s = s.replace("\n", "\r\n")
io.open(p, "w", encoding="utf-8", newline="").write(s)
print("ok r2")
