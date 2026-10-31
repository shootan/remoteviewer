import io

BS = chr(92)
p = 'docs/업데이트_기능_설계.md'
d = io.open(p, encoding='utf-8').read()

anchor = '### 3.11 Relaunch — 무엇을 다시 띄우는가 (2026-09-08 구현)'
assert anchor in d, '3.11 anchor'

section = '''### 3.14 서버가 내보내는 것 ↔ 세 런타임이 읽는 것 (2026-09-08, W8)

세 런타임이 **공유 벡터**와 일치한다는 것은 이미 각자의 스위트가 보인다. 그것이 보이지 **않는** 것은
**서버가 실제로 발행하는 문서**가 그 벡터와 같은 모양이냐다. 여기가 어긋나면 **세 스위트가 전부
초록인 채로 현장에서는 아무것도 설치되지 않는다.**

| 필드 | 서버 발행 (`update_manifest.js` `buildManifest`) | C++ 이 쓰는 곳 | Kotlin 이 쓰는 곳 | 없으면 |
|---|---|---|---|---|
| `schema` | 항상 `2` | `load_manifest` 가 다르면 `UnsupportedSchema` | 동일 | 문서를 아예 안 읽는다 |
| `releaseId` | 필수 | **staging 디렉터리 키**(`staging_dir_for`) · `Swap` 진입 가드 | (미사용) | **시도 간 릴리스 격리가 사라진다**(3.8.1) |
| `platform` | 필수 | 호출자 플랫폼과 대조 | `load(..., "android")` | 남의 플랫폼 릴리스를 설치한다 |
| `arch` | 필수 | `expectedArch` 대조 | 동일(기본 `arm64`) | 다른 아키텍처를 설치한다 |
| `version` | 필수 | `compare_versions` · **등록의 `DisplayVersion`** · health 대조 | 표시용 | 업데이트 여부를 못 정한다 |
| `artifact` (반복) | 항목당 한 줄, **주어진 순서 그대로** | 파일별 다운로드·크기/해시 검증·교체 목적지 | APK 선택 + 다운로드 검증 | 받을 것이 없다 |
| ┗ `name` | 상대 경로(구분자 포함 가능) | payload 이름 검사 → 교체 목적지 | `.apk` 판별 | 어디에 놓을지 모른다 |
| ┗ `size` | 바이트 | 다운로드 상한 + 검증 | 동일 | 무한정 받는다 |
| ┗ `sha256` | **소문자 64자** | 검증 | 검증 | **서명이 파일이 아니라 문서에 대해서만 말하게 된다** |
| ┗ `url` | https 전용 | `https_get_file` | `HttpsURLConnection` | 받을 곳을 모른다 |
| `versionCode` | **android 릴리스에만** | (미사용) | **설치 가부의 권위** — Android 가 이것으로 판단 | **승인 뒤에 시스템이 거절한다** |

**표가 아니라 테스트가 이것을 지킨다.** `apps/directory/test/update_publish_contract_test.js` 가
`buildManifest` 의 출력 키 집합과 **공유 벡터의 키 집합이 같은지**를 기계적으로 비교하고, 서버가
자기 출력을 되읽어 각 필드가 살아남는지 확인한다. 문서 표는 낡을 수 있지만 **이건 깨진다.**

특히 고정해 둔 두 가지:
- **이름 안의 구분자가 뭉개지지 않는다** — `ui@@shell.html` 이 `uishell.html` 로 오면 다른 곳에 쓴다.
- **windows 릴리스에는 `versionCode` 줄이 없다** — 없는 것이 0 으로 읽혀 실제 값처럼 다뤄지면 안 된다.
  그리고 android 문서에서 `versionCode` 는 **artifact 줄들 뒤에** 나온다(key=value 에 순서 규칙이
  없고 세 파서 모두 줄 단위로 읽는다). 첫 artifact 줄에서 멈추는 파서였다면 이걸 잃고 **Android 는
  아무것도 설치하지 않는다.**

'''
section = section.replace('@@', BS)
d = d.replace(anchor, section + anchor, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('3.14 tracking table recorded')
