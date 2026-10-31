import io

BS = chr(92)
p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()

entry = '''
### 447) 2026-09-08 업데이트 조건 5·7 — 단일 릴리스 스냅샷 staging + 실제 파일 회귀(신규 스위트 `remote60_update_release_test`)
- **조건 5 (스냅샷 staging)**: `Download` 가 검증된 manifest 의 **목록 전체**를 받아 `<stagingDir>@@r-<releaseId>` 아래에 받는다. 파일마다 "지금 latest" 를 다시 묻지 않는다. `VerifyDownload` 는 **모든** 아티팩트의 크기·해시를 검사하고, 하나라도 어긋나면 릴리스 전체를 버린다. `Swap` 은 시작 전에 **staged 릴리스 존재**와 **payload 이름 전부가 그 릴리스에 있는지**를 확인한다 — "전부 받고 검증한 뒤에만 교체" 가 관례가 아니라 구조다. 버전 일관성 검사는 **Setup 계열 이름에만** 적용한다(데이터 파일은 UTF-16 버전 리터럴을 갖지 않으므로 전 아티팩트에 요구하면 통과할 수 없다).
- **조건 7 (실제 파일 회귀)** — 신규 스위트가 `apps/shared/update_manifest/files/` 의 **실제 벡터 본문**(24 / 48 / 18 바이트, 그중 하나가 `GNLinkSetup.exe`, 하나가 하위 폴더 `ui@@shell.html`)을 쓴다. 길이가 서로 다르므로 **엉뚱한 곳에 놓으면 내용 이전에 길이에서 걸린다**. 기존 effects 스위트는 이름만 다른 같은 blob 을 복사했기 때문에 잘못된 배치를 구분할 수 없었다.
  - **R1 정확 목적지**: 3개가 각자 자리에 각자 바이트로. 어떤 릴리스도 이름대지 않은 파일은 그대로. **서버는 아티팩트당 정확히 1회만 요청받았다(fetches==3)** — 파일별 latest 재조회가 없다는 직접 증거. 성공 경로에서 **실제로 프로세스 1개를 종료**시켜, 아래 "종료 0" 단정들의 대조군을 만든다.
  - **R2 누락 / 중간 실패 / 해시 오류 3종**: 셋 다 `AbandonedBeforeSwap`, **종료 0**, 대기 중이던 더미 프로세스 생존, **설치 디렉터리 전체가 바이트 동일**(스냅샷 비교라 잔여 파일·누락·`.gnlink-old` 도 같이 잡힌다).
  - **R3 latest 가 도중에 바뀜**: 서버가 첫 파일 뒤 릴리스 B 로 옮겨가도 거부되고 **설치는 바이트 동일, B 의 바이트는 한 개도 들어가지 않는다.** 이어서 정직하게 B 를 요청하면 **B 가 전부** 설치되고 **A 의 바이트는 하나도 남지 않는다**. staging 키잉 자체도 직접 단정한다 — A·B 를 연달아 받으면 **두 디렉터리에 6개**로 나뉘고 **A 의 바이트가 B 의 다운로드에 덮이지 않는다**.
  - **R4 교체 중간 실패**: (a) 1단계에서 세 번째 파일이 열려 있어 실패 → 이미 치운 것 전부 복원, 레지스트리는 이전 버전. (b) 교체·등록 성공 후 **health 실패** → 파일 전부 이전 빌드로, `DisplayVersion` 도 **0.2.104 로 복귀**(버려지는 버전이 아니라). (c) **2단계 중간 실패** → 아래 참조.
- ⚠️ **테스트가 실제 결함 2건을 잡았다 (제품 코드 수정)**:
  - **롤백이 "추가된 파일" 을 남겼다.** 롤백은 `movedAside_` 만 되돌리는데, 릴리스가 **새로 추가하는 파일**은 백업이 없어 대상이 아니었다. 일어나지 않은 업데이트가 설치를 영구히 바꾼다. → 교체가 **실제로 놓은 이름(`placed_`)** 을 따로 기록하고, 롤백이 **백업 없이 놓인 것은 지운다.**
  - **목적지가 없는 하위 폴더면 교체가 실패했다.** 벡터가 이미 `ui@@shell.html` 을 담고 있고, 폴더를 새로 만드는 릴리스는 놓을 수 없었다. → 2단계가 복사 전에 **상위 폴더를 만든다**(이름은 이미 `check_payload_names` 를 통과했으므로 밖으로 못 나간다). 롤백은 **이 시도가 만든 폴더만** 깊은 것부터 비어 있을 때만 지운다.
- ⚠️ **헛단정 2건을 대조군이 잡았다 (교훈 기록: 설계문서 3.10)**:
  - 추가-파일 회귀의 첫 판은 **복사가 실패한 파일 자체를 "추가된 파일" 로 골랐다.** 복사가 실행되지 않았으니 치울 것이 없었고, **롤백 코드를 꺼도 그대로 통과**했다. 지금은 추가 파일을 payload 맨 앞, 막을 파일을 맨 뒤에 둔다 — 코드를 끄면 실패한다(실측: 2 FAIL).
  - `GetFileAttributesW(p) & FILE_ATTRIBUTE_DIRECTORY` 는 **없는 경로에도 참**이다(`INVALID_FILE_ATTRIBUTES` 가 전 비트 1). 폴더 생성을 그렇게 확인하던 단정 2개가 폴더 없이도 통과하고 있었다 → `is_directory()` 로 교체.
- **전환 브리지 제거**: staging 이 이제 `artifacts[]` 를 소비하므로, `load_manifest` 가 단일 아티팩트를 legacy 필드로 복사하던 브리지와 `ManifestFields::artifact/size/sha256` 을 **삭제**했다. 파일의 정체성이 오는 곳이 하나다. (#446 이 "staging 이 목록을 소비하면 없어진다" 고 적어 둔 그것이다.)
- **격리(변함없음)**: 새 스위트도 `update_process_targets.cpp` 를 **링크하지 않는다** — 이미지 이름으로 실제 `GNLinkHost.exe` 를 찾는 코드가 바이너리에 없다. 경로는 전부 임시 디렉터리, 레지스트리는 `HKCU@@Software@@GNLinkReleaseTest-<pid>`, 소켓 0. 종료되는 프로세스는 하네스가 직접 띄운 더미뿐.
- 검증 — **`qwinsta`: `console` 만 Active, `rdp-tcp` 는 Listen → RDP 미접속.**
  - C++ 업데이트 **9종 합계 672 checks / 0 failed**(신규 release **80**, effects 147, manifest 82, state_machine 61, http 36, check 28, payload_name 54, version_compare 139, install_registration 45). 전체 빌드 오류 0.
  - **대조군 3회 실측**(각각 제품 코드를 한 줄 끄고 재빌드·재실행): 롤백의 추가파일 제거 끔 → **2 FAIL** / 폴더 생성 끔 → **3 FAIL** / 폴더 정리 끔 → **1 FAIL**. 끄면 실패하고 켜면 통과한다.
  - JS 디렉터리 스위트 exit 0. **Kotlin 은 재실행하지 않았다 — 이번 변경에 Kotlin·JS 소스 변경이 없다**(`git status` 로 확인).
  - 라이브 제품 무영향 실측: `GNLinkHost`(5156)·`GNLinkInputService`(10820)·`GNLinkStream`(19384) PID 동일, 실제 `HKLM@@...@@Uninstall@@GNLink` `DisplayVersion` = **0.2.104** 그대로.
- **미검증 한계**: 실 HTTPS 왕복 0회(fetch 는 주입 함수) · 실제 서비스/방화벽/시작메뉴 등록 0회(기록만) · 관리자 권한 실증 없음 · `Relaunch`/`HealthCheck` production 미구현(주입 스텁) · 클라 셸 시작 시 비동기 확인 미착수.
- 변경 파일: `apps/native_poc/src/update_release_test.cpp`(신규) · `update_effects.hpp`·`update_effects.cpp`·`update_effects_test.cpp` · `update_manifest.hpp`·`update_manifest.cpp` · `apps/native_poc/CMakeLists.txt` · `docs/업데이트_기능_설계.md`(3.8·3.9·3.10) · `docs/history.md` · `docs/구현계획.md`.
- 버전 인상·설치본 생성·설치·라이브 조작·서버 배포·push 없음.
- 상태: 검증용 검사 대기. **전체 완료 아님.**
'''
entry = entry.replace('@@', BS)
d = d.rstrip('\n') + '\n' + entry
io.open(p, 'w', encoding='utf-8', newline='').write(d)

p = 'docs/구현계획.md'
d = io.open(p, encoding='utf-8').read()
old = '- [ ] **단일 릴리스 스냅샷 staging** (조건 5) — 모든 파일 staging·검증 후에만 교체 진입, 파일마다 latest 재조회 금지\n- [ ] **실제 파일 N개 실행 회귀 4종** (조건 7) — 정확 목적지 / 누락·해시오류·중간실패 시 종료 0·설치변경 0 / latest 변경 중 혼합 0 / 교체 중간 실패 시 전부 복원\n'
new = '''- [x] **단일 릴리스 스냅샷 staging** (조건 5) 완료 (history #447) — staging 이 `releaseId` 로 키잉되고, 모든 아티팩트를 검증한 뒤에만 `Swap` 이 진입한다(staged 릴리스 존재 + payload 이름 전부 확인). 파일별 latest 재조회 없음 — **fetches==3 으로 직접 단정**. 전환 브리지와 legacy 단일 아티팩트 필드 삭제
- [x] **실제 파일 N개 실행 회귀 4종** (조건 7) 완료 (history #447) — 신규 `remote60_update_release_test` 80 checks, 실제 벡터 본문(24/48/18 바이트, Setup 포함, 하위 폴더 포함). ⚠️ **제품 결함 2건을 잡음**: 롤백이 릴리스가 추가한 파일을 남김 / 목적지 하위 폴더가 없으면 교체 실패. ⚠️ **헛단정 2건**도 대조군이 잡음(설계문서 3.10)
- [ ] step4-10 `Relaunch`/`HealthCheck` production 구현 + 클라 셸(`client_shell_main.cpp:679`) 시작 시 비동기 업데이트 확인(네트워크 실패가 실행·로그인을 막지 않을 것)
'''
assert old in d, 'plan anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('history #447 and plan updated')
