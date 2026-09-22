# 2026-09-15 최초 GPU 타임아웃 — Codex 직접 덤프 분석

## 결론

**AMD 커널 드라이버와 연결된 GPU 엔진 타임아웃 및 Windows의 엔진 리셋 경로를 공식 디버거로 확인했다.** 추가 TDR 데이터에는 타임아웃 관련 프로세스 객체 주소와 `GNLinkStream.e` 이름도 인접해 있다. 따라서 "앱 관련 정보가 전혀 없다"고 말할 수 없다.

하지만 **GNLink가 잘못된 GPU 명령을 보냈다 / AMD 드라이버 버그다 / 하드웨어 고장이다 / LDPlayer가 최초 원인이다** 중 어느 것도 확정하지 못했다. 기록된 프로세스와 고장을 일으킨 주체는 동일하다고 단정할 수 없다. 다른 작업에 의해 영향을 받은 프로세스일 가능성도 열린다.

## 방법·재현 근거

- 원본: `C:/Windows/LiveKernelReports/WATCHDOG/WATCHDOG-20260915-1347.dmp`. 이벤트에는 13:47:19.864 생성 성공이 기록돼 있다. 비승격 File.Open(Read)은 실제 AccessDenied였다.
- 사용자가 검증용 세션에 직접 기존 덤프 분석을 지시했고, 검증용이 승격 파일 복사1회로 사본을 확보했다. Codex는 이 사본을 읽었다. 새 live dump·실행 프로세스 attach·중단·재부팅 없음.
- 사본: `.claude/urgent-126-20260915/WATCHDOG-20260915-1347.dmp`, 499,618B, SHA256 `912c5fde8b474e1ca9eedd695fb9b74a930d15e088b2e577366da62abb9b8115`.
- Codex는 [Microsoft 공식 배포 경로](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/)의 WinDbg 패키지1.2606.22001.0에서 amd64 분석 파일만 작업 폴더로 추출했다. OS 앱 설치 없이 사용했고 추출한 exe/dll105개 모두 Microsoft Authenticode Valid를 확인했다. 기록 `.claude/tools/microsoft-debugger/{download-record,authenticode}.json`.
- Microsoft CDB10.0.29617.1000, Microsoft 공개 심볼(`https://msdl.microsoft.com/download/symbols`)을 사용. 오프라인 `-z <dump>`로 `!analyze -v`, `lmvm amdkmdag`, `kv`, `.enumtag`, `!thread`, `!process`, `dt`를 실행했다. 덤프를 외부에 업로드하지 않았다.
- 최초 기호 경로를 슬래시로 지정한 실행은 symbol store 인식에 실패했다. Windows 경로로 정정한 **`codex-windbg-symbol-analysis.log`가 성공한 심볼 분석 정본**이며 최초 로그도 보존했다.

## 디버거가 확인한 내용

| 항목 | 관측 |
|---|---|
| dump | Mini Kernel Dump, registers/stack 중심 |
| 사건 시각 | 2026-09-15 13:47:19.864 KST |
| 시스템 uptime | 8일16:32:11.795 |
| 코드 | `VIDEO_ENGINE_TIMEOUT_DETECTED (141)` |
| Arg1 | `ffffe68c8c565050` — TDR recovery context 주소 |
| Arg2 | `fffff8025226a6d0` — `amdkmdag.sys+0xFA6D0` |
| Arg3 / Arg4 | `0` / `ffffe68cad8f1080` |
| failure bucket | `LKD_0x141_IMAGE_amdkmdag.sys` |
| 보고 스레드 | `ffffe68c7fa4a4c0`, System의 `dxgmms2!VidSchiWorkerThread` |

[Microsoft의0x141 정의](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/bug-check-0x141---video-engine-timeout-detected)는 디스플레이 엔진 응답 지연과 Arg2의 드라이버 이미지 연관을 설명한다. 이 정의만으로 드라이버 버그나 특정 앱 귀책이 입증되지는 않는다.

공개 심볼로 복원된 스택에는 다음 흐름이 있다(아래는 호출 방향으로 요약):

`VidSchiWorkerThread → VidSchiRun_PriorityTable → VidSchiScheduleCommandToRun → VidSchiCheckHwProgress → VidSchiResetEngines → VidSchiResetEngine → TdrCollectDbgInfoStage1 → 커널 live dump 수집`

이것은 실제 unwinding 결과다. 검증용의 최초 `stack candidates`는 스택 메모리에서 모듈 범위에 속하는 주소를 나열한 별도 자료이며, 그 순서를 실제 호출 프레임으로 사용하지 않는다.

## 추가 TDR 데이터의 GNLink 단서

`!analyze`는 **보고 스레드 소유 프로세스 `System`과 별도로** `PROCESS_OBJECT: ffffe68cad8f1080`을 표시한다. 이 주소는 Arg4와 같다.

`.enumtag`가 보여 준 TDR 추가 데이터 GUID `{270A33FD-3DA6-460D-BA89-3C1BAE21E39B}` 영역을 원본 바이트와 대조했다.

- 파일offset `0x40BF8`: 같은 주소 `ffffe68cad8f1080`.
- 바로 인접한 `0x40C05`: ASCII `GNLinkStream.e`(잘린 이미지 이름).
- 파일header `0x58`의 Arg4와 추가 데이터의 포인터가 일치함을 코드로 확인.
- 구조 요약/해시/offset: `.claude/urgent-126-20260915/codex-gpu-analysis.json`.

**이는 GNLinkStream과 연결되는 프로세스 메타데이터의 단서다.** 이 추가 데이터 블록의 전체 사설 레이아웃을 타입으로 해석한 것은 아니며, 정확한 GPU 명령·원인 함수·앱 결함을 확정하는 증거로 확대하지 않는다.

## 현재 자료로 더 못 좁힌 이유

1. `!process ffffe68cad8f1080 0` 및 `dt nt!_EPROCESS ... ImageFileName UniqueProcessId ...`는 필요한 EPROCESS 페이지가 없어 memory read error. 보고 스레드의 System 프로세스 정보는 읽히지만 별도 관련 객체는 읽히지 않는다.
2. `dxgkrnl!_TDR_RECOVERY_CONTEXT` 타입은 가져온 공개 심볼에 없다. AMD 드라이버의 사설 함수 심볼도 없다.
3. 이 분석으로 GPU의 구체적 미완료 명령, 당시 온도·전압·하드웨어 상태, 앱별 작업 기여를 확정하지 못했다. 모든 추가 데이터가 무가치하거나 어떤 방법으로도 원인을 못 찾는다는 뜻은 아니다.

원인 규명을 더 진행하려면 동일 시점의 온전한 TDR/드라이버 진단 데이터 또는 재발 시 GPU 스케줄러/앱 작업을 연결하는 추적이 필요할 수 있다. 이는 새 수집·재현 범위이며 이번에 실행하지 않았다. AMD 드라이버 교체·GPU reset·앱 종료·재부팅도 하지 않았다.

이 GPU 사건과 이후 GNLink 제어 디스패처 장기 차단은 별도 단계다. 후자는 응답 없는 창의 미리보기 호출 격리·시간 제한·제한 재시도/skip task `t-8tlz9aam`으로 수정 중이며, 그 수정이 최초 GPU 타임아웃까지 해결한다고 주장하지 않는다.

## 로컬 분석 로그

- `.claude/urgent-126-20260915/codex-windbg-symbol-analysis.log`: !analyze/실제 스택/드라이버/추가 TDR 데이터.
- `codex-windbg-context.log`: 스케줄러 스레드·System 프로세스·사설 타입 한계.
- `codex-windbg-owner.log`: 별도 PROCESS_OBJECT 페이지 부재 및 TDR 추가 데이터.
- `.claude/inspect_windbg_package.py`, `extract_windbg_tools.py`: 공식 패키지 부분 다운로드/추출 경로. 실행 파일은 모두 작업 폴더 안에 있고 Git에 포함하지 않는다.
