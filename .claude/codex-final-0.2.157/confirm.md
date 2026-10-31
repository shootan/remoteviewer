# PC 0.2.157 최종 게시 근거 확인

2026-10-12 KST. **배포 완료(마일스톤) · 회사/집 PC 업데이트 후 실기 대기.** 제품 게시를 막는 새 결함은 발견하지 않았다. 현재 사용자 창구는 검증용 `remote#34id7hp2`다.

## 고정 소스·산출물

- 검토 소스 `09266e5f8a7bc526fe817efbc87b9bd1d25d3820` → 버전 `5e0e82f2bcb4026ca8d5662c1c813064a27f9245` → 게시 기록 `82bdf0145be78148e49fc7599974add62e1940d5`.
- 두 후속 diff를 직접 읽었다. 버전 commit은 `product_version.hpp` 0.2.156→0.2.157 한 줄, docs commit은 `docs/history.md`만 변경한다. `git ls-remote origin refs/heads/main`은 `82bdf01`과 일치했다. tracked 변경 없음.
- PC 범위: NVIDIA 타입 전 RC 계획/MaxQP 51/VBV 200 ms, 회선 R와 인코더 E 분리, 재초기화·no-op·지연 적용, startup keyint, WGC 콜백 signal-only 및 collector/수명/진단 보수, GPU scaler 준비 대기 제한. APK·와이어 변경 없음.

## Codex 직접 실행한 최종 확인

`verify.py`와 `result.json`에 재현 코드·원시 결과를 보존했다. 확인 시작 UTC `2026-10-12T08:10:46.599965+00:00`.

- NAS `/opt/gnlink/update-manifests/windows.manifest`와 `windows.sig`를 SSH로 읽어 보존했다. 두 파일 모두 로컬 release와 바이트 일치, version `0.2.157`, releaseId `r-0.2.157`.
- 저장소 운영 공개키를 사용하는 `gnlink_verify_manifest.js`를 실행했다. 환경 공개키 override를 제거한 프로세스에서 원본 수락·변조 거부, exit 0. 인증된 업데이트 API의 응답 계약이나 실제 설치를 이 검사로 대신했다고 주장하지 않는다.
- 서명된 manifest의 공개 HTTPS URL 11개를 직접 다운로드했다. HTTP 200, 바이트 수, SHA-256, 로컬 payload 일치 **11/11**.
- `D:/remote/remote/dist/GNLinkSetup-0.2.157.exe`도 서명된 Setup 및 외부 수신 파일과 같은 해시.
- 버전 문자열이 들어 있는 Client/Host/Setup/Stream/Viewer는 UTF-16 `0.2.157`, 모든 payload exe에 `0.2.156` 문자열 없음. 다른 exe에는 버전 문자열 자체가 없으므로 그 검사만으로 버전을 단정하지 않는다.
- 출하 Stream에 collector 실패/게시 정지 seam 2종 및 clamp test 표식 2종, RSDS가 없음을 직접 확인했다. 문자열 검사는 빌드 gate를 보강하는 확인이며 모든 가능한 시험 기능 부재의 독립 증명이라고 확대하지 않는다.

| 항목 | SHA-256 |
|---|---|
| live windows.manifest | `051672418d69c92c1b2f6fa390df7b33b7681e0a4a576c71019b0c3e93c33730` |
| live windows.sig | `cb5eb786f53aa63b2afefad12f66791297733a9250e6065edc64553c21b0bc28` |
| GNLinkStream.exe | `5a38ef06ddf44bc9ddd82064033afb1e4268af3789ae50c4a1633071355bf13b` |
| GNLinkSetup.exe | `bbcad18ba72ca3ec492bbad8ce91785038559a819460a68819139daf41ee187f` |

설치본 공개 위치: https://gnlink.shotan.net/updates/0.2.157/GNLinkSetup.exe

## 읽어 대조한 검증용 실행 근거

- main worktree `.claude/rel/release_0.2.157.log`: build commit 고정, parity/payload-set/checksums, installer 10/10, 제품 서명 검증·변조 거부, RELEASE_EXIT=0. 출하 빌드를 Codex가 재실행하지 않았다.
- `.claude/rel/deploy-0.2.157.log`: 서버 파일 11/11, 0.2.156 pair 백업(667b7bbf…/73f39427…), pair 게시, 외부 해시 확인. 이번 Codex SSH/HTTPS 검사는 이 로그와 독립이다.
- 루트 `D:/remote/remote/.claude/rel/integ_0.2.157/summary_integ157{,b}.txt` 및 실제 수신기/전환/회복 로그: 소스 09266e5, actual receiver smoke·회복 왕복·RC 전환 72/72 통과. 통합 결과 41/43, encode_epoch 재실행 통과. wire_cap_defer의 리다이렉트 조건 실패는 보존한다. 이전 코드도 같은 조건에서 실패/콘솔 실행은 양쪽 30/30 통과했다는 분류는 검증용 보고이며, Codex가 이번에 재현한 것은 아니다.
- Home5는 이전 검토에서 원시 자료를 읽어 판단했다. r14 NVIDIA HW 후보 valid·watchdog/hang dump/stall 0, motion 41.5fps, text 37.8fps, 720p 23.6fps, 전환 74/74. baseline invalid이므로 상대 FPS 우월성 PASS로 바꾸지 않는다. 승인된 절대 지표와 사용자 환경의 교착 재현/해소 증거로 출시 조건을 충족한다.

## 문서 상태 정정과 남은 실기

게시 시점 `docs/구현계획.md` E1에 NVIDIA 실효 대기, Home5 미완료/출하 NO-GO, 초기 44 상태가 남아 있었다. 게시물 결함과 분리하여 A2A seq 4625로 검증용에 같은 릴리스의 상태 정정을 한 묶음 전달했다. **`592ed9670d211623b4aa20b6b17c01713d2ad67e`에서 E1 상태만 수정되었음을 diff로 재확인했고, 원격 main도 이 commit과 일치했다.** 최종 51/예산 정책, Home5 완료(상대 비교 불가 경계 유지), Home4 진단 대체, 0.2.157 게시·현장 미완료가 반영되었다. 문서 잔여는 종결이며 제품 산출물은 변경되지 않았다.

사용자 행동은 회사/집 PC 업데이트 후 평소 사용 10~15분, 이후 검증용 NAS 로그 확인이다. 실제 인앱 업데이트 end-to-end, 회사 DXGI release wedge/exit 44(별건), NV12 opt-in 실HW, 장시간/WAN, 저rate 1080p 글자 가독성은 미확인이다. 이 릴리스가 회사 DXGI exit 44를 해결했다고 주장하지 않는다.

Codex는 사용자 설치본/프로세스/설정, NAS 서비스나 게시 파일을 변경하지 않았다. 미추적 검토 기록만 작성했으며 제품·공식 docs·commit/push를 직접 수행하지 않았다.
