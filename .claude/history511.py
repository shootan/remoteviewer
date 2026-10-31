# -*- coding: utf-8 -*-
import io

p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()

entry = u'''
### 511) 2026-09-11 `0.2.111` — **버전 전용 인앱 시험 릴리스** (기능 변경 0)
**부트스트랩이 끝났다.** 검증용 실측: 설치본 `C:\\Program Files\\GNLink\\GNLinkUpdater.exe` = **538,624B `0ac7aea84d5268aa…`**(09-11 09:55) — **수정된 업데이터가 실제로 설치돼 있다.** 사용자가 0.2.110 을 손으로 깔았다.
그런데 **설치 0.2.110 + 게시 0.2.110 은 `is_newer=false` 라 아무 일도 일어나지 않는다.** 인앱 경로를 실기로 태우려면 **더 높은 버전 하나**가 필요하다 — 그것이 이 릴리스의 전부다.

**변경 범위**: `product_version.hpp` 의 **버전 상수 한 줄**(`0.2.110` → `0.2.111`). 기능 추가·리팩터·무관 fix **없음**. **Android `0.2.16` 무변경.** 기존 0.2.110 파일·서명 **보존**(새 버전 디렉터리).

**산출물** (빌드 commit **`8e2ab0f`** 기준 — 아래 커밋)
| 파일 | 크기 | sha256 |
|---|---|---|
| `GNLinkHost.exe` | 660,480 | `97bb02586b07de24…` |
| `GNLinkStream.exe` | 1,000,448 | `a23a656cd2518799…` (110 과 동일) |
| `GNLinkInputService.exe` | 226,304 | `1fd2aa569880cbe9…` (동일) |
| `GNLinkCapture.exe` | 127,488 | `253734df347adb2b…` (동일) |
| `GNLinkClient.exe` | 707,584 | `ab9202f6d89ec0c8…` |
| `GNLinkViewer.exe` | 867,328 | `82b79c2046ed37f8…` (동일) |
| `GNLinkSetup.exe` | 4,358,144 | `bde79817af80a280…` |
| `GNLinkUpdater.exe` | 538,624 | `3022e9b0e9cdde85…` |
| `ui\\shell.html` | 12,648 | `15aa648adf28a6a2…` (동일) |
| `ui\\macro.html` | 10,996 | `f589f5df16d24fc8…` (동일) |
- manifest **1,687B `1cf08822c0a84f8d…`**(artifact 10줄) · `windows.sig` 파일 `adfa79e62c0b2c4b…`
- ⚠️ **버전 상수를 임베드하는 바이너리만 해시가 바뀌었다**(Host·Client·Updater·Setup). Stream·Capture·InputService·Viewer·html 2개는 **110 과 바이트 동일**하다 — 새 버전 디렉터리이므로 불변성 가드와 무관하다.

**검사**
- preflight **7번 게이트**: `payload set    the manifest names everything the update replaces` · `artifacts 10/10 match the manifest, byte for byte`
- 제품 C++ `default_verifier()` — **게시 전 문서** Ok / 1바이트 변조 SignatureInvalid, **게시 후 서버에서 되받은 바이트**로 다시 Ok(`version=0.2.111 releaseId=r-0.2.111 artifacts=10`). 되받은 pair 는 서명본과 **바이트 동일**.
- 설치기 임베드 대조 **9/9**(`verify_installer.ps1`, RCDATA 를 바이트로 해시). Setup 자신은 자기를 임베드할 수 없으므로 9개이고, manifest 의 10번째가 Setup 이다.
  ⚠️ 처음엔 **2건 FAIL** 이었다 — `-PayloadDir` 을 설치 이름 레이아웃(`ui/`)으로 줬는데 설치기는 **평면**(`shell.html`)으로 임베드한다. **도구 인자를 잘못 준 것이지 결함이 아니다**(0.2.108 때와 같은 자리). 두 html 은 양쪽 경로에서 **바이트 동일**함을 따로 확인했다.
- 버전 민감 스위트(Release): `updater_assembly` 49 · `update_release` 80 · `update_effects` 210 · `version_compare` 139 · `update_manifest` 85 · `update_stop_process` 15 — 전부 rc=0. **제품 로직 무변경이라 전체 스위트는 반복하지 않았다.**
- **외부 공개 URL 10개 전부 200 + size·hash 일치.**
- **재시작 0 · env 0 · 동시 배포 가드·동일버전 다른바이트 가드 준수.**

**⭐ 백업이 이제 정상이다** — `manifest-backups/0.2.110/` = `7c030901…`(manifest) + `142e0a7e…`(sig), **artifact 10개**. #510 에서 *"맨 이름 백업은 전부 swap 을 끝낼 수 없다"* 고 적었는데, **이번 백업부터는 맨 이름도 유효한 롤백 지점**이다. 7번 게이트가 앞으로 그 형태를 막는다는 예상이 실제로 그렇게 됐다.
현재 백업: `0.2.108`(9, 무효) · `0.2.109`(9, 무효) · `0.2.109-20260910T151705Z`(10) · **`0.2.110`(10, 유효)**.

**미검증 — 이 릴리스의 목적 그 자체**
**인앱 업데이트 완주는 아직 확인되지 않았다.** ⚠️ **화면 표시만으로 성공을 단정하지 않는다.** 판정은 사용자가 0.2.110 Host 에서 업데이트 확인을 직접 실행한 뒤 `updater.log` 의 **Req → Quiesce → Swap 10파일 → Register → RequiredHealth → Commit → Optional** 과 **설치된 version · `GNLinkUpdater.exe` 바이트**로 한다.
그 외: 언인스톨 항목 실제 갱신 · 별건 0.0.4~0.0.7.
**하지 않은 것**: 앱 임의 설치·종료 없음 · **업데이트 버튼 실행 없음** · `git push` 없음 · 광역 cleanup 없음 · NAS 다른 서비스 변경 없음 · APK 무변경 · 새 서명키 없음.
'''

d = d.rstrip('\n') + '\n' + entry
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok history')

p2 = 'docs/작업목록.md'
t = io.open(p2, encoding='utf-8').read()
old = u'''0.0 [~] **0.2.109 실기 대기**'''
new = u'''0.0 [~] **0.2.111 인앱 시험 대기** — 부트스트랩 완료(설치본 `GNLinkUpdater.exe` = 538,624 `0ac7aea8…`, 수정본). **0.2.111 게시 완료**(버전 전용, 기능 변경 0, manifest `1cf08822…` artifact 10, 공개 URL 10/10). **남은 것은 사용자가 0.2.110 Host 에서 "업데이트 확인" 을 직접 실행하는 것** → `updater.log` 의 Req/Quiesce/Swap10/Register/RequiredHealth/Commit/Optional 과 설치 version·Updater 바이트로 판정. ⚠️ **화면 표시만으로 성공 단정 금지** (history #511)

0.0 [~] **0.2.109 실기 대기**'''
assert t.count(old) == 1, 'tasklist'
t = t.replace(old, new, 1)
io.open(p2, 'w', encoding='utf-8', newline='').write(t)
print('ok tasklist')

p3 = 'docs/구현계획.md'
q = io.open(p3, encoding='utf-8').read()
q = q.rstrip('\n') + u'''

## 인앱 업데이트 실기 (2026-09-11) — 상태

- [x] 업데이터 종료·relaunch 경로 수정 1~4 (history #503·#504)
- [x] 전달 경로 5·6·7 — Updater 자기 교체 / ready 전 required-set / 게시 전 차집합 거부 (history #501)
- [x] 부트스트랩 — 0.2.110 수동 설치로 수정된 업데이터가 설치됨(538,624 `0ac7aea8…`)
- [x] 시험용 상위 버전 게시 — **0.2.111**, 버전 전용 (history #511)
- [ ] **인앱 완주 판정** — 사용자가 업데이트 확인 실행 → `updater.log` 전 구간 + 설치 version·Updater 바이트
- [ ] 언인스톨 항목 실제 갱신 확인
'''
io.open(p3, 'w', encoding='utf-8', newline='').write(q)
print('ok plan')
