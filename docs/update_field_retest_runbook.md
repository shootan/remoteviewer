# 인앱 업데이트 실기 재시험 — 절차와 복구

2026-09-11. **읽는 순서대로 실행한다.** 이 문서가 있는 이유는 하나다 — 2026-09-11 에 업데이트가 두 번
실패하면서 이 PC 가 못 쓰는 상태가 됐고, 그때 **무엇을 봐야 하고 무엇을 되돌려야 하는지가 적힌 곳이
없었다.** 복구는 사람이 프로세스를 손으로 찾아 죽여서 됐다.

---

## 0. 시작 전 (반드시)

```
qwinsta                                      # console 만 활성일 것. rdp-tcp#N Active 면 중단
Get-CimInstance Win32_Process -Filter "Name LIKE 'GNLink%'" |
  Select ProcessId, ParentProcessId, Name, CreationDate
Get-NetUDPEndpoint -LocalPort 43000,3478 | Select LocalPort, OwningProcess
Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\GNLink" |
  Select DisplayVersion
(Get-Content "C:\Program Files\GNLink.update\updater.log").Count      # 이 줄 수 이후가 이번 시도
```
- **PID 는 매번 바뀐다.** 시작 직전에 다시 열거해 신원을 잡는다. 과거 PID 를 현재 프로세스에 매핑하지 않는다.
- 설치 8파일 + `ui/` 2개의 크기·sha256 을 적어 둔다. **되돌릴 때 비교할 기준이 그것뿐이다.**

## 1. 트리거는 제품 진입점에서만

**트레이 아이콘 → "Check for updates"**.
- ⚠️ **`GNLinkUpdater.exe` 를 직접 부르지 않는다.** Host 의 handoff/ack 단계를 건너뛰고, 그 단계가 검증
  대상의 일부다. 건너뛴 실행을 완주로 세면 판정이 무의미해진다.
- ⚠️ **자동화로 누를 수 없다.** Host 는 `requireAdministrator` 로 링크돼 있어(`CMakeLists.txt:333`)
  비관리자 세션에서 보낸 `WM_COMMAND` 는 UIPI 가 막는다(`ERROR_ACCESS_DENIED`). 사람이 누른다.
- ⚠️ **Client 를 미리 끄지 않는다.** 업데이터가 정지 목록에 넣어 닫고, 끝나면 **사용자 권한으로** 다시
  띄우는 것이 계약이다(`update_relaunch_plan.cpp:19-21`). 그 경로를 시험하는 것이 목적이다.

## 2. 판정 — 화면이 아니라 로그와 파일로

`C:\Program Files\GNLink.update\updater.log` 에서 **0절에 적은 줄 수 이후**를 본다. 전 구간이 있어야 한다:

| 단계 | 있어야 하는 것 |
|---|---|
| 핸드오프 | `host_app.log` 에 `[host-app] update check: UpdateAvailable` → `update: handed over to the updater` → `ExitNow` |
| **자식 정리** | `[host-app] update: standing down -- child pid N exited` ← **2026-09-11 에 없던 정보** |
| Quiesce | 대상이 실제로 종료 |
| Swap | **10파일** |
| Register | `registration ok completed=Service,Firewall,Shortcuts,UninstallEntry` |
| RequiredHealth | `health version=<새 버전>` 뒤 `directory online` |
| Commit / Optional | `result: ...` ← **이 줄이 없으면 성공으로 세지 않는다** |

최종 상태:
- 설치 `DisplayVersion` = 새 버전, 설치 10파일 해시 = 그 릴리스의 값
- **`GNLinkUpdater.exe` 바이트가 바뀌었을 것** — 업데이터가 자기를 교체하는 것이 관측점이다
- **실행 중 Host 1개 · 고아 0개.** `Get-NetUDPEndpoint -LocalPort 43000,3478` 의 소유자가 **현재 Host 의
  자식**일 것
- 다시 뜬 Client 가 **관리자가 아닐 것**: `IsElevated=false`, integrity **Medium**, 부모가 explorer 계열.
  승격된 채로 떴다면 그 자체가 결함이다(`UserProcess` 로 분류한 이유)

⚠️ **화면에 "완료" 가 떠도 완주가 아니다.** 2026-09-11 11:17 은 파일이 바뀐 뒤 health 에서 실패하고
롤백됐는데 화면은 정상으로 보였다.

## 3. 🔴 실패했을 때 — 재시도하지 않는다

**다시 누르면 로그가 덮인다.** 그 상태의 로그가 원인을 찾는 유일한 근거다.

### 3.1 증상: 새 Host 가 계속 죽는다
`host_app.log` 에 `udp bind failed on every candidate port` / `streaming host exited abnormally code=3`
가 반복되고 `streak=` 이 올라간다.

**원인은 고아다.** 포트를 쥔 프로세스를 찾는다:
```
Get-NetUDPEndpoint -LocalPort 43000,3478 | Select LocalPort, OwningProcess
Get-CimInstance Win32_Process -Filter "ProcessId=<그 PID>" |
  Select ProcessId, ParentProcessId, Name, CreationDate
Get-Process -Id <부모 PID> -ErrorAction SilentlyContinue      # 없으면 고아 확정
```
세 조건을 **모두** 확인한 뒤에만 종료한다: **이름이 `GNLinkStream.exe`** · **부모가 이미 없음** ·
**그 포트를 쥐고 있음**.
```
Stop-Process -Id <고아 PID> -Force        # 관리자 권한 필요
```
⚠️ **관리자가 아니면 `Access is denied` 가 난다** — Host 가 elevated 이므로 그 자식도 elevated 다.
작업 관리자를 관리자로 실행해 끝내는 것이 가장 빠르다. **이름으로 광역 kill 하지 않는다.**

그 뒤 **Host 만** 실행한다. 자식은 Host 가 띄운다.
```
"C:\Program Files\GNLink\GNLinkHost.exe" --tray     # UAC 승인 1회 필요
```
`host_app.log` 에 `directory online public=...` 이 나오면 복구 완료다.

### 3.2 증상: 설치본이 반쯤 바뀐 것 같다
설치 10파일 해시를 0절 기록과 비교한다. 롤백이 돌았으면 **이전 버전 값**이어야 한다.
섞여 있으면 **해당 릴리스의 설치본을 수동 설치**한다(아래 4절).

### 3.3 증상: 업데이터가 남아 있다
`GNLinkUpdater.exe` 가 살아 있으면 **기다린다**(health 대기 최대 30초 + 여유). 그 뒤에도 남아 있으면
로그를 보전한 뒤 종료한다. **`.update\staging` 을 지우지 않는다** — 다음 조사의 근거다.

## 4. 안전한 설치 (업데이트 경로를 타지 않는 길)

고아 결함은 **업데이트 경로에서만** 생긴다. 그 경로를 아예 안 타려면 **설치본 수동 실행**이다.
- ⚠️ **대화형으로 설치한다.** `/S`(silent) 는 이 PC 실측에서 **Host 를 내린 뒤 다시 띄우지 않았다.**
  silent 를 쓸 경우 설치 후 **GNLink 를 한 번 실행**해야 한다.
- 설치기는 자기 포트를 쥐지 않고, 실패하면 이전 설치본이 남는다. **기기가 불능이 될 경로가 없다.**
- ⚠️ **설치할 릴리스는 "Client 확인 수정 + 고아 수정" 이 **둘 다** 든 것이어야 한다.**
  `0.2.114` 에는 **고아 수정(`ecf340c`)이 없다** — 그것만 깔면 업데이트가 보이게 되고, 누르는 순간
  고아 경합을 탄다. 손이 닿지 않는 기계에는 **둘 다 든 릴리스 하나만** 깐다.

## 5. 이 문서가 닫지 못하는 것

- **"실제 `StreamingHostProcess` 가 인수인계로 물러날 때 자식이 실제로 죽는다"** — 회귀에는
  `(static)` 순서 검사까지만 있다(`update_stop_process_test`). **행위 관측은 이 재시험이 유일한 자리다.**
- 11:48 업데이터의 **트리거 정체**, `result:` 줄 부재의 **원인**, `CREATE_BREAKAWAY_FROM_JOB` 성공 시
  **무기록** — 별건. 이 재시험에서 로그가 더 나오면 함께 본다.
- **Client 실팝업** — 로그인 후 확인 배선은 정적 배선 검사까지만 증명됐다.
