# 검증용 독립 검사 — PC 0.2.163 릴리스 후보 (remote#yj8mrmrx, 2026-10-26)

## 고정 대상
- `rel/0.2.163` @ **90192f4**(빌드 기준) + 66bb5e9(gate·docs, `apps libs` diff 0). worktree `C:/work/gnlink-wt/rel-0163`, tracked clean.
- rel 디렉터리 `C:/work/gnlink-wt/rel-0163/.claude/rel/0.2.163/`: windows.manifest(미서명) `5ea1ac74…f60a476`, SHA256SUMS.txt `9b1a21dd…f3c172b5` — **`sha256sum -c` 11/11 OK(검증용 직접)**.
- 시험 빌드(product-equivalent test build): 같은 commit 90192f4 의 `build-test`(출하 build 와 분리). 시험 exe 해시는 `hashes_before.txt`, 세 번의 실행 전후 불변(5/5 OK). **출하 exe 해시와 다름 — CLAUDE.md 허용 범위**: 같은 source commit, 시험 seam(`REMOTE60_STREAM_TEST_SEAM` 등 대역)만 차이. 수신 smoke 는 **출하 payload 의 GNLinkStream.exe**(`ddc5b399…`)로 실행.

## 직접 실행 결과 (이 PC 콘솔 세션 `qwinsta` console Active·RDP Listen 만, SLOT_REQUEST CreateNew→삭제 3회, 다른 시험 프로세스 0)
| 시험 | rc | 마지막 요약 |
|---|---|---|
| 버전 문자열(payload, UTF-16/ASCII) | — | Viewer/Stream/Host/Client `0.2.163`=1, Setup 5, `0.2.162`=0 전부 |
| 출하 gate `--dir payload` | 0 | ALL PASS (9 checks) |
| gate `--self-test` | 0 | ALL PASS (51 checks) |
| PS `viewer_prelogin_login_e2e_test --still-screen` | 0 | ALL PASS (32) — 선택 뒤 첫 디코드 A 486 ms / B 154 ms / B2 185 ms, 3 s 에 5 프레임(500 ms), 워커 생존(rc 15/14 없음), WGC 요청 0, luma spread 209 |
| UA `host_capture_lifecycle_ua_e2e_test` | 0 | ALL PASS (54) — WGC·DXGI 각 2왕복, 변화 뒤 새 캡처 IDR 디코드 220~414 ms, 같은 연결(epoch 1), 상태 응답 requested=2 effective=2 active=2 flags=1 |
| UA `--fullframe-fails` | 0 | ALL PASS (18) |
| LK `--lock-screen` | 0 | ALL PASS (68) |
| LK prelogin(기본) | 0 | ALL PASS (79) |
| `viewer_udp_recovery_test --real-recv-smoke <payload GNLinkStream.exe>` | 0 | PASS |
- 1차 실행은 `REMOTE60_ALLOW_HOST_E2E`·`REMOTE60_ALLOW_UI_TEST` 미설정으로 전부 SKIPPED(real-recv 는 skip 인데 rc 0 — 결과 줄로 판정). 판정은 설정 뒤 실행만.
- 원본 출력: 이 폴더 `*.txt`.

## 증명하지 못하는 것 (실기 대기)
- 실제 SYSTEM·Winlogon 로그인 전 화면(시험은 시험 사용자 Default 를 Winlogon 이름으로 서비스), 진짜 무변화 화면에서 duplication 재생성 분기(대역으로 강제), 실제 UAC 를 WGC 가 찍는지(보안 데스크톱은 이름 대역), 실 LogonUI 틀린 비밀번호 → 재시도 → 해제, 비관리자 시험 빌드라 requireAdministrator·UAC·SCM 권한 경로.
- 사용자 실기 묶음(게시 뒤 1회): 실 재부팅 로그인 전 화면 picker→바탕화면 / 실 UAC 예·아니오 같은 연결 2회 / 폰(0.2.31) WGC 뒤 PC 접속 = DXGI / 실 잠금 화면 틀린 비번→재시도→해제.
