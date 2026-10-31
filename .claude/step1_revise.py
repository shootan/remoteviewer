import io

p = 'docs/업데이트_기능_설계.md'
doc = io.open(p, encoding='utf-8').read()
new4 = io.open('.claude/section4_new.md', encoding='utf-8').read().rstrip('\n')

# --- replace the whole of section 4 (from its heading to the '## 5.' heading) ---
start = doc.index('## 4. Codex 결정 대기')
end = doc.index('## 5. 미확인 항목')
# keep the '---\n\n' separator that precedes '## 5.'
tail_sep = doc[:end].rstrip()
sep = '\n\n---\n\n' if tail_sep.endswith('---') else '\n\n---\n\n'
doc = doc[:start] + new4 + sep + doc[end:]
print('section 4 replaced')

# --- 5절 미확인: resolve #1, lower risk on #2 ---
old1 = ('1. **현 NAS 디렉터리 서버가 TLS 를 켜고 실행 중인지** — `REMOTE60_DIR_TLS_KEY/CERT` 는 배포 환경변수라 '
        '저장소 코드로는 알 수 없다. `automation/deploy_directory.ps1` 의 유닛 정의는 이번 조사 범위에서 열지 않았다.')
new1 = ('1. ~~현 NAS 디렉터리 서버가 TLS 를 켜고 실행 중인지~~ — **해소됨(4.6).** 검증용 Claude 의 read-only 실측: '
        'nginx 가 :443 LISTEN + Let\'s Encrypt 인증서 보유, 그러나 29180/8080 프록시 vhost 없음, '
        '`REMOTE60_DIR_TLS_KEY/CERT` 미설정 → 디렉터리는 :29180 평문 노출. '
        'HTTPS 추가는 인증서 확보가 아니라 nginx vhost 설정 작업이다.')
assert old1 in doc, 'unconfirmed item 1 not found'
doc = doc.replace(old1, new1, 1)

old2 = ('2. **`taskkill /T` 의 자식 열거 시점 의미론** — 문서상 "프로세스와 그 자식 트리" 이지만, '
        '실행 순간 살아있는 부모 사슬만 따라간다는 3.2 (D1) 의 전제는 **코드가 아니라 플랫폼 동작에 대한 판단**이다. '
        '실행 검증(테스트)으로 확정해야 한다.')
new2 = ('2. **`taskkill /T` 의 자식 열거 시점 의미론** — 여전히 코드가 아닌 플랫폼 동작에 대한 판단이라 확정하지 않았다. '
        '**다만 설계 위험은 크게 낮아졌다**: 3.2 가 Codex 확정에 따라 `/T` 를 폐기하고 **종료 대상을 정확히 지목**하는 방식으로 '
        '다시 쓰였으므로, 이 의미론은 더 이상 설계의 전제가 아니다. '
        '남은 관련성은 (기존 설치기의 `stop_running_product()` 를 그대로 쓰는 경로가 남을 경우에 한한) 부수적인 것이다.')
assert old2 in doc, 'unconfirmed item 2 not found'
doc = doc.replace(old2, new2, 1)

old3 = ('3. **`%ProgramFiles%\\GNLink.update\\` 의 실제 ACL 상속** — `%ProgramFiles%` 하위가 관리자 전용이라는 것은 '
        '설치기 주석의 근거(`installer_main.cpp:1-13`)와 일반적 Windows 동작이지만, 새로 만든 하위 디렉터리의 상속 결과를 '
        '이번에 실측하지 않았다.')
assert old3 in doc, 'unconfirmed item 3 not found'

# --- 3.7: version-comparison harness now follows the contract + vectors model ---
old7 = ('- **버전 비교**: 2(d) 의 추출을 전제로 `compare_versions` 단위 테스트를 새 타깃으로 추가한다. '
        '현재 테스트 0건이므로 경계값(성분 개수 불일치, 접미사, 큰 성분)이 전부 신규 커버리지다.')
new7 = ('- **버전 비교**: 2(d) 확정대로 **하나의 공통 테스트 벡터 파일**을 두고 C++·Kotlin·JS 세 런타임의 테스트가 '
        '각각 그것을 읽어 같은 답을 내는지 확인한다(헤더 공유가 불가하므로 일치는 벡터로 보장한다). '
        '현재 C++ 쪽 테스트가 0건이므로 경계값(성분 개수 불일치, 선행 0, 비숫자 접미사, 성분 오버플로)이 전부 신규 커버리지다.')
assert old7 in doc, '3.7 version-comparison bullet not found'
doc = doc.replace(old7, new7, 1)

# --- 6절: reflect that section 4 is now decided ---
old6 = '- 4절 네 항목의 Codex 확정을 받은 뒤 이 문서의 해당 절을 채운다.'
new6 = ('- ~~4절 네 항목의 Codex 확정을 받은 뒤 이 문서의 해당 절을 채운다.~~ **완료(4절).** '
        '남은 것은 구현이며, 배포는 4.5 의 별도 승인 사항이다.')
assert old6 in doc, 'section 6 bullet not found'
doc = doc.replace(old6, new6, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(doc)
print('5절/3.7/6절 updated')
