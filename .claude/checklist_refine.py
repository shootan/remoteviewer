import io

p = 'docs/수동확인_체크리스트.md'
d = io.open(p, encoding='utf-8').read()

# Method column gains the conditions that decide the outcome; 근거 gains commit hashes.
rows = [
    # (old fragment, new fragment)
    ('| UPD-FIELD-01 | P0 | **셸 경유 비승격 재실행** — 업데이트 뒤 `GNLinkClient.exe` 가 관리자가 아닌 보통 사용자 토큰으로 뜨는가 '
     '| 설치본 설치 후 실제 업데이트 1회. 작업 관리자 "권한 상승됨" 열 또는 Process Explorer 무결성 수준 '
     '| 클라이언트 무결성 수준 **Medium**(High 면 실패). Explorer 없는 세션·다중 데스크톱에서의 거동과, 실패 시 동작(**띄우지 않음**)이 사용자에게 어떻게 보이는지도 함께 기록 '
     '| `docs/업데이트_기능_설계.md` 3.5·3.11, history #448 | [ ] |',
     '| UPD-FIELD-01 | P0 | **셸 경유 비승격 재실행** — 업데이트 뒤 `GNLinkClient.exe` 가 관리자가 아닌 보통 사용자 토큰으로 뜨는가 '
     '| 설치본 설치 후 실제 업데이트 1회. 작업 관리자 "권한 상승됨" 열 또는 Process Explorer 무결성 수준. **조건을 나눠 각각 1회**: ⓐ 평소 데스크톱 ⓑ **Explorer 가 죽어 있는 세션** ⓒ **다중 데스크톱/다중 세션** '
     '| ⓐ 클라이언트 무결성 수준이 **Medium**(**High 면 실패** — 승격 토큰을 물려받았다는 뜻). ⓑⓒ 셸 경로가 없으면 **클라이언트가 뜨지 않아야** 하고 **승격된 채로 뜨면 실패**. 그때 사용자에게 무엇으로 보이는지(창이 안 뜸/무반응/로그만)와 시작 메뉴로 직접 실행하면 정상인지까지 기록 '
     '| `09e6aee` (`update_relaunch.cpp` `launch_via_shell`), 설계 3.5·3.11, history #448 | [ ] |'),
    ('| `docs/업데이트_기능_설계.md` 3.5·3.6, history #443 | [ ] |',
     '| `09e6aee`·`c812dcd`, 설계 3.5·3.6, history #443 | [ ] |'),
    ('| `docs/업데이트_기능_설계.md` 4.1, history #446 | [ ] |',
     '| `44e869c` (스키마 2)·`09e6aee`, 설계 4.1, history #446 | [ ] |'),
    ('| `docs/업데이트_기능_설계.md` 3.6, history #445 | [ ] |',
     '| `9ee9a3b` (Commit 지점)·`5045802`, 설계 3.6, history #445 | [ ] |'),
    ('| `docs/업데이트_기능_설계.md` 3.11, history #448 | [ ] |',
     '| `09e6aee` (`update_relaunch.cpp` — **어디에도 링크되지 않은 채 커밋된 코드**), 설계 3.11, history #448 | [ ] |'),
    ('| `docs/업데이트_기능_설계.md` 3.5·3.13, history #441·#448 | [ ] |',
     '| `09e6aee` (`client_shell_main.cpp` `start_update_check`), 설계 3.5·3.13, history #441·#448 | [ ] |'),
]
for old, new in rows:
    assert old in d, old[:60]
    d = d.replace(old, new, 1)

# The precondition, stated once as a line right where the rows are, not only in the prose below.
anchor = '| UPD-FIELD-01 | P0 |'
lead = ('\n**UPD-FIELD-01~06 공통 전제**: 여섯 항목 모두 **설치본을 만들어 실제로 설치한 뒤에만** 확인할 수 '
        '있다. 지금 빌드한 바이너리를 그대로 실행하면 이 PC 에서 돌고 있는 **라이브 0.2.104 와 충돌한다.** '
        '버전 인상·설치본 생성은 **사용자 승인 뒤 한 번에** 한다.\n\n')
# put the lead just before the table header that precedes the rows
header = '| ID | 우선순위 | 수동 확인 항목 | 실행/환경 | 통과 기준 | 출처 | 상태 |'
assert header in d, 'table header'
d = d.replace(header, lead.lstrip('\n') + header, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('checklist refined')
