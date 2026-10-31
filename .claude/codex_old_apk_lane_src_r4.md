# rel-0164 old APK lane r4 — default holder diff 확인

2026-10-30. **949fe07 SOURCE OK. default holder 실행 경로의 배열 풀림 지적을 닫고, 기존 조건으로 S→P 새 run을 허용한다.** 기존 N1/N2 수용·무인증 update check 예외는 유지한다. 호환 EVIDENCE/출하 판정은 실제 결과 뒤다.

## 고정/변경 범위

- HEAD `949fe07d7bf577cc62fe0b59d8e24fb3dc0f20b4`, 부모494d05b. tracked clean(미추적 시험 산출물 있음), diff --check 통과.
- 보고 r4 SHA-256 `0adb30a107b5978d3e93178c665eeb4fd9f4077092f69cca8c46dead75b5167b`.
- snapshot SUMS `9505e65e00b1f2fae67d122141c53af97956e66c00285f01ec98cc79f04f6d4f`: **18파일의 commit blob/canonical 및 raw checkout hash 모두 직접 일치**.
- diff는 fixture/slot/orchestration, 시험 stand-in cpp와 CMake target 총5파일이다. 실제 S/P에 쓰는5개 build target의 제품 동작 변경은 없고 r3의 binary hash 귀속을 유지할 수 있다. 새 stand-in은 orchestration 전용이며 실제 holder와 혼동하지 않는다.

## 확인한 보수

`$hc = @(if (...) { $HolderCmd } else { "...holder_tool.exe" })`는 if 결과 전체를 array로 받아, default exe 하나도 `$hc[0]`에서 전체 경로로 유지한다. Codex가 같은 Windows PowerShell에서 직접 대조했다.

```text
old:   Type=String, First=D
fixed: Type=Object[], Count=1,
       First=D:\review-build\remote60_ka_standby_holder_tool.exe
```

fixture의 상대 BuildDir 거부와 slot의4개 path 사전 검사를 읽었다. 실제 `oa_slot.ps1`에 Apk/BuildDir/ServerJs/JobRun을 각각 일반 상대 경로로 넣어 **4건 모두 exit2/RESULT INVALID**임을 직접 확인했다. 이 preflight는 fixture/Host/AVD 실행 전에 끝나며, 실제 SDK adb/기기/slot을 사용하지 않았다. 결과는 `.claude/codex_old_apk_review/r4_949/preflight_checks.json`에 있다.

작업용의 orchestration20/20과494d05b 음성의 default case 실패는 제출 보고의 근거다. source의 새 default case가 HolderCmd 없이 실제 기본 파일명을 hash/start/ready하고, 다른 대상은 viewer/network 없는 stand-in을 쓰는 것을 확인했다. Codex가 이 전체20건을 다시 실행했다고 하지는 않는다. 이전 S/P INVALID 두 건은 그대로 보존하며 삭제·PASS 변경하지 않는다.

## 경로 조건의 정확한 범위

`IsPathRooted`는 모든 Windows 경로의 **완전한 절대 경로** 판정과 같지는 않다. 실제 PowerShell에서 `D:relative\build`도 true였다. 이 보강을 “모든 drive-relative/root-relative 경로를 막았음”으로 확대하지 않는다.

**이번 실행은 기존 계획대로 네 인자를 전부 drive-qualified absolute (`D:\...`, `C:\...`) 또는 완전한 UNC로 명시하는 조건**이다. 실제 명령/해시를 기록하고 `D:...`/`\relative...` 같은 불완전한 경로를 쓰지 않는다. 이미 고정된 완전한 경로의 S/P 실행을 위해 별도 보수 commit/왕복을 만들 필요는 없다. 추후 이 인터페이스의 일반 경로 검증을 주장할 때는 해당 구분도 테스트해야 한다.

## 실행/다음 담당

검증용은 동일한5 target의 전체 hash를 재확인하고 새로운 S/P run·자기 슬롯으로 진행한다. 제품 target 재빌드가 필요 없는 script 수정이며, 재빌드한다면 새 hash를 연결한다. 기본 경로 대조용 `_stubbuild`가 아닌 고정 검증 build의 실제 holder/Viewer를 사용한다.

`OA_PM_CLEAR_TIMEOUT_SEC=60`, SDK adb pin·시험 serial/boot, install/pm clear 성공 전 start0, P 시작 후 NO_CREDENTIAL 추가 검사, 원본/되받은 APK hash, Host input none/power off 두 줄 launch/final guard, child EOF/log·job 종료/잔여0, repo 경계·시험 package 한정·clipboard 미조작을 유지한다. 실패를 채우기 위한 자동 반복이나 사용자 앱/다른 프로그램 조작은 하지 않는다. 같은 정상 단계의 go를 다시 받지 않는다.

S의 directory/relay/park와 P의 수동 TCP control·UDP video/종료·새 연결 매핑을 구분하고, WithStandby 사용 여부와⑦을 정확히 보고한다. P의 무인증 운영 update manifest 논리 check1회만 허용하며 로그인·download/install로 확대하지 않는다. 운영 HTTPS에서 게시 APK가 새 서버 로그인/relay를 직접 검증했다는 표현은 계속 금지한다.

다음은 검증용의 실제 S/P 결과다. PC 통합·서버 먼저/Android pair 불변·push 보류 및 별도 APK0.2.33 보류를 유지한다.

이번 Codex는 diff/hash·순수 PowerShell 배열 대조·실제 slot의4개 거부 preflight만 실행했다. Host/Viewer/AVD/adb·GPU/OS 입력·설치/초기화·운영 API·삭제·제품 수정·commit/push는 없다. 미추적 검토 기록만 작성했으므로 제품 commit은 만들지 않았다. 사용자 현재 조치 없음.

## 재실행 INVALID 보충 — Viewer의 encoded-experiment 전제

10-30 00:14:21–00:14:35의 S/P 각 rc2/7초·기존5 target hash 불변을 `verify_0164/old_apk_lane_summary.txt`에서 확인했다. 검증용 보고상 배열 경로는 정상화되어 holder Init까지 도달했지만 Viewer가 h264 startup을 거부해 holder4/ready0으로 끝났고, AVD 미시작·기기 조작0·job 종료 확인이다. 두 run을 호환 성공으로 세지 않고 INVALID로 보존한다.

검토 시점 HEAD는 아직949fe07이다. holder가 offscreen 좌표만 설정하고 `e2e_isolated_environment`를 만든 뒤 h264 Viewer를 시작하며, encoded-experiment를 명시하지 않았음을 확인했다. helper가 임의의 필요한 기능 env를 만들어 주는 것으로 가정하면 안 된다. 앞 Source 검토에서 이 Viewer startup 전제를 놓쳤다. child 조기 종료가 holder4로 전달된 점은 N2의 fail-closed 동작 근거이며, 이번 실패로 기존 초기화/배열/종료 판정을 전부 다시 열지는 않는다.

검증용이 이미 지시한 **명시적인 child env·필수 runtime env 표·실제 holder/Viewer startup의 보수 전 실패/후 성공**을 같은 task의 보수로 연결한다. 환경이 우연히 부모에 남아 있을 때만 성공하는 대조를 피하고, 부모에 해당 변수가 없는 조건도 검증한다. OS 입력/전원·repo/UDF 격리 경계는 약화하지 않는다. 기기 없는 대조라도 실제 Host/GPU를 쓰면 기존 guard/측정 슬롯을 지킨다.

현재 S/P 추가 실행은 보류하고 다음 고정 commit의 해당 diff/대조만 재확인한다. 이것을 제품 코드 변경이나 APK 호환 실패로 분류하지 않는다. 사용자 조치 없음.
