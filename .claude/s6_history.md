
### 440) 2026-09-08 업데이트 step4-3 — 등록 로직을 공유 단위로 추출((D)안), 버전을 인자로 · 실패 단계를 보고 · 롤백용 스냅샷 (Codex 승인 (D) + 조건 7건)
- 배경: `RegisterInstall` 을 어떻게 만들지 (A)자체구현 / (B)설치기 `/S` 재사용 / (C)설치기에 등록 전용 인자 중에서 물었고, **Codex·검증용이 셋 다 배제하고 (D) 를 확정**했다. (C) 를 죽인 사실을 **내가 원문에서 재확인**했다:
  - `write_uninstall_entry` 가 `set(L"DisplayVersion", kProductVersion)` — **등록하는 바이너리에 컴파일 시점 상수로 박힌 값**(`installer_main.cpp:269`).
  - **`kPayload` 에 `GNLinkSetup.exe` 가 없다(0건)**. 설치기는 자기 자신을 `CopyFileW(self, setupPath)` 로 따로 복사한다(`:314`).
  → 업데이터가 교체를 마쳐도 디스크의 `GNLinkSetup.exe` 는 **여전히 구버전**이고, 거기에 등록을 위임하면 **구버전 상수가 `DisplayVersion` 에 쓰인다.** `read_installed_version()`(`:436`)이 그 키를 읽으므로 **이후 모든 "더 새로운가" 판정의 기준점이 오염**된다.
- **(D) 추출**: `apps/native_poc/src/install_registration.{hpp,cpp}` 신설. 서비스 등록 · 방화벽 규칙 · 시작메뉴 2개 · Uninstall 키를 한 단위로 묶고, **설치기와 업데이터가 같은 코드를 링크**한다. **버전은 상수가 아니라 인자** — 설치기는 `kProductVersion` 을, 업데이터는 검증된 manifest 버전을 넘긴다. 선례는 `compare_versions` 추출(#433, `f0beef9`)과 같은 패턴이다.
- **조건 3 — 중간 실패를 감추지 않는다**: `RegistrationResult{ok, completed[], failedAt, detail}`. **첫 실패에서 멈추고 어느 단계인지 반환**하며, 그때까지 완료된 단계를 **부분 상태로 남긴다**. 현행 설치기는 서비스 등록 실패 후 메시지만 띄우고 **최종 성공으로 반환**하는데(`do_install()`), 그 관행은 가져오지 않았다 — 완전 등록과 부분 등록을 구별 못 하는 호출자는 롤백 여부를 판단할 근거가 없다.
- **조건 1 — 롤백은 이전 설치 값으로 복원한다(검증용이 빠뜨렸다고 정정한 항목)**: `capture_registration()` 을 **교체 전에** 찍고, 롤백 시 `restore_registration()` 으로 되돌린다. **새 manifest 버전을 재사용하면 파일은 구버전인데 레지스트리는 신버전을 주장** — (C) 를 배제한 것과 정확히 같은 오염이다. 스냅샷이 `present == false`(첫 설치)면 **빈 값을 쓰는 게 아니라 키를 지운다.**
- **조건 2 — 원자성 표현 정정**: `update_effects.hpp` 의 "The swap is all-or-nothing by construction" 을 **"THE FILE SWAP is all-or-nothing"** 으로 좁히고, **"업데이트 전체는 원자적이지 않으며 그렇게 만들 수도 없다 — 파일 교체 + 시스템 등록 4종은 트랜잭션이 아니라 순서다. 대신 복구 가능한 단계 계약을 갖는다"** 를 명시했다. 이 코드의 원자성 언급은 **파일 교체에 한정해 읽어야 한다**고 주석에 못박았다.
- **조건 6 — `request_process_stop` 의 재사용 창 제거**: 신원을 확인하고 핸들을 **닫은 뒤** PID 로 창을 열거하던 형태라, 검사와 WM_CLOSE 사이에 PID 재사용 창이 남아 있었다. 이제 **검증된 핸들을 함수 끝까지 열어 둔다** — Windows 는 핸들이 살아 있는 동안 PID 를 재활용하지 않으므로, 그것이 위 검사를 유효하게 유지하는 수단이다.
- 검증 — **`qwinsta`: `console` 만 Active, `rdp-tcp` 는 `Listen` → RDP 미접속.**
  - `remote60_install_registration_test` **45 checks / 0 failed**, exit 0. **핵심 단정**: `DisplayVersion` 이 **넘긴 버전**이지 상수가 아님 · 다른 버전으로 재등록하면 값이 따라 움직임 · 실패 3종(service/firewall/shortcuts)에서 **어느 단계인지 + 그때까지 완료 목록** 보고 · 실패 시 **Uninstall 키가 아예 쓰이지 않음** · 스냅샷 왕복(구버전 캡처 → 신버전 등록 → **복원 후 구버전으로 되돌아옴**) · 첫 설치 스냅샷 복원은 **키 삭제**.
  - **격리**: 레지스트리 루트는 `HKCU\Software\GNLinkRegistrationTest\<pid>`(HKLM 아님), 서비스명·방화벽 규칙명은 테스트 전용, netsh·SCM·바로가기는 **기록만 하고 실행하지 않는다**. 테스트가 시작하면서 **"scratch 키가 실제 Uninstall 경로가 아님"** 을 스스로 단정한다.
  - **실제 시스템 무영향 실측**: 테스트 후 실제 `HKLM\...\Uninstall\GNLink` 의 `DisplayVersion` = **0.2.104 그대로**, `GNLinkSecureInput` = **Running**, HKCU 테스트 잔재 **없음**.
  - `remote60_installer` 재빌드 exit 0 — 추출 후에도 설치기가 링크되고, 설치기는 계속 `kProductVersion` 을 넘긴다.
- **조건 5 조사 결과 — 구버전 Setup 이 유지보수 바이너리로 남는 문제(임의 결정하지 않음)**: 확인된 사실 — 업데이트는 `GNLinkSetup.exe` 를 교체하지 않으므로(kPayload 에 없음) `UninstallString` 이 가리키는 것은 **영원히 구버전 바이너리**다. 그리고 그 바이너리의 언인스톨 경로는 **전부 자기 컴파일 시점 상수**에 묶여 있다: payload 목록(`:440` 이 `kPayload` 순회) · 서비스명(`kServiceName`, `:40`) · 바로가기 이름(`:417`) · 설치 폴더명(`kInstallFolderName`, `:39`) · Uninstall 키 경로(`:455`). → **신버전이 payload 를 추가/제거/개명하거나 서비스명·폴더명을 바꾸면 구 언인스톨러가 남기거나 엉뚱한 것을 지운다.** 선택지(제안만): ① `GNLinkSetup.exe` 를 payload 에 넣어 교체 대상으로 삼는다 ② 업데이터가 교체 후 새 Setup 을 별도로 복사한다 ③ 언인스톨 대상 목록을 바이너리 상수가 아니라 **설치 시 기록한 매니페스트**에서 읽는다. **패키지/유지보수 바이너리 계약 변경이라 결정을 요청한다.**
- 미검증·미착수: **실제 서비스 등록·실제 방화벽 규칙·실제 시작메뉴 생성 0회**(전부 기록형 stand-in) · 관리자 권한 실증 없음 · **업데이터의 `RegisterInstall` 이 이 단위를 호출하도록 하는 배선은 아직**(다음 커밋) · `Relaunch`/`HealthCheck` production 미착수 · UI 진입점 미착수 · 실 HTTPS 왕복 0회.
- 변경 파일: `apps/native_poc/src/install_registration.hpp`·`install_registration.cpp`·`install_registration_test.cpp`(신규) · `installer/installer_main.cpp` · `src/update_effects.hpp`(표현 정정) · `src/update_process_targets.cpp`(핸들 창) · `apps/native_poc/CMakeLists.txt` · `docs/history.md` · `docs/구현계획.md`.
- 버전 인상·설치본 생성·설치·라이브 조작·서버 배포·push 없음.
- 상태: 검증용 검사 대기. **전체 완료 아님.**
