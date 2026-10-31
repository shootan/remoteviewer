# report sh-filter-impl r1 — 선명도 끄기 / 약하게 0.20 / 강하게 0.35 (작업용2 → 검증용 remote#wwml7h91, 2026-10-22)

task t-b7ss11ov / rev r1 / kind=result. 정본 `.claude/task_sh_filter_impl_r1.md`, 계약 `.claude/codex_sh_filter_r1.md`.
**판정: 구현 + AVD 검사 완료 · 휴대폰 실기 대기. S4 지연 기준은 AVD 에서 미달(아래 수치 그대로) — 폰 실측으로만 판정.**
버전 불변(0.2.30 / 29), 설치·게시·push 없음.

## 고정 후보
- worktree `C:/work/gnlink-wt/feat-sh-filter`, 브랜치 `feat/sh-filter`, base main `5063cd9`.
  - `c568fbb` 제품: SharpnessLevel · SharpenPathPolicy(순수 상태기계) · SharpenRenderer(EGL14/OES, 전용 HandlerThread) · SharpenLatencyStats · native handoff ring + JNI · 설정/뷰어 메뉴 3단 · 저장 · seam 소스셋 분리 · 단위 시험 SharpenPathPolicyTest 13.
  - `2e9428b` 테스트/문서만: `automation/sh_filter_avd.py`(AVD 하네스), `automation/sh_pattern_window.ps1`(1920x1080 패턴 창), `docs/sh_filter_phone_procedure_2026-10-22.md`, `docs/history.md` 항목. **제품 코드 무변경.**
- APK(같은 c568fbb, assembleDebug): 출하형 `C:/work/gnlink-wt/sh-avd/app-noseam.apk` SHA256 `51B35B1D69784D738C0D4F79615FECF4A2B0E0912956F10CF278354A7EC54871`(dex 에 seam 표식 없음 확인) / 시험 seam `app-shseam.apk` `79FCD624…`(`-Pgnlink.shTestSeam=true`, 표식 있음 — AVD 전용).
- 환경: AVD gnlink_kbd_test(x86_64 API34, 1080x2400, **SwiftShader 소프트웨어 GL**), 시험 Host `GNLinkStreamMigrationTest` 0.2.161(fix-dxgi-quarantine-build, `--bind-address 127.0.0.1`, 입력 주입 none), DESKTOP(DXGI) 캡처 + 맨 위 패턴 창. 증거 분류: 실 APK·실 UI 조작의 ⑵ 상당(에뮬레이터) — ⑶ 실기·실 GPU·발열·터치 아님.
- 완료 판단은 각 실행의 `RESULT:` 줄(아래 run 디렉터리 `C:/work/gnlink-wt/sh-avd/run-*`).

## acceptance 별 증거
1. **선택 즉시 반영·off↔on 뒤 화면 복귀** — compare `run-compare9` RESULT ALL PASS 24/24: 뷰어 메뉴로 off→0.20→0.35→off→0.20, 매번 그림·테두리 확인, 다시 끄기 A_text edge 18.184 = 처음 18.185. lifecycle `run-life5` 14/14.
2. **가는 글씨 선명** — edge 에너지 A(가는 글씨) off 18.2 < 0.20 23.0 < 0.35 24.7, B(저대비) 1.8 / 3.5 / 4.7. C(1px 선)은 포화로 0.20≈0.35(116.232/116.234) — 변별 못 함. 단색 D_flat_edge 0 유지. 눈 비교 `run-compare9/crop_text_off_weak_strong.png`(0.20/0.35 가 더 또렷).
3. **터치 기하 불변 — 실행 안 함**(Host 입력 주입 none). 확인한 것은 **그림 기하**만: 전 수준 x0=275 동일, 네 테두리 위치 정상, identity(seam s=0 GL vs 끄기) 움직이는 띠 밖 평균 |Δ| 0.10·최대 10 — 이동·잘림·색 변화 없음. 터치는 폰 절차 2단계.
   - 1088 경우(계약 1)는 **AVD 에서 안 나옴**: 전 실행 `coded=1920x1080 crop=0,0,1919,1079`(goldfish). 1088 패딩 경로는 미검증 — 폰에서 확인.
4. **GL 실패 → '선명도 적용 불가, 원본 표시' + 원본 계속** — faults `run-faults2` 28/28, context / shader / swap:30 / never 각각:
   - fallback 로그 requested=weak effective=off
   - 직결 재결합
   - 원본 그림 + 2 s 뒤 새 프레임
   - 같은 surface 회차 재시작 0
   - 안내 Toast: 감시기가 **아무 Toast 나** 잡으므로 PASS 줄은 증거가 아님. 캡처 4장을 직접 열어 보니 context·shader·never 3장은 '선명도 적용 불가, 원본 표시' 였다. **swap:30 은 다른 Toast**('Right click: hold a finger…' 뷰어 안내)였다 — fallback 안내가 그 뒤 대기열에 섰을 가능성이 높지만 **미확인**. 하네스 결함으로 기록한다(문구 대조 없음).
   - 참고: GL 이 안 되는 기기에서는 기본 0.20 이라 **뷰어 들어갈 때마다 안내가 한 번씩** 뜬다. 계약(회차별 latch)대로이며, 사용자가 끄기를 고르면 끝난다.
5. **지연(S4) — AVD 기준 미달, 그대로 공개** `run-lat1`, 수준별 60 s(5 s 창 12개):

| 수준 | n | unmatched | p50 중앙값 | p95 중앙값 (범위) | GL draw 중앙값 |
|---|---|---|---|---|---|
| 끄기 | 1275 | 0 | 65.6 ms | 70.5 (68.3–72.0) | – |
| 0.20 | 1276 | 0 | 71.4 | **89.0** (87.7–89.9) | 14.5 ms |
| 0.35 | 1272 | 0 | 75.8 | **88.9** (79.8–94.4) | 14.0 ms |
| 끄기 다시 | 1276 | 0 | 58.7 | 72.0 (69.6–78.4) | – |

   - p95 +18.5 ms(기준 4 ms). 소프트웨어 GL draw ~14 ms 가 대부분으로 보이나 단정하지 않음.
   - 끄기↔끄기 p50 차 7 ms 가 이 환경 잡음 폭.
   - 창당 n≈106 은 수준 무관이고 glSkipped=0(프레임률 저하 없음). 원본은 WinForms 타이머 한계로 ~21 fps.
   - 발열은 AVD 로 판단 불가.

## lifecycle (`run-life5`, 14/14) — 각 단계가 실제로 탄 경로
- off↔0.20 5회: producer 직렬 전환(release→direct, start→sharpen).
- 대상 선택 화면 왕복 3회: 앱의 releaseVideoSurface(unbind) 경로.
- HOME 3회: 앱이 실제로 뒤로 갔다 돌아옴(topResumedActivity 확인). 다만 **TextureView surface 는 파괴되지 않음**(stop 에서 유지).
- 다크모드 on/off: activity 재생성 → `onSurfaceTextureDestroyed appReleases=1`(renderer 소유 중 파괴 경로) → 다시 고르면 0.20 유지·새 프레임·render 스레드 1.
- release timeout / fallback / FATAL 0. **SurfaceTexture 를 앱이 release 한 순간 자체는 로그가 없다**(timeout 부재로만 간접 확인).
- 회전 없음(이 AVD 에서 뷰어 가로 고정) → 폰 절차.

## 시험 중 발견 (이 변경 밖 — ledger 후보, Codex 판단 요청)
- **수동 IP(TCP 제어) 접속 10 s 정지**: Host `udp control session ended epoch=1 resumed=0 reason=none` 이 접속 약 10.0 s 뒤에 4회 관측됐다. 그때 스트림이 재생 중이면 inactive 로 내려가 '화면이 멈췄습니다 · 다시 연결하는 중…' 으로 굳는다(끄기에서도 동일: debug2-off-low, run-compare7/8). 유력 원인은 `host_startup_control.cpp` `UdpControlLink(&…, 10000)` 읽기 timeout. 앱이 이 경로에서 UDP 제어를 ping 하지 않는지는 클라이언트 소스로 확인하지 않았다. 실사용 영향: 수동 IP 로 붙어 10 s 안에 화면을 고르면 멈출 수 있다. 하네스는 접속 10 s 뒤에 고르는 것으로 우회.
- 이전 수동 연결 중 화질 변경 뒤 세션 종료 관찰도 같은 원인일 가능성이 있다(미확인).
- **철회**: 앞서 의심한 "필터 경로가 renderer Ready 전에 온 선택 키프레임을 버려 정지 화면에서 '준비 중' 고착"은 정지 원본 4회 probe(desktop/window × off/0.20)에서 재현 안 됨 — 매번 Ready 뒤 키 도착, 그림 정상. 앞선 고착은 위 10 s 정지와 시각이 겹친다. 이론상 창(키가 Ready 전 도착)은 남지만 그때는 키 재요청으로 회복된다(Host `reasons=viewer` 응답 관찰).

## 미검증 · 사용자 행동
- 휴대폰(SM-S948N) 전부: 체감 선명도, S4 4 ms·발열, 회전/크게보기 뒤 탭 위치, 1088 패딩, 실 GPU 드라이버의 GL 실패. 절차 `docs/sh_filter_phone_procedure_2026-10-22.md`(약 40분). 설치는 검증용 경유 사용자 승인 뒤. S5(버전·keepalive-multi·A6) 사용자 미결 그대로.
- 사용자가 지금 할 일: 없음(폰 설치·버전 결정 시점은 Codex/검증용이 안내).

## 다음
- 작업용2 는 지금부터 **dxgi-wedge r2 보수**(t-dreqrszk, `codex_dxgi_wedge_fix_r1.md` D1~D4/E1)로 전환한다. 정본 r2 지시가 이 세션 버스에 도착하지 않아 SH 를 끝까지 진행했다 — DW 우선 규칙 위반 시간 약 1시간.
- GPU 슬롯 `C:\work\gnlink-wt\SLOT_REQUEST.txt` 해제함.
