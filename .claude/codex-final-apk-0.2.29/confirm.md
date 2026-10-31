# APK 0.2.29 — Codex 최종 게시 근거 확인

판정: **배포 완료(마일스톤) · 휴대폰 실기 대기.** 확인 시각은 `result.json`의 2026-10-15T02:18:32.959705Z(한국 11:18)이다. 사용자 설치/재시작은 수행하지 않았다.

## 직접 확인

- 소스 승인 `0e5ddae68e96c0bd5aff56c880f6623c569ea59d` → 릴리스 `de5a0357cdd9c56fdfe1416c4a9fdc7ddd89a0da`. 비교 변경은 APK Gradle 버전 두 줄과 docs/작업목록.md뿐이다. origin/main을 ls-remote로 직접 확인했고 de5a035와 일치했다.
- NAS에서 읽은 Android manifest/sig가 로컬 게시본과 일치한다. 제품에 포함된 운영 공개키를 읽는 기존 검증기로 원문 서명 수락 및 변조 manifest 거절을 직접 확인했다. 공개키 환경 override는 제거했다.
- 공개 HTTPS에서 익명으로 내려받은 APK: HTTP200, **12,562,092B**, SHA256 **942b898ff8a3eb73f47e9ed6ba77f0041e79fa5895f754dd2fe480b60191b020**. signed manifest, 로컬 payload, 보존 dist의 바이트가 모두 같다.
- 내려받은 APK를 apksigner로 직접 검증했다. 인증서 SHA256 **dcc806aeb30b2e3c53e4a0b96b9675f9e25af2bf071dbb37a3cb09f6b8ec2990**, 기존 인증서와 같다. aapt2로 com.remote60.androiddirect / versionName **0.2.29** / versionCode **28**, ZIP에서 arm64-v8a/armeabi-v7a/x86/x86_64의 제품 native lib를 확인했다.
- NAS Windows manifest/sig는 앞서 확인한 0.2.158과 바이트가 같다. 이 확인에서 PC payload를 다시 게시하거나 PC 앱을 조작하지 않았다.
- main의 Android test XML 16 suite를 읽어177 tests / failures0 / errors0 / skipped0을 확인했다. 이것은 빌드 결과 원문 확인이며 Codex의 테스트 재실행은 아니다.

## 고정 hash / 위치

- Android manifest: `f82318f9d02abb4cc58ea0d03daefe2553a1e2ee607ed2634ba3a65e7bb11523`
- Android sig: `d5534f81e74cd94aaf8a8de1e31b252ec00eb7076831ebe5aa6926d92cd77897`
- Windows manifest: `78df5305b70cc429e6779d484a4a534bf13875d81e05e2fc6e5f8da263eb9306`
- URL: https://gnlink.shotan.net/updates/0.2.29/GNLink-0.2.29.apk
- 보존 파일: `D:/remote/remote/dist/GNLink-0.2.29.apk`
- 실행/결과: 이 폴더의 verify.py / result.json / apk-certificate.txt / apk-badging.txt. verify.py exit0, all_checks_pass=true.
- deploy 로그 원문에서 0.2.28 manifest/sig 백업 및 서버 재시작 없음도 확인했다. 백업 파일 자체를 이 확인에서 다시 내려받은 것은 아니다.

## 효과와 미검증

매초 수신/decode 통계를 보존해 보내고, replay·무효 전환·startup grace·짧은 표본을 실제 측정 구간으로 처리하여 건강한 모바일 접속의 잘못된 ABR 강등을 줄이는 변경이다. 소스 판정과 검증용 독립 core/AVD 근거는 `fix-mobile-abr/.claude/codex_mobile_abr_0e5ddae.md`에 있다. 이번 외부 검증이 AVD나 실폰 행동을 재실행한 것은 아니다.

실기기 MediaCodec/화면 표시, 장시간/WAN, 다른 selection 거절 사유와 실제 앱 UI, 미측정 latency/tail 및 F2의 시험 환경 세부 원인은 미확인으로 남긴다. 고정 Host selection_locked 거절 왕복은 검증용 run1/run3에서 확인했다. 구 APK는 업데이트 전까지 기존 경로를 쓴다. 글자 정제·IDR 비용 및 keepalive는 이 릴리스의 해결 범위가 아니다.

사용자 다음 행동: **휴대폰 앱을 0.2.29로 업데이트하고 평소 호스트에1~2분 접속**. 검증용은 사용 구간의 보고 주기·clientDecodeValid·ABR evidence를 확인한다. 유실/실제 decode 정지에 대한 정당한 강등까지 없어야 한다는 뜻은 아니다.

## 기록 잔여

현재 main docs/작업목록.md의 MA는 아직 “게시 대기”이고 history에는 최종 게시 항목이 없다. 검증용은 위 검증된 사실로 게시 이력과 해당 상태를 갱신하고, docs/구현계획.md에는 대응 진행 상태만 반영한다. 새 제품 변경 없이 docs 전용 기록/commit으로 마무리할 수 있다. 실폰 검증 전 task 전체 완료로 바꾸지 않는다. 이 확인서는 소스/릴리스 승인 범위를 넘어서는 추가 배포·사용자 설치 권한을 부여하지 않는다.
