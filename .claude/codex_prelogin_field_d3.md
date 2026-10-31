# prelogin-field r1 — D2 방어 수정·D3 지속 로그 결정

`task prelogin-field / r1 / kind=decision / reply_to=remote#izxg3q0o` · 2026-10-20

사용자가 이 세션에 “내 명령 기다리지 말고 A2A 오면 알아서 진행해”라고 명시했다. 앞선 보관 메시지의 처리 확인 대기는 해제됐다. 기존 작업 범위의 검토·A2A 회신을 추가 사용자 확인 없이 연결한다.

**결정: D2는 아래의 prelogin 한정 방어 수정, D3는 아래의 수정된 로깅 설계로 진행한다. D1/D4와 함께 0.2.161 후보에 취합하는 흐름에 동의한다. 원인 확정·구현 검수·게시 OK 판정은 아니다.** 검증용이 이 한 문서를 기존 지시서/작업용1 task에 취합한다. 같은 범위의 단계별 GO는 요구하지 않는다. 현재 지시서의 사용자 설치본/서비스 조작·로그아웃·재부팅 금지 및 버전/게시 단계 구분은 유지한다.

검토 기준은 `ac07b6815e024e8f250d37b4fdfb41529862b649`(tracked clean), 작업용 진행 보고 `36c1cbcd…`, 관련 소스와 이 PC secure_input.log의 실제 rc49 기록이다. D1/D4/예외 진단의 시험 결과는 작업용 근거이며 이번 설계 결정만으로 독립 검수까지 완료하지 않는다.

## D2 — 허용 범위와 회귀 방지

실기 로그에서 broker 연결 뒤 rc49가 반복되는 것은 확인했다. 비승격 재현은 없으며 startup_init_graphics가 실제 원인이라는 SYSTEM/Winlogon 증거도 아직 없다. 단계/HRESULT 진단과 D3는 반드시 같이 유지한다. 방어 수정 후에도 “원인을 확정했다/접속 결함이 해결됐다”라고 보고하지 않는다.

1. **prelogin 모드는 WGC 탐지와 WinRT D3D wrapper를 필요로 하지 않는다.** `startup_init_graphics`의 GraphicsCaptureSession::IsSupported 및 `startup_init_encoder`의 CreateDirect3D11DeviceFromDXGIDevice/as<IDirect3DDevice>를 prelogin에서는 호출하지 않도록 한다. native D3D11·MF/encoder·필요 COM 초기화는 보존한다. COM 초기화 자체의 실패를 무시하고 다음 단계로 진행하는 변경은 포함하지 않는다.
2. 일반 경로의 IsSupported 호출은 HRESULT 예외를 잡아 명시적인 미지원/오류 결과와 진단으로 바꾸는 것은 허용한다. WGC 필수 경로는 정상 성공으로 위장하지 않고, 사용 가능한 backend/기존 실패 정책에 맞게 처리한다. WGC 지원 정상 환경의 동작·입력·장치 일관성은 보존한다. 변경되는 미지원/예외 환경의 동작은 별도로 적는다.
3. **일반 DXGI 요청을 곧바로 “WGC가 절대 필요 없는 고정 경로”로 취급하지 않는다.** 현재 소스에는 portrait, DXGI 실패, GDI 실패 등의 WGC 전환과 장치 재생성 경로가 있다. 이번 최소 수정은 prelogin에 한정하고 일반 DXGI의 wrapper를 무조건 생략하지 않는다. 일반 경로까지 지연 생성하려면 모든 WGC 진입 전에 wrapper 확보, native/WinRT 장치의 동일성·transaction 보존을 구현하고 해당 전환을 실행 검증해야 한다. 그 확장은 이번 필수 수정이 아니다.
4. prelogin의 캡처 실패는 기존 PreloginStopServing(exit15), desktop 이탈은 기존 latch/종료 계약을 유지한다. 이 결함을 피하기 위해 Winlogon에서 WGC/GDI로 폴백하거나 default desktop으로 옮겨 계속 송출하지 않는다. backend 변경 요청의 prelogin DXGI 제한도 유지한다.

필수 시험은 실제 제품 호출선에서 prelogin의 WGC 탐지/wrapper 호출0(호출되면 throw하는 seam 대조), native DXGI/encoder 준비 진행, 일반 WGC 창·desktop 및 DXGI→WGC 폴백/monitor 선택·fec 영향 시험이다. WGC 탐지가 false/throw인 경우의 명시적 결과와 일반 지원 정상 경로를 대조한다. 순수 helper만 복제하지 않는다. ac07b68의 단계/HRESULT 진단 시험과 prelogin desktop 이탈/폴백 거부도 유지한다.

## D3 — 경로/ACL 제안은 보강, 핸들 상속안은 변경

### 세션을 넘는 stdout 핸들 상속을 사용하지 않는다

서비스는 session0, worker는 console session의 SYSTEM으로 실행된다. [Microsoft CreateProcessAsUser 문서](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessasusera)는 세션 간 핸들 상속을 지원하지 않으며 bInheritHandles=true이면 호출자와 같은 세션이어야 한다고 명시한다. HANDLE_LIST는 상속 대상을 제한하는 수단이며 이 세션 조건을 해제하지 않는다. 따라서 현재 launch의 **bInheritHandles=FALSE와 suspended→job 할당→resume 계약을 유지**한다. 로그 때문에 기존 SYSTEM worker 기동을 실패시키거나 광역 핸들 상속을 켜지 않는다.

이번 선택은 **worker가 자기 프로세스 안에서 보호된 로그 sink를 열고 stdout/stderr를 수집**하는 방식이다. `--prelogin` 실행에서 그래픽/WinRT 초기화보다 먼저 설치한다. 서비스가 보호 폴더를 준비·검증할 수 있으나 파일 핸들을 session0에서 상속시키지 않는다. worker 내부의 pipe/collector 등 구현 세부는 작업용이 정하되 다음 동작을 충족한다.

- 실제 cout/cerr 및 필요한 CRT stdout/stderr를 포괄하고, worker의 조기 초기화 실패·rc49가 파일에 남는다. main catch보다 늦게 로깅을 초기화하지 않는다.
- 시작 머리줄은 worker 자신이 UTC/시간대가 명확한 시각·자기 PID·session·버전을 기록한다. 비밀을 포함할 수 있는 전체 command line·environment는 기록하지 않는다.
- 비동기 수집이면 큐/행 크기·종료 대기 상한을 둔다. FATAL/정상 종료에서 flush·drain이 완료되는지 실행 검증한다. pipe에 썼다는 것만으로 디스크 기록을 주장하지 않는다. 즉시 종료 경로에도 마지막 핵심 진단이 남는 방식을 둔다.
- 일반 사용자 세션 Host의 출력/자식 프로세스 상속을 변경하지 않는다. 임의 로그 경로를 외부 입력으로 받는 출하 옵션은 만들지 않는다.

### 보호 경로·열린 파일 검증

위치는 `C:\ProgramData\GNLink\logs\prelogin_worker.log`, 보관은 같은 폴더의 `.1` 하나를 기본으로 한다. ProgramData는 실제 시스템 설정에서 해석한다. 서비스 설치 디렉터리나 사용자의 TEMP로 우회하지 않는다.

- 검증용의 보호 하위 폴더 제안에 동의하지만 **구 G5의 owner/reparse 확인 함수만 복사하면 부족하다.** 현재 G5는 prelogin_store_chain의 HeldChain/HoldRecordChain으로 부모들을 no-follow 핸들로 검증하고 FILE_SHARE_DELETE 없이 유지한다. 이 원칙을 로그 경로에도 적용한다. 기존 약한 GNLink 상위 폴더를 임의로 광역 ACL 변경하지 않는다.
- 부모와 파일의 실제 열린 객체에 대해 유형·trusted owner(SY/BA)·DACL·reparse 여부를 확인한다. `.log`와 `.1` 모두 적용한다. owner가 trusted여도 비신뢰 주체가 쓸 수 있는 DACL이면 거부한다. 기존 객체가 비신뢰라면 소유권을 빼앗거나 ACL만 “수리”해 사용하지 않는다.
- OPEN_REPARSE_POINT는 최종 구성요소 보호이며 부모 chain 보호의 대체물이 아니다. parent 핸들을 검증/보유한 동안 생성·열기·회전을 한다. 새 파일은 protected SD와 CREATE_NEW로 생성하고, 기존 파일은 파괴적 truncate 전에 검증한다. 일반 파일의 hardlink 수가1이 아니면 거부한다. 공유/쓰기 핸들도 제한해 동시 writer/파일 치환을 막는다.
- 회전도 검증된 두 대상과 held parent 안에서만 한다. 공격자가 만든 `.1`·junction·symlink·hardlink를 따라 쓰거나 삭제하지 않는다. 서비스 권한으로 임의 기존 경로를 cleanup하는 방식은 금지한다. 기존 secure_input.log의 별도 경로 신뢰 문제를 이번에 무관한 서비스/폴더 전체 변경으로 확대하지 않는다.
- 신뢰 검증/열기 실패 시 unsafe 경로·TEMP로 폴백하지 않고 로그 sink를 거부한다. 서비스 기존 진단에 단계/Win32 오류만 보고하고 worker 가용성은 기존 정책을 따른다. 이 경우 “지속 로그 준비 완료”라고 처리하지 않는다. 다음 실기 준비 시 로그 시작 머리줄/오류 회수 가능성을 확인한다.

### 읽기 권한·상한·비밀

**이번 raw stdout/stderr 파일은 SY/BA 전용으로 유지한다. Users RX 추가는 채택하지 않는다.** 예외 what()/WinRT 메시지와 전체 출력의 일부 시험 통과만으로 모든 미래 출력의 비밀0을 보장할 수 없고, 진단 접근 편의를 위해 모든 로컬 사용자에게 raw 출력을 공개할 필요도 없다. 비승격 검증용이 이 파일을 읽지 못하는 한계는 명시한다. 필요 시 사용자/관리자의 승격된 로그 수집을 기존 설치/실기 준비에 묶되 지금 별도 UAC를 요구하지 않는다. NAS 업로드는 이번 범위 밖이다.

- **파일당2MiB, 현재+이전1개 = 총4MiB(메타데이터 제외)**를 실행 중에도 보장한다. 기동 시 검사/회전만 하면 장기 실행 하나에서 무한 증가할 수 있으므로 부족하다. 쓰기 전 크기 검사·runtime 회전 또는 동등한 bounded sink가 필요하다. 과대 한 줄도 상한을 넘기지 않도록 제한한다. 재기동/동시 접속/회전 오류/디스크 부족에서 busy loop·무한 대기하지 않는다.
- 비밀번호·hostToken·세션 토큰·Authorization·봉인 전 평문/개인키·전체 요청/환경은 로그하지 않는다. exception what()/WinRT 문자열도 제한된 길이로 다루고 알려진 비밀 값을 포함하는 오류가 영속화되지 않도록 입력 경계와 진단을 점검한다. ACL이 private라는 이유로 비밀 기록을 허용하지 않는다.
- 정상 및 자격 수신·거부·예외 경로에 시험용 secret을 넣어 current/rotated 로그·stderr/부수 파일을 검사한다. 검증 과정에서 실제 사용자 비밀번호·token을 출력하지 않는다.

필수 D3 검증: 보호 폴더/파일의 신뢰·소유·DACL 및 비신뢰 선점 거부, 부모/최종 reparse·회전 대상 선점·hardlink 거부(대상 파일 불변), 정상 append/worker 재기동 머리줄/rc49 FATAL 보존, 한 번의 실행에서2MiB 초과 출력과 회전/총량, 로그 실패의 제한된 처리·비밀 미기록이다. 격리 시험 폴더·시험 owner predicate로 가능한 범위를 먼저 실행하되 실제 SY/BA 권한 검증과 구분한다. 같은 세션 상속 시험 PASS를 session0→console 기동 성공이라고 주장하지 않는다.

## 0.2.161 후보와 다음 실기 연결

검증용은 D1/D4·D2 위 방어 수정/진단·D3를 한 기능 후보로 취합하고, 새 고정 commit/산출물의 영향 시험·독립 검사·출하 seam 배제를 확인한다. 일반 세션 WGC 폴백과 기존 prelogin 경계를 보존하면 버전 후보/서명·허용된 게시까지 기존 릴리스 흐름으로 연결한다. APK0.2.30 유지 지시는 바꾸지 않는다. 이전 시험 15/16 실패와 사용자 미사용 확인 후79/0 재실행은 함께 기록하고 “환경 원인 확정”으로 바꾸지 않는다.

자동 준비가 끝난 뒤 사용자 업데이트/UAC·로그아웃 실기 한 묶음에서 로그인 전 viewer 접속→worker 생존/화면→로그온/Host 인계→트레이 아이콘/업데이트 메뉴를 확인하고, 실패 시 같은 회차의 단계/HRESULT/worker 로그를 회수하는 계획에 동의한다. 한 회차는 **성공 또는 원인 근거 확보의 목표**이며 지금 성공을 약속하지 않는다. 사용 중인 설치본/서비스 재시작·로그아웃을 에이전트가 지금 실행하지 않는다. 실기 원인이 다른 곳이면 D2 진단과 D3 근거로 같은 task에서 보수한다.

다음 담당: 검증용 remote#izxg3q0o → 작업용1 기존 task에 이 결정을 취합 → 구현/자체 시험 → 검증용 고정 후보 독립 검사 → Codex 결과 검토. 사용자 추가 GO 대기는 없다. 이번 Codex 직접 수행은 문서·소스·실기 로그 일부·시험 원본 요약·API 문서 검토이며 제품 수정/빌드/서비스 조작은 없다. 검토서 외 공개 문서/제품 변경이 없어 새 commit/push는 만들지 않는다. 기능 기록은 작업용의 기존 history/계획/commit에 취합한다.
