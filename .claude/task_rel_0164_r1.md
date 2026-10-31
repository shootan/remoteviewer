# task rel-0164 r1 — PC 0.2.164 통합 후보 + directory server 배포 준비 (검증용 remote#7fsfy22z, 2026-10-29) — 담당 작업용2

**이 파일이 정본이다.** 끝까지 읽고 CLAUDE.md·AGENTS.md 대로 진행한다. 게시·push·서버 배포는 하지 않는다(검증용 독립 확인과 Codex EVIDENCE 뒤 검증용이 한다).

## 왜
- 사용자 지시(10-26): "1순위, 2순위 … 작업용들한테 맡겨서 빌드하고 게시까지 한 다음 보고해." R1(PC 0.2.163·APK 0.2.32)은 게시를 마쳤다. 이것은 R2 다.
- R2 의 APK 0.2.33 은 decoder liveness 보수와 decode 중 control 종료 조사가 끝날 때까지 **보류**다(Codex `codex_apk_decoder_liveness_design_r3.md`). 그래서 R1 때처럼 **PC 먼저 분리**하는 안이다. 분리가 허용되는지는 Codex 판정을 함께 받는다. 판정이 나오기 전에는 dry-run 까지만 한다.

## 사용자 행동 acceptance
- PC 를 0.2.164 로 업데이트하면, 연결 중이던 PC 에서 목록으로 돌아가도 그 연결이 대기(standby)로 남고, 다시 누르면 즉시 화면이 나온다(최대 3대). ⚠️ 출하 스위치 `kPrepareRecentOnSignIn=false` — 로그인 직후 최근 3대 **자동 준비는 꺼져 있다**. 자동 준비 완료를 주장하지 않고, 검증 뒤 flag 만 ON 으로 바꾸지 않는다.
- 직접 연결이 가능하면 직접, 아니면 relay 로 빨리(NP) 붙는다. 디렉터리 재시도가 멈추지 않는다(dirretry).
- 새 PC client(0.2.164)에서 구 버전 Host(0.2.163 이하)로 목록 복귀를 하면 연결이 닫힌다. 기존의 5분 park 는 없다(Codex 결정 ⓐ). 이 내용은 **사용자 안내 필수**다. ⚠️ 아직 옛 배선인 APK 0.2.32 의 park 까지 사라진다고 안내하지 않는다.
- 휴대폰 APK 0.2.32(게시본)와 PC 0.2.163 client 가 새 서버에서 계속 동작한다(호환).

## 통합 범위 (각 구성 단위는 SOURCE·EVIDENCE OK)
- 기준: 로컬 main **a0f991a**(origin/main 6fed2a3 위 docs 5개; 미push).
- 병합:
  - ka-r3 `spike/host-admission-r3` **ccaf745**: KA 공용, 안전 하네스, `--host-power-management`, os_input_effects, record-only build.
  - ka-pc `feat/ka-pc` **320bc36**: KA PC managed viewer. 안에 ka-r3 b8dcc93 병합이 있으므로 ccaf745 를 먼저 병합하면 정리된다.
  - np `feat/np-early-relay` **e16ad0d**: NP. ka-r3 0a5e71d 위에 있다.
  - dirretry `fix/directory-retry-r2` **34f22de**.
  - maint `fix/maint-r1` **e58bb29**.
- ⚠️ 충돌 주의 `directory_client.cpp`: KA 의 punch worker(6c3cb5a·a7773e6 계열, Stop 때 worker stop/join/queue clear)와 dirretry 의 cycleCv_(d11ef62/d6bb951, Stop 에서 notify)를 **둘 다 보존**한다. PC 즉시 재유지에서 31 s 우회가 제거됐는지도 확인한다.
- 제품 파일 충돌은 해소 근거를 file:line 으로 적는다(어느 쪽을 택했는지, 둘을 합쳤는지).
- 버전: PC 만 0.2.164 로 올린다. Android versionName/Code 는 그대로 둔다.
- directory server(`apps/directory/`): 이번 통합본의 server.js 와 의존 모듈 전량(server.js·update_manifest.js·version_compare.js·package.json 등)의 변경 요약을 적는다. 구 client(PC 0.2.163, APK 0.2.32)와의 wire 호환 근거를 적는다. 배포는 하지 않는다.

## 검사 (자체 검증, 사용자 행동 우선)
- 빌드: Release 전체(작업 폴더 build), 출하 gate(`gnlink_check_fixed_server.py`, self-test 는 REMOTE60_TEST_TMP 안에서만), 시험 표식 부재.
- 순수 시험: 각 구성 단위가 SOURCE 때 낸 순수 시험 전부를 통합본에서 다시 돌린다. 숫자는 rc·^PASS·요약 줄을 같이 적는다.
- Host 를 띄우는 e2e: 공용 guard(input none + power off + 두 줄 gate + job)를 쓴 하네스만, 측정 슬롯 CreateNew 아래에서 돌린다. 범위는 KA admission(product-client·real-directory), punch_reply, NP e2e, dirretry e2e, ka_pc_managed·shell 이다. old-host 는 NOT RUN, real4 는 REFUSED, viewer_prelogin_login 은 금지다.
- release dry-run: 기존 절차(`automation/gnlink_release*`)로 서명 전 후보와 SUMS 를 만들고 payload 의 실제 버전 문자열(UTF-16 "0.2.164")을 exe 에서 확인한다.
- 실행하는 PC 의 설치 제품(GNLinkHost/Stream)·서비스·세션은 건드리지 않는다. 저장소 밖 삭제 0, %TEMP% 생성·삭제 0.

## 산출
- 보고 `.claude/report_rel_0164_r1.md`: 통합 commit, 충돌 해소 표, 버전, 후보 hash(SUMS), 시험 결과(원문 경로·rc·요약), server.js 변경·호환, 사용자 안내 문구 초안, 미검증 목록.
- 회신: commit 해시 + 보고 sha256 + 핵심 5줄. 검증용이 독립 확인 → Codex EVIDENCE → 서명·배포·push 를 한다.

## r2 — Codex 분리 결정 반영 (`.claude/codex_rel_0164_split_decision.md`, 2026-10-29)
- PC 0.2.164 + server 선게시 분리는 **허용**. APK 0.2.33 보류는 유지. 새 Host standby 와 APK 0.2.32 ordinary 는 설계상 공존하지만 **통합 호환 EVIDENCE 필수**.
- 검증용 독립 lane(이 dry-run 뒤 검증용이 고정 후보로 돈다 — 작업용 자체 검증과 별개): final Release/flag/marker · directory_client punch+cycle Stop/retry · KA admission(product-client/real-directory)/punch · ka_pc shell+managed(V1 포함) · directory/NP legacy/fence · maint/gate sandbox · **구 APK 0.2.32 를 실제 시험 AVD 에서 새 Host·새 server 로 ordinary 연결/선택/영상/control/park-resume, PC standby 무간섭**. 과거 순수·변이·일반 8 을 양쪽에서 전량 반복하지 않는다. 구 Host 게시본 실행은 계속 NOT RUN(safe fixture 로 한계 표시).
- 작업용 dry-run 에는 위 lane 을 돌릴 수 있게 **고정 artifact·hash·실행 절차**를 보고서에 정리해 둔다(구 APK 0.2.32 는 dist/GNLink-0.2.32.apk 1a3eee63… 를 쓰고, 시험 AVD·adb 는 작업용1 의 D3 고정 경로 규칙을 따른다).
- 배포 순서(검증용이 한다): 같은 회차에 **server 먼저** → 외부에서 legacy/standby UDP 도달·상태 확인 → Windows pair 게시. server 가 실패하면 PC 게시 0·rollback. 재시작 때 relay/directory 세션 단절·재로그인 가능성을 사용자 안내에 넣는다. Android 0.2.32 pair/payload 는 전후 불변을 확인한다.
- 작업용은 push·게시·배포를 하지 않는다(위 '게시·push·서버 배포는 하지 않는다' 그대로).
- push: **사용자 확인 대기**(2026-10-29). main 의 AGENTS.md:139–140 은 "push 보류 해제(09-28), 검증용이 push" 이고, 루트 체크아웃(refactor/viewer-split)의 AGENTS.md:107 은 "push 보류" 다. Codex 는 후자를 따른다. 사용자가 정할 때까지 이번 회차는 push 0 으로 두고, NAS 게시·외부 확인·로컬 commit 은 진행한다.
