import io

BS = chr(92)
p = 'docs/업데이트_기능_설계.md'
d = io.open(p, encoding='utf-8').read()

anchor = '''버전 일관성 검사(`file_contains_utf16_version`)는 **Setup 계열 이름에만** 적용한다.'''
assert anchor in d, '3.8 anchor'

section = '''#### 3.8.1 "혼합 0" 은 네 장치가 함께 만든다 — 무엇이 무엇을 막는지

이 절을 **"`releaseId` 가 혼합을 막는다"** 로 요약하면 안 된다. 그렇게 요약해 두면 나중에 누군가
나머지 셋 중 하나를 걷어내도 요약과 어긋나지 않고, **방어가 조용히 사라진다.** 각 장치가 정확히
무엇을 막는지 적어 둔다.

| 장치 | 정확히 무엇을 막는가 | 없으면 무슨 일이 일어나는가 | 증거 |
|---|---|---|---|
| **재조회를 하지 않는다** | 업데이터가 파일마다 "지금 latest" 를 다시 묻는 것 | 서버가 도중에 움직이면 **그 자체로** 절반씩 섞인 설치가 만들어진다 | `fetches == 3` (R1) |
| **서명이 목록 전체를 덮는다** | 이름을 다른 해시와, 해시를 다른 URL 과 짝짓는 것 | 목록의 일부만 진짜인 문서가 통과한다 | manifest 스위트 |
| **아티팩트별 size/sha256 대조** | 약속된 것과 **다른 바이트**가 staging 에 들어오는 것 | 같은 URL 에 다른 내용을 내주는 서버를 못 걸러낸다 | R3 의 실제 실패 사유가 `size mismatch for ui\\shell.html` |
| **`releaseId` 로 키잉된 staging + `Swap` 진입 가드** | **시도를 가로질러** 섞이는 것 — 절반 받은 A 를 B 의 파일로 완성하는 것 | 두 릴리스가 같은 staging 자리를 쓰고, 교체가 어느 쪽인지 모른 채 진행된다 | A·B 를 연달아 받으면 **두 디렉터리에 6파일**, A 바이트 미덮임 |

특히 헷갈리기 쉬운 지점: **R3 에서 실제로 거부를 만드는 것은 `releaseId` 가 아니라 해시 대조다.**
서버가 첫 파일 뒤 릴리스 B 로 옮겨가 같은 URL 에 다른 바이트를 내놓을 때, 그것을 잡는 것은 검증된
manifest 가 못 박아 둔 크기·해시다. `releaseId` 는 **그 시도 안에서** 무엇을 막는 장치가 아니라,
**시도와 시도 사이에** 절반 남은 것이 다른 릴리스로 완성되지 않게 하는 장치다.

'''
section = section.replace('\\\\shell', BS + BS + 'shell')

d = d.replace(anchor, section + anchor, 1)
d = d.replace('코드를 꺼면 실패한다.', '코드를 끄면 실패한다.', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('3.8.1 recorded')
