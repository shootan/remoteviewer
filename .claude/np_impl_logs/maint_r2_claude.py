p = r'C:\work\gnlink-wt\maint-r1\CLAUDE.md'
b = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in b
s = b.replace('\r\n', '\n')
old = """  - 🔴 **부분 교체 실패는 자동 원복이 보장되지 않는다.** 두 rename 은 `mv && mv` 로 이어져 있어,
    **첫 번째만 성공하면 manifest 는 새 것 · sig 는 이전 것**으로 남는다. 스크립트는 이때 `.tmp` 를
    지우지 않고 남기고 **exit 4** 로 끝난다(RV-14, 09-23 부터). 2026-10-28(maint r1)부터는 어느
    `.tmp` 가 남았는지(읽기만)로 `swap-state manifest-only`(짝 불일치 — backup pair 로 직접 원복) /
    `neither`(둘 다 미교체 — 그래도 backup 의 SHA256SUMS 로 확인) / `unknown` 을 구분해 말한다.
    어느 경우든 **정확한 backup pair 로 확인·원복**하고, 게시 실패는 **blocker 로 보고**한다.
"""
new = """  - 🔴 **부분 교체 실패는 자동 원복이 보장되지 않는다.** 두 rename 은 `mv && mv` 로 이어져 있어,
    **첫 번째만 성공할 수 있다.** 스크립트는 이때 `.tmp` 를 지우지 않고 남기고 **exit 4** 로 끝난다
    (RV-14, 09-23 부터). 2026-10-28(maint r1·r2)부터는 두 가지를 **따로** 말한다:
    - `swap-state`(**단계 추정**, 어느 `.tmp` 가 남았나 — 읽기만): `manifest-only` / `neither` / `unknown`.
      ⚠️ `manifest-only` 는 "manifest rename 만 끝났다" 일 뿐 **짝 불일치의 증거가 아니다** — 같은 서명
      릴리스를 다시 게시하던 중이면 남은 sig 가 그 manifest 에 그대로 유효하다(Codex maint M1).
    - `pair-state`(**지금 게시된 것**, 읽기 전용으로 가져와 바이트 비교 + 게시 전과 같은 검증기·키로 서명
      검증): `verified-this-release` / `verified-another-pair` / `does-not-verify`(클라이언트가 거부) /
      `incomplete`(파일 없음) / `unknown`(읽기·검증 실패 — 어느 쪽으로도 단정하지 않음).
    어느 경우든 exit 4 이고, **정확한 backup pair 로 확인·원복**하며 게시 실패는 **blocker 로 보고**한다.
    "클라이언트가 거부한다" 는 `does-not-verify` 일 때만 쓴다.
"""
assert s.count(old) == 1
s = s.replace(old, new)
open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))
print('ok')
