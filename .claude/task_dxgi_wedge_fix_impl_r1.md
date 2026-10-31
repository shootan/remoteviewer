# task dxgi-wedge-fix-impl r1 — DXGI wedge 격리 영속화·선택 복원·진단 구현 (검증용 remote#wwml7h91, 2026-10-22) — 담당 작업용2

**이 파일이 정본이다. 끝까지 읽고 CLAUDE.md 대로 진행한다.** 조사 task(dxgi-wedge-first-session r1, t-p6gs4g4i)와 별개의 구현 task 다.
설계 확정: `.claude/codex_lockkeys_dxgi_r1.md` §B(표 + "B 구현 계약" 1~8) — **그 조건이 구속 계약**이다. 여기 요약과 다르면 Codex 파일이 우선.
근거: `.claude/report_dxgi_wedge_first_session_r1.md`(당신의 조사), 참고 `docs/OSLink_정적분석_2026-10.md` §1.

## 사용자 행동 acceptance (이게 완료 기준)
1. 호스트에서 DXGI wedge(exit 44)가 나면, 뷰어는 창을 닫았다 다시 열더라도 **"재연결 중" 뒤 같은 데스크톱/모니터의 새 화면**이 자동으로 나온다(picker 로 떨어지지 않음). window 는 확실히 같은 창일 때만, 아니면 picker + 이유.
2. 그 PC 에서 호스트를 재시작·재부팅해도 같은 GPU·드라이버면 **처음부터 WGC** 로 시작해 첫 접속 wedge 가 반복되지 않는다(마지막 wedge 후 7일, 드라이버·어댑터 변경·만료·손상이면 정상 기본 DXGI).
3. 격리 중에는 휴대폰/뷰어의 DXGI 요청이나 모니터·창 전환으로 DXGI 로 되돌아가지 않는다.
4. 다음 wedge 때 원인 판단에 쓸 진단(재연결 줄·3 s 경고 시 OS 상태 snapshot, 조건 충족 시 별도 helper minidump)이 남는다.

## 범위 (Codex 계약 요약)
- **Bⓐ2 격리 영속화**(계약 2·3·4): known-folder `ProgramData\GNLink\capture-recovery\`, SY/BA 보호 DACL, 링크/reparse/hardlink 거부(prelogin store chain·worker log 원칙 공유), 같은 폴더 CREATE_NEW 임시→교체, 동시 supervisor 직렬화. 내용 = schema/version·실제 wedge child adapter 의 안정 식별 hash(LUID 단독 금지)·driver version·최초/최근 exit44·만료(마지막 wedge+7일, 연장 금지). 4KiB/장치·최대 16개. 손상·미래 시각·권한 실패 → 거부+사유, 기존 in-memory 정책 유지(같은 run 격리 지우지 않음). **prelogin/서비스 worker 에는 적용하지 않는다**(prelogin wedge 로 WGC 활성화 금지). 격리 시 child 에 "effective=WGC" 를 명시 전달.
- **Bⓐ3 격리 우선**(계약 5): requested vs effective 분리, startup/restart/desktop 전환/monitor·window 선택 후 복귀/자동 promotion 전부 공유 결정. 만료 해제는 다음 안전한 재시작/선택 경계에서. WGC 실패 시 몰래 DXGI 로 풀지 않음. requested/effective/사유/만료 로그. protocol 확장 없음(APK 가 dxgi 로 보일 수 있는 한계 기록).
- **Bⓑ1 선택 복원**(계약 6): recoverable 43/44/46 자동 재접속에만, 마지막 **확정·렌더된** 대상만, child/operation/ownerEpoch/hostId 에 묶인 handoff(capability·토큰·암호 미포함), 새 인증/connect 경유, monitor 는 안정 output identity 로 재해석(raw HMONITOR/index 금지), 사라짐/모호 → picker+이유, window 는 엄격 검증(최소 구현은 window=picker 유지 가능). 취소·다른 host·로그아웃·새 operation 은 복원 취소. 재시도 3회 정책 확대 금지.
- **Bⓒ 진단**(계약 8): shell 재연결 한 줄(exit/wait/operation/attempt/복원 결과, credential 없음). 3 s 경고 snapshot = 전경 프로세스명·PID·창/monitor rect·표시 모드, bounded 비동기, 제목·키·환경 수집 금지, "fullscreen 확정" 이라 부르지 않음, watchdog 은 GPU/UIA/락/디스크를 기다리지 않음.
  - **minidump**: owned child handle·생성 identity 로 pin 한 **별도 helper process**, DbgHelp 직렬화, 5 s terminate/relaunch 를 늦추지 않음, helper 정체에도 복구 유지, 일반 사용자 capture child 만(prelogin/SYSTEM/입력 서비스/credential agent 제외), full-memory 금지, `ProgramData\GNLink\diagnostics` SY/BA, 16 MiB/파일·3개·48 MiB·7일, NAS 업로드·로그 수집·installer 제외. **helper 가 멈춘 반례에서 5 s 복구 유지 실행 증거가 없으면 dump 는 보류**하고 로그 진단만 낸다.
- **Bⓐ1 MT A/B**(계약 1): **test-only compile gate 시험 빌드만** 준비(출하 무변경, 출하물에 토글/시험 exe 없음). 장치 생성 직후·다른 스레드 사용 전 적용, 이후 상태는 `GetMultithreadProtected` 로 기록(Set 반환값은 이전 상태). 회사 PC 실기는 사용자 조율 — **실행 절차(arm 별 ≥5회 교차, 회당 ~10분, 비교 지표)와 산출물 위치만 준비**해 보고. 이것이 나머지의 blocker 가 아니다.
- **Bⓑ2(directory 신호)는 하지 않는다.**

## 시험 (Codex 계약 7)
- fixture 가 **실제 supervisor 하위 child** 를 exit44 → "재연결 중" → 같은 desktop/monitor **새 프레임 렌더**까지 UI·프로세스·시간 기록(렌더 창 스크린샷 + 소요 시간).
- supervisor 자체 종료·재시작 후 같은 driver 면 WGC, driver 변경/만료/손상이면 기본 — 실제 child 환경·시작 로그로.
- 반례: 구형 APK 식 dxgi 요청·monitor/window 전환이 격리를 뚫지 않음, 동시 다른 host·취소·monitor 제거·창 identity 재사용, 파일 링크/권한 공격. 부정 대조가 실제로 실패하는지 확인.
- test-only root(격리 ProgramData 대체 경로)는 시험 빌드에만, 출하 gate 로 부재 확인.

## 규칙
- worktree: main `5063cd9` 에서 새 worktree·브랜치(예: `C:/work/gnlink-wt/fix-dxgi-quarantine`, `fix/dxgi-quarantine`). 루트·`.claude/worktrees/main-stability-integration` 사용 금지. webview2 junction(제거 시 rmdir 먼저).
- 사용자 PC·회사 PC 설치본·서비스·세션 조작 금지. 버전·서명·게시·push 금지. 측정 슬롯 CreateNew. GPU 시험은 SLOT_REQUEST 규칙.
- 커밋 Git MCP(`.claude/mcp_git_client_wt.py <worktree>`), history 항목은 이 task 커밋에. 작업목록(DW 행)은 검증용 docs 커밋과 충돌하지 않게 검증용이 통합 때 갱신한다.
- 다른 신뢰 경계·protocol 이 필요해지면 그 부분만 검증용 경유 Codex 재판정, 나머지 계속.

## 보고 (1회, 기능 완성 시)
`D:/remote/remote/.claude/report_dxgi_wedge_fix_impl_r1.md` + `remote#wwml7h91` pin.
내용: commit 목록·worktree·제품/테스트/문서 구분, acceptance 1~4 증거(무엇이 fixture/제품 UI/실기인지), 시험 exe·종료 코드·부정 대조, 출하 gate, dump 포함/보류 여부와 근거, MT A/B 시험 빌드 위치·실기 절차.

## r2 보수 (2026-10-22) — Codex 158aff3 NEEDS_CHANGES (정본 `.claude/codex_dxgi_wedge_fix_r1.md`, D1~D4 + E1 한 묶음) — **SH 보다 먼저**
- **D1 (필수)** 명시 monitor 복원: 화면 목록 미지원·4 s 미수신일 때 현재 primary(0)를 자동 선택하는 fallback(e711f08)은 동일성 계약 위반. GDI DISPLAY 이름+rect 만으로 안정 output identity 를 단정하지 않는다. "논리 desktop" 과 "명시 monitor" 를 구분하고, 명시 monitor 를 확인할 수 없으면 picker + 이유(또는 안정 identity 설계 delta 를 검증용 경유 Codex 에).
- **D2 (필수)** 복원 대상 보고 시점: reveal/InvalidateRect 직후·toolbar decode 시점에 Done/last-rendered 를 보고하지 말고, **실제 present 성공한 그 generation/frame** 에서 descriptor 를 commit. present 실패 시 마지막 실제 화면 유지. present 실패 반례 시험 필수.
- **D3 (저장소)** 시작 시 거부한 future(last>skew) 기록을 NextRecord 가 max 로 재사용해 실제 새 wedge 가 다음 시작에 격리되지 않는 결함 수선. 최초 lock 파일 share=0 동시 생성의 sharing violation 도 bounded 직렬화(지금 32 동시 시험은 미리 만든 파일로만 확인).
- **D4 (test-only)** MT A/B 의 장치 재생성 경로에서 Set 이 MF device-set 뒤에 있음 → 다른 소비자에게 공개되기 전으로. off 강제 변경·출하 MT 변경 없음.
- **E1** 보수 후 고정 후보로 반례 시험 + (검증용이) 독립 UI 사슬. 영향 없는 GPU 시험 전량 반복은 불필요. minidump 보류는 허용.
- 보고: `.claude/report_dxgi_wedge_fix_impl_r2.md` + 검증용 pin. 같은 브랜치에 커밋.

## r3 근거 보완 (2026-10-22) — Codex f0ee888: SOURCE D1~D4 OK / EVIDENCE NEEDS_CHANGES (정본 `.claude/codex_dxgi_wedge_fix_r2.md`) — **제품 변경 없이 근거만**
- **E2** 실제 명시 monitor 경로(특히 toolbar 로 monitor 전환): 접수 → monitor 로 보고 → shell → 새 뷰어가 picker + 사유를 보이고 desktop 자동 선택 0 → 수동 선택. 순수 DecideRestore 시험만으로는 새 wiring 증명이 안 됨. 이 PC 가 모니터 1개라 실장비 제약이면 정확히 기록하고, 가능한 제품 동등 프로세스 경로(예: 시험 호스트의 가상/모의 monitor 목록)로 자동 검증.
- **E3** present 실패 반례: A 실제 present → B 선택 ACK/decode → B present 실패 → B 보고·완료 0, shell 은 A 유지 → 재연결 시 A. B present 성공 대조 포함. 제품 paint/render 경계에 격리 시험 빌드 실패 주입 허용 — 출하 seam 부재 + 양성 대조.
- desktop/GPU/store 전량 반복 불필요. D4 recreate 실행·회사 PC·NVIDIA·승격 ProgramData·재부팅은 별도 미검증, MT 출하 보류 유지.
- 보고: `.claude/report_dxgi_wedge_fix_impl_r3.md` + 검증용 pin.

## r4 보수 (2026-10-22) — Codex 4db9f17 NEEDS_CHANGES (정본 `.claude/codex_dxgi_wedge_fix_r3.md`) — ①·③ 한 묶음 (②는 별도 조사 task)
- **① monitor 변이 재작성**: 지금 변이는 뷰어 충돌(0xC0000005)이라 무효. guard 와 decision 을 일관되게 우회해 **실제로 잘못된 desktop 자동 선택/present 가 일어나 FAIL** 하게 만들 것. crash/timeout 을 안전성 검출로 세지 않음. 보고서의 해당 서술 정정.
- **③ 출하 뷰어 화면 목록 요청 (이번 DW 범위 포함)**: 호스트 지원 capability 확인 뒤 picker 여부와 무관하게 초기 목록 조회(기존 큐 사용). 미지원 호스트에는 요청 0, 조회만으로 select/stream/generation/descriptor 변경 0, 연결별 중복 요청 제한. **시험 뷰어 보조 없이 출하 뷰어로** 툴바 → E2 독립 사슬 + 유효 변이 확인.
- E3 은 닫힘(B 미보고·A 보존·성공 대조·report_on_reveal 변이). 첫 35/1 FAIL 과 이후 3 PASS 는 그대로 기록하고 소급 PASS 금지. runner 는 기존 총 기한·3회 정책 안에서 operation/attempt 별 A 복귀를 볼 수 있음(첫 PID 고정 요구 아님).
- 무관한 store/GPU 전량 반복 불필요. 보고: `.claude/report_dxgi_wedge_fix_impl_r4.md` + 검증용 pin.
