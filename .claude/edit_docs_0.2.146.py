import io, sys
def load(p):
    s = io.open(p, encoding="utf-8", newline="").read()
    crlf = "\r\n" in s
    return s.replace("\r\n", "\n"), crlf
def save(p, s, crlf):
    io.open(p, "w", encoding="utf-8", newline="").write(s.replace("\n", "\r\n") if crlf else s)

# history.md: append the entry
h, hc = load("docs/history.md")
e, _ = load(".claude/history_entry_0.2.146.md")
assert "2026-10-01 file-copy 네트워크 완료" not in h
if not h.endswith("\n"): h += "\n"
h += e if e.startswith("\n") else "\n" + e
save("docs/history.md", h, hc)

# 구현계획.md: K2 checkboxes / progress
p, pc = load("docs/구현계획.md")
def rep(old, new):
    global p
    n = p.count(old)
    if n != 1: print("ANCHOR", n, old[:70]); sys.exit(1)
    p = p.replace(old, new)
rep("- [ ] 네트워크 쪽(작업용1): 제어 65~79 · Pong 0x800 · bulk 통합 · 뷰어 offer/Prepare/Pull — 도우미는 `file_copy_pipe.hpp` 로만 연결",
    "- [x] 네트워크 쪽(작업용1) — ✅ **0.2.146 게시(df274be, 2026-10-01)**: 제어 65~79 · Pong 0x800 · bulk 통합 · 뷰어 offer/Prepare/Pull — 도우미는 `file_copy_pipe.hpp` 로만 연결. 세션 epoch·헬퍼 instance 소유권 보수 r4~r12(`history.md` 2026-10-01 항목), 검증용 독립 실행 + Codex 소스 OK. P2 한계: 도우미가 세션 중 재기동되면 source PC 에서 재복사 필요")
rep("(작업용2, feat/file-copy-helper, 2026-09-28, 검수 대기):",
    "(작업용2, feat/file-copy-helper, 2026-09-28; 소스 병합 7fb4c5e, **0.2.146 부터 출하**):")
rep("- [ ] 실제 explorer.exe Ctrl+V·진행창 취소 실기(설치본)",
    "- [ ] 실제 explorer.exe Ctrl+V·진행창 취소 실기(설치본) — 0.2.146 으로 가능, 작업목록 V14(두 PC 양방향·대용량·취소, 도우미 첫 출하라 한쪽은 Setup 설치 권장)")
save("docs/구현계획.md", p, pc)
print("ok")
