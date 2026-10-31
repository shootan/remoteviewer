import io

p = 'docs/수동확인_체크리스트.md'
d = io.open(p, encoding='utf-8').read()

# The lead line now says which of the two categories these rows are.
old = ('**UPD-FIELD-01~06 공통 전제**: 여섯 항목 모두 **설치본을 만들어 실제로 설치한 뒤에만** 확인할 수 '
       '있다. 지금 빌드한 바이너리를 그대로 실행하면 이 PC 에서 돌고 있는 **라이브 0.2.104 와 충돌한다.** '
       '버전 인상·설치본 생성은 **사용자 승인 뒤 한 번에** 한다.')
new = ('''> ⚠️ **2026-09-08 정정.** 아래 UPD-FIELD 행들은 원래 "구현했으나 실기를 못 했다" 로 적혀 있었다.
> **사실이 아니었다.** `run_update` 의 제품 호출자가 0건이고, 업데이터 실행 파일 자체가 없으며,
> Android 는 파싱 코드에 호출자가 없다 — 즉 **아직 배선되지 않았다.** 두 가지는 성질이 전혀 다르다:
> 하나는 사람이 확인만 하면 되는 것이고, 다른 하나는 **아직 만들어야 하는 것**이다. 남은 배선은
> `docs/업데이트_배선_계획.md` 에 있다.
>
> 그래서 각 행에 **[배선 필요]** / **[구현됨·실기 대기]** 를 붙였다. **[배선 필요]** 인 항목은
> 배선이 끝나기 전에는 실기 자체가 불가능하다.

**UPD-FIELD-01~06 공통 전제**: 여섯 항목 모두 **설치본을 만들어 실제로 설치한 뒤에만** 확인할 수 '''
       '''있다. 지금 빌드한 바이너리를 그대로 실행하면 이 PC 에서 돌고 있는 **라이브 0.2.104 와 충돌한다.** '''
       '''버전 인상·설치본 생성은 **사용자 승인 뒤 한 번에** 한다.''')
assert old in d, 'lead anchor'
d = d.replace(old, new, 1)

# Tag each row.
tags = [
    ('| UPD-FIELD-01 | P0 | **셸 경유 비승격 재실행**',
     '| UPD-FIELD-01 | P0 | **[배선 필요]** **셸 경유 비승격 재실행**'),
    ('| UPD-FIELD-02 | P0 | **UAC 프롬프트 횟수**',
     '| UPD-FIELD-02 | P0 | **[배선 필요]** **UAC 프롬프트 횟수**'),
    ('| UPD-FIELD-03 | P1 | **실 HTTPS 왕복**',
     '| UPD-FIELD-03 | P1 | **[일부 배선 필요]** **실 HTTPS 왕복**'),
    ('| UPD-FIELD-04 | P1 | **실제 등록 4단계**',
     '| UPD-FIELD-04 | P1 | **[배선 필요]** **실제 등록 4단계**'),
    ('| UPD-FIELD-05 | P1 | **`update_relaunch.cpp` 실행 실증**',
     '| UPD-FIELD-05 | P1 | **[배선 필요]** **`update_relaunch.cpp` 실행 실증**'),
    ('| UPD-FIELD-06 | P2 | **UI 실동작**',
     '| UPD-FIELD-06 | P2 | **[일부 배선 필요]** **UI 실동작**'),
]
for old_t, new_t in tags:
    assert old_t in d, old_t[:40]
    d = d.replace(old_t, new_t, 1)

# The prose section made the wrong argument. Replace it with the corrected one.
start = d.find('## 업데이트 기능 실기 (UPD-FIELD-01~06) 에 대하여')
assert start != -1, 'prose section'
d = d[:start] + '''## 업데이트 기능 실기 (UPD-FIELD-01~06) 에 대하여

**먼저, 이 절이 원래 하던 주장은 틀렸다.** 여기에는 "이 항목들은 자동 회귀로 대체할 수 없다" 고
적혀 있었고, 특히 `update_relaunch.cpp` 가 **어디에도 링크되지 않은 것**을 격리의 대가로 정당화하고
있었다. 그것은 격리 품질의 증거가 아니라 **그 코드가 한 번도 실행되지 않았다는 자백**이다.

`CreateProcess` 자체는 라이브 파괴가 아니다. **dummy exe · 임시 설치 root · 임시 로그 경로를
주입하면 같은 OS 실행 경로를 실제로 검증할 수 있다.** 지금 안 되는 이유는 실행 코드가 제품 이미지
이름을 스스로 정하기 때문이고, 그건 받아들일 제약이 아니라 고칠 설계다. 방법은
`docs/업데이트_배선_계획.md` §2 에 있다.

정정 뒤 남는 것은 훨씬 좁다:

- **UAC 프롬프트 횟수**(UPD-FIELD-02) 는 OS 동작이라 관측 외에 방법이 없다.
- **서비스 등록·시작의 성공 경로**(UPD-FIELD-04·05 일부) 는 관리자 권한이 필요하다. 실패 경로는
  회귀로 덮을 수 있다.
- **사람이 봐야 하는 것**(UPD-FIELD-06 의 일부) — 알림이 실제로 읽히는지, 로그인 창을 막지 않는지.
- **운영 인증서 체인**(UPD-FIELD-03 의 일부) — 격리 서버로는 정상 체인과 실패 경로까지만 덮인다.

나머지는 **회귀로 덮어야 하고, 덮을 수 있다.**

**모든 UPD-FIELD 항목의 선행 조건**: `docs/업데이트_배선_계획.md` §1 의 배선이 끝나야 한다. 그
전에는 설치본을 만들어도 **업데이트를 시작할 수 없다** — `run_update` 를 부르는 코드가 없다.
버전 인상·설치본 생성은 **사용자 승인 뒤 한 번에** 한다.
'''
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('checklist corrected')
