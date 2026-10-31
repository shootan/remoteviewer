import io

p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()
entry = '''
### 479) 2026-09-09 0.2.105 / APK 0.2.13 후보 산출물 — **자동업데이트 실사용 준비완료가 아니다**
- **버전**: Windows `kProductVersion` **0.2.104 → 0.2.105**(`product_version.hpp`, 설치기의 `DisplayVersion`·등록·제목이 전부 이 상수를 쓴다). Android **`versionCode` 11 → 12**, **`versionName` 0.2.12 → 0.2.13**.
- **산출물**(`dist/`, 버전 붙인 이름):
  - `GNLinkSetup-0.2.105.exe` — **4,127,744 B** · sha256 `f4c0ef6b1f53f615c80a735d48757e0a2d91a6c64c45fad0cc572d50865ad253`
  - `GNLink-0.2.13.apk` — **11,501,102 B** · sha256 `4da45491800a891be8569d57b751448fca7f3e98cfa7cec30c6bda6c2cc14d2b`
- **직접 확인한 것**:
  - 설치기 안에 **0.2.105 가 3건, 0.2.104 는 0건**(UTF-16 스캔). payload 이름 **`GNLinkUpdater.exe`·`GNLinkHost.exe`·`ui\\shell.html`·`macro.html`** 존재 — `ui\\shell.html` 이 **깨지지 않은 형태**로 들어 있다(#468 의 escape 결함 회귀 확인).
  - payload 스테이징 9종 전부(`GNLinkUpdater.exe` **482,816 B** 포함).
  - APK: **package `com.remote60.androiddirect`**(불변) · **versionCode 12**(배포본 11 초과) · **서명 SHA-256 `dcc806aeb30b2e3c53e4a0b96b9675f9e25af2bf071dbb37a3cb09f6b8ec2990`** — **배포본과 같은 debug keystore**, `assembleDebug` 산출물. **새 키를 만들지 않았다.**
- ⚠️ **이 후보는 지금 업데이트를 확인하지도 설치하지도 못한다. 그리고 그것이 정상 동작이다.**
  - `trusted_public_key_hex()` 가 **빈 문자열**이고(`update_manifest.cpp:300`, "Deliberately empty"), `default_verifier()` 는 **키 길이가 안 맞으면 거부**한다(`key.size() != kP256PublicKeyBytes` → `false`). 따라서 **어떤 manifest 도 서명 검증을 통과하지 못한다.**
  - Android 도 같다: `UPDATE_MANIFEST_URL`·`UPDATE_PUBLIC_KEY_HEX` 가 **빈 기본값**이고, `UpdateFlow` 는 "이 빌드에는 업데이트 엔드포인트도 신뢰키도 없다" 를 보고하고 **아무것도 하지 않는다.**
  - **fail-closed 를 유지한 결과지 결함이 아니다.** 운영 서명키는 승인이 필요한 결정이고 이 코드가 내릴 결정이 아니다.
- ⚠️ **"자동업데이트 실사용 준비완료" 로 읽지 말 것.** 이번 승인 범위는 **구현·격리검증**이고 **실제 자동업데이트 배포 승인이 아니다.** Codex 도 `837ac5f..bbeefe8` handoff diff 를 직접 대조한 것이지 **약 23k 전수 감사를 한 것이 아니라고 명시**했다.
- **실기에서만 확인 가능한 것(그대로 남는다)**: UAC 추가 창 0 · 원 사용자 컨텍스트 Client 실행 · 운영 서비스 등록 · 실 HTTPS 경로 · 실기기 APK 설치 · 운영키/배포 인프라 의존 항목 · **백업 홀더 — 현재 재현되지 않으며 원인 미특정**.
- **테스트가 기계에 한 일(3건)과 현재 경계**: 저장소 밖 `%TEMP%` 삭제(#474 해소) · 이름만으로 프로세스 종료(#471 해소) · 라이브와 같은 전역 뮤텍스 점유(#475 해소). 지금은 **테스트 root 가 저장소 안**이고, **경로가 확인된 것만** 종료하며, **세션 범위 lock** 을 쓴다. **과거 실행이 남긴 잔재 4건**(`%TEMP%\\gnlink-exec-*` 3 · `.claude/scenario-runs/scn-27308` 1)은 **지시대로 삭제하지 않았다.**
- **하지 않은 것**: 설치·실행·게시·push·라이브 조작. **0.2.104 는 그대로**다(PID 3종 불변).
- 변경 파일: `apps/native_poc/src/product_version.hpp` · `apps/android_direct_client/app/build.gradle.kts` · `dist/GNLinkSetup-0.2.105.exe`(신규) · `dist/GNLink-0.2.13.apk`(신규) · `docs/history.md` · `docs/구현계획.md`.
'''
assert '### 479)' not in d
d = d.rstrip('\n') + '\n' + entry
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('history 479')
