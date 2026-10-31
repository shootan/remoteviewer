# task rel-0163 r1 — PC 0.2.163 통합 + 릴리스 후보(dry-run 까지) (검증용 remote#yj8mrmrx, 2026-10-26) — 담당 작업용1

**이 파일이 정본이다. 끝까지 읽고 CLAUDE.md 대로.** 상위 계획 `.claude/plan_rel_0163_2026-10-26.md`. 사용자 10-26 지시로 게시·push 까지 포괄 승인 — 단 **이 task 는 dry-run 까지**, 서명·NAS 게시·push 는 검증용 독립 검사 + Codex EVIDENCE 뒤 검증용이 한다.

## 결정: R1 을 PC 먼저 / APK 나중으로 분리
작업용2(SH r2·UA-2 APK)가 오프라인으로 미착수. 1순위 보수(로그인 전 화면·UAC·잠금 재시도)를 앱 때문에 묶어 두지 않는다. PC 뷰어는 backend 요청을 보내지 않으므로 UA-2 UI 없이 완결. 구 APK 0.2.31 의 매 접속 wgc 요청은 새 Host 에서 그 세션만 적용(호환). APK 0.2.32 는 SH r2 + UA-2 APK 완료 뒤 별도 게시.

## 입력 (Codex SOURCE OK)
- `fix/capture-lifecycle` @ **adc9482** — LK-UX(`fix/lk-retry` 8d8655e, `codex_lk_ux_retry_src_r4.md`) 포함 + capture-lifecycle(`codex_capture_lifecycle_src_r4.md`).
- 기준 `main` @ **a1c551c**(= 82b3d14 + docs PS 행).

## 할 일
1. worktree `C:/work/gnlink-wt/rel-0163`, 브랜치 `rel/0.2.163` = main a1c551c. **webview2 는 실제 복사**(`robocopy D:\remote\remote\third_party\webview2 <wt>\third_party\webview2 /E`, junction 금지, 원본 파일 수 전후 불변).
2. `git merge --no-ff fix/capture-lifecycle`(adc9482, git CLI — 두 번째 parent 보존). 충돌은 문서 양쪽 보존(작업목록 PS 행 + LK·capture 기록), 제품 충돌은 보고 후 대기.
3. 버전 commit(Git MCP, `.claude/mcp_git_client_wt.py <worktree>`): 0.2.162 버전 commit 과 같은 파일 집합을 0.2.163 으로, UTF-8 BOM·줄바꿈 바이트 유지. history/구현계획/작업목록에 0.2.163 후보(게시 전) — PS·UA(PS 행 갱신)·LK-UX 반영, 남은 실기 명시.
4. `automation/gnlink_release.sh --dry-run`(Bash, POSIX PATH) → rc·rel 디렉터리·미서명 manifest 해시·SHA256SUMS.
5. 산출물 검사:
   - 4 exe(Viewer/Stream/Host/Client) UTF-16 `0.2.163` 존재·`0.2.162` 잔존 0, payload 버전 carrier.
   - gate `--dir` + self-test — **TEST_MARKERS 합집합에 이번 시험 표식 추가**: LK(`ForTest`·LK seam 들) + capture(`GNLINK_STREAM_TEST_DESKTOP_ALIAS`·`GNLINK_STREAM_TEST_STILL_SCREEN`·`GNLINK_STREAM_TEST_SECURE_DESKTOP_EVENT`·WGC_STILL 표식/define·`TestStillOff`), 각 표식 부정 대조. 생성 vcxproj 기준 출하 타깃에 시험 define(`REMOTE60_STREAM_TEST_SEAM` 등) 0, payload 시험 exe 0, 제품에서 test hook 호출 0.
   - `ui/shell.html` 해시 = 소스 = payload.
   - 통합 영향 smoke(전량 반복 아님): 빌드 rc0, LK e2e `--lock-screen`·prelogin, capture PS `--still-screen`·UA e2e(WGC/DXGI)·`--fullframe-fails` 각 1회, real-recv-smoke 등 기존 릴리스 smoke, `dxgi_wedge_reconnect_runner` 1회(미뤄 둔 것 — AMD 크래시 주의: 에뮬레이터 동시 실행 금지).
6. 측정 슬롯 CreateNew, repo root 밖 삭제 금지, 사용자 설치본·서비스·세션·주 모니터 무조작, 서명·게시·push 금지.

## r2 (2026-10-26) — E1: 실제 renderer 표시 증거 (Codex `codex_rel_0163_evidence_r1.md` §E1, 원문 읽기)
EVIDENCE 보류 사유: PS/UA/real-recv 는 FrameBuffer 도착·lastPresented setter 까지(render telemetry d3dPresentSuccess/gdiFallbackPresented/paintEnter 0) — 실제 표시가 아님. 게시 전 한 묶음:
1. **PS**: 로그인 전 이름 대역 워커(기존 --still-screen 경로) → **실제 제품 뷰어 renderer**(GNLinkViewer 창의 present 경로 또는 그것과 같은 renderer 코드)로 picker 10 s → 바탕화면 선택 → **실제 presented ≤1 s**(d3dPresentSuccess 등 present telemetry + 창 스크린샷/marker), 이후 정지 화면 최소 갱신이 실제 present 로 이어짐, 워커 생존.
2. **UA**: 이미 WGC 인 같은 control 연결에서 secure↔Default 2왕복 → 전환별 새 source/IDR 과 **렌더된 구별 가능한 내용·스크린샷**을 연결(전환 전후 내용이 달라야 함), 옛 raw/AU 미송출 source fence 대조. lastPresented setter·디코드 총합·AU header 를 지연 그림에 붙이는 방식은 불인정.
- 제품 소스·릴리스 payload 해시 불변(90192f4 / 위 payload 표). 시험 배선만 바꾸면 그 시험 변경 commit·시험 exe 해시를 따로 고정해 명시. 기존 renderer/하네스 재사용 우선. 전량 반복 불요.
- 화면 증거는 격리 창(시험 창·제품 뷰어 창) 캡처 — **사용자 주 모니터를 덮거나 조작하지 않는다**. 설치본·서비스·실 UAC 무조작.
- 별건 정정(기록만): dxgi_wedge runner 의 `not recorded … folder-unresolved err=3` 은 "영속 쓰기 실패, 시도만 PASS" — report 에 그렇게 정정하고 후속 ledger 로.
- 보고: report_rel_0163_r1.md 'r2 E1' 절(시험 commit·exe 해시·명령·rc·요약·스크린샷 경로) + pin. 검증용이 같은 시험을 독립 실행한다.

## r3 (2026-10-26) — 로그인 전 PC 뷰어 picker 선택 결함 수정 (Codex `codex_rel_0163_e1_decision.md`, 원문 읽기)
E1 재현(report_rel_0163_r2_e1_blocker.md): PreloginV1 Host 에서 picker 바탕화면 선택 → WindowSelect 미송신(allowlist 밖) → ack 없음 → DropAwaitingAck → present 0. **0.2.163 필수 포함, 기존 후보(90192f4) 게시 보류.**
- 방향 (a) 보강: **known PreloginV1 + desktop(0) 선택 전용 local ackless selection transaction**. Begin 단순 생략·클릭 즉시 hide 금지 — pending/op epoch·decoder/첫 IDR gate 유지, **실제 받은 gen 을 해당 op 에 결합 → UI reveal 재검증 → 실제 present 뒤 restore 보고**.
- WindowSelect/List allowlist 확대·가짜 HostAck 금지. (b) MonitorSelect 대용 불가(즉시 MonitorList 응답에 gen/적용 완료 없음).
- role 이 첫 pong 전에 클릭돼도 같은 intent 를 유예 후 1회 실행 / 이미 queued 된 것은 0 으로 전환 처리. UAC·LockCred 만으로 local mode 발동 금지(PreloginV1 일 때만).
- 반례: cancel / old callback / reveal / gen 불일치 / 첫 pong 전 클릭 / 일반 Host 회귀(기존 ack 경로 그대로).
- shared/APK 의 같은 ack 대기는 좁게 확인만 해서 보고(APK 0.2.32 정본으로 취합 — 수정은 APK 단계).
- 순서: rel/0.2.163 위 제품 fix commit → Codex SOURCE → 재빌드·재 dry-run(fresh hash) → PS E1 실 renderer(수정 전 3/3 FAIL 보존) + UA E1 → 검증용 독립 실 render E1.

## 보고 (1회)
`.claude/report_rel_0163_r1.md` + remote#yj8mrmrx pin: 브랜치·merge/버전 commit·BOM, dry-run rc·rel 디렉터리·manifest 해시·SHA256SUMS, exe/Setup 해시, 버전 문자열·gate/defines/seam/HTML/smoke 결과. 그 뒤 KA r2 A 보수(S1~S4 + relay ⓐ, `task_keepalive_r2.md` '10-26 갱신')로 진행.
