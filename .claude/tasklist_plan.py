# -*- coding: utf-8 -*-
import io

# ---------------------------------------------------------------- 작업목록: one line, at the top
p = 'docs/작업목록.md'
d = io.open(p, encoding='utf-8').read()
old = u'''## 🔴 남은 작업 — 우선순위 순

0. [~] **IME/입력+잠금해제**'''
new = u'''## 🔴 남은 작업 — 우선순위 순

0.0 [~] **0.2.109 실기 대기** — 캐시 토큰 `/healthz` 광고(`422196b`) + 업로더 종료 계약(`b4d2a7a`) 반영본을 **NAS 게시 완료**(manifest `743fbb1f…`, 아티팩트 9/9 익명 https 검증). 배포는 이제 `automation/gnlink_deploy.sh` 로 한다(정책: `AGENTS.md` "배포 완료 기준", history #498). **남은 것은 실기 하나** — 업데이트 후 Host 가 `directory=online` 이 되는지. ⚠️ 캡처·인코더 의존 스위트 4개는 **콘솔 세션이 Disc 인 동안 관측한 red 라 판정 아님**(history #497), 재연결 후 재실행 필요

0. [~] **IME/입력+잠금해제**'''
assert d.count(old) == 1, 'tasklist anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok tasklist')

# ---------------------------------------------------------------- 구현계획: status only
p = 'docs/구현계획.md'
d = io.open(p, encoding='utf-8').read()
d = d.rstrip('\n') + u'''

---

## 배포 자동화 (2026-09-10) — 상태

- [x] 배포 절차의 스크립트화 — `automation/gnlink_deploy.sh` (`--dry-run` / `--verify-only` / 게시)
- [x] 게시 전 서명 검증 — `automation/gnlink_verify_manifest.js` (서버와 같은 구현, 공개키는 `update_manifest.cpp` 에서 읽음)
- [x] 회귀 — `automation/gnlink_deploy_test.sh` (격리 root, 40 PASS)
- [x] 정책 문서 — `AGENTS.md` "배포 완료 기준", `CLAUDE.md` "릴리스 배포 계정 (NAS)", `docs/history/history_2026-W37.md`
- [x] 실서버 `--verify-only` 로 0.2.109 게시 상태 확인 (재게시 없음)
- [ ] **ssh·curl 경로의 실패 케이스** — 격리 회귀가 대신하지 못하는 부분, 실서버에서만 드러남
- [ ] 서버앱(디렉터리 서버) 갱신 경로의 자동화 — 의존 모듈·env 차분·재시작 판단까지 (현재는 문서 규칙만)
'''
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok plan')
