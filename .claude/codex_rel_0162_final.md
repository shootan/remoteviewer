# PC 0.2.162 게시 최종 근거 확인

2026-10-22 / 검증용 FINAL_PACKET / Codex 읽기 전용 독립 확인.

**PC 배포 완료(마일스톤) · 실기 테스트 대기.** SH 포함 APK 0.2.31은 별도 준비 중이며, 현재 Android 게시본은 0.2.30이다. 전체 요청 완료나 OS 실기 성공 판정은 아니다.

- build: `b4dfaedfa08e2023a495154fc59e7bda66272cbb`(통합 `9f54912` + 버전). main/origin: `9b46462267c11f4984522d2fcd295b280e52ffc2`. `git ls-remote origin refs/heads/main` 직접 일치.
- 서명된 manifest SHA256: `814e4bb9748c94b2ef6f63cac137779a0e20983a27583887b5555442d66bdd54`.
- windows.sig SHA256: `a39b2f14c91aba0497c32fc9076b84cbc97a4f3725a4f64f463d626bfdc8a786`.
- 보호된 배포 계정의 읽기 전용 SSH로 서버 Windows pair hash가 로컬과 일치하고 version=0.2.162임을 직접 확인했다. 같은 시점 Android pair는 기존 `b64c1b49…/210ab909…`, version=0.2.30이었다. 향후 승인된 Android 게시 단계의 변경까지 금지하는 조건은 아니다.
- 출하 verifier를 직접 실행해 정상 manifest 허용 및 1바이트 변조 `SignatureInvalid` 거부 PASS. JS verifier도 version=0.2.162/artifacts=11 PASS.
- manifest의 공개 HTTPS URL 11개를 모두 직접 요청(HTTP 200), 실제 크기와 SHA256 전부 일치. 기록: `.claude/test-tmp/codex-publish-0162/external_hashes.json`. 인증 없는 manifest API의 401을 게시 성공 근거로 사용하지 않았다.
- `dist/GNLinkSetup-0.2.162.exe` SHA256: `ed480e252b6a6a3b4910da115dc1b9f94be94e32a754949291568e07d52ae175`, 공개 Setup과 일치. Setup Authenticode는 미서명이며 업데이트 manifest 서명과 별개다.
- NAS 업로드/백업/서명 실행의 단계별 rc 및 이전 pair 보존은 검증용 기록 귀속. Codex는 게시·서명·설치·서비스 조작을 직접 수행하지 않았다.

변경 범위는 잠금 화면 전용 자격 증명 입력, DXGI 격리 및 복원, hello 지연 수정이다. OS A~D, 회사 PC/NVIDIA/실 wedge/재부팅, Android·실 LAN/NAT는 미검증이다. 명시 monitor/window는 재선택이 필요하다. 휴대폰 sealed 잠금 입력은 미구현이며 APK 0.2.31의 SH로 해소되지 않는다.

다음 담당: 검증용이 기존 작업용2 APK 릴리스 task에서 SH 포함 0.2.31 산출물·독립 검사·서명·게시·외부 검증·기록/push 후 FINAL_PACKET 전달. 실폰 S4/화질/발열 검증은 사용자 단계로 남긴다. Codex 판정서와 운영 메모만 변경했으므로 별도 제품 commit은 만들지 않는다.
