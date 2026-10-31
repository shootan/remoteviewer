# -*- coding: utf-8 -*-
import io

p = 'docs/nas_release_handoff_2026-09-10.md'
d = io.open(p, encoding='utf-8').read()

pairs = [
 # ---- APK facts, read from the files
 (u'| `dist/GNLink-0.2.15.apk` | 11,518,126 | `9748a54eb0e845188558663665be8611c191b234a648e88e72c61866f6eaed99` |',
  u'| `dist/GNLink-0.2.15.apk` | 11,518,126 | `cd33168c49a13d09531a6c97dba2d14038e49e0f2475c21a1a22841c80e8548e` |'),
 (u'\n| `dist/GNLink-0.2.16.apk` | **11,846,438** | `c4077363a87fcf245dc13e5479fa52293bf1709b8d1e6d0761a0e347924f2d4f` |',
  u'\n| `dist/GNLink-0.2.16.apk` | 11,518,126 | `91c7f0c764fc0a53a2aac2ba7bc59a7c44cf58f78df975b91f2d5e3b8f098c6c` |'),
 (u'| `/updates/0.2.16/GNLink-0.2.16.apk` | `dist/GNLink-0.2.16.apk` | **11,846,438** | `c4077363a87fcf245dc13e5479fa52293bf1709b8d1e6d0761a0e347924f2d4f` |',
  u'| `/updates/0.2.16/GNLink-0.2.16.apk` | `dist/GNLink-0.2.16.apk` | 11,518,126 | `91c7f0c764fc0a53a2aac2ba7bc59a7c44cf58f78df975b91f2d5e3b8f098c6c` |'),
 # ---- android manifest hash changed with the APK
 (u'| `.claude/rel/0.2.108/android.manifest` | 326 | `78737f8bce97096f65b9fcdef8a2b0cdfd987b21efbfd1a9bd631ae940d9e13d` |',
  u'| `.claude/rel/0.2.108/android.manifest` | 326 | `dcc87276f0ac00cada35e5fe9184bc62a9ba9610f68a34b5a21bff208742432a` |'),
 # ---- staged layout now matches the URLs
 (u'| `/updates/0.2.108/ui/shell.html` | 〃 `shell.html` | 12,648 | `15aa648adf28a6a27b62a4e0548c425d6339ff2bd45ce700d3c4c79aeee4b4a4` |',
  u'| `/updates/0.2.108/ui/shell.html` | 〃 `ui/shell.html` | 12,648 | `15aa648adf28a6a27b62a4e0548c425d6339ff2bd45ce700d3c4c79aeee4b4a4` |'),
 (u'| `/updates/0.2.108/ui/macro.html` | 〃 `macro.html` | 10,996 | `f589f5df16d24fc8932d230da35f1661f0e97c1c63a6f2d43ad7a8fcc095529b` |',
  u'| `/updates/0.2.108/ui/macro.html` | 〃 `ui/macro.html` | 10,996 | `f589f5df16d24fc8932d230da35f1661f0e97c1c63a6f2d43ad7a8fcc095529b` |'),
 # ---- the "only the version differs" paragraph, now true of the files as well
 (u'''⚠️ **"버전 값뿐" 을 실제로 확인했다.** 두 APK 를 항목 단위로 비교하면 **내용이 다른 항목은 두 개**뿐이다 —
`AndroidManifest.xml`(versionCode·versionName)과 `classes3.dex`(`BuildConfig` 상수). 나머지 118개 항목은 **CRC 까지 같다.**
- 다만 **APK 파일 크기는 다르다**(11,518,126 vs 11,846,438). 압축 방식은 같고 **모든 항목의 원본 크기도 같으므로**,
  차이는 **내용이 아니라 아카이브 압축·배치**에서 온다. **크기가 같아야 한다고 기대하지 말 것.**''',
  u'''⚠️ **"버전 값뿐" 을 실제로 확인했다.** 두 APK 를 항목 단위로 비교하면 **내용이 다른 항목은 두 개**뿐이다 —
`AndroidManifest.xml`(versionCode·versionName)과 `classes3.dex`(`BuildConfig` 상수). 나머지 118개 항목은 **CRC 까지 같고**,
**파일 크기도 11,518,126 으로 같다.**
- ⚠️ 처음 만든 target APK 는 **11,846,438** 이었다. 328,312 바이트가 더 있었는데, **항목에 들어 있지 않았다** —
  `classes2.dex` 와 첫 네이티브 라이브러리 **사이의 빈 공간**이었다(증분 빌드가 남긴 죽은 바이트). 유효한 APK 이고 서명도
  통과했지만, **설명할 수 없는 바이트가 든 산출물**이라 **clean 빌드로 다시 만들었다.** 지금 것은 빈 공간 **0** 이다.
- 두 APK 에 **공개키가 실제로 들어 있다**(dex 문자열 검색으로 확인). baseline 에 키가 없으면 **사용자가 baseline 을 깔아도
  target manifest 를 검증하지 못한다** — 이 쌍의 전제다.'''),
]

for i, (old, new) in enumerate(pairs):
    assert d.count(old) == 1, 'pair %d: %s' % (i, old[:70])
    d = d.replace(old, new, 1)

# ---- a staging check the NAS can repeat
old = u'''- 경로 이름을 바꾸려면 **manifest 를 다시 만들고 다시 서명해야 한다** — URL 이 서명 대상 안에 있다.'''
new = u'''- 경로 이름을 바꾸려면 **manifest 를 다시 만들고 다시 서명해야 한다** — URL 이 서명 대상 안에 있다.
- ⚠️ **스테이징 배치는 URL 과 같은 모양이어야 한다.** `ui/` 두 개는 `payload/ui/` 아래에 있다.
  평면으로 두면 exe 7개는 받아지고 **html 2개만 404** 가 되며, 증상은 **다운로드 도중 실패**라 **서버 문제처럼 보인다.**
- **올린 뒤 대조**: manifest 의 모든 `artifact=` 줄을 **실제 파일의 크기·SHA256 과 다시 맞춰 볼 것.**
  ⚠️ **서명 검증은 이것을 대신하지 못한다** — 서명은 *"문서가 서명 이후 바뀌지 않았다"* 만 말하고,
  *"문서가 실제로 존재하는 파일을 가리킨다"* 는 말하지 않는다. 이 PC 에서도 그 둘이 갈린 적이 있다(9절).'''
assert d.count(old) == 1, 'staging note'
d = d.replace(old, new, 1)

# ---- section 9 additions
old = u'''| 전체 회귀 | C++ 62 스위트 `^PASS` 1733 · JS 335 · Android 68 |'''
new = u'''| **manifest ↔ 실제 파일 대조** | **10/10 일치**(크기·SHA256, 스테이징 경로 그대로). ⚠️ 이 검사가 없었을 때 **서명은 통과하는데 APK 해시가 낡은** 상태를 만들었다 — 서명 검증은 그것을 잡지 못한다 |
| baseline 에 공개키 존재 | 0.2.15 · 0.2.16 **둘 다 dex 안에 128자 존재**(0.2.14 에는 없다) |
| 두 APK 차이 | 내용이 다른 항목 **2개**(`AndroidManifest.xml` · `classes3.dex`), 나머지 118개 **CRC 동일**, 파일 크기도 동일 |
| 전체 회귀 | C++ 62 스위트 `^PASS` 1733 · JS 335 · Android 68 |'''
assert d.count(old) == 1, 'section 9'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('handoff fixed')
