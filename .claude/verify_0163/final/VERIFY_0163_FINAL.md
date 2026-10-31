# 검증용 독립 검사 — PC 0.2.163 최종 후보 (remote#yj8mrmrx, 2026-10-26)

## 고정 대상
- `rel/0.2.163` HEAD **fae0e1fc787456e852af37af35de7b19f87f01ac** (제품 eefac09 · 시험 0854ac4 · docs; tracked clean). 이전 후보 90192f4 산출물은 폐기.
- rel `C:/work/gnlink-wt/rel-0163/.claude/rel-final/0.2.163/`: windows.manifest(미서명) `6d89ae48…dfd3`, SHA256SUMS.txt `2a3f5163…6c0d` — **`sha256sum -c` 11/11 OK(직접)**. payload GNLinkStream `825feb97…`, shell.html `8fac49cd…`.
- 시험 빌드 `build-test`(같은 commit) 시험 exe 6개 해시 = 작업용 보고와 일치, 실행 뒤 재해시 6/6 동일(`hashes_before.txt`). 출하 exe 와 해시가 다른 product-equivalent test build — 차이는 시험 seam 뿐.

## 직접 실행 (콘솔 세션, RDP 없음, SLOT_REQUEST CreateNew→삭제, env REMOTE60_ALLOW_HOST_E2E=1·REMOTE60_ALLOW_UI_TEST=1, 시험 창은 모든 모니터 밖)
| 시험 | rc | 결과 |
|---|---|---|
| 버전 문자열 | — | Viewer/Stream/Host/Client 0.2.163=1, Setup 5, 0.2.162=0 |
| gate `--dir payload` / `--self-test` | 0/0 | 9/9 · 51/51 |
| selection gate 단위 | 0 | PASS |
| window proc isolated | 0 | 55/0 |
| **PS E1 실 renderer** `--still-screen --render` | 0 | **39/39** — 카드 클릭 → 제품 renderer present **287 ms**(d3dPresentSuccess 6→7), 10 s picker 뒤 재선택 297 ms, 이후 3 s 4~6 present, 역할 전 클릭 1회 실행(pong 4 s 보류 → 3494 ms), R3 (4)(5)(6) 취소·새 op 경계 version 불변/대기, present 실패 0 |
| **UA E1 실 renderer** `--render` | 0 | **28/28** — WGC 같은 연결 secure↔Default 2왕복, 전환마다 새 attachment 프레임 present(adopted=presented captureUs), 스크린샷 새 색 100%·이전 0% |
| LK `--lock-screen` / prelogin | 0/0 | 68/0 · 79/0 |
| UA e2e / `--fullframe-fails` | 0/0 | 54/0 · 18/0 |
| real-recv-smoke(payload GNLinkStream) | 0 | PASS |
- 스크린샷(검증용 실행): `ps_e1/E1-1-picker.bmp`(picker) → `E1-2-presented.bmp`(제품 뷰어 창에 원격 화면 표시 — 시험 host 가 이 PC 화면을 로그인 화면 이름으로 서비스하므로 내용은 이 PC 화면; 로컬 보관만), `ua_e1/E1-UA-0..4`(red→green→blue→yellow→magenta). 원본 출력 `*.txt`.

## 증명하지 못하는 것 (사용자 실기 대기)
실제 SYSTEM·Winlogon 로그인 전 화면(재부팅), 진짜 무변화 화면의 duplication 재생성(대역), 실제 UAC 를 WGC 가 찍는지, 실 LogonUI 틀린 비밀번호 → 재시도 → 해제, requireAdministrator·UAC·SCM 권한 경로(비관리자 시험 빌드), 회사 PC.
