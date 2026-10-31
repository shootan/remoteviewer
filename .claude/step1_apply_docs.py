import io

# 1) history.md -- append entry 427 (UTF-8 BOM, LF preserved)
frag = io.open('.claude/step1_history_fragment.md', encoding='utf-8').read()
hist_path = 'docs/history.md'
hist = io.open(hist_path, encoding='utf-8-sig').read()
assert '### 427)' not in hist, 'entry 427 already present'
assert hist.endswith('\n'), 'history does not end with newline'
hist = hist + frag
io.open(hist_path, 'w', encoding='utf-8-sig', newline='').write(hist)
print('history.md appended, entry 427')

# 2) 구현계획.md -- new checklist section (no BOM, LF)
plan_path = 'docs/구현계획.md'
plan = io.open(plan_path, encoding='utf-8').read()
assert '업데이트 기능' not in plan, 'update section already present'
section = (
    '\n\n---\n\n'
    '## 업데이트 기능 (2026-09-08~, 설계: `docs/업데이트_기능_설계.md`, Codex 계획)\n\n'
    '- [x] step1 현행 배선 조사 + 설계/완료조건 문서 (history #427) — 설치기·권한표·수명주기·서버·Android 사실조사, 기존 장애 4건 기록, 11단계 상태기계 초안·완료조건·하네스 재현법. 문서만, 제품 변경 없음\n'
    '- [ ] Codex 확정 대기 4건 — 아티팩트 출처 검증(서명) · Android 서명·키 정책과 앱 데이터 보존 · manifest 스키마 · 서버 신규 엔드포인트\n'
    '- [ ] 검증용 Claude 문서 검사 (step1 완료 판정)\n'
    '- [ ] step2 `compare_versions()` 공용 헤더 추출 + 단위 테스트 (현재 테스트 0건)\n'
    '- [ ] step2 상태기계 seam 설계와 결정론 회귀 (교체 실패·트리 종료·재실행·상호배제)\n'
    '- [ ] step2 기존 장애 (a) 종료/payload 목록 일원화·원자 교체·롤백 · (b) 업데이터 트리 분리 · (c) 교체 후 재실행 (원장 `docs/full_code_audit_2026-09-08.md` I01)\n'
    '- [ ] 미확인 6건 해소 — NAS 서버 TLS 가동 여부 · `taskkill /T` 열거 의미론 실행 검증 · 스테이징 디렉터리 ACL 상속 실측 · 클라 셸 백그라운드 훅 · Android Kotlin 네트워크 코드 · 설치본 잔존 파일\n'
)
plan = plan.rstrip('\n') + section
io.open(plan_path, 'w', encoding='utf-8', newline='').write(plan)
print('구현계획.md section appended')

# 3) 작업목록.md -- one line after item 1.8 (no BOM, LF)
tasks_path = 'docs/작업목록.md'
tasks = io.open(tasks_path, encoding='utf-8').read()
assert '1.9 [~] **업데이트 기능**' not in tasks, 'task line already present'
lines = tasks.split('\n')
idx = next(i for i, l in enumerate(lines) if l.startswith('1.8 '))
new_line = (
    '1.9 [~] **업데이트 기능** — step1 배선 조사·설계 문서 완료 '
    '(`docs/업데이트_기능_설계.md`, history #427). '
    '기존 장애 (a) 설치 중 Client/Viewer 미종료로 혼합버전 · (b) `taskkill /T` 가 업데이터를 같이 죽임 · (c) 교체 후 재실행 없음. '
    '다음: 검증용 문서 검사 + Codex 확정 4건(서명·Android 키·manifest·서버 엔드포인트) → step2 구현'
)
lines.insert(idx + 1, new_line)
io.open(tasks_path, 'w', encoding='utf-8', newline='').write('\n'.join(lines))
print('작업목록.md line inserted after 1.8')
