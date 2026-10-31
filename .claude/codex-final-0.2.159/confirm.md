# PC 0.2.159 — Codex 최종 외부 대조

2026-10-18 KST. **배포 완료(마일스톤) · 사용자 PC 업데이트 실기 대기.** 원시 결과 result.json(all_checks_pass=true), 재현 verify.py(exit0). 직접 확인 시작 UTC 2026-10-17T17:51:46.477310+00:00.

## 고정 소스와 산출물

- 승인 source a428abf928b257dd8c8bb66577bb88759a7b3153 → PC버전64177a78062473fb06a71ea3dda587c94ae3908f → docs a622f04b5e70cb32ea5db01ad477f8c31437b91a. ls-remote origin/main=a622f04 직접 확인.
- source→버전 diff는 apps/native_poc/src/product_version.hpp뿐. 사용자 선택 B(poll-only), C/C500·catch-up은 미포함.
- 공개 GNLinkStream SHA256 16e484d02706397549ce753a78d81ff959e87b19d926c2c300dae6faefbdb736.
- 공개 Setup 및 dist/GNLinkSetup-0.2.159.exe SHA256 984269547dcfa04b3859800d99327eedd817c983e5040a9246cec48ac0ec0b8d.

## 직접 실행한 확인

- NAS live windows.manifest e78a4fed15d4ea97d366ad0d5fdcbcefa2da02537ce5c4f48306b66734e5c7ed, windows.sig 3106d6e4f6d22e216edd7814708744df76d3ccd01eca2f17878c72fb6e5cd54d: 로컬 release와 바이트 일치.
- 운영 신뢰 공개키(환경 override 제거)로 manifest 서명 검증 및 변조 거부 exit0. version0.2.159 / releaseId r-0.2.159 / artifacts11.
- 서명 manifest의 외부 HTTPS11개를 직접 익명 다운로드: 전부 HTTP200·길이·SHA256·로컬 payload 일치. 공개 Stream과 Setup은 이 폴더에도 보존했다.
- 다운로드한 실제 GNLinkStream에 encoder_poll_build_gate.py 실행 PASS. poll 진단/catch-up/정제 연구 문자열 부재. 이 gate의 탐지 범위를 넘어 모든 미출하 기능 부재를 단정하지 않는다.
- 버전 wide문자열을 가진 exe는0.2.159, 모든 exe에0.2.158 wide문자열0. 버전 문자열이 없는 Capture/InputService/ClipHelper/Updater는 이 검사만으로 버전을 판정하지 않는다.
- Android manifest/sig는 직전 APK0.2.30 확인본과 동일. APK 자체도 익명GET200, 13,492,132B·SHA256 e6a6eecbf129f92998d5c4d4e2844057485315489c001023b24f5966a93608cf로 동일. APK 유지 지시가 실제 공개물에서 충족됨을 확인했다.

Setup URL: https://gnlink.shotan.net/updates/0.2.159/GNLinkSetup.exe

## 실행 보고와 잔여

빌드/서명/installer gate·서버 백업0.2.158 pair·통합 회귀 및 r7의 사전 고정6run은 검증용 FINAL_PACKET 인용이다. Codex가 사용자 Host나 GPU 시험을 실행하지 않았다. 원인 미확정 느린 episode는 r6 판정대로 미해결 관찰로 유지한다. 부하 겹침 run은 통상 성능 근거로 쓰지 않으며, 뒤6run은 별도 보고 근거다.

integ159의 wire_cap_defer 기존 간헐·gpu_scaler1ms cadence 환경 분류는 검증용 보고 인용, 이 외부 산출물 대조로 그 테스트를 재실행/통과시킨 것은 아니다. 같은 시각의 DIAG에서 held/deadline-cut0이었다고 trace없는 출하run의 모든 느린 episode 원인을 확정하지 않는다.

사용자가 수용한 일부 정지 후 글자품질 비용은 유지한다. 모든 버벅임/정지화질이 해결된 릴리스라고 하지 않는다. 1.5M지속모드·NVIDIA/Intel/동기MFT 실측·DesktopText·S1단발오류·실Host인앱업데이트 완주는 미확인이다.

사용자가 지금 할 일: **원격 화면을 보내는 Host PC를0.2.159로 업데이트1회**. 인앱업데이트/UAC와 실제 접속 성공은 사용자 실행 후 확인한다. APK는0.2.30 그대로이며 업데이트/재설치를 요구하지 않는다. 필요 시 검증용이 사용자 사용시각의 NAS 로그와 연결한다.

Codex는 검토·다운로드·서명/hash 대조와 이 기록만 수행했다. 제품 수정·빌드·사용자 설치/재시작·push·배포는 하지 않았다.
