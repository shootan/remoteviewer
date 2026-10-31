import sys
p = sys.argv[1]
b = open(p, 'rb').read().decode('utf-8')
nl = '\r\n' if '\r\n' in b else '\n'
s = b.replace('\r\n', '\n')
old = """  (`automation/gnlink_deploy.sh:435-436`). 즉 **두 `mv` 사이에 manifest 와 sig 가 짝이 맞지 않는
  창이 있다.** 그 창은 업로드 전체 길이가 아니라 rename 두 번 폭으로 **좁혀진 것**이지 없어진 것이
  아니다. 그 사이에 읽은 클라이언트는 서명 불일치로 **거부**한다(안전한 방향이다). 이 사실을
  "원자적" 으로 바꿔 적지 않는다.
  - 🔴 **부분 교체 실패는 자동 원복이 보장되지 않는다.** 두 rename 은 `mv && mv` 로 이어져 있어
    (`:440`), **첫 번째만 성공하면 manifest 는 새 것 · sig 는 이전 것**으로 남는다. 뒤이은
    `rm -f *.tmp` 는 그 상태를 되돌리지 못한다. 이때는 **정확한 backup pair 로 직접 원복**해야 하고,
    게시 실패는 **blocker 로 보고**한다.
    ⚠️ 스크립트의 `die` 문구 *"the previous release is still published"*(`:442`)는 **이 경우에 맞지
    않는다** — 운영 한계로 기록해 둔다.
  - **후속 2건**(제품 스크립트라 이번 문서 작업에서 건드리지 않음): 위 `:442` 실패 메시지,
    그리고 `:445` 의 로그 문구가 아직 *"env 를 바꿀 때만 재시작"* 취지라 아래 재시작 항목과 어긋난다.
"""
new = """  (`automation/gnlink_deploy.sh` 의 `publish_pair`). 즉 **두 `mv` 사이에 manifest 와 sig 가 짝이 맞지 않는
  창이 있다.** 그 창은 업로드 전체 길이가 아니라 rename 두 번 폭으로 **좁혀진 것**이지 없어진 것이
  아니다. 그 사이에 읽은 클라이언트는 서명 불일치로 **거부**한다(안전한 방향이다). 이 사실을
  "원자적" 으로 바꿔 적지 않는다.
  - 🔴 **부분 교체 실패는 자동 원복이 보장되지 않는다.** 두 rename 은 `mv && mv` 로 이어져 있어,
    **첫 번째만 성공하면 manifest 는 새 것 · sig 는 이전 것**으로 남는다. 스크립트는 이때 `.tmp` 를
    지우지 않고 남기고 **exit 4** 로 끝난다(RV-14, 09-23 부터). 2026-10-28(maint r1)부터는 어느
    `.tmp` 가 남았는지(읽기만)로 `swap-state manifest-only`(짝 불일치 — backup pair 로 직접 원복) /
    `neither`(둘 다 미교체 — 그래도 backup 의 SHA256SUMS 로 확인) / `unknown` 을 구분해 말한다.
    어느 경우든 **정확한 backup pair 로 확인·원복**하고, 게시 실패는 **blocker 로 보고**한다.
  - 예전 문구(*"the previous release is still published"*, *"env 를 바꿀 때만 재시작"*)는 09-23 커밋
    65fcee7 에서 이미 바뀌었다. 이 절의 옛 줄 번호(:435~:445)는 그 이전 기준이었다.
"""
assert s.count(old) == 1
s = s.replace(old, new)
open(p, 'wb').write(s.replace('\n', nl).encode('utf-8'))
print('ok')
