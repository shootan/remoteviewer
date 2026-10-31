import io

p = 'docs/구현계획.md'
d = io.open(p, encoding='utf-8').read()

old = '''- [ ] **실기 검증 필요(사용자 확인 항목)** — 실 HTTPS 왕복 · 실제 서비스/방화벽/시작메뉴 등록 · UAC 프롬프트 0회(호스트발)/1회(클라발) · `update_relaunch.cpp` 실행 실증 · 호스트/클라 UI 실동작. 전부 라이브 0.2.104 와 충돌하므로 설치본을 만든 뒤에만 가능
'''
new = '''- [x] 설계문서에 **"혼합 0" 방어 4요소 구분** 기록 완료 (history #449, 설계 3.8.1) — 재조회 없음 / 목록 전체를 덮는 서명 / 아티팩트별 size·sha256 / `releaseId` 키잉 + Swap 가드. 각각 **무엇을 막는지**를 적었다. "`releaseId` 가 혼합을 막는다" 로 요약하면 **나머지 셋 중 하나를 걷어내도 요약과 어긋나지 않아** 방어가 조용히 사라지기 때문
- [x] **실기 항목을 문서로 고정** 완료 (history #450) — `docs/수동확인_체크리스트.md` **UPD-FIELD-01~06**. A2A 보고에만 있으면 세션이 끝날 때 사라진다

### 업데이트 기능 — 격리 상태에서의 최종 상태 (2026-09-08)

**완료(코드 + 회귀)**: 버전비교 공유벡터(3런타임) · payload 이름 검증 + manifest 경로 연결 · manifest 스키마 2 `artifacts[]`(3런타임 동일 한계) · HTTPS 정책(평문 리다이렉트·URL 자격증명 거부) · ECDSA P-256 서명 검증 · 상태기계 + **Commit 지점** · 등록 공유단위(**버전을 인자로**) + 롤백 스냅샷 · **단일 릴리스 스냅샷 staging** · **실제 파일 회귀 4종** · 하위 폴더 목적지 · **롤백이 추가된 파일도 제거** · **Relaunch**(4종 분기, 클라는 셸 경유 비승격, 감독받는 자식은 안 띄움) · **HealthCheck**(재실행 전 offset 이후만) · 클라 시작 시 비동기 확인.
**C++ 업데이트 10종 729 checks / 0 failed** — release 80 · effects 147 · relaunch 57 · manifest 82 · state_machine 61 · http 36 · check 28 · payload_name 54 · version_compare 139 · install_registration 45. JS 디렉터리 스위트 exit 0, Kotlin 11 tests.

**격리로 인해 하지 못한 것**: 실 HTTPS 왕복 0회 · 실제 서비스/방화벽/시작메뉴 등록 0회 · 관리자 권한 실증 없음 · `update_relaunch.cpp` **실행 0회**(일부러 어디에도 링크하지 않음) · UI 실동작 0회. 전부 `docs/수동확인_체크리스트.md` UPD-FIELD-01~06 으로 옮겼다.

**다음 단계는 구현이 아니라 배포 준비다**: 버전 인상 + 설치본 생성 → 설치 → UPD-FIELD 실기. **사용자 승인 뒤 한 번에** 한다(원장: 커밋마다 설치본을 만들지 않는다).
'''
assert old in d, 'plan anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()
entry = '''
### 450) 2026-09-08 업데이트 기능 — 격리 범위 마무리: 실기 항목을 문서로 고정하고 최종 상태 정리
- 새 기능 없음. **검증용이 지시한 마무리 3건**만 수행했다.
- **① "혼합 0" 방어 4요소 구분** — 이미 #449 (설계 3.8.1) 로 들어가 있어 확인만 했다.
- **② 실기 항목을 문서로 고정** — `docs/수동확인_체크리스트.md` 에 **UPD-FIELD-01~06** 추가. **A2A 보고에만 있으면 세션이 끝날 때 사라진다**는 지적이 정확했다. 각 항목에 실행 환경·통과 기준·출처를 붙였다:
  - **UPD-FIELD-01 (P0) 셸 경유 비승격 재실행** — 클라이언트 무결성 수준이 **Medium** 이어야 한다(High 면 실패). Explorer 없는 세션·다중 데스크톱 거동과 **실패 시 "띄우지 않음"** 이 사용자에게 어떻게 보이는지도 기록.
  - **UPD-FIELD-02 (P0) UAC 프롬프트 횟수** — **호스트발 0회 / 클라발 정확히 1회**, 취소 시 교체 안 하고 계속 사용 가능.
  - **UPD-FIELD-03 (P1) 실 HTTPS 왕복** · **04 (P1) 실제 등록 4단계** · **05 (P1) `update_relaunch.cpp` 실행 실증** · **06 (P2) UI 실동작**.
  - 문서에 **자동 회귀로 대체할 수 없는 이유가 항목마다 다르다**는 점을 적었다 — 01·05 는 **코드가 일부러 테스트에 링크되지 않기 때문**(실제 프로세스를 만들고 SCM 을 건드리는 코드가 테스트 바이너리에 있으면 그 테스트는 남의 세션을 망가뜨릴 수 있는 테스트가 된다), 02 는 OS 동작이라 관측 외 방법이 없고, 03·04 는 주입으로 대체한 대가, 06 은 사람이 봐야 하는 것.
- **③ 최종 상태 정리** — `docs/작업목록.md` 1.9 와 `docs/구현계획.md` 에 완료분·미검증분을 한 곳에 모았다. C++ 업데이트 **10종 729 checks / 0 failed**(release 80 · effects 147 · relaunch 57 · manifest 82 · state_machine 61 · http 36 · check 28 · payload_name 54 · version_compare 139 · install_registration 45).
- **다음 단계는 구현이 아니라 배포 준비**임을 명시했다: 버전 인상 + 설치본 생성 → 설치 → UPD-FIELD 실기. **사용자 승인 뒤 한 번에** 한다.
- 문서만 변경. 제품 코드·CMake·빌드·테스트 변경 없음. 버전 인상·설치본·설치·라이브 조작·배포·push 없음.
- 변경 파일: `docs/수동확인_체크리스트.md` · `docs/작업목록.md` · `docs/구현계획.md` · `docs/history.md`.
'''
d = d.rstrip('\n') + '\n' + entry
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('plan and history 450 written')
