# 검증용 독립 빌드 — APK C r2 plain debug (2026-10-30, remote#7fsfy22z)

Codex `codex_apk_inbox_c_src_r1.md` "독립 debug APK" 절의 기록 항목.

- commit: `d50415e94df845911a7d650c928d3bd23f554250` (feat/apk-inbox-c; 제품 바이트 마지막 8a40eff, d50415e 는 시험만)
- tree: 새 worktree `C:\work\gnlink-wt\verify-c2-apk` (git worktree add --detach), tracked 변경 0, 빌드 출력은 그 안 `apps/android_direct_client/app/build` 만
- 명령: `gradlew.bat --no-daemon assembleDebug -Pgnlink.testDirectoryUrl=http://10.0.2.2:18410` (apps/android_direct_client), BUILD SUCCESSFUL 1m14s, 47 tasks, 로그 `build_plain.log`
- toolchain: JAVA_HOME = Android Studio jbr (openjdk 21.0.9), ANDROID_HOME = %LOCALAPPDATA%\Android\Sdk, buildCMakeDebug 4 ABI
- BuildConfig: BUILD_TYPE=debug, VERSION_NAME=0.2.32 / VERSION_CODE=31(브랜치 버전 그대로), DIRECTORY_URL=http://10.0.2.2:18410, LIVENESS_TRACE=false (plain; trace/seam/auto-prepare 속성 없음)
- signer: debug keystore (gradle 기본), ABI: arm64-v8a, armeabi-v7a, x86, x86_64
- 산출물: `app-debug.apk` sha256 **D894BC389C29337BE757E2883DE1AE466754D4558207A3112AE0DB6DE953E532**, 17,425,004 B
- 작업용 plain-debug-c2.apk(ccd965c7…, 20,717,756 B)와 비교: entry 157 = 157, 압축/비압축 entry 합 17,350,675/17,454,771 vs 17,350,659/17,454,755(차 16 B = lib/x86 .so 16 B). 파일 크기 차 약 3.3 MB 는 entry 밖(정렬/패딩·패키징 방식)으로 보이며 내용 차이 근거 아님 — 다만 .so 16 B 차이는 debug 비재현으로 기록만(source 동등성 주장 아님).
- 실제 설치 대상: emulator-5554 (C batch SH-off 120 s, offSha 를 위 hash 로 고정 — 실행은 C SOURCE OK 뒤)

## 재빌드 — C r3 commit 2b492ef (2026-10-30 07:27–07:28) — 이것이 C 실기용 현재 기준
- tree: 같은 worktree 를 `git checkout --detach 2b492ef`, tracked 변경 0
- 명령: `gradlew --no-daemon clean` (rc0, outputs 제거 확인) → `gradlew --no-daemon assembleDebug -Pgnlink.testDirectoryUrl=http://10.0.2.2:18410` (rc0, 1m12s, buildCMakeDebug 4 ABI). 첫 시도 `clean assembleDebug -P…` 한 번에는 빌드 스크립트가 "testDirectoryUrl is for debug builds" 로 거부(rc1, `build_plain_2b492ef.log` 덮어쓰기 전 기록은 task 출력).
- BuildConfig: debug, VERSION_NAME 0.2.32, DIRECTORY_URL http://10.0.2.2:18410, LIVENESS_TRACE=false; ABI 4종(arm64 3413360, v7a 2216860, x86 3465628, x86_64 3333296 B)
- 산출물: `plain-debug-2b492ef-verify.apk` sha256 **9F7B0BD08A252865F45994B65A399CEA56ED0C3DA5E5964687EE4673528EE07B**, 17,425,276 B
- 주의: `plain-debug-STALE-d50415e-copy.apk`(D894BC38) 는 첫 시도 실패 때 잘못 복사된 이전 빌드 — 사용 금지.
