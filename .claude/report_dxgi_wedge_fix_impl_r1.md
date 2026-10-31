# report dxgi-wedge-fix-impl r1 — 작업용2 (remote_작업용2 / sei64muv), 2026-10-22

task t-dreqrszk / 정본 `.claude/task_dxgi_wedge_fix_impl_r1.md` / 계약 `.claude/codex_lockkeys_dxgi_r1.md` §B.
worktree `C:/work/gnlink-wt/fix-dxgi-quarantine` (branch `fix/dxgi-quarantine`, base main 5063cd9).
빌드 `C:/work/gnlink-wt/fix-dxgi-quarantine-build` (Release, 전체 빌드 OK). 버전 변경·서명·게시·push·사용자 PC 설치본 조작은 하지 않았다.

## 상태 한 줄
구현·시험 끝(커밋 7개). **계약 7 의 전체 사슬 fixture 가 수정본으로 ALL PASS** 했다: 실 supervisor 의 child 가
제품 watchdog 으로 exit 44 → 뷰어 43 → shell 이 restore 를 담아 자동 재연결 → '재연결 중' → 클릭 없이 같은
desktop 의 새 프레임. wedge→복원 20.1 s. 남은 것은 회사 PC 실기(사용자 조율)와 보류 항목(minidump·Bⓑ2·MT 출하)이다.

## ⚠️ 이 PC 에 남긴 영향 1건 (사용자에게 알릴 것)
- 10-22 01:35 의 첫 전체 사슬 실행이 scratch 경로 시험 호스트(`.claude/test-tmp/dxgi-wedge-*/stream/GNLinkStreamMigrationTest.exe`)를 0.0.0.0 에 bind 해서 **Windows 방화벽 알림 대화상자**가 떴다(`PickerHost.exe FirewallNotificationDialogServer`). 저는 누르지 않았다.
- 02:00 무렵 대화상자가 사라졌다(누가 어떻게 닫았는지는 모른다). 응답에 따라 그 scratch 시험 exe 에 대한 허용 또는 차단 규칙이 생겼을 수 있다. 제품 exe 와는 무관하다. 확인·정리는 사용자 판단이다.
- 재발 방지: 시험 호스트는 이제 `--bind-address 127.0.0.1` 이고 runner 포트도 loopback 이다. 수정본 실행에서는 대화상자가 다시 뜨지 않았다(PickerHost 없음 확인).

## 커밋 (worktree, 미push) — 제품/테스트/문서 구분
| commit | 내용 | 구분 |
|---|---|---|
| c58fcb2 | Bⓐ2 격리 영속화 + Bⓐ3 child 격리 분리 + 어댑터 식별 줄, 저장소·supervisor 시험 | 제품+테스트 |
| 9d5c9fe | monitor_fallback e2e `--quarantine`/`--quarantine-control` | 테스트 |
| 03a1744 | Bⓑ1 뷰어 보고/복원 + shell 전달·재연결 로그(Bⓒ 일부), shell 시험 R1~R6, restore 순수 시험, 전체 사슬 하네스/runner, test seam(wedge·포트), MT A/B test 빌드, 출하 gate 표식 | 제품+테스트 |
| ec6680b | Bⓒ 3 s OS snapshot(비동기) + 시험 | 제품+테스트 |
| 111ae20 | history 항목, MT A/B 실기 절차 `docs/dxgi_wedge_mt_ab_procedure_2026-10-22.md` | 문서 |
| e711f08 | primary 화면 복원 4 s fallback(화면 목록이 안 오면 15 s 대기 뒤 picker 로 가던 퇴행 방지) | 제품 |
| 158aff3 | history 에 사슬 ALL PASS 결과 반영 | 문서 |

## 구현 요약 (계약 대응)
- **B5 격리 우선**: 격리는 별도 env `REMOTE60_CAPTURE_DXGI_QUARANTINE` 와 `DesktopBackendState::dxgiQuarantined` 로 표현한다. 원래 `active = requested` 이던 모든 곳 — startup·reattach·클라이언트 요청·climb-back·window/monitor item 선택(`host_loop_helpers` 486/502) — 이 `EffectiveRequested()` 를 쓴다. dxgi 요청은 희망값으로만 저장하고 `(kept quarantined)` 로그를 남긴다. supervisor 는 이 env 를 항상 지운 뒤 격리일 때만 넣는다. 부모 env 의 값이 새어 들어가지 않는다. prelogin worker 는 이 값을 무시한다. protocol 확장은 없다(APK UI 가 dxgi 를 희망값으로 보이는 한계는 그대로다).
- **B2/B3/B4 영속화**: child 가 장치 생성·재생성 때 `[capture-adapter] key=<16hex> driver=<UMD>` 를 출력한다. key 는 vendor/device/subsys/rev+설명의 FNV 해시이고 LUID 는 쓰지 않는다. supervisor 는 exit 44 를 그 child 가 보고한 어댑터에 대해서만 기록한다(줄이 없으면 `not recorded reason=no-adapter-line`). 저장 위치는 known-folder `ProgramData\GNLink\capture-recovery\<key>.rec` 이다. `prelogin_store_chain` 의 HoldRecordChain(SY/BA, reparse 거부)을 공유하고, 파일은 private DACL·링크 1개를 handle 에서 검사한다. 쓰기는 CREATE_NEW 임시파일 → 핸들 rename 이고, 폴더 안 lock 파일에 LockFileEx(최대 3 s)를 건다. 4 KiB·16개 제한(가장 오래된 것부터 검사 후 삭제)이다. 매 launch 마다 다시 읽는다. 만료는 마지막 wedge+7일이고 연장하지 않는다. expired·driver-changed·adapter-absent·future·malformed·security 는 각각 사유를 로그에 남기고 기본값(DXGI)으로 둔다. 같은 run 의 메모리 격리는 유지한다.
- **B6 복원**: 뷰어는 picker reveal(호스트 확정+첫 프레임 디코드) 때와 toolbar 화면 전환의 더 새 세대 첫 프레임 때만 `[viewer-restore] target=...` 를 보고한다. reveal 뒤에는 화면 목록을 한 번 요청해 이름과 좌표까지 보고한다. shell pump 는 operation 별로 보관하고, recoverable 43/44/46 자동 재연결에만 `--reconnect --restore-target` 을 넘긴다. 연쇄 재연결은 마지막으로 렌더된 대상을 이어받는다. 사용자 시작·취소·다른 PC·로그아웃·새 operation 이면 버린다. 새 뷰어는 picker 의 transaction(`begin_pc_target_selection`)으로 고른다. 화면은 새 호스트 목록에서 이름과 사각형으로 정확히 하나를 찾는다. gone·changed·ambiguous·window·목록 timeout 이면 picker 와 한국어 사유를 보인다. 표시 문구는 "재연결 중 — 이전 화면으로 돌아가는 중…" 이다. 재시도 3회 정책은 바꾸지 않았다.
- **B8 진단**: shell 이 exit/attempt 를 한 줄로 남긴다(code·ranMs·attempt·delay·host·restore, credential 없음). watchdog 3 s 경고 때 slot 만 잡고 detached 스레드가 OS snapshot 한 줄을 남긴다(전경 pid·exe 파일명·rect·monitor·display mode·coversMonitor, 제목 없음).
- **B1 MT A/B**: `GNLinkStreamMtAb.exe`(REMOTE60_MT_AB_TEST_SEAM, arm 은 exe 옆 `GNLinkStream.mt-ab.txt`). 장치 생성 직후 Set 하고 Get 으로 확인한다.

## acceptance 별 증거 (fixture / 제품 UI / 실기 구분)
1. **exit44 → '재연결 중' → 같은 화면 새 프레임**
   - fixture(shell 레벨): `client_recovery_ui_test` 의 R1~R6 이 실제 begin_session·pump·handle_viewer_exit 로 돌았고, 뷰어는 stand-in 이다. R1 은 보고된 화면을 `--reconnect --restore-target` 으로 넘긴다. R2 는 보고가 없으면 picker 다. R3 연쇄는 마지막 대상을 유지한다. R4 사용자 세션과 R5 취소, R6 다른 PC 는 복원하지 않는다. 결과 ALL PASS. mutation(재연결에 restore 를 넘기지 않음)을 넣으면 FAIL 한다.
   - 순수 시험: `viewer_restore_target_test` 26/26. 실제 뷰어 줄 형식과 index 가 같아도 다른 화면이면 거부하는 부정 대조를 포함한다.
   - **실 사슬 fixture — 수정본 ALL PASS(02:00 run, fixture 보존 `.claude/test-tmp/dxgi-wedge-9KcLQn`)**: 직접 실행한 결과다.
     - 무엇이 제품인가:
       - 제품: shell(client_shell_main.cpp, entry 만 바꾼 test seam 빌드), 실제 `GNLinkViewer.exe`, 사설 directory(제품 server.js), `GNLinkHostUiTest`(제품 창과 supervisor; 창으로 로그인)
       - 시험 빌드: stream 은 `GNLinkStreamMigrationTest`. 제품 소스에 loopback directory 와 wedge seam, 포트/주입 off 인자를 더했다.
       - 첫 세션의 desktop 카드는 게시한 WM_LBUTTONDOWN/UP 으로 눌렀다(물리 마우스 아님).
     - 흐름:
       - 첫 세션 reveal 뒤 `[viewer-restore] target=desktop`, 이어서 화면 목록이 와서 `target=desktop:5c5c2e5c444953504c41593232:0,0,1920,1080:1`(why=monitor_list).
       - host_app.log: `02:01:07.868 [os-snapshot] at=dxgi-slow phase=frame_handler phaseAgeMs=3292 fgExe=GNLinkViewer.exe … mode=1920x1080@60Hz/32bpp coversMonitor=0` → `dxgi-worker-wedge phase=frame_handler phaseAgeUs=5294581 … exit 44` → `self-terminated … (44)` → `02:01:23.834 startup requested=dxgi effective=wgc dxgiQuarantined=1 reason=wedge-this-run`.
       - client.log: `viewer exit operation=1 code=43 ranMs=21578 reconnect=scheduled attempt=1 delayMs=1000 … restore=desktop:5c5c…:0,0,1920,1080:1`.
       - 새 뷰어: `[restore] waiting target=…` → `requested desktop monitor=0 (primary)` → `result=restored` → reveal 재보고. picker 클릭 0회.
     - 화면(BitBlt 스크린샷, `shots/`):
       - 1 picker: picker 배경색 48.0%
       - 2 첫 세션 stream: 0.0%
       - 3-reconnecting-0: 머리말 "재연결 중 — 이전 화면으로 돌아가는 중…" 과 전체 화면 카드 선택. 제가 이미지를 직접 확인했다.
       - 4 복원 뒤: 0.0%, 실시간 데스크톱(자기 화면 재귀). 이후 3 s 동안 decodedFrames 가 계속 증가했다.
     - 시간선: wedge 파일 4.8 s → 뷰어 종료 감지 22.5 s(+17.7; 제품 5 s watchdog + 뷰어 session-dead) → 새 뷰어 23.5 s(+1.0) → 복원 24.9 s(+1.5). **wedge→복원 20.1 s.**
     - 저장소는 이 UI 시험 빌드에 없어서 `not recorded … reason=folder-unresolved` 였다(예상된 결과). 영속은 acceptance 2 의 별도 fixture 에서 봤다.
   - 수정 전 01:35 run 은 shell parser 결함(타임스탬프 접두) 때문에 restore=picker 였다. 이 사슬 시험이 결함을 찾았고 03a1744 에서 고쳤다.
   - 제품 UI 실기(회사 PC·게임·실제 DXGI wedge): 미관측. 이 fixture 의 wedge 는 frame_handler 에서 일부러 붙잡은 것이고, field 의 주된 phase=release 와 다르다.
2. **호스트 재시작·재부팅 뒤 같은 드라이버면 처음부터 WGC**
   - fixture: `host_capture_quarantine_test` 23/23. 실제 StreamingHostProcess 를 끄고 다시 띄웠고, child 는 stand-in 이며 시작 env 를 기록한다. 결과:
     - 같은 어댑터·드라이버: 첫 child 부터 `persisted:key=…`.
     - 드라이버 변경·어댑터 없음·만료·손상·미래 시각: 기본값, 사유 로그.
     - 어댑터 줄 없는 exit44: 기록 안 하고 이번 run 만 WGC.
     - 부모 env 누수: 차단.
   - 저장소: `capture_recovery_store_test` 53/53. 실제 폴더에서 loose ACL·외부 hardlink·junction 폴더 거부(대상 파일 바이트 불변), 16개 제한, 동시 writer 32/32, lock 대기 3 s 한계를 확인했다. 제품 owner 규칙이 사용자 소유 폴더를 거부하는 부정 대조도 넣었다.
   - 실제 어댑터 줄: 이 PC AMD 에서 `key=612561e68f454fd9 driver=31.0.21924.61 vendor=0x1002 device=0x1638` 이 찍혔다.
   - **미검증**: 승격된 실 GNLinkHost 가 SY/BA ProgramData 에 실제로 쓰는 것. 시험은 테스트 사용자를 신뢰하는 규칙으로 했다. 재부팅·실 NVIDIA 도 미검증이다.
3. **격리 중 DXGI 요청·화면/창 전환으로 되돌아가지 않음**
   - fixture(실 test-build 호스트, GPU): `host_monitor_fallback_e2e_test --quarantine` 16/16. APK 식 dxgi 요청, 화면 B 와 A 선택, desktop, 이탈→idle detach→복귀, 2차 요청을 거치는 동안 **DXGI 시작 0, WGC 5** 였다.
   - 대조 `--quarantine-control` 15/15 는 **DXGI 5** 였다(probe 가 DXGI 를 볼 수 있음).
   - mutation(EffectiveRequested 무력화)이면 3 FAIL(dxgi=5). 원복 뒤 PASS 했고 exe 해시가 바뀐 것을 확인했다.
   - 최종 빌드에서도 다시 실행해 16/16·15/15 였다. 기본 모드 173/173(이후 host 변경 전 실행).
   - 창 identity 재사용 반례: 창은 복원하지 않으므로(window→picker) 해당 경로가 없다.
4. **다음 wedge 진단**
   - 재연결 줄: R1 에서 확인했다. 실 사슬 client.log 에도 있다(위).
   - OS snapshot: `host_os_snapshot_test` 7/7. 제목이 없고, collector 를 멈춰도 요청은 0 ms 에 반환하며, 중복 요청은 거부된다.
   - snapshot: 실 사슬(02:01:07.868)에서 3 s 시점 줄을 관측했다(위). wedge 파일을 만든 뒤 붙잡힌 상태에서 찍혔고, 5 s 종료는 그대로였다.
   - **minidump: 보류.** 계약 8: "helper가 멈춘 반례에서도 5초 복구가 유지되는 실행 증거가 없으면 dump 부분은 보류하고 허용된 로그 진단만 먼저 진행한다." 별도 helper·SY/BA diagnostics 폴더·예산 구현과 그 반례 증거가 없으므로 보류했다.

## 시험 실행 요약 (모두 종료코드 0, 마지막 요약 줄 확인)
store 53/53 · supervisor 23/23 · backend_policy PASS · monitor_fallback `--quarantine` 16/16 · control 15/15 · 기본 173/173 · client_recovery_ui ALL PASS(R1~R6 포함) · **전체 사슬 dxgi_wedge_reconnect_runner ALL PASS** · viewer_restore_target 26/26 · os_snapshot 7/7 · selection_gate · layout · exit_upload 13/13 · recovery_process PASS · 전체 빌드 OK.
태그: store·restore·snapshot 은 pure-logic/filesystem, monitor_fallback·chain 은 display-capture/gpu. GPU 시험은 SLOT_REQUEST 를 CreateNew 로 잡고 해제했다. 세션: 콘솔(이 PC).

## 출하 gate
- `automation/gnlink_check_fixed_server.py --dir <Release>` 결과 ALL PASS 9. GNLinkClient/Host/Stream/Viewer/Updater/InputService 에 시험 표식이 없다. 새 표식 `mt-ab`·`GNLINK_CHAIN_` 을 추가했다(self-test 18/18).
- wedge seam env 는 기존 `GNLINK_STREAM_TEST_` 표식으로 걸러진다. 확인 대상은 이 worktree 빌드 산출물이고, 릴리스 패키지가 아니다.

## MT A/B (Bⓐ1)
- 시험 빌드: `<build>\apps\native_poc\Release\GNLinkStreamMtAb.exe`. 이 PC 확인 결과:
  - arm=on: `[mt-ab] arm=on before=0 after=1`, `d3d-mt at=device-created state=on`.
  - arm=off: 0/0.
- 실기 절차: `docs/dxgi_wedge_mt_ab_procedure_2026-10-22.md`. 사용자 승인 아래 설치 exe 를 교체한다. arm 마다 5회 이상 교차하고 회당 약 10 분, 호스트 재시작 직후 첫 DXGI 세션으로 한다. 실기 동안 capture-recovery 폴더를 비운다. 비교 지표를 적고, AMD 대조 뒤 원복하며 해시를 확인한다. 결과 기록 위치는 `.claude/mt_ab_field_<날짜>.md`.

## 한계·후속
- 같은 모델의 GPU 두 장은 key 를 공유한다. 격리가 둘 다 덮으므로 WGC 쪽으로 틀린다.
- `take_operation_restore` 가 끝나기 직전 ms 에 pump 가 줄을 넣으면, 죽은 operation 항목 하나가 남을 수 있다(동작 영향 없음).
- 스크린샷에는 이 PC 의 창 제목 목록이 찍혀 있다(picker 카드). scratch 안에만 있고 업로드하지 않는다.

## 다음 담당
- 검증용: 고정 후보 **158aff3**(제품 마지막 e711f08) 독립 검사, Codex 검토로 연결.
- 사용자(Codex 경유): 회사 PC 실기(격리 영속·재부팅 뒤 WGC 시작·게임 중 자동 복원), MT A/B 실기 조율, 방화벽 규칙 확인(선택).
