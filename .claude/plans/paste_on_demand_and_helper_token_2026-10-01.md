# 사용자 실기 2026-10-01 — 두 결함과 설계 초안 (검증용 remote_claude 안, Codex 합의 전)

## 사실 (검증용이 이 PC=호스트에서 직접 확인)
- 설치 0.2.146(인앱 업데이트 10-01 10:10, `updater.log` "Updated -- 0.2.146"). 8개 exe 해시 = 0.2.146 manifest. **`GNLinkClipHelper.exe` 없음** — 0.2.145 업데이터는 자기 payload 목록(10개)만 교체(예상된 1회 공백, `docs/file_copy_field_test.md` 준비 절).
- `host_app.log` 10:11:29·10:28:20: `[file-copy] offer refused verdict=5 files=1 (token: linked-token-duplicate-failed err=1346)` → `(helper start backing off)`. 뷰어 바 문구 `viewer_clip_transfer_bar.cpp:159` "원격 PC의 GNLink 를 한 번 더 업데이트해야 파일을 붙여넣을 수 있습니다" = 사용자가 본 것.
- 텍스트: `host_app.log` 10:10:20 `[clipboard] update applied seq=1` — 뷰어의 `WM_CLIPBOARDUPDATE`(viewer_window_proc.cpp:361) → `capture_local_clipboard` 가 **복사 즉시** 호스트 클립보드에 씀. 파일도 같은 자리(:280~293)에서 즉시 Offer → 호스트 도우미가 원격 클립보드에 게시(바이트는 paste 때).

## 결함 A — 도우미 기동이 승격 호스트에서 항상 실패 (P1, 출시 차단)
- 원인: `file_copy_helper_host.cpp:131~155` 가 `GetTokenInformation(TokenLinkedToken)` 의 토큰을 `DuplicateTokenEx(..., SecurityImpersonation, TokenPrimary)` 한다. **SeTcbPrivilege 없는 호출자가 받는 linked token 은 SecurityIdentification 수준**이라 Impersonation/Primary 로 올리는 복제가 `ERROR_BAD_IMPERSONATION_LEVEL(1346)` 로 실패한다(Windows 동작). 시험 빌드는 비관리자라 이 경로를 못 탔고, 필드 시험 문서가 "자동으로 증명 못 한 것 #1" 로 남긴 바로 그 항목.
- 검증용 안: **셸(explorer) 프로세스 토큰**을 기준으로 바꾼다 — 제품에 이미 실증된 경로 `update_relaunch.cpp:182~215 launch_as_shell_user`(GetShellWindow → OpenProcessToken(TOKEN_DUPLICATE|QUERY) → DuplicateTokenEx Primary → CreateProcessWithTokenW, SeImpersonatePrivilege 필요 — 승격 호스트에 있음). 기존 검사(사용자 SID = 대화형 사용자, 세션 일치, Medium RID, 비제한, `TokenElevationTypeLimited`)는 **그 셸 토큰에 그대로** 건다 → explorer 가 승격돼 있거나 다른 사용자/세션이면 거절(폴백 없음 원칙 유지).
- 대안(비교용): ⑴ SYSTEM 서비스(GNLinkInputService)가 `WTSQueryUserToken` 으로 기동 — 서비스↔스트림 IPC 신설, 범위 큼. ⑵ linked token 으로 `CreateProcessAsUser` — 1346 동일. ⑶ 스케줄러 — 무거움. → ⑴ 은 explorer 가 없을 때(셸 교체·로그온 직후)도 되는 장점이 있으나 이번엔 셸 토큰 + 거절로 충분하다고 본다.
- 부수: 뷰어 문구가 HelperUnavailable 전부를 "한 번 더 업데이트" 로 말한다. 이번처럼 업데이트로 안 고쳐지는 원인에도 그렇게 말해 오도한다 → 호스트가 이유를 둘로 나눠 보낸다(`helper-missing` = 도우미 파일 없음 → "원격 PC의 GNLink 업데이트 필요" / 그 밖 = "원격 PC에서 파일 복사 도우미를 시작하지 못했습니다(…)"). 와이어에 사유 코드 1개 추가 또는 기존 verdict 값 분리 — Codex 확정.
- 시험: 비관리자 시험 빌드로는 재현 불가 → **승격 실측 1회**가 필요(`automation/file_copy_helper_elevated_check.ps1` 이 이미 있음, 사용자 UAC 1회). 자동으로는 셸 토큰 경로의 검사 함수 단위시험(가짜 토큰 대신 현재 셸 토큰으로 verdict ok, 승격 explorer 시뮬레이션은 불가 → 정적 근거).

## 결함 B — 사용자 요구: "복사는 GNLink 창에서 Ctrl+V 할 때" (UX 변경, 텍스트·이미지·파일 공통)
- 지금: 로컬 Ctrl+C 순간 원격 클립보드가 바뀐다(텍스트 내용 전송, 파일은 offer 게시). 사용자가 로컬에서만 쓰려고 복사해도 원격 클립보드를 덮고, 텍스트는 원격에 붙일 생각이 없어도 전송된다.
- 검증용 안(뷰어→원격 방향만): **paste-on-demand**.
  1. `WM_CLIPBOARDUPDATE` 는 아무것도 보내지 않고 "로컬에 새 복사가 있다(seq)" 만 기록한다(세션 시작 push `kMsgPushClipboardNow` 도 같은 규칙으로 제거 또는 보류).
  2. 뷰어 창에 포커스가 있을 때 **Ctrl+V / Shift+Insert keydown** 을 잡으면: 그 키를 아직 보내지 않고, 로컬 클립보드의 현재 내용(텍스트/이미지/파일 offer)을 호스트로 보내 **호스트 클립보드 반영 ack** 를 기다린 뒤(상한 예: 텍스트 1.5 s·파일 offer 5 s), 원래 키 시퀀스를 보낸다. 같은 seq 를 이미 보냈으면 대기 없이 바로 키를 보낸다.
  3. 상한 초과·실패면 키는 그대로 보내고(원격의 이전 클립보드가 붙음) 바에 "붙여넣을 내용을 원격 PC에 아직 보내지 못했습니다" 를 띄운다 — 키를 삼키지 않는다.
  4. 우클릭 메뉴 "붙여넣기" 는 키가 아니라 잡을 수 없다 → 바/툴바에 **"이 PC 클립보드 보내기"** 버튼 1개(또는 창 포커스 진입 때 보내기 옵션). 사용자 확인 필요.
- 원격→뷰어 방향(원격 Ctrl+C → 내 PC Ctrl+V)은 내 PC 의 다른 앱 Ctrl+V 를 가로챌 수 없으므로 **지금처럼 즉시** 둔다(사용자 요청 범위는 클라→원격). 사용자 확인 필요.
- 와이어: 텍스트 update 에 이미 seq·"update applied" 가 있다 — 호스트→뷰어 ack 가 없으면 추가(제어 메시지 1종 또는 기존 응답 재사용). 파일 offer 는 Offer 응답(Accept)이 ack 역할. 이미지도 같은 모양. Codex 확정.
- 입력 경로 주의: 키 지연 동안 뒤따르는 키(빠른 Ctrl+V 연타, Ctrl 떼기)의 순서 보존 — 보류 큐에 같이 쌓아 ack 뒤 한 번에. host IME 모드(`forward_physical`)도 같은 보류를 탄다.

## 순서 제안
A(도우미 토큰)가 출시 차단이고 범위가 작다 → 먼저. B 는 UX·입력·와이어라 Codex 합의 + 사용자 확인 2개(우클릭 붙여넣기 대체 수단, 역방향 유지) 뒤 착수. A 만으로 0.2.147 을 내면 이 PC 는 인앱 업데이트로 도우미가 깔린다(0.2.146 업데이터는 11개 목록을 안다).

## Codex 반론/합의

### 판정·범위

**A 먼저 보수하는 순서에 동의한다.** 0.2.146은 배포 완료였으나 실제 승격 도우미 기동 성공은 미확인으로 남았고, 이번 실기가 그 경로의 P1을 확인했다. B의 사용자 요구(뷰어의 로컬 복사만으로 원격 클립보드를 바꾸지 않음)에도 동의하되, 실패 시 옛 원격 내용을 자동으로 붙여넣는 안과 seq만으로 재전송을 생략하는 안은 수정한다. 제품 코드는 이번 설계 검토에서 변경하지 않았다.

Codex 직접 읽은 사실: 이 PC `%LOCALAPPDATA%\GNLink\host_app.log`의 10-01 10:11:29.501/10:28:20.287에서 `linked-token-duplicate-failed err=1346`, 10:10:20.705에서 텍스트 `update applied seq=1` 확인. `C:\Program Files\GNLink.update\updater.log`의 10:10:03.197은 `Updated -- 0.2.146`; 설치 디렉터리에는 8개 exe만 있고 GNLinkClipHelper.exe가 없다. **도우미 파일 부재와 토큰 실패는 각각 해결해야 한다.** 현재 토큰 검사 단계가 파일 존재 검사보다 먼저라 파일 부재가 이 1346 뒤에 가려진다.

### A — 셸 토큰을 쓰되, 검사·수명·실측을 함께 고정

1. **TokenLinkedToken을 launch용 토큰의 원천으로 쓰지 않고, 현재 셸 프로세스의 Primary 토큰을 얻는 방향 승인.** `GetShellWindow` → PID → 열린 프로세스 HANDLE → `OpenProcessToken` 순서로 잡는다. explorer.exe 이름을 열거해 고르거나 SYSTEM/WTSQueryUserToken 서비스·다른 사용자로 폴백하지 않는다. 셸 부재/종료/권한 부족은 명시적 실패다. updater의 `launch_as_shell_user`는 참고할 토큰 취득 경로이지 그대로 호출할 launcher가 아니다: helper의 suspended 생성→Job 편입→Resume→PID/nonce Hello 검증을 우회해서는 안 된다.
2. **셸 토큰에서 확인할 계약**: Primary, SID=해당 호스트 세션의 대화형 사용자, token session=호스트 process session, 정확히 Medium IL, TokenIsElevated=false, 비제한 토큰. 읽기 실패도 거절한다. 최종 복제 토큰에도 이 조건을 재확인한다. `Limited`만 강제하지 않는다. `Limited` 또는 `Default`이며 위 권한 조건을 모두 만족하는 정상 셸 토큰을 받는다. Default는 linked token이 없다는 뜻이므로 이를 높은 권한으로 단정하면 표준 사용자 계정을 잘못 배제한다. Full/High/System은 여전히 거절한다. 근거: [TOKEN_ELEVATION_TYPE](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ne-winnt-token_elevation_type).
3. 취득/복제/프로세스 HANDLE을 RAII로 한 소유자에게 묶는다. 초기 복제 마스크는 TOKEN_QUERY/TOKEN_DUPLICATE/TOKEN_ASSIGN_PRIMARY였으나, **r3 29729dd 검토에서 이 기동 함수에 한해 MAXIMUM_ALLOWED의 제한 예외를 수용**했다: 복제본 속성 재판정·비상속·국소 수명·토큰 조작/전송 없음·생산 CREATE_SUSPENDED 유지가 조건이다. 이는 HANDLE 접근 범위의 변경이며 오류 5의 원인/승격 기동 성공은 아직 매트릭스 실측 대기다(정본 `.claude/worktrees/feat-helper-shell-token/.claude/codex_helper_shell_token_29729dd.md`). 토큰 session을 바꿔 다른 세션으로 옮기지 않는다. 일반 데스크톱의 도우미를 띄우며, High나 secure desktop에 Medium helper를 붙이는 우회는 하지 않는다. `CreateProcessWithTokenW`는 SeImpersonatePrivilege와 Primary 토큰을 요구하며 caller의 세션에서 실행하므로 세션 일치 검사가 필수다. 부족하면 1314 등 원인으로 실패시키고 비밀번호·새 서비스·추가 권한으로 우회하지 않는다. [CreateProcessWithTokenW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createprocesswithtokenw).
4. **1346의 범위는 정확히 기록**: 현재 DuplicateTokenEx 위치와 1346은 직접 확인했다. Identification impersonation token을 Primary로 복제할 수 없다는 OS 계약은 맞다([ZwDuplicateToken](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-zwduplicatetoken)). 다만 이 로그만으로 모든 Windows/caller가 항상 같은 linked token을 받는다고 증명하지는 않았다. 보수 실측에서 기존 linked token의 Type/ImpersonationLevel과 셸 토큰 Type/Elevation/IL/session/SID 일치 여부를 출력해 해당 원인을 고정한다. 토큰 자체·nonce·원본 파일 경로·자격 증명은 로그에 남기지 않는다.
5. **A의 오류 표시·와이어 확정**: 긴급 0.2.147에서는 와이어를 그대로 둔다. launch 내부는 Missing/TokenRejected/NoShell/SpawnFailed/HandshakeFailed를 구조적으로 구분하고 stage+Win32 error를 기록한다. 원격의 기존 verdict=5는 '원격 PC의 파일 복사 도우미를 시작하지 못했습니다. 설치 상태와 실행 권한을 확인해 주세요'처럼 원인을 단정하지 않는 문구로 바꾼다. 실제 Missing을 확인한 로컬 경로만 업데이트/설치를 안내한다. 정확한 원격 세부 사유는 후속 협상 계약에서 보내도록 한다. **이유 byte를 그냥 붙이거나 새 verdict를 무조건 보내는 안은 반대**: 0.2.146 OfferReply/PrepareReply 파서는 verdict<=8 및 `r.done()`을 요구하므로 구 peer에서 parse 실패가 된다. 세부 사유를 A에 꼭 넣어야 한다면 새 능력의 양방향 협상과 구 peer의 verdict=5 폴백을 별도 계약으로 먼저 확정한다.
6. **검증/출시 기준**: 자동화로 정상 셸 토큰 취득·속성 판정, SID/session/High/제한 토큰 거절, 파일 부재와 토큰 오류의 별도 표시, 실패 후 HANDLE/Job 회수를 확인한다. 그 뒤 현재 도우미 후보를 가진 같은 source의 `--elevated-check`를 사용자 UAC 1회로 실행해 **High 호스트→검증된 Medium helper→Job→Hello→사용자 권한 Stat→Shutdown**까지 확인해야 A를 고쳤다고 판정한다. non-admin 성공 총합만으로 이 경로를 대신하지 않는다. release 준비·일반 회귀는 계속하되, 핵심 승격 기동 미검증을 다시 '수정 완료'로 발표하지 않는다. 격리된 0.2.146 updater→0.2.147 payload 전환 시험도 helper **실제 생성/해시**를 단언한다(11개 manifest 이름만으로 설치 성공을 주장하지 않음). 실제 사용자 설치본 교체/재시작은 사용자의 별도 조작 범위로 남긴다.

### B — 붙여넣기 의도와 clipboard Applied를 결합

- **시작 조건**: WM_CLIPBOARDUPDATE는 로컬 변경/echo 추적만 하고 네트워크 push/offer 게시를 하지 않는다. 세션 최초 push, 폴링/worker와 다른 텍스트·이미지·파일 경로도 같은 조건을 따라야 한다. 포커스를 얻었다는 이유만으로 자동 push하는 옵션을 기본 해결책으로 쓰지 않는다. 지원 형식은 기존 text/image/files 범위이며 unsupported/empty 상태를 옛 원격 clipboard로 조용히 대체하지 않는다.
- **한 paste 작업의 identity**: connection epoch + paste request id + 로컬 snapshot revision/content identity + 형식 + 원격 target generation을 붙인다. gesture를 받은 시점의 snapshot을 전송한다. 연속 paste/자동 반복은 유계로 직렬화하고 중복 key injection을 막는다. UI WndProc에서 기다리지 않고, 세션/포커스/대상 전환·설정 OFF·취소 시 작업과 ACK를 폐기한다. 키보드/마우스 처리와 영상 표시를 네트워크 대기 때문에 멈추지 않는다.
- **ACK는 '수신/버퍼 완료'가 아니라 실제 클립보드 게시 성공**이어야 한다. 텍스트는 SetClipboardData 성공, 이미지는 최종 OS clipboard 적용 성공, 파일은 살아 있는 같은 helper의 PublishResult/OleSetClipboard 성공까지 확인한다. 파일 OfferReply Accept는 그 게시를 기다리는 기존 경로를 활용할 수 있다. 텍스트/이미지는 실제 적용 지점을 확인한 뒤 Applied ACK와 capability/version 계약을 잡는다. 늦은 ACK가 새 세션/다른 요청의 키를 해제하지 못하게 한다.
- **로컬 seq가 같다는 이유만으로 ACK를 생략하지 않는다.** 그 사이 원격 사용자가 복사했거나 helper가 재기동되면 remote clipboard가 달라질 수 있다. 초기 버전은 매 gesture마다 적용 확인을 받는다. 최적화하려면 원격 clipboard/owner generation을 함께 확인하고 외부 변경·helper/session 교체 때 receipt를 무효화해야 한다.
- **실패/timeout 정책은 수정**: 자동으로 기존 원격 clipboard를 붙이는 키를 보내지 않는다. 이번 paste를 실패로 끝내고 '전송하지 못해 붙여넣기를 실행하지 않았습니다' + 재시도를 제공한다. 원격의 기존 내용을 의도적으로 붙이는 기능이 필요하면 명시적인 별도 동작으로 제공한다. 이는 사용자가 요청한 새 내용과 다른 내용이 입력되는 것을 막기 위한 동작 정의다. text 1.5s/offer 5s 같은 값은 초기 deadline 후보이며, 이미지의 기존 전송 상한과 cold helper 기동까지 별도 측정·유계 계약을 정한다.
- **입력 순서**: Ctrl/Shift의 down이 이미 호스트로 갔을 수 있다. V/Insert만 보류하고 modifier up까지 함께 묶어 수초 대기시키면 원격에서 modifier가 눌린 채 남는다. 보류·성공·실패·focus loss마다 remote modifier release를 즉시 처리하고, paste gesture의 필요한 modifier/down/up를 입력 상태기계에서 재구성한다. 뒤따른 입력은 크기/시간 상한이 있는 순서 큐로 처리하며 overflow/취소 때 눌림 상태를 정리한다. legacy VK와 `forward_physical`/host IME가 같은 paste admission을 거쳐야 한다. release-all/긴급 중지/연결 종료는 ACK 뒤 큐에 묶어두지 않는다.
- **구 peer**: 새로운 Applied/capability를 모르면 확인 없는 Ctrl+V를 새 on-demand 성공으로 취급하지 않는다. 업데이트 필요/동기화 불가를 명시하고 새 intent에 대해 fail 처리한다. ACK 기반 기능과 원격의 기존 clipboard paste를 구분한다. 숫자·payload layout은 현재 번호표를 확인해 하나의 협상 계약에 고정한다.
- **시험 최소 묶음**: 로컬 복사만으로 remote 변경/전송 없음(text/image/files), 실제 Ctrl+V·Shift+Insert→해당 snapshot 적용→정확히 한 번 paste, 원격 clipboard 변경 뒤 같은 로컬 seq, ACK 지연/누락/거절/옛 epoch·request id, helper 재기동, V 연타와 Ctrl release/빠른 후속 입력, focus/target 전환, 양 IME 경로, native transfer bar의 실패/재시도 클릭. 복제된 규칙/stub ACK가 아닌 product-equivalent UI/OS clipboard 경로로 확인한다.

### 사용자 선택·다음 담당

사용자에게 비동기 선택 2개를 제시했으며 하단 사용자 답을 반영한다(역방향 확정, 우클릭 대체만 답변 대기):
1. 권장: GNLink **'내 클립보드 붙여넣기'** 버튼이 전송·Applied 확인·붙여넣기까지 한 번에 실행. 대안: **'클립보드 보내기'** 버튼은 전송만 하고 사용자가 원격 메뉴를 사용. 원격 앱의 우클릭 메뉴 클릭을 자동으로 알 수 있다고 주장하지 않는다.
2. **사용자 확정: 원격→로컬은 기존 즉시 반영 유지.** 다시 묻지 않는다.

이 선택은 시간 경과만으로 승인하지 않는다. B의 종단 UX는 답변을 반영해 확정한 뒤 구현한다. A의 위 보수·자동화·승격 실측 준비는 이에 의존하지 않는다. 검증용은 A를 한 지시서로 작업용에 초기 task 위임하고 실제 task ID/검증 후보를 이 논의에 연결한다. 사용자 설치본에 임의 파일 복사·재시작은 하지 않는다. 최종 릴리스는 검증용 고정 후보 OK·서명·NAS 게시·외부 확인 후 보고하며, 사용자가 새로 확정한 '게시=push' 범위는 검수된 관련 main/버전/릴리스 문서의 정상 push까지다.

A 초기 실행 task 연결 확인: **t-laigg5j4** — 담당 `remote_작업용1`(`remote#6jyin6x0`), 검수 `remote_claude`(`remote#gakap8v9`), worktree `feat-helper-shell-token`(main 43f5c45 기준). 정본 `.claude/worktrees/feat-helper-shell-token/.claude/task_helper_shell_token_r1.md`을 Codex가 읽어 A 합의·와이어 불변·B 제외·사용자 설치본 미조작·승격 실측 준비가 반영됐음을 확인했다. 버스 조회 때 state=submitted(발행/위임 확인이며 착수·완료 증거는 아님). 기다리는 결과는 고정 candidate commit·자체 시험/해시·승격 실측 명령, 다음 담당은 작업용1→검증용의 독립 검증이다.

## 사용자 답 (2026-10-01 11:1x)
- 역방향(원격 Ctrl+C → 내 PC): **지금대로 즉시 반영** — 확정.
- 우클릭/메뉴 붙여넣기 대체 수단: 사용자가 질문 의미를 되물음 → 검증용이 풀어서 재질문 중(답 대기).

## 사용자 답 (2026-10-02 10:2x)
- 우클릭/메뉴 붙여넣기 대체: **필요 없음(①)** — "마우스로 안 할 거야. 차라리 드래그 앤 드롭을 하지." → B 는 Ctrl+V / Shift+Insert 경로만. 드래그 앤 드롭 파일 전송은 별도 후속 후보로 작업목록에.
- 역방향: 지금대로(10-01 확정).
→ B 구현 착수 가능(합의 정본 = 이 파일의 "## Codex 반론/합의 / B" 절).

## 2026-10-06 재개 — 2단계 결정 (검증용; Codex 확정 요청)
- 사용자 "바로 작업 진행해" → t-y4wj64jw r2(worktree feat-paste-on-demand `.claude/task_paste_on_demand_r2.md`), 작업용2.
- 설계안: feat-paste-on-demand `.claude/design_paste_on_demand_r1.md` (와이어 §1, 입력 상태기계 §4, 구 peer §3).
- 번호(검증용 지정, main 398d6dc 빈 값): msg 80 ControlPasteTextApply · 81 ControlPasteApplied · Pong 0x1000 kCaptureFlagPasteOnDemandV1 · WM_APP+17 · 타이머 0x714. Hello 비트 없음.
- Q1 latestCopy: GNLink 창 복사 키·R→P 반영이면 PassThrough, hostUserCopyGen 증가는 v1 SendLocal+로그. Q2 세션 시작 로컬 지원 형식 있으면 Local. Q3 빈/미지원/이미지 한도 초과 = 실패(텍스트 대체 없음). Q4 마우스 down·Esc·바 취소 = 취소+문구, 포커스/대상/세션 = 로그만. Q5 호스트 동기 Invoke 2 s, deadline 3 s/8 s/이미지 기존.
- ⚠️ 원 합의는 "와이어는 Codex 확정 뒤 구현" 이었다. Codex 가 버스에 없어 구현을 병행 착수했다 — 와이어 레이아웃(80/81 필드·result/stage enum·0x1000 의미)과 Q1 은 Codex 확정 전 main 머지 금지, 바뀌면 같은 task 다음 revision.
- 2026-10-06 오후: 검증용 독립 통과(verify-c2-ef533f2/.claude/rv_pod/verdict_fd03819.md). Codex 계속 오프라인 → 사용자 선택 **"Codex 없이 게시"**(AskUserQuestion). main ff fd03819 → build-integ 회귀 18종 exit 0 → push → 버전 de5a4ea(0.2.149). Codex 와이어·Q1 확정은 사후 검토로 남김(돌아오면 seq 3649 그대로 처리, 이견은 후속 revision).
