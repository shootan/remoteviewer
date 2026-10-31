import io

p = 'docs/구현계획.md'
plan = io.open(p, encoding='utf-8').read()

pairs = [
    (
        '- [ ] Codex 확정 대기 4건 — 아티팩트 출처 검증(서명) · Android 서명·키 정책과 앱 데이터 보존 · manifest 스키마 · 서버 신규 엔드포인트',
        '- [x] Codex 확정 4건 반영 완료 (history #430) — B1 업데이트 전용 WinHTTP HTTPS 클라이언트 신설 · '
        'B2 manifest detached 서명 + 내장 공개키(ECDSA P-256+SHA-256 기본, RSA-2048 PSS 대안, Ed25519 제외) · '
        'B3 Android 동일키 in-place 업데이트(데이터 보존) · manifest 스키마와 서버 엔드포인트 확정. '
        '**설계 확정이며 배포 승인 아님**(운영키·운영 도메인·실배포는 별도 승인)'
    ),
    (
        '- [ ] step2 `compare_versions()` 공용 헤더 추출 + 단위 테스트 (현재 테스트 0건)',
        '- [ ] step2 버전 비교 **계약 + 3개 런타임(C++/Kotlin/JS) 공통 테스트 벡터** — 헤더 공유는 불가하므로 '
        '"공용 헤더 추출" 이 아니라 계약과 공유 벡터로 일치를 보장한다. 추출은 **C++ 안에서만** (현재 C++ 테스트 0건)'
    ),
    (
        '- [ ] 미확인 6건 해소 — NAS 서버 TLS 가동 여부 · `taskkill /T` 열거 의미론 실행 검증 · 스테이징 디렉터리 ACL 상속 실측 · '
        '클라 셸 백그라운드 훅 · Android Kotlin 네트워크 코드 · 설치본 잔존 파일',
        '- [~] 미확인 6건 — **1번 해소**(검증용 실측: nginx :443 + Let\'s Encrypt 보유, 29180/8080 프록시 vhost 없음, '
        'TLS env 미설정 → HTTPS 추가는 vhost 설정 작업). **2번 위험 하락**(3.2 가 `/T` 폐기·정확 대상 종료로 재작성돼 '
        '`taskkill /T` 의미론이 설계 전제에서 빠짐). 남은 것: 스테이징 디렉터리 ACL 상속 실측 · 클라 셸 백그라운드 훅 · '
        'Android Kotlin 네트워크 코드 · 설치본 잔존 파일'
    ),
]

for old, new in pairs:
    assert old in plan, 'anchor not found: ' + old[:50]
    plan = plan.replace(old, new, 1)

# new follow-up items from the confirmed design
anchor = '- [ ] step2 상태기계 seam 설계와 결정론 회귀 (교체 실패·트리 종료·재실행·상호배제)'
assert anchor in plan
plan = plan.replace(
    anchor,
    anchor + '\n'
    '- [ ] step2 정지 방식 재설계 — `/T` tree kill 폐기, 교체 대상 6개 exe 인스턴스를 **정확히 지목해 종료**, '
    'job 수명 이용(Stream→Capture KILL_ON_JOB_CLOSE), 업데이터는 어떤 job 에도 미소속\n'
    '- [ ] step2 Android — `signingConfigs` 명시(동일 debug 키 유지), `REQUEST_INSTALL_PACKAGES` + 승인·취소 경로, '
    '설치 방식 (A)파일 Intent / (B)PackageInstaller 세션 선택 → 그에 따라 FileProvider 필요 여부 결정, '
    'versionCode 는 실배포 최고값 확인 후 증가\n'
    '- [ ] **배포 단계 별도 승인 사항**(구현과 분리) — 운영 서명키 생성·보관·교체 절차 · Android keystore 백업·보관 위치 · '
    '운영 도메인과 nginx vhost 설정 · 실서버 배포',
    1)

io.open(p, 'w', encoding='utf-8', newline='').write(plan)
print('구현계획.md updated')
