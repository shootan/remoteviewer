# -*- coding: utf-8 -*-
import io

p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()

entry = u'''
### 499) 2026-09-10 `0.2.109` 복구 게시 — **manifest 에 `GNLinkSetup.exe` 가 빠져 있었다**
검증용 실측: 서버 `0.2.109` 디렉터리에 **exe7 + ui2 = 9개**, `GNLinkSetup.exe` 없음. **공개 Setup URL 404.**

**왜 이게 빠지면 안 되나 — 제품 소스가 이미 그렇게 말하고 있었다**
```cpp
// update_process_targets.cpp:36  product_image_names()
// The installer counts too, now that it is a member of the update package.
L"GNLinkSetup.exe",
```
- 언인스톨 항목이 **`<installDir>\\GNLinkSetup.exe`** 를 가리킨다(`install_registration_test.cpp:9`). 업데이트가 앱만 바꾸고 이 파일을 안 바꾸면 **언인스톨러가 사라진 버전을 설명하게 된다.**
- `payload_name_test.cpp:49` 는 `GNLinkSetup.exe` 를 **Ok** 로 단정한다. `product_image_names()` 는 이미 그것을 **정지 대상**에 넣어 두었다.
→ 즉 **제품은 이미 그 파일이 패키지 멤버라고 전제하고 있었고, manifest 만 그렇지 않았다.** 이것은 결정이 아니라 **누락**이었다.

**게시한 것**: **version 은 `0.2.109` 유지**(기존 9개 바이트 불변 — 안 바뀐 것을 바꿨다고 말하는 번호는 증거가 아니다), **`releaseId` 만 `r-0.2.109` → `r-0.2.109-2`**. 두 리더 모두 releaseId 에 **비어 있지 않을 것** 외의 제약이 없다(`update_manifest.cpp:195`, `update_manifest.js:163`).
| | 값 |
|---|---|
| `windows.manifest` | 1,703B `1d5c11b580b2a28a…` (artifact **10줄**) |
| `windows.sig` | 128자, 파일 `80203ff67f85016f…` |
| 추가된 아티팩트 | `GNLinkSetup.exe` **4,352,000B** `f52ba327c6a897e1…` |
| 백업 (교체 전 원본 pair) | `/opt/gnlink/manifest-backups/0.2.109/` — `743fbb1f…` + `85fdc3c7…` + `SHA256SUMS` |
서명은 **최초 0.2.109 와 같은 도구·같은 키**(`.claude/sign_release_109.ps1`, `%LOCALAPPDATA%\\GNLink\\ReleaseSigning\\p256-a0e184579c5d4c2d`, mtime 무변경). **새 키 없음, 비밀키는 이 PC 밖으로 나가지 않았다.**

**🔴 게시 전에 스크립트 결함 1건을 고쳤다 — 지시받은 백업 단계가 보장되지 않고 있었다**
`backup_current_pair` 는 백업 경로를 **게시된 version 만으로** 만들었다(`manifest-backups/0.2.109/`).
- 1회차는 정상이다. **2회차가 문제**다 — 부분 실패 후 재실행하면 `cur_version` 이 다시 `0.2.109` 라 **`cp -p` 가 원본 pair 를 새 pair 로 덮는다.** 롤백 지점이 **롤백해야 할 대상 자체로** 바뀐다.
- ⚠️ **다른 모든 곳을 안전하게 만든 멱등성이 여기서는 정확히 반대로 작동한다.** 스크립트가 재실행을 권장하는 구조라 더 위험했다.
- 수정: 백업 디렉터리가 이미 있으면 **덮지 않고** `0.2.109-<UTC>` 로. **음성 대조**: 가드를 빼면 신규 회귀 2건이 FAIL 하고 백업이 실제로 새 manifest(`743fbb1f…`→`1d5c11b5…`)로 덮인다.

**dry-run 이 항상 exit 1 이던 것도 고쳤다**: 아직 올리지 않은 아티팩트는 당연히 404 인데 외부 검증이 그걸 실패로 셌다. ⚠️ **항상 1인 종료 코드는 신호이기를 그만둔다** — dry-run 에서는 외부 검증을 건너뛰고 **건너뛰었다고 말한다.**

**🔴 그리고 이번에 한계 하나를 없앴다 — `remote60_verify_release`(신규 도구)**
지금까지 C++ 쪽은 **fixture 만** 검증했다. 그건 *"박힌 키가 이 키의 서명을 받아들인다"* 를 증명하지 *"제품이 이 문서를 받아들인다"* 를 증명하지 않는다 — **다른 주장이다.** 이제 **제품이 쓰는 `load_manifest()` + `default_verifier()` + 컴파일된 키**로 **실제 문서**를 검증한다.
- `.claude/rel/0.2.109-r2` 문서 → **Ok**, 1바이트 변조 → **SignatureInvalid**.
- 게시 후 **서버에서 되받아온 바이트**로 다시 → **Ok**(`version=0.2.109 releaseId=r-0.2.109-2 artifacts=10`). 서버가 실제로 들고 있는 것을 **제품의 검증기가 받아들인다.**

**게시 검증 (전부 실행함)**
- **manifest ↔ 디스크 10/10** · **서버 10파일 size+sha256 10/10 일치**, 전부 world-readable(신규 644, 기존 755)
- **공개 URL 10개 전부 200 + 해시 일치** — `GNLinkSetup.exe` **404 → 200 f52ba327…**
- 서버에서 되받은 manifest·sig 가 서명한 것과 **바이트 동일**
- **production required-set 대조 기계적으로 실행**: `product_image_names()` **7/7** manifest 에 존재 · 설치기 payload **9/9** 존재(`.claude/check_release.py`, 목록을 소스에서 읽는다 — 손으로 옮긴 목록은 어긋난다)
- Node(서버 구현)·OpenSSL 도 실제 문서 수락 + 변조 거부
- **재시작 0 · env 변경 0 · 기존 9개 파일 무변경 · APK 무변경**

**부수효과(의도된 것으로 기록)**: 이제 **0.2.108 이하에서 올라오는 호스트가 `GNLinkSetup.exe` 를 받아 설치 디렉터리에 쓴다**(+4.35MB/회). 위의 언인스톨 항목 계약이 그것을 요구한다. **이미 0.2.109 인 호스트는 `isNewer` 가 false 라 아무 것도 하지 않는다.**
**미검증**: 실기 업데이트 완주(호스트가 실제로 이 manifest 를 받아 10개를 교체하는 것) · 인증 필요한 `/api/update/manifest` 응답 본문(무인증 **401** 은 잠금장치를 본 것이지 방 안을 본 것이 아니다) · 언인스톨 항목이 실제로 갱신되는지.
**하지 않은 것**: live 앱 설치·종료 없음 · 광역 cleanup 없음 · `git push` 없음 · 기존 버전 파일 덮어쓰기·삭제 없음 · nginx/systemd/env/ACL 무변경(NAS 에스컬레이션 불필요) · **파일명·RCDATA 확인만으로 완료 판정하지 않았다.**
'''

d = d.rstrip('\n') + '\n' + entry
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok history 499')
