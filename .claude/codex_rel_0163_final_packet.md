# PC 0.2.163 게시 최종 증거 확인

> **Finding-3 추가:** 게시·서명/hash·main/dist 사실은 유지하되 실제 Host capture/render의 재확인에는 독립 wake/전원 효과 차단도 포함한다. 아래 배포 확인은 무조작 행동 검증 완료를 뜻하지 않는다. [Finding-3](codex_test_injection_finding.md).

> **추가 정정(2026-10-28 Finding-2): 게시·서명/해시·외부 도달·main/dist 정합 확인은 유지한다.** 다만 prelogin 시험을 사용한 PS/LK 등의 행동 EVIDENCE는 안전한 재확인 대기이며, 아래 배포 마일스톤을 무주입 격리/전체 행동 검증 완료로 읽지 않는다. [입력 주입 Finding-2](codex_test_injection_finding.md) 참조.

2026-10-26 / 검증용 remote#yj8mrmrx FINAL_PACKET / build **fae0e1fc787456e852af37af35de7b19f87f01ac** / 게시 기록·main **5970f046f56dd8f55b98b4ab971ed7376c6d5848**.

**PC 0.2.163 배포 완료(마일스톤)·사용자 실기 대기.** SOURCE/EVIDENCE 판정은 codex_rel_0163_evidence_r2.md를 유지한다. 실제 UAC/Winlogon 성공이나 APK/R2 전체 작업 완료로 확대하지 않는다.

## Codex 직접 확인

- deploy_0163.log 전문의 실제 게시·서버11개 검증·이전0.2.162 pair 백업·외부 HTTPS11개 검증을 대조했다.401 응답은 배포 증거로 쓰지 않는다.
- 로컬 signed manifest SHA256은 **6d89ae487a125ebc620e46e6be5396cb3131dc6380878b596a85b47edf64dfd3**, windows.sig는 **57d1d2ca5b34568e580ea0676504e4aed7583610870b2b0f50f4716a0174d01a**. 승인한 manifest 본문 hash가 서명 전후 동일하다.
- 최종 후보의 gnlink_verify_manifest.js를 현 signed pair에 직접 실행해 rc0, version0.2.163/release r-0.2.163/artifacts11 확인. 이 verifier 안의 한 글자 변조 거부 대조도 통과했다. C++ verifier의 정상·변조 거부는 검증용 직접 실행 보고에 귀속한다.
- **Codex가 manifest의 공개 HTTPS11개 URL을 별도로 GET**해 모두 status200·크기/hash 일치11/11을 확인했다. 서명된 목록과 같은 현물이다.
- 로컬 main·origin/main과 git ls-remote origin refs/heads/main이 모두 **5970f046f56dd8f55b98b4ab971ed7376c6d5848**로 일치한다.5970f04는 history/작업목록/계획의 게시 기록3파일 commit이며 제품 변경이 없다.
- dist/GNLinkSetup-0.2.163.exe SHA256 **907e5702ac744c558e023f11d9751a8f7e2a3d6d5795123cbb2729ab8979cc1d**, 승인 payload Setup과 일치한다. 공개 설치 URL은 https://gnlink.shotan.net/updates/0.2.163/GNLinkSetup.exe 이다. manifest 서명과 Setup Authenticode는 별개다.

## 검증용 보고에 귀속한 서버 상태

검증용은 직접 ssh로 서버 디스크 Windows pair6d89ae48/57d1d2ca와 Android pair309a9c5e/cbb5b7d6 불변을 확인했다. Android는0.2.31 그대로다.0.2.162 Windows pair814e4bb9/a39b2f14가 백업됐다. Codex가 해당 ssh를 재실행했다고 주장하지 않는다.

## 반영 내용·남은 실기

반영은 capture-lifecycle PS-A/B·UA-1/2, 로그인 전 PC picker의 영구 ack 대기 해소, 같은 연결의 LK-UX 비밀번호 재시도다. 실제 renderer 독립 검사와 원본/IDR/present 연결은 EVIDENCE r2의 근거를 유지한다.

사용자 설치·재시작은 자동 수행하지 않았다. 호스트/뷰어를0.2.163으로 업데이트한 뒤 동일 연결의 UAC 예/아니오 표시·승인/취소 후 화면 복귀를 확인한다. 실제 재부팅 로그인 전 picker→바탕화면, 실제 잠금 화면 틀린 비밀번호→재시도→해제, 폰0.2.31 WGC 세션 뒤 PC가 기본DXGI로 시작하는지 및 회사 PC는 미검증이다. 실제 SYSTEM/Winlogon·LogonUI/SCM 권한 경로도 fixture 성공으로 대신하지 않는다.

APK0.2.32(SH r2·UA-2·shared picker 선택 보수)는 아직 미게시/미완료다. KA/NP도 R2의 별도 진행 항목이다. 작업용2 오프라인에 따른 APK 담당/실행 연결은 검증용의 기존 창구에서 정리하며 PC 게시 완료로 전체 목표를 닫지 않는다. DW 영속 격리 기록 실패 ledger도 별도로 유지한다.

Codex는 최종 hash/서명 verifier·공개 HTTPS GET·remote ref·기록을 직접 확인했고, 제품 build/설치·서명/배포·commit/push를 대신 실행하지 않았다. 공개 기록/Git MCP commit·push는 검증용이 완료했으므로 추가 중복 commit은 만들지 않는다.
