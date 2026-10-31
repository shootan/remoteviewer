# -*- coding: utf-8 -*-
import io

p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()

entry = u'''
### 507) 2026-09-11 `0.2.110` 게시 — **업데이터가 처음으로 자기 자신을 담아 나간다**
1~7 이 전부 들어간 Windows 단독 후보. **Android 무변경**(APK 파일을 하나도 건드리지 않았다).

**바뀐 바이너리 — 수정이 실제로 들어갔다는 증거**
| | 0.2.109 | 0.2.110 |
|---|---|---|
| `GNLinkUpdater.exe` | 532,480 | **538,624** `0ac7aea84d5268aa…` ← 종료·relaunch 수정이 여기 있다 |
| `GNLinkHost.exe` | 660,480 | 660,480 `cfefaaefd20ec909…` |
| `GNLinkStream.exe` | 1,000,448 | 1,000,448 `a23a656cd2518799…` |
| `GNLinkSetup.exe` | 4,352,000 | **4,358,144** `3f0c3f5ca6b3fa42…` |
manifest **1,687B `7c030901b2c45645…`**(artifact **10줄**) · sig 파일 `142e0a7e1afccdcc…`

**⭐ 이번 manifest 는 목록을 스스로 적지 않는다**
`make_release_manifest_110.py` 는 이름을 **`product_payload_names()` 에서 읽는다.** 0.2.109 가 `GNLinkSetup.exe` 없이 나간 이유가 *"이 스크립트가 자기 사본을 들고 있었고 그 사본이 틀렸다"* 였다. 7번(게시 전 차집합 거부)이 반대편을 막고, 이건 같은 구멍을 **애초에 만들지 않는다.** 목록의 출처가 하나다.

**게시 검증**
- preflight 에서 **7번 게이트 통과**(`the manifest names everything the update replaces`)
- 제품의 C++ `default_verifier()` 로 **실제 문서** 검증 Ok / 1바이트 변조 SignatureInvalid
- manifest ↔ 디스크 10/10 · 서버 10파일 일치 · **공개 URL 10/10 익명 https 해시 일치**
- **재시작 0 · env 0 · 기존 버전 디렉터리 무변경**

**⭐ 백업 가드가 실제로 값을 했다**
```
a backup of 0.2.109 already exists; this one goes to 0.2.109-20260910T151705Z
backed up 0.2.109:  1d5c11b5…(r2 manifest)  80203ff6…(r2 sig)
```
`manifest-backups/0.2.109/` 의 **원본(최초 게시분)은 그대로 남고**, 이번에 교체되는 r2 짝은 타임스탬프 경로로 갔다. 가드가 없었다면 **롤백 지점이 r2 로 덮여** 최초 게시분으로는 돌아갈 수 없었다.

**🔴 게시하면서 또 한 건 — dry-run 이 하지도 않은 검사를 "전부 통과" 라고 보고했다**
`verify_on_server` 가 dry-run 에서 아직 없는 파일을 **건너뛰고도** 마지막에 *"all 10 artifacts on the server match"* 를 찍었다. **10개를 하나도 안 보고 10개가 맞다고 말한 것**이다.
→ **검사한 개수**를 세어 보고하도록 고쳤다: `0 of 10 artifacts checked on the server; 10 not present yet (dry run)`.
⚠️ **요약 줄이 방금 한 일을 설명하지 않으면 그건 요약이 아니다.** 이번 라운드에서 같은 형태를 세 번째 잡았다(테스트 detail 이 다른 시점을 읽은 것, 서명 변조가 변조하지 않은 것, 그리고 이것).

**버전 선택**: 검증용이 *"게시 후보 진행하세요"* 라고 두 번 승인했고 번호는 **다음 순번**이라 `0.2.110` 으로 했다. **Android 는 올리지 않았다** — 바뀐 것이 없는데 번호를 올리면 그 번호가 아무것도 증명하지 않는다(0.2.109 와 같은 판단).

**⚠️ 사용자에게 반드시 전할 순서 (검증용 요구)**
**새 버전을 게시해도 현재 설치된 0.2.108 업데이터로는 이 수정이 실행되지 않는다.** 그 업데이터에는 수정이 **하나도 없고**, 업데이터는 지금까지 **어떤 릴리스로도 교체된 적이 없다**(5번). **수동 설치 1회가 선행돼야** 그 다음부터 인앱 업데이트가 이 코드로 돈다. **그전까지 인앱 재시도 안내 금지.**

**남은 미검증**: 실기 완주 · 언인스톨 항목 실제 갱신 · **설치된 업데이터와 동일 조건의 10파일 업데이트**(bootstrap 선행 필요) · `gdi_capture_process`(#506, 별건).
'''

d = d.rstrip('\n') + '\n' + entry
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok')
