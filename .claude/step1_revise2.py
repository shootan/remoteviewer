import io

p = 'docs/업데이트_기능_설계.md'
doc = io.open(p, encoding='utf-8').read()

pairs = []

# header: task lineage + revision note
pairs.append((
    '- 작성: 2026-09-08, 작업용 Claude (`remote#r7oj98oc`). 위임: 검증용 Claude (`remote#0jkgf453`), A2A task `t-m8f7us6c`.\n'
    '- 대상 트리: `refactor/viewer-split` @ `8d9ec74` (제품 버전 `0.2.104`, `apps/native_poc/src/product_version.hpp:12`).\n'
    '- **이 문서는 문서 산출물이다.** 제품 코드·CMake·gradle 변경 없음, 빌드·테스트 실행 없음.',

    '- 작성: 2026-09-08, 작업용 Claude (`remote#r7oj98oc`). 위임: 검증용 Claude (`remote#0jkgf453`), A2A task `t-m8f7us6c`.\n'
    '- **개정: 2026-09-08** (task `t-mlza55qw`) — Codex 가 B1(전송)·B2(서명)·B3(Android 키)을 확정하고 검증용 Claude 의 '
    'read-only 실측 2건이 더해져 **4절을 미결에서 확정으로 교체**했다. 그에 맞춰 2(d)·3.2·3.7·5절·6절을 고쳤다. '
    '1절(사실조사)·2절 (a)(b)(c)·3.1·3.3~3.6 은 그대로다.\n'
    '- 대상 트리: `refactor/viewer-split` @ `8d9ec74` (제품 버전 `0.2.104`, `apps/native_poc/src/product_version.hpp:12`).\n'
    '- **이 문서는 문서 산출물이다.** 제품 코드·CMake·gradle 변경 없음, 빌드·테스트 실행 없음. '
    '**설계 확정이지 배포 승인이 아니다** — 4.5 참조.'
))

# section 0 rule about pending decisions
pairs.append((
    '- Codex 확정을 기다리는 항목은 4절에 **Codex 결정 대기** 로만 둔다. 3절(설계 초안)은 그 결정에 의존하지 않는 부분만 담는다.',

    '- 4절은 **확정된 결정**이다(개정 전에는 "Codex 결정 대기" 였다). 출처를 **[Codex 확정]** / **[검증용 실측]** 으로 구분해 적는다.\n'
    '- **확정 ≠ 배포 승인.** 운영 서명키·운영 도메인·실배포는 전부 별도 승인 사항이며 4.5 에 분리해 두었다.'
))

# section 3 intro
pairs.append((
    '## 3. 설계 초안 — 결정 가능한 부분만\n\n'
    '4절의 미결(서명 체계, manifest 스키마, 서버 엔드포인트, Android 정책)에 **의존하지 않는** 부분만 적는다. '
    '미결에 걸리는 지점은 본문에서 "→ 4.x" 로 표시하고 비워 둔다.',

    '## 3. 설계\n\n'
    '이 절은 최초 작성 시 4절이 미결이라 "결정 가능한 부분만" 담았다. 개정 뒤에는 4절이 확정됐으므로 '
    '전송·서명에 걸리는 지점은 "→ 4.x" 로 **확정된 절을 가리킨다**(비워 둔 것이 아니다). '
    '3.2 는 Codex 확정(`/T` 폐기 + 정확 대상 종료 + job 수명 + 실행 위치를 함께)에 맞춰 다시 썼다.'
))

for old, new in pairs:
    assert old in doc, 'anchor not found: ' + old[:60]
    doc = doc.replace(old, new, 1)

# 3.1 Verify row: point at the now-decided 4.1 instead of "결정 대기"
old_v = '(iii) **출처 검증 → 4.1 Codex 결정 대기**'
new_v = '(iii) **출처 검증 — manifest detached 서명을 내장 공개키로 검증(4.1.2)**'
assert old_v in doc, 'Verify row anchor not found'
doc = doc.replace(old_v, new_v, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(doc)
print('header / section 0 / section 3 intro / 3.1 Verify updated')
