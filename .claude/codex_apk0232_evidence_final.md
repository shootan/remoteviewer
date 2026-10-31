# APK 0.2.32 최종 EVIDENCE 판정

> **추가 정정(2026-10-28 Finding-3): 실 GNLinkStream 스트리밍을 사용한 selector/renderer·SH 등 행동 근거는 입력 router OFF와 별개인 wake/전원 효과도 확인되지 않아 안전한 재확인 대기다.** 관측 자료·순수 계산/소스·서명/hash는 보존한다. [입력·전원 효과 Finding-3](codex_test_injection_finding.md) 참조.

> **안전 판정 정정(2026-10-28 Finding-2): viewer_prelogin_login_e2e_test의 android-select/prelogin fixture 및 같은 run의 행동 근거는 관문 사용 보류/안전한 재확인 대기다.** 기존 시각 자료·기능 관측과 별도 SOURCE/서명·artifact hash는 보존한다. 아래 최초 EVIDENCE OK는 이 정정 범위로 제한된다. [입력 주입 Finding-2](codex_test_injection_finding.md) 참조.

2026-10-27 / Codex remote#tt7fcvky / 검증용 remote#7fsfy22z 인계 ①.

**최신 판정: EVIDENCE OK — 아래 Addendum E1 재판정으로 잔여 관찰을 닫는다. APK SHA-256 1a3eee63…815ab에 한해 승인된 서명·Android 게시·외부 검증 절차를 즉시 진행할 수 있다. 게시 완료·실폰 성공 판정은 아니다. 아래 최초 NEEDS_EVIDENCE 사유는 보완 이력으로 보존한다.**

## 고정 대상과 직접 확인

- source/test `fix/apk-prelogin-select` b554e762613e5de82584564a96713589c1c28e06. picker 9afcec7, UA-2 482a901, SH d29a034의 기존 SOURCE OK 및 evidence A 설계를 대조했다.
- `.claude/rel-apk-0232/GNLink-0.2.32.apk`의 SHA-256을 직접 계산: **1a3eee63a992371f7ad455deedfabbc0e0b627c1c7432fa9f9251b8183d815ab**. 인계 및 독립 보고와 일치한다.
- `verify_0232/VERIFY_0232_FINAL.md`, r6/summary.txt, 작업용 통합 보고 r6, 기존 SH r2/r4/r6·picker r3·UA-2 r3 판정을 읽었다. r6/summary에는 6개 실행 파일의 전후 hash 일치와 독립 배치 시각이 있다.
- `caplife/sh_r2/v6_A.log`, v6_A_ctl.log, v6_ua2d.log, v6_layout.log 원문을 읽고 `v6_A/prelogin_1_shown.png`, `v6_ua2d/ua2d_2_gdi.png`를 직접 열었다. 실제 APK의 원격 패턴 표시 및 GDI 접수/현재 DXGI 구분은 화면과 부합한다.
- A 15/15는 실제 제품 APK의 선택→local 시작→표시, 표시 전 취소/늦은 프레임 억제, 같은 연결 재선택 및 SH 전환을 기록한다. 일반 Host 대조는 local 표시/선택을 못 하고 금지 window-list/select 13줄로 기대대로 실패한다. UA-2 desktop 로그는 CHECKING→IDLE, GDI 접수와 실제 DXGI, 이후 DXGI 적용 표시를 구분한다. layout 로그는 회전·모서리·letterbox·pan·resize·입력 fence·실패 direct 복귀를 기록한다.

위 행동 실행은 **검증용 귀속**이며 Codex가 AVD/Host를 재실행했다는 뜻이 아니다. 보고의 나머지 단위/gate/strength/touch/latency/실 Host 결과도 실행 주체를 그대로 유지한다. A는 시험 Host 역할 광고·소유 WGC 패턴과 실제 APK의 조합이며 실제 directory 인증/SYSTEM/Winlogon/DXGI worker까지 단일 E2E로 증명하지 않는다.

## E1: 스크롤 shimmer 관찰은 아직 열려 있음

`codex_sh_strength_r2.md` §4.4는 움직이는 글씨·가는 선 등의 최종 출력 관찰을 게시 전 조건으로 두었고, `codex_sh_strength_r4.md`는 수치 oracle 대신 **짧은 통제 스크롤/연속 프레임의 직접 정성 관찰**로 범위를 줄였다. 실폰 S4·발열·최종 체감만 게시 후 실기로 분리했다. 현재 독립 FINAL은 이 관찰을 수행하지 않았다고 명시한다. 기존 조건을 낮출 새 측정 근거나 자동화 불가 사유가 없다.

따라서 동일 후보와 승인된 소유 pattern/격리 AVD에서 다음 제한 보완만 한다.

1. 원본 글씨/1px 선/체커 등 식별 가능한 패턴을 정해 같은 방향·속도의 짧은 스크롤을 주고 off/A/B(강도와 확대율 고정)의 실제 최종 출력 연속 자료를 보존한다. 원본 움직임/시각 또는 프레임 식별과 캡처 간격·누락을 기록한다. 시험 빌드가 필요하면 기존 허용된 seam 차이를 명시하며 출하 후보 hash는 유지한다.
2. 검증용이 해당 연속 자료를 열어 미세선 반짝임·halo·세부 소실/비정상 출력 여부를 **관찰한 범위 그대로** 적는다. 한 장의 정적 캡처나 edge 점수로 shimmer PASS를 만들지 않는다. 프레임 샘플링으로 판단 불가하면 그 이유와 실제 확보 자료를 남긴다.
3. FINAL에 짧은 addendum과 자료 경로를 붙여 같은 검토자에 회신한다. 다른 닫힌 시나리오의 전량 재실행이나 제품 SOURCE 재승인은 불필요하다. 제품 결함이 관찰되면 그 영향 범위만 분리한다.

실폰/Adreno·Mali 화질·발열·S4·최종 체감, 실제 Winlogon/UAC/재부팅·NAT는 미검증으로 유지한다. 이번 E1을 통과해도 그것들을 실기 성공으로 보고하지 않는다.

## 다음 담당과 게시 경계

검증용 remote#7fsfy22z가 기존 SH/통합 담당의 같은 작업에 E1을 취합한다. addendum 검토 후 같은 APK의 EVIDENCE를 갱신하면 검증용이 승인된 Android 서명·`--platform android` 게시·외부 hash/manifest 검증·Windows 0.2.163 pair 불변·dist/관련 기록을 수행한다. **이 문서는 아직 게시 OK가 아니다.** push 권한은 현재 사용자 지시를 우선하며 이 검토서가 별도 권한을 부여하지 않는다.

Codex 제품 편집·빌드·시험 실행·설치·삭제·서명/게시·commit/push 없음. 이 파일은 미추적 운영 판정서이며 공개 history/계획·Git MCP commit은 검증용이 해당 릴리스의 같은 기록에 취합한다. 사용자 현재 조치 없음.

## Addendum E1 재판정 — EVIDENCE OK (2026-10-27)

검증용 remote#7fsfy22z의 같은 FINAL 'Addendum E1', `caplife/sh_r2/vv_shimmer.log`, `vv_shimmer/shimmer_summary.json`, 하네스 **5f53879a670e7d097af754ef35a20e675e013c9c** 전체2파일 diff를 읽었다. 출하 APK SHA-256을 재계산해 **1a3eee63a992371f7ad455deedfabbc0e0b627c1c7432fa9f9251b8183d815ab**로 그대로임을 확인했다. 5f53879는 automation2파일뿐이며 제품 소스 변경0이다.

Codex는 `verify_0232/shimmer/sheet_off.png`, `sheet_A_weak_s20.png`, `sheet_A_strong_s60.png`, `sheet_B_display_s60.png`, `lines_2x_off_Astrong_B.png` **5개를 직접 열람**했다. off/A약/A강/B의 서로 다른 실제 출력에서 번호·글씨·가는 선·체커의 위치 변화와 B의 강한 획 대비가 보인다. 검증용 실행은 같은 연결/일반 제품 설정 UI, 원본4px/33ms 이동과 번호 구간 off1~188/A약189~378/A강379~571/B572~760, mp4/정지 자료와 출력 누락을 함께 기록했다. 6/6은 녹화 성립일 뿐 화질 점수 PASS가 아니라는 구분도 적절하다.

**E1의 요구였던 짧은 통제 움직임·최종 출력 자료 확보 및 직접 정성 관찰은 수행됐다.** 읽은 시트에서 추가 보수를 요구할 명백한 깨짐/halo·ringing/선 굵기 이상은 찾지 못했다. 이는 정적 시트와 기록에 대한 제한 관찰이며 Codex가 원속도 영상 재생이나 AVD를 새 실행한 것은 아니다. 획의 진함 차이는 관찰이지 same-frame 정량 개선률이나 미세 shimmer 부재 증명이 아니다.

관찰 한계를 추가로 정확히 한다. A강418→420→440/B615→623→642 등 원본 번호 사이가 크게 뛰며, 번호가 없는 시트 열도 있다. 하네스의 기존 **200ms pulse timer가 정적 lbmp를 Push하는 경로를 scroll 중에도 유지**하므로 이런 정적 출력은 통제 source 자체에 섞일 수 있다. 이 자료를 모든 인접 source 위상에 대한 순수 연속 스크롤 시험 또는 시간적 깜빡임0의 증거라고 부르지 않는다. AVD SwiftShader·screenrecord H.264 재압축·샘플링 누락에 의한 미세 반짝임 은폐 가능성도 그대로 남긴다. 기존 요구는 새 수치 oracle/실폰 완성 판정이 아니라 공개된 한계 아래 짧은 정성 관찰이었으므로 이 한계를 숨기지 않고 E1을 닫으며 전량 재검사/추가 화질 프로젝트는 요구하지 않는다.

### 게시 인계

**최초의 NEEDS_EVIDENCE 및 '아직 게시 OK가 아니다' 문장은 이 addendum으로 대체한다. 이 고정 APK의 제품 SOURCE + 독립 EVIDENCE OK.** 검증용은 기존 승인된 Android manifest 서명·`gnlink_deploy.sh --platform android`·외부 HTTPS/manifest 서명/버전/전체 artifact hash 확인·Windows0.2.163 pair 불변·dist/기록·검수 commit 통합 절차를 진행한다. 새 추상 GO 대기는 없다. 0.2.33/KA의 별도 SOURCE 반려를 0.2.32 게시 blocker로 섞지 않는다. 실제 게시/외부 확인 결과가 오기 전에는 배포 완료라고 보고하지 않는다. push는 사용자 권한 지시를 우선하고 이 검토서가 새로운 권한을 부여하지 않는다.

실폰 화질/지연/발열·S4/체감 shimmer, 실제 directory→SYSTEM/Winlogon/UAC·NAT의 미검증은 앞선 범위 그대로다. 기본 A·명시 선택 B(실험) 계약을 유지하며 폰 실기 성공을 주장하지 않는다.

### 하네스 운영 지적 H1 — 제품 EVIDENCE와 분리

5f53879의 `scenario_scroll_shimmer`는 `adb shell rm -f /sdcard/shimmer_<segment>.mp4`를 호출한다. 시험 AVD의 자기 scratch라는 주석만으로 저장소 밖 삭제가 허용된다는 근거는 없다. 완료된6/6 실행은 이 호출 경로를 포함하지만 삭제 반환코드가 무시돼 실제 삭제 성공/기존 동명 파일 여부까지는 이 로그로 확정하지 않는다. **후속 실행 전에 바깥 삭제 호출을 제거하고 원본을 보존**하며 유일 run 이름/소유 기록으로 충돌을 피한다. 재정리를 실행하거나 새 삭제 허가를 물을 필요 없다. 검증용은 삭제 후보/운영 기록에 경로·실행 경계·확인되지 않은 영향을 사실대로 남긴다. Codex는 새 삭제를 하지 않았다. 이 운영 보수는 APK 제품 변경/재빌드/같은 전체 배치 재실행 조건이 아니며 EVIDENCE OK와 별도로 처리한다.

다음 담당은 검증용 remote#7fsfy22z의 서명·게시·외부 확인 및 최종 사용자 안내다. Codex는 판정서 갱신 외 제품 변경/빌드/실행/commit/push가 없고, 공개 history/계획·Git MCP commit은 같은 릴리스 기록에 취합한다. 사용자 현재 조치 없음.
