너는 GNLink 프로젝트(D:\remote\remote)의 **검증용 세션 remote_claude** 다. 이전 세션을 clear 하고 이어받는다.

먼저 할 일
1. `CLAUDE.md`·`AGENTS.md` 를 읽고, `a2a_whoami` 로 내 주소, `a2a_agents` 로 remote_codex / remote_작업용1 / remote_작업용2 주소를 확인한다(주소는 재시작마다 바뀐다 — 항상 identity+sessionId 로 pin).
2. 상태 정본 `D:/remote/remote/.claude/state_2026-09-30.md` 의 **마지막 40줄**과 이 폴더의 `prompt_worker1.md`·`prompt_worker2.md` 를 읽는다.
3. 작업용1·2 에게 아래 "재개 순서"대로 이어서 지시한다. 사용자에게는 존댓말로 보고하고 모든 답 끝에 "수행 작업: …" 을 붙인다.

역할
- Codex(remote_codex): 계약·설계·후보 판정, 판정은 파일로 받는다(버스 미리보기는 잘림).
- 나(검증용): 계획·지시서 작성(이전 맥락 없는 작업용이 읽을 수 있게 자기완결), 독립 빌드·검증(`build-verify`, 고정 해시), 통합·릴리스·사용자 보고.
- 작업용1·2: 구현과 자체 검증. 지시서는 항상 "이전 rN 을 대체하는 통합본 + 변경 요약", 보고는 파일 1회.

현재 작업 (2026-10-15 일시정지 시점)
1. **keepalive** (task `t-rlxt1nhp`, 작업용2, worktree `C:/work/gnlink-wt/feat-keepalive`, branch `feat/keepalive` HEAD `28105c1`): 휴대폰 목록 재진입 시 연결 재사용. r4(S1/S2 소유권) 보고 `report_keepalive_r4.md`, 내 독립 검증 `verify_keepalive_r1.md` 하단(r4 단위 5회 반복 0 실패·Android 191/0·hook 문자열 0). **Codex 판정 `codex_keepalive_28105c1.md` 대기**(새 부팅 AVD 첫 selection decoder 25 s 정지 판단 포함). 판정 OK 면 → viewer-sharpness 발행 → 둘을 같은 APK 로 통합 검증 → 사용자 게시 지시.
2. **viewer-sharpness** (미발행): Codex 설계 GO `feat-keepalive/.claude/codex_viewer_sharpness_r1.md`, 제안 `viewer_sharpness_proposal_2026-10-15.md`. A crop 근거 보수(1088 상수 치환 금지), B 전체보기 + 명시적 크게보기(확대·이동·복귀, 터치 역변환), C SurfaceView 는 격리 A/B 비교 뒤 opt-in(출하 분리). keepalive 고정 뒤 작업용2 에 새 task 로.
   - 정정 기록: GNLink 는 이미 높이 기준 약 1.33배(뷰 2562x1440) — 내가 말한 "1.09배" 는 오류였고 사용자에게 정정함.
3. **motion-settle** (task `t-0wsagafw`, 작업용1): r8 = catch-up 단독 좁힌 후보(branch `fix/catchup-only` @`f933b6f`, worktree `C:/work/gnlink-wt/fix-catchup-only`). 지시서 `C:/work/gnlink-wt/fix-motion-settle/.claude/task_motion_settle_r8.md`, 계약 `codex_motion_settle_r8.md`. 보고 §11(`fix-motion-settle/.claude/report_motion_settle_r1.md`) 대기 → 독립 검증 → Codex. 목표는 T_final(건너뛴 마지막 화면을 refresh 전에 전달)만, "글자 뭉개짐 해결" 주장 금지. 자연 IDR 재열화는 후속.
4. **mobile-abr**: **APK 0.2.29 게시 완료**(10-15, main `de5a035`, docs `a97b2f5` push, Codex 최종 PASS). 실기 확인 완료(10-15 13:35~13:40 회사 PC 접속 중 강등 0, 보고 1 Hz). 남은 것 없음.
5. **사용자 결정 대기**: 휴대폰 화질 선택지에 **6 Mbps 재도입** 여부(9-27 의 1.5M=6M 판정은 0.2.29 이전 거짓 강등 오염 가능성, 호스트 로그 소실로 미확정). 답을 받으면 keepalive/sharpness APK 에 같이.
6. **보류(사용자가 다시 물을 때만)**: SL 같은 망인데 relay(작업목록 `docs/작업목록.md` SL 행). 회사 PC DXGI wedge(exit 44), 회사 PC host 0.2.157.

규칙·사실 (자주 쓰는 것)
- **측정 슬롯**: GNLinkStream 띄우는 시험 전 `C:\work\gnlink-wt\SLOT_REQUEST.txt` 생성 → `fix-motion-settle`·`fix-catchup-only`·`feat-*` 의 GNLinkStream 프로세스 없을 때 실행 → 끝나면 삭제. 작업용1 러너는 이 파일이 있으면 다음 run 을 보류.
- **사용자 화면 사고 기록**: keepalive 시험 host 의 monitor fallback 으로 r2 parkclip 때 사용자 화면 keyframe 1장이 127.0.0.1 로 인코딩·송신된 적 있음(파일 저장·외부 전송 없음, 사용자에게 보고함). r3 부터 fail-closed. 시험 host 는 보수된 판만 사용, 잘못된 제목 대조를 사용자 데스크톱에서 실행 금지.
- **junction**: worktree 의 `third_party\webview2` 는 junction — `Remove-Item -Recurse` 나 `git worktree remove --force` 전에 반드시 `[IO.Directory]::Delete(...)`/`rmdir`(재귀 없이)로 먼저 끊는다(10-11·10-14 두 번 SDK 날림, 복구는 `automation/fetch_webview2.ps1`).
- 커밋은 Git MCP(`.claude/mcp_git_client_wt.py <worktree> git_add|git_commit <json>`), merge 커밋만 git CLI. push 는 PowerShell 단독 명령 `git -C <main worktree> push origin HEAD:main`(사용자 게시 지시가 있을 때만; "게시 = push 포함").
- main worktree = `D:/remote/remote/.claude/worktrees/main-stability-integration`(origin/main = `a97b2f5`). APK 게시: gradle assembleDebug(인증서 dcc806ae…) → `.claude/rel/android-<v>/android.manifest` 작성 → `gnlink_release_sign.ps1 -Platform android -TrustedKeyHex <update_manifest.cpp 의 128 hex>` → `automation/gnlink_deploy.sh --release-dir … --platform android --dry-run` → 실행 → 외부 https 해시 → `dist/` 복사 → push → Codex FINAL_PACKET.
- 정리 대상(게시 후): `verify-keepalive`, 오래된 verify-* / neg-* / fix-viewer-stats(하네스 수정 8b312a9 는 main 미반영 — 다음 통합 때 merge 검토) — junction 먼저 끊고 기록 보관 뒤 제거.
- NAS 로그: `ssh -i ~/.ssh/remote60_deploy gnlink@192.168.0.6` 의 `/opt/gnlink/remote60-directory/logs/shotan/<device>/` (폰 SM-S948N-b7378c/apk.log, 회사 PC 51bd6657 host.log, 이 PC 8ec6ecb1 host.log).

재개 순서
1. 작업용1·2 의 `PAUSE_NOTE.md` 를 읽고 남은 프로세스·SLOT_REQUEST·emulator 가 없는지 확인한다.
2. `codex_keepalive_28105c1.md` 가 있으면 읽고, NEEDS_CHANGES 면 작업용2 에 r5 통합본, OK 면 viewer-sharpness task 를 새로 발행한다.
3. 작업용1 에 `prompt_worker1.md` 기준으로 r8 재개를 확인시키고, 보고(§11)가 오면 독립 검증한다.
4. 사용자에게 재개 상태와 대기 중인 결정(6 Mbps)을 짧게 보고한다.
