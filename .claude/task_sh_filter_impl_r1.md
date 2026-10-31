# task sh-filter-impl r1 — 휴대폰 화면 선명화 필터 구현 (검증용 remote#wwml7h91, 2026-10-22) — 담당 작업용2

**이 파일이 정본이다. 끝까지 읽고 CLAUDE.md 대로 진행한다.**
설계 확정: `.claude/codex_sh_filter_r1.md`(S1~S4 + 구현 계약 1~8 + S4 측정·완료 기준) — **구속 계약**. 그 파일 머리의 "사용자 보류" 는 **해제됐다**(사용자 10-22: "sh 결과 반영해서 작업시켜. 일단 0.2 0.35 선택되게 둘 다 봐 보자").
배경: `.claude/proposal_sh_filter_r1.md`(현재 경로·제안), OSLink 분석 `C:\work\oslink-analysis\android\q8_sharpen_renderer.md`(참고만 — 코드 복제 금지, 관찰한 수식만 우리 코드로).

## 사용자 결정과 Codex 계약에서 바뀐 점 (이 task 의 델타)
1. **커널 = 5탭 십자 선명화**(OSLink 에서 관찰한 형태, CAS 대신): `out = (1+4s)·C − s·(L+R+T+B)`, RGB 에 적용, alpha 1. 이웃 간격 = **보이는 소스 1 pixel**(계약 1 그대로: crop·회전 반영, padding 금지, 외곽 tap 은 유효 crop 에 clamp). 출력은 0..1 clamp — halo/ringing 은 계약 2 대로 실제 화면으로 비교한다.
2. **강도 선택지 = 끄기 / 약하게(s=0.20) / 강하게(s=0.35)**. 사용자가 0.20 과 0.35 를 직접 비교하려는 것이므로 설정 화면에 값이 보이게(예: "선명도: 끄기 · 약하게 0.20 · 강하게 0.35"). 미설정 기본 = 약하게(0.20), 명시적 끄기 보존(계약 S2).
3. 그 밖(기하 불변·끄기=기존 direct·producer 전환 직렬화·GL 소유·rebind/park 의미·표시 단계 구분·자동 fallback·데이터량 의미)은 Codex 계약 1~8 그대로.

## 사용자 행동 acceptance
1. 휴대폰 뷰어 설정에서 선명도를 끄기/0.20/0.35 로 고르면 바로(정적 화면 포함) 화면에 반영된다. 끄기↔켜기 전환은 짧은 재연결 깜박임이 있어도 화면이 돌아온다.
2. 0.20/0.35 에서 가는 글씨가 끄기보다 또렷하다(같은 원격 화면 비교 스크린샷).
3. 확대·이동·회전·크게보기에서 터치한 곳이 원격의 같은 곳을 누른다(기하 불변).
4. GL 이 실패하면 "선명도 적용 불가, 원본 표시" 가 짧게 보이고 원본 화면이 계속 나온다.
5. 끊김·멈춤·발열이 끄기 대비 눈에 띄게 늘지 않는다(S4 기준: codec handoff→TextureView latch p95 증가 ≤ 4 ms 등).

## 시험 (Codex S4 그대로, 요약)
- AVD 실제 APK UI: off/0.20/0.35 같은 프레임 비교(얇은 한글/영문·저대비·1px 선·평탄색·고대비 경계·움직이는 영상), 1088 crop·회전·확대 이동 후 원격 탭 위치, 반복 수명(surface 재생성·전환·park/resume·target 전환) 누수·stale frame, 실패 주입 → direct 새 프레임.
- 지연: frame PTS/id+generation 매칭으로 codec handoff→최종 latch A/B, 선택 fps(30/45/60) 구분, 10분 연속 자원·열 비교(AVD 에서 불가한 항목은 unavailable 로).
- 실기(SM-S948N): **준비만** — 설치는 검증용 경유 사용자 승인 뒤. 폰 결과 없이 4 ms·발열 통과 주장 금지.
- 출하 gate: 시험 seam·fixture 강제값이 게시 APK 에 없음(GA: 실제 게시 산출물 기준).

## 규칙
- worktree: main `5063cd9`(또는 그 위 docs commit) 에서 새 worktree·브랜치(예: `C:/work/gnlink-wt/feat-sh-filter`, `feat/sh-filter`). Android 빌드 환경은 메모리 규칙(JAVA_HOME=Android Studio jbr, ANDROID_HOME).
- **APK 버전 번호·keepalive-multi 동반·A6 대응(휴대폰 잠금 화면 비밀번호)은 사용자 결정 미결** — versionName/versionCode 를 올리지 않는다. 게시·서명·push·사용자 폰 설치 금지.
- 측정 슬롯·AVD 사용은 기존 규칙(CreateNew SLOT_REQUEST). 이 PC 의 다른 세션 UI 시험(작업용1 LK)과 포그라운드 충돌 주의.
- 커밋 Git MCP(`.claude/mcp_git_client_wt.py <worktree>`), history 항목은 task 커밋에. 작업목록 SH 행은 검증용이 통합 때 갱신.
- dxgi-wedge-fix(t-dreqrszk)는 검증용 독립 검사 중 — NEEDS_CHANGES 가 오면 그쪽을 먼저 처리하고 SH 로 돌아온다.

## 보고 (1회, 기능 완성 시)
`D:/remote/remote/.claude/report_sh_filter_impl_r1.md` + `remote#wwml7h91` pin. commit·제품/테스트/문서 구분, acceptance 1~5 증거(AVD UI / 순수 / 실기 대기 구분), off/0.20/0.35 비교 스크린샷 위치, 지연·자원 수치(샘플 수 포함), 실패 주입 결과, 출하 gate, 폰 실기 묶음 절차.

## r2 보수 (2026-10-22) — Codex c568fbb+2e9428b NEEDS_CHANGES (정본 `.claude/codex_sh_filter_impl_r1.md`, H1~H5 한 묶음) — **DW r2 를 먼저 끝낸 뒤**
- **H1** releaseTimeout 뒤 상태가 NONE 이 되어 다음 sync bind 가 옛 EGL 이 아직 소유한 surface 에 새 producer 를 붙이려 함 → generation 별 blocked/release 소유, 늦은 callback·다중 surface 정리.
- **H2** 표시 확인 gate 가 첫 latch 뒤 baseline 을 잡아 두 번째 frame 을 요구하고 총 카운터라 옛 target 으로도 통과 가능, VIEWER 상태 off→weak 의 never-draw 감시 없음 → frame/selection/surface 별 확인.
- **H3** redraw swap 수를 다음 임의 latch 에서 빼면 coalesce 시 새 video 계수가 오염 → level·세대별 분포 분리, 전체 구간 p95 보존(지금 raw 는 5 s 창 p95 요약이고 +18.5 는 그 중앙값 차).
- **H4** gate 직접 재현: noseam 과 shseam 둘 다 `--apk` ALL PASS 5/rc0 → shseam 을 잡지 못함. Gradle release 빌드도 test source 선택을 거부하지 않음 → 출하 차단 추가.
- **H5** 폰 전 자동화 보강: 실 APK 터치 → host 좌표, 생산 shader 의 crop/flip/padding fixture, resize defer 중 source 크기 ≠ EGL 크기 탭 보수, swap:30 정확한 Toast 문구 대조, H1/H2 독립 UI 반례. 21 fps 입력으로 30/45/60 PASS 라고 하지 않음. AVD 미달·실폰 미관측은 그대로 분리 보고.
- 별건: 수동 IP 10 s 정지는 ledger 등재(검증용) — 이 브랜치에 host 수정 섞지 않음, 11.5 s 우회를 "수동 접속 성공" 으로 쓰지 않음.
- 버전·게시·폰 설치·S5 미결 그대로. 보고: `.claude/report_sh_filter_impl_r2.md` + 검증용 pin.

## r3 보수 (2026-10-22) — Codex 7609d0b NEEDS_CHANGES (정본 `.claude/codex_sh_filter_impl_r2.md`, R1~R3 한 묶음)
- **R1** Gradle seam 차단 우회: `-Pgnlink.shTestSeam=true :app:assembleRel --dry-run` 이 rc0 BUILD SUCCESSFUL(실제 graph 에 packageRelease/assembleRelease 포함). 완전명 assembleRelease 만 거부됨. startParameter 문자열 대신 **resolved release variant/task** 단위로 차단.
- **R2** native buffer release 뒤에 handoff ring 을 공개하는 경합 — 그 사이 첫 GL/UI latch 가 info null 로 지나가고 timestamp 만 기록돼 재callback 이 redraw 로 early return → 정지 원본이면 불필요한 3 s fallback. metadata-before-buffer 또는 bounded 동일 latch 재평가 + 지연 반례(실측 발생 주장은 아님).
- **R3** 남은 자동 검증: 생산 shader 의 crop/flip/padding fixture(decoder 1088 미지원은 생략 근거 아님 — 별도 fixture), native 입력 큐 → Host 좌표 수신 확인. 사용자 바탕화면 주입 불필요. (핵심 AVD 독립 실행은 검증용이 보수 후보로 한다.)
- 기록 정정: 최종 final3 identity 는 mean 0.179 / p99 3 / max 223(전체 picture) — 보고의 "움직이는 띠 밖 0.10 / max 10" 과 다름. 최종값과 영역을 정정.
- 버전·설치·서명·게시·push 없음. 무관 전량 반복 불필요. 보고: `.claude/report_sh_filter_impl_r3.md` + 검증용 pin.
