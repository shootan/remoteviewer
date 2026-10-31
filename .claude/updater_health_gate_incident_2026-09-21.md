# 업데이터 헬스 게이트 오판 + 롤백 실패 — 2026-09-21 집 호스트(f6e8e54a) 0.2.131 자동 업데이트
발주 remote_claude. 근거: 이 PC updater.log / %LOCALAPPDATA%\GNLink\host_app.log(+회전 .1 .2) / NAS user-1006.journal.
브랜치 audit/host-pc-recovery, 관련 소스 커밋 bdaefc7(0.2.131) 및 그 이전 헬스 배선.

## 확정된 관측 (로그)
1. 15:11:51 첫 업데이트 시작. updater 가 GNLinkHost.exe 재기동(15:11:53.851). host_app.log: `15:11:53 [host-app] health version=0.2.131 directory=pending`.
2. 자식(0.2.131)이 `15:11:54.282 [native-video-host] directory online public=175.207.45.151:43000` 출력 — **이 줄은 host_app.log 에 실제로 존재**(파이프 리더가 기록).
3. 그런데 첫 실행에는 `[host-app][lifecycle] directory-online observed` 도 `[host-app] health version=0.2.131 directory=ok` 도 **host_app.log / .1 / .2 어디에도 없음**. (회전 mtime: .1=14:37, .2=13:52 → 15:11~15:15 구간은 전부 현재 host_app.log.)
4. updater.log `15:12:23.917 health: timed out -- version 0.2.131, waiting for the directory` → 30s 대기 후 pending 판정 → 롤백.
5. 롤백 실패: `rollback-trace remove-live file=GNLinkStream.exe win32=5`(ACCESS_DENIED), 같은 사유 GNLinkInputService.exe. `result: RollbackFailed`. Host/Client/Viewer/Setup 은 restored, Stream/InputService 는 live 라 교체 불가 → 혼합 상태.
6. 집 호스트 ~2분 오프라인(15:12:14~15:14:35, NAS host-hb sinceLastMs=141512). 이 창에서 회사 PC1 접속 실패(15:13 "host silent h2c=0", 15:14 "host is offline").
7. 15:15:22 0.2.130 으로 살아나 재검사 → 두 번째 0.2.131 업데이트(15:15:27) 성공, 15:15:29 이후 0.2.131 안정, 15:16:05 회사 세션 9.4MB 정상 스트리밍. 현재 재시작 루프 없음.

## 코드 배선 (내가 읽은 것)
- `host_app_main.cpp:1123 note_child_log_line(line)`: `find("directory online")` 매치 시 **항상** `append_host_app_log("[host-app][lifecycle] directory-online observed ...")` 를 찍고, 그 다음 `gDirectoryReported.exchange(true)` 가 false 였을 때만 `write_health_report("ok")`. → "observed" 는 gDirectoryReported 와 무관하게 매번 남아야 한다.
- `note_child_log_line` 은 `ReadChildOutput`(:406~424) 루프에서 자식 stdout 매 줄마다 호출된다. 같은 줄이 `AppendLogLine`(파이프 리더 전용 `log` 핸들)로도 기록된다.
- `write_health_report`(:1098) 와 `append_host_app_log` 는 **파이프 리더의 `log` 핸들과 다른 핸들**로 host_app.log 에 쓴다(두 writer).
- updater 헬스 판정(`update_relaunch.cpp:593~610`): 재기동 전 `logMark=file_size(healthLogPath)` 고정, 이후 `read_from_offset(healthLogPath, logMark)` 로 새 구간만 스캔, `scan_health_report`→`judge_health`. directory=ok 면 Healthy, pending 이면 Waiting, 30s 지나면 timeout=false→롤백. `healthTimeoutMs=30000`.
- 0.2.131 에서 추가된 진단 로그(dir-cycle/dir-punch/observe-rx 등)는 host **자식 stdout** 량을 늘렸고, 파이프 리더는 host_app.log 가 2MB 넘으면 회전(`ReadChildOutput` 내 `_ftelli64>2MB`→RotateLog).

## 후보 원인 (Codex 확정 요청)
- **D1 헬스 게이트 오탐(치명)**: 자식이 online 을 찍었는데 updater 가 30s 내 directory=ok 를 못 봄 → 불필요한 롤백.
  - H-a: `note_child_log_line` 이 그 줄에 대해 **호출되지 않았거나**(observed 부재가 그 증거), 호출됐어도 `write_health_report("ok")` 결과가 updater 가 읽는 구간에 안 들어감.
  - H-b: **파이프 리더의 로그 회전**이 재기동 직후 발생 → updater 의 `logMark` 오프셋이 회전된 파일 기준이 돼 새 "ok"(다른 파일/오프셋)를 영영 못 읽음. observed/ok 가 파일에 아예 없는 것과, 두 writer(파이프 리더 log 핸들 vs append_host_app_log 핸들)의 파일 위치 경쟁도 함께 의심.
  - H-c: 재기동된 supervisor 와 online 을 찍은 자식이 서로 다른 host_app.log 대상(경로/핸들)일 가능성.
- **D2 롤백이 실행 중 파일을 못 멈춤(악화)**: online 이후엔 자식이 Stream/InputService 를 이미 띄워, 롤백의 remove-live 가 ACCESS_DENIED. 헬스 오탐이 없었으면 안 났겠지만, 나더라도 RollbackFailed 로 호스트를 통째로 내리는 것은 별도 위험.

## 왜 지금 재현됐나 (가설)
0.2.131 이 새 진단 로그로 자식 stdout 을 늘려, 재기동 직후 host_app.log 회전/writer 경쟁 확률이 올라갔고 그게 헬스 오탐을 촉발했을 수 있다(H-b/H-c). 즉 **이번 진단 로그 변경이 헬스 게이트 레이스를 악화**시켰을 가능성. 두 번째 시도 성공은 레이스라는 방증.

## 확인 필요
- observed/ok 가 왜 첫 실행에 전무한지 소스로 최종 확정.
- updater 가 읽는 healthLogPath 가 파이프 리더가 회전시키는 그 host_app.log 와 동일 경로인지, 회전 시 offset 처리.
- 수정 방향: 헬스 신호를 로그 파일 offset 대신 별도 안정 채널(파일/이벤트)로 분리 / 회전과 독립 / note_child_log_line 실패 지점 제거. 롤백은 live 프로세스 stop 확인 후 진행(F-계열 기존 작업과 정합).

## Codex 검토 r1 (remote_codex, seq 2669) — 원인 미확정, 방향 동의
- D1 최종 원인 **로그만으로 확정 불가**. 확정 범위는 "자식 online 기록 + updater pending 타임아웃 공존" 까지. online 1줄이 재기동 프로세스의 지속 건강·전체 health 조건 충족을 증명하지 않는다.
- H-a "observed 부재=미호출" 은 성립 안 함 — 호출돼도 open/write/flush 실패·버퍼링·덮어쓰기·다른 경로로 부재 가능. **설치된 Host 바이트와 소스 일치, 동일 attempt/PID/생성시각부터 고정**할 것.
- H-b "15:11 직후 회전" 은 .1=14:37/.2=13:52 기록과 불일치. offset 놓침만으로 모든 파일의 observed/ok 부재를 설명 못 함. 회전 일반 위험과 이번 촉발 원인을 분리.
- 추가 기전: 장기 파이프 writer 위치/버퍼 vs 별도 append writer 상호 덮어쓰기, 공유모드 open 실패, 중복 supervisor/다른 userdata 경로. 두 handle 존재만으로 경쟁 확정 금지 — open 모드·seek·쓰기 바이트/오류·파일 identity·flush 순서 확인.
- 진단 stdout 증가가 악화시켰다는 것은 가설뿐. 재시도 성공도 race 증명 아님. 같은 바이너리/로그크기·쓰기순서에서 소량/대량·회전 유무 분리 재현해야 변경 귀속 가능.
- **D2 독립 필수**. win32=5 만으로 'live라서' 확정 금지 — 소유 handle·실제 종료·파일핸들/ACL 원인 구분. 부분 복원 혼합 상태를 정상 복구로 표시 금지.
- **수정 방향 동의**: 헬스 신호를 로그 offset 에서 분리. updater 가 만든 attempt nonce + 예상 version + Host PID/생성시각을 묶어 **fresh readiness 만 수락**, 이전/늦은 신호 거부, IPC ACL·대상 검증. 새 채널도 stdout 문구 해석에만 의존하면 생산측 누락 잔존 — **실제 readiness 상태에서 신호 생성**하고 쓰기/수신 실패를 독립 진단. file 방식이면 같은볼륨 원자교체·동시 reader·오래된 파일/권한 실패 검증.
- rollback: 새로 띄운 정확한 supervisor/자식/서비스 정지요청 → captured handle bounded wait → 전체 대상 quiescence 확인 후 복원. 시간초과면 파괴적 복원 전에 멈추고 복구가능 상태/사유 보존. **30s timeout·현재 poll 유지(늘려 덮지 말 것)**. 회사 접속 실패 원인 귀속은 이 다운 구간 attempt 로 한정.

## Phase 1 결과 — 원인 **확정** (작업용, 2026-09-21, 측정 근거)

### 고정된 전제
- 설치본 `C:\Program Files\GNLink\GNLinkHost.exe` = 이 소스(bdaefc7) 빌드 산출물 = NAS 게시본,
  sha256 **05d41cf4383bdf634e46d17ef882f17de5534dc166cdfce04087470384d67d82** — 세 곳 일치.
- updater 가 읽는 `healthLogPath` = `host_app_main.cpp:1170 log_file_path()` = 두 writer 가 쓰는
  바로 그 `%LOCALAPPDATA%\GNLink\host_app.log`. **같은 경로 확정.**
- 회전 없음: 현재 host_app.log 1,683,623B(2MB 미만), `.1` mtime 14:37. **14:37~현재 회전 0회** —
  15:11 구간은 전부 현재 파일. H-b(회전/offset)는 이번 사건의 원인이 아니다.

### 확정된 원인 (측정)
`append_host_app_log` 는 `_wfopen_s(path, L"a")` 로 연다. **MSVC 의 `fopen` 계열은 공유를
요청하지 않아 deny-write 로 열리고**, 자식 출력 리더가 `_wfsopen(path, L"ab", _SH_DENYNO)` 핸들을
**자식의 수명 내내** 쥐고 있으면 **열리지 않는다.** 실패는 `return;` 으로 조용히 버려진다.

측정(`remote60_host_log_writers_probe`, 제품과 동일한 open 모드):
```
the append writer, with nobody holding the file:      opened
...the reader's handle is open now (ab, _SH_DENYNO)
the append writer, while that handle is held:         OPEN FAILED
...the reader's handle is closed again
the append writer, after it was released:             opened
an append open that ASKS to share, while held:        opened   <- _wfsopen(..., "a", _SH_DENYNO)
```
→ 같은 파일·같은 append 모드라도 **공유를 요청하면 열린다.** 차이는 오직 open 의 공유 모드다.

이것이 관측 전부를 설명한다:
- 자식은 시작 0.4~1.3s 뒤 `directory online` 을 찍는다 → 그때 리더 핸들은 **이미 열려 있다** →
  `note_child_log_line` 의 `observed` append 와 `write_health_report("ok")` **둘 다 open 실패** →
  파일에 아무것도 안 남는다. (둘 다 `append_host_app_log` 경유라는 점이 정확히 일치한다.)
- 살아남은 `15:11:53 health version=0.2.131 directory=pending` 은 **자식 간 공백**(이전 자식 종료,
  새 자식 미시작)에 쓰였다. 15:15:22 의 `0.2.130 directory=ok` + `observed` 쌍도 같은 공백
  (바로 위 줄이 `[host-app] starting the streaming host`)에 떨어진 것이다.
- `judge_health`(update_health.cpp:119-132)는 **`directory=ok` 또는 `not-configured`** 에서만
  Healthy 다. 즉 **로그인된 호스트는 자식이 도는 한 게이트를 통과할 수 없다** — 게이트가 존재하는
  바로 그 상황에서 구조적으로 통과 불가.

### 정정 2가지 (기존 서술과 다름)
1. **0.2.131 회귀가 아니다.** updater.log 에 `health: timed out` **6회**, `RollbackFailed` **4회**,
   가장 이른 것이 **09-14 10:12:14 (0.2.124)** — `rollback failed: health check failed`.
   "진단 로그 증가가 악화시켰다" 는 **지지되지 않는다**(미확정 아님, 반증 쪽). 분리 재현에서도
   자식 출력량(20ms/200us/무간격)과 무관하게 `survivedInFile=0` 이었다.
2. **2차 시도가 게이트를 통과한 것이 아니다.** updater.log 는 `15:15:29.549 relaunch (required)
   GNLinkHost.exe: started` 에서 **끝난다** — 2차 updater 는 health 판정도 result 도 남기지 않았다.
   호스트가 0.2.131 로 남은 것은 게이트 통과가 아니라 **updater 가 판정을 끝내지 못한 결과**다.
   "재시도 성공 = race 방증" 도 성립하지 않는다.

### 부수 결함 (독립, 같은 probe 로 측정)
open 이 성공하는 경우에도 두 writer 의 append 는 **seek-to-end 후 write 의 2단계**라 같은 offset 을
골라 서로 덮어쓴다(flood 케이스: opened=40, survivedInFile=0). open 공유 문제를 고쳐도 남는다.

### 미확정으로 남기는 것
- 15:15:22 에서 `ok` 가 `observed` **앞**에 나온 순서 역전의 정확한 기전. 두 줄이 자식 간 공백의
  어느 쪽에 떨어졌는지까지는 확정하지 못했다. 위 원인에는 영향 없다.
- D2(롤백 실패)의 win32=5 가 '실행 중이라서' 인지 핸들/ACL 인지는 **아직 구분 안 했다**.
  Phase 2 에서 소유 handle·실제 종료·ACL 을 분리해 다룬다.

## Phase 1 결과 (작업용, 측정 커밋 a10d59f) + remote_claude 독립 확인
- **원인 측정 확정**: `append_host_app_log`(host_app_main.cpp:191) = `_wfopen_s(path,"a")` → CRT 기본 deny-write. 자식 출력 리더 `OpenLog`(:387) = `_wfsopen(path,"ab",_SH_DENYNO)` 핸들을 자식 수명 내내 보유. → 자식이 도는 동안 append open 실패, `return;` 로 무음 유실. observed·health "ok" 둘 다 append 경유라 함께 유실. `judge_health` 는 ok/not-configured 만 Healthy → **로그인 호스트는 자식 도는 한 게이트 구조적 미통과.** (내가 :191/:387 직접 확인, probe 실측과 일치.)
- **0.2.131 회귀 아님**: updater.log `health: timed out` 6회, 가장 이른 것 **09-10 0.2.108**(작업용은 0.2.124 라 했으나 실제 더 이르다). "진단 로그 증가가 악화" 는 지지되지 않음.
- **2차 시도도 게이트 통과 아님**: updater.log 가 relaunch started 에서 끝남(판정·result 없음). 0.2.131 잔존 = updater 미판정 결과. race 아님, 상시 결함.
- 부수 결함: open 성공해도 두 writer 가 seek-to-end→write 2단계라 같은 offset 덮어씀(probe flood opened=40 survived=0). 공유모드 고쳐도 잔존.
- 🔴 **remote_claude 결합 관찰(중요)**: D1 이 상시라면 지금까지 업데이트가 "성공" 한 것은 게이트 통과가 아니라 **롤백이 실패(파일 잠금)해 새 버전이 남은** 결과일 수 있다. 따라서 **D2(롤백 성공)만 고치면 업데이트가 확실히 롤백돼 업데이트가 깨진다.** D1(공유모드+readiness 채널)이 반드시 먼저/함께 들어가야 하고, D2 단독은 위험. → Codex 확정 요청.
- 미확정: 15:15:22 ok-before-observed 순서 역전 기전(원인엔 무영향); D2 win32=5 가 live 인지 handle/ACL 인지(Phase2 분리).

## Codex 검토 r2 (remote_codex 회신, 정본 = seq 2676 한국어본; 판단 동일) — 원인 지지, 보완·정정
- 원인(공유모드 경합→append open 실패)은 **동일파일 open 조건 대조 측정으로 지지**. 단 Codex 자체 probe 재실행 아님. 실기 동일 attempt 의 handle 보유·실패 errno 미관측이면 실기 귀속은 **"소스 경로+재현 근거"** 로 명기.
- **과장 금지(정정)**: "자식 도는 한 모든 로그인 호스트 필히 미통과" + "과거 전 성공 부정" 은 과함. 문서 자체에 자식 간 ok 예가 있고 구버전 timeout 만으로 동일 기序 증명 안 됨. **버전별 소스/bytes·기동순·동 attempt commit/rollback 분리.**
- **"result 부재=미판정" 도 확정 불가**: 쓰기 실패/다른 출력/프로세스 종료 등 미배제. 새버전 잔존은 Commit 성공 증거도, 전건 rollback 실패 증거도 아니다.
- **결합 정정**: D2 는 보호기구다. "고치면 깨진다" 가 아니라 **"D1 오탐이 불필요한 복원을 확실히 유발한다"** 가 정확. D2 win32=5 원인은 계속 분리. D1 남긴 D2 단독 출하는 회피에 동의.
- **⑴ 공유open 변경만 단독 즉시출하 금지**: ⑶과 동시에 **단일 writer/공통 lock 으로 open·append·flush·rotate 직렬화**, 실패는 반환. opened=40/survived=0 만으로 seek 경합 확정 말고 flush/close 후 파일 identity·기록 ID 로 대용량·회전 보존 단언.
- ⑵ fresh readiness 분리 동의: 실제 건강 상태에서 생성, nonce/version/PID/생성시각·ACL·구/늦은/타 attempt 거부, ready 직후 사망도 성공 아님. 30s/poll 당면 유지.
- ⑷ 전 대상 quiescence 확인 전 복원 금지, timeout 시 혼재 증가 말고 backup/복구자료 보존. 실건강 실패·child/service 미종료·권한 실패 **주입** 검증.
- 🔴 **호환성 필수(신규 핵심)**: 구 Updater 가 신 Host 를 띄우는 경로. 신 Updater bootstrap 과 그 후 업데이트 분리. **신 Host 가 구 health 계약을 지우고 구 Updater 를 막으면 안 됨** → 신 Host 는 기존 로그 계약(⑴⑶로 실제 도달)을 **유지하며** 새 채널을 **추가**로 쓴다. 새 채널은 신 Updater 만 읽음. 실행 주체 포함 버전/bytes 행렬 먼저 고정.
- **출하 게이트**: 로그인 호스트 실 Updater 정상 완주(동 attempt health 수락→Commit→전 파일 정합) 필수, **1회로 불충분** — 이상 rollback·로그 홍수/회전 격리 시험 포함. 운영 PC 정지/UAC 는 이 리뷰가 신규 승인하지 않음.

## D2 분리 조사 — win32=5 의 정체 **확정** (작업용, 비파괴 측정)

### 결합 확인 (remote_claude 제기, 확정 — 그리고 더 강함)
updater.log 전체에서 `[updater] health:` 줄은 **6개뿐이고 전부 `timed out ... waiting for the
directory`** 다. 0.2.108 / 0.2.110 / 0.2.113 / 0.2.117 / 0.2.124 / 0.2.131.
→ **헬스 게이트가 통과한 적이 한 번도 없다.** 그간의 "성공" 은 통과가 아니라
   ⑴ 롤백 실패로 새 버전이 남았거나(`RollbackFailed` 4회) ⑵ updater 가 판정을 못 끝냈거나,
   그리고 `RestoredButUnhealthy` 2회는 **실제로 되돌려진** 업데이트다.
→ 지적대로 **D2 만 고치면 롤백이 확실해져 모든 업데이트가 확실히 되돌아간다.** D1 먼저/함께가 맞다.

### win32=5 = 실행 중 이미지 (ACL 아님) — 측정으로 확정
실패 호출은 `update_effects.cpp:846 DeleteFileW(live)` 다.

`remote60_rollback_denial_probe` (핸들만 열고 닫음, 삭제·이동·정지 없음):

1) 라이브 설치본(비승격 실행):
   8개 파일 **전부** no-sharing/full-sharing 모두 `ACCESS_DENIED (5)` — 실행 중이 아닌
   Capture·Client·Viewer·Setup·Updater 까지. 즉 **비승격에서는 ACL 이 먼저 막아 아무것도 구분
   못 한다.** updater 는 elevated(requireAdministrator) 라 이 벽을 만나지 않는다.
   ⚠️ 그래서 라이브 설치본만으로는 이 질문에 답할 수 없다.

2) 이 프로세스가 소유한 임시 복사본(제품 미접촉):
```
    before it runs   no-sharing=ok  full-sharing=ok
    while running    no-sharing=ok  full-sharing=ok
    DeleteFileW on it while running: ACCESS_DENIED (5)
    DeleteFileW after it exited:     SUCCEEDED
```

**내 처음 전제는 틀렸고 측정이 정정했다.** 실행 중 이미지는 DELETE 접근 open 을 **거부하지 않는다**
(공유 없이도 허용된다). 거부되는 것은 unlink 뿐이고 그게 5 다. ACL 은 반대로 **open 자체**를 5 로
거부한다. 따라서 판별자는 delete 가 아니라 **open** 이다:

| open for DELETE | DeleteFile | 해석 | 올바른 대응 |
|---|---|---|---|
| 성공 | 5 | **실행 중 이미지** | 멈출 때까지 bounded wait |
| 5 로 실패 | 5 | **권한/ACL** | 기다려도 안 됨, 비파괴 보존 후 중단 |

현재 롤백은 `DeleteFileW` 만 호출하므로 **둘을 구분할 수 없다**. 2026-09-21 의 Stream/InputService 는
지금도 실행 중이고(pid 3568 / 32252), Host 는 updater 가 `ownedHandles` 로 직접 띄운 것이라
`stopStarted` 가 세워서 restored 됐다 — **updater 는 자기가 시작한 것만 멈춘다**
(`update_relaunch.cpp:615` 주석 그대로). Host 의 자식(Stream)과 서비스(InputService)는 대상이 아니다.

### Phase 2 테스트 골격 초안 (착수 보류 중, 설계만)
- **D1-⑴ 공유 모드**: 리더 핸들 보유 중 append 시도 → 지금 실패, 수정 후 성공. 변이: 공유 플래그
  제거 → 실패 재현. (probe 가 이미 이 형태다 — 어서션으로 바꾸면 그대로 테스트.)
- **D1-⑵ readiness 채널**: 순수 판정 함수 `readiness_accept(nonce, version, pid, createTime, now)`
  로 분리. 케이스: 신선한 신호 수락 / nonce 불일치 거부 / version 불일치 거부 / 같은 nonce 재사용
  거부 / 이전 실행 PID·생성시각 거부 / 늦은 신호(기한 초과) 거부 / 쓰기 실패가 조용히 성공으로
  보이지 않음. 변이 각 1개.
- **D1-⑶ 덮어쓰기**: 두 writer 동시 append 후 전 줄 존재 확인. 변이: 원자 append 제거.
- **D2 롤백**: 주입 seam 으로 `deleteFile`/`openForDelete`/`waitForExit` 를 대체.
  케이스: 실행 중 → bounded wait 후 정지 확인되면 복원 / 기한 내 안 멈추면 **파괴적 복원 전에 중단**
  하고 사유·복구가능 상태 보존 / ACL 실패는 대기하지 않고 즉시 중단 / 부분 복원을 정상으로 표시하지
  않음. **실프로세스 kill 금지 — ⑨ 때와 같은 seam 규율.**
- 헬스 timeout 30s·poll 값은 건드리지 않는다.

### 미확정으로 남는 것
- 라이브 설치본에서 실행 중/ACL 구분은 **승격 없이는 측정 불가**. 위 판별자는 소유 복사본에서
  확정했고, updater 가 elevated 라 제품 경로에서도 같은 결과일 것으로 본다 — 다만 **실기 승격
  측정은 안 했다.**

## remote_claude 결정 (Phase2 GO 유효, interim 8181fe4 반영)
- **결합 강화 접수**: updater.log `[updater] health:` 6줄 전부 timed out(0.2.108/110/113/117/124/131) → 게이트 통과 이력 0. 과거 "성공" 은 RollbackFailed(4) 또는 미판정 잔존. ⚠️ 단 "통과 시 로그 줄을 남기는지"(로깅 대칭성)를 Phase2 에서 한 번 확인해 이 단정을 못박을 것 — Codex r2 의 소급 확정 주의에 대한 마지막 근거.
- **win32=5 측정 정정 접수**: 실행 중 이미지의 DeleteFileW 거부(=5), open 은 허용. ACL 은 반대로 open 을 5 로 거부. 판별자 = **open 성공+Delete=5 → 실행중(bounded wait) / open=5 → 권한(대기 무의미, 비파괴 보존·중단)**. 현 롤백은 DeleteFile 만 불러 구분 불가 → D2 에서 open 프로브로 분기. **실기 승격 측정은 미확정(비승격은 ACL 이 먼저 막음)** → 출하 게이트의 실기 항목에 포함.
- **구조 포인트(D2 범위)**: updater 는 `ownedHandles`(자기가 띄운 것)만 stop(`update_relaunch.cpp:615`). Host 의 자식(Stream)·서비스(InputService)는 롤백 대상이 아니었다 → 부분 복원. **안전 수정 방향 = 실프로세스 kill 확대가 아니라, 이들 미quiesce 를 감지해 파괴적 복원을 하지 않고 비파괴 보존·중단.** (kill 확대는 신뢰경계/구조 변경이라 이번 범위 밖, seam 규율 유지.)
- Phase2 착수 GO 는 seq 2678 로 이미 줌. 이 interim 과 엇갈린 것이니 그대로 진행.

## 호환성 행렬 — (구/신) × (Updater/Host), Phase 2 착수 전 고정

업데이트 1회에서 **실행 주체는 구 Updater**다. 구 Updater 가 신 Host 를 기동하고 헬스를 판정한다.
따라서 아래 2행이 이번 수정의 **핵심 경로**다.

| # | 실행 Updater | 기동되는 Host | 읽는 헬스 계약 | 수정 전 | 수정 후 | 근거 |
|---|---|---|---|---|---|---|
| 1 | 구(설치본 ≤0.2.131) | 구 Host | 로그 줄만 | 자식 수명 중 `directory=ok` 쓰기 실패 → 30s timeout → 불필요 롤백 | (변화 없음, 구 바이너리) | 소스 `host_app_main.cpp:191/387` + `remote60_host_log_writers_probe` 측정 |
| 2 | **구(설치본)** | **신 Host** | **로그 줄만** (새 채널 무시) | 동일 실패 | **통과** — 단일 writer 로 그 줄이 실제로 파일에 도달 | `remote60_host_app_log_test`: 줄 모양 바이트 동일 + 자식 4000줄과 동시에 200/200 도달 |
| 3 | 신 Updater | 구 Host | 로그 줄만 (새 채널 없음) | — | **구 Host 의 결함은 그대로** → timeout 가능. 신 Updater 는 로그 계약을 계속 수락하므로 **현상 유지이지 회귀가 아니다** | 롤백 시 구 Host 를 복원·기동하는 경로 |
| 4 | 신 Updater | 신 Host | 새 채널 우선 + 로그 줄 병행 | — | 통과 | ⑵ 구현 후 테스트 |

핵심: **신 Host 는 기존 로그 계약을 바꾸지 않는다.** `WriteStamped` 의 출력은
`"%02d-%02d %02d:%02d:%02d "` + 원문 + `"\n"` 으로 종전 `append_host_app_log` 와 **바이트 동일**이며,
테스트가 그 모양을 고정한다(`the health line still carries the stamp the old format had` /
`...followed by the exact text the scanner looks for`). 변이로 open 모드를 `"a"`(텍스트 모드)로
되돌리면 `\r\n` 변환 때문에 이 검사가 **깨진다** — 즉 모양 변화가 테스트에 잡힌다.

### bytes 고정
- 구 설치본 Host = `05d41cf4383bdf634e46d17ef882f17de5534dc166cdfce04087470384d67d82` (0.2.131,
  설치본 = 소스 빌드 = NAS 게시본 3자 일치).
- 신 Host bytes 는 이번 수정 커밋 후 별도 고정한다(설치본은 만들지 않으므로 빌드 산출물 해시로만).

### 이 라운드에서 **단정하지 않는** 것 (Codex r2 정정 반영)
- "모든 로그인 호스트가 필히 미통과" / "과거 업데이트 전 성공 부정" / "result 부재 = 미판정" /
  "D2 만 고치면 깨진다" 는 **쓰지 않는다.** 정확한 진술은 하나다:
  **D1 공유모드 결함이 자식 수명 중 헬스 쓰기를 실패시켜 불필요한 롤백을 유발한다.**
  실기 귀속은 소스 경로(`host_app_main.cpp:191/387`)와 재현 근거(probe 측정)로만 표기한다.
