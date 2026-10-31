# DW2 — OSLink q9 기반 수정 목표 판정

2026-10-28 · **목표·단계 구조 수용, 아래 내용으로 구체화한다.** 현재는 정적 분석과 목표 수립 판정이다. NVIDIA 현장 wedge 원인 확정·제품 SOURCE/출하 승인이나 사용자 기기 실험의 실행 결과가 아니다. 같은 내용을 다시 설계 상신할 필요 없이 검증용은 이 합의본으로 사용자에게 목표안을 보고할 수 있다.

## 목표와 우선순위

**DW2: DXGI와 공유 context의 동시 사용 위험을 줄여 NVIDIA의 캡처 wedge를 억제하고, 실패가 남아도 복구 가능한 화면으로 돌아오는 시간을 줄인다.** “모든 NVIDIA wedge를 없앴다”는 보편 보증과 시험에서 무사고였다는 관측은 구분한다.

| 단계 | 범위 | 결정 |
|---|---|---|
| 1 | G-f 앱 소유 DXGI/context 직렬화 + G-a runtime MT 보호 비교 | 4개 arm으로 비교. G-f는 알려진 겹침 보수 후보이며 성능·수명 gate를 통과해 채택 |
| 병행 | G-e 실패 감지·재연결 지연 축소 | 독립 설계/검수·출하 가능. 우선 11초의 실제 경로·시간 기준을 확정 |
| 2 | G-b 캡처 장치/처리 장치 분리 | 단계 1에 wedge나 굶주림이 남거나 외부 MFT 공유 경계를 다루지 못하면 진행 |
| 장기 | G-d 캡처 process 분리(CS) | 유지. 당장 1단계의 필수 선행 작업으로 만들지 않음 |
| 별도 최적화 | G-c 조기 Release | 현재도 onFrame의 copy/submit 뒤 release하는 구조. 1차 주력 수정에서 제외하되 hold 계측에 따라 재검토 |

## 근거와 확정하지 않은 것

- report_dxgi_oslink_q9_r1.md SHA-256 **266a7e1ecd01ec0549acbf61bf6cd81d4182ff6b563fe64dd4afecc615e0b8ab**, A 정본 q9_dxgi_usage.md **6bcdf58b024fc5c1a11705cfa0664c1a9d439c60fb32067693973261de63127b**를 읽고 일치 확인했다. GNLink 대조 기준은 **af49c26ff8fff967003433d07126f58384f93cc3**다.
- 고정 source에서 DXGI Acquire/ReleaseFrame/DuplicateOutput의 앱 직렬화 부재, onFrame의 readback Submit이 별도 context mutex를 잡는 것, readback worker의 context 사용 및 독립 5초 watchdog을 직접 대조했다.
- Microsoft는 immediate context와 대부분 DXGI 호출의 동시 사용을 허용하지 않고 같은 thread에서 수행하는 방향을 권고한다. 현재 source에는 그 사용이 겹칠 수 있는 경로가 있다. 다만 이것만으로 **13건 모두의 원인이 그것이었다**고 확정하지 않는다. [D3D11·DXGI 멀티스레딩](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-intro).
- OSLink의 관측된 D1/D2 분리와 캡처 쪽 짧은 copy 경로는 설계 비교 근거다. IID 바이트 부재만으로 모든 동적 호출·미확인 thread의 보호 상태까지 증명하지 않는다. 미확인 thread 3개는 그대로 남긴다. **GNLink의 현재 공유 장치에 SINGLETHREADED 플래그를 복사하지 않는다.**
- 기존 off/on 현장 통계는 WGC 선행·장치 생성 경로가 함께 바뀐 관찰이다. MT on의 인과 효과를 분리한 실험이 아니다. AMD 무사고도 NVIDIA 수정을 입증하지 않는다.
- 확인한 MF 보호 권고는 **decoder** 문서다. encoder MFT에 대한 필수 규정으로 바꾸지 않는다. G-a의 근거는 공유 context의 runtime 보호 수단과 알려진 구조적 위험이며, vendor encoder 내부 사용은 미확인으로 둔다. [MF decoder 문서](https://learn.microsoft.com/en-us/windows/win32/medfound/supporting-direct3d-11-video-decoding-in-media-foundation), [ID3D11Multithread 계약과 비용](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_4/nn-d3d11_4-id3d11multithread).

## G-f의 범위와 구현 gate

**G-f를 결과와 무관하게 최종 채택한다고 미리 결정하지는 않는다.** 앱이 소유한 context와 DXGI 호출의 알려진 겹침을 제거하는 방향은 채택 후보로 수용한다. 그러나 앱 mutex는 MFT 내부 thread가 따르지 않으므로 **공유 장치 전체의 배타성**을 증명하지 않는다. 장치/context/호출 thread/잠금의 표로 앱 소유 경로와 외부 MFT/WGC 경계를 구분한다. G-a만으로 앱의 모든 다중 호출 순서가 맞아진다고도 가정하지 않는다.

구현은 다음 조건을 만족해야 한다.

1. Acquire/ReleaseFrame/DuplicateOutput 및 동일 device의 관련 DXGI 호출을 앱 context의 직렬화 규약에 넣는다. recreate/실패 cleanup/stop 경로까지 호출 목록을 검토한다. MF 내부 경계까지 어떤 보호로 덮는지는 별도로 설명한다.
2. **Acquire→onFrame→Release 전체에 기존 non-recursive mutex를 잡지 않는다.** onFrame→Submit이 이미 같은 mutex를 잡으므로 자기 교착이 된다. DXGI API 호출 구간별 배타성과 기존 context 작업을 맞추고, callback·queue 대기·Sleep·Stop/join에는 그 lock을 유지하지 않는다. lock 순서를 기존 readback 수명 규칙과 대조한다.
3. lock 안의 Acquire는 새 frame을 기다리며 readback을 굶기지 않게 timeout 0을 시험하고, 양보/프레임 pacing은 lock 밖에서 한다. busy spin·과도한 폴링은 피한다. **현재 Host 설정은 env를 1~1000으로 clamp하므로 env=0만 보내면 0 시험이 되지 않는다.** G-f의 실제 0 경로와 effective timeout 로그가 필요하다.
4. timeout 0은 새 desktop frame을 기다리는 시간을 없애는 설정이다. driver 호출 자체가 절대 멈추지 않는다는 보증이나 5초 watchdog의 대체물이 아니다. [AcquireNextFrame timeout 계약](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgioutputduplication-acquirenextframe).
5. G-a는 해당 device에서 worker/MFT 공유 시작 전에 적용하고 실제 보호 상태를 로그로 확인한다. app mutex/runtime MT의 lock 순서·WGC 전환·device 재생성 뒤 상태를 검토한다. 시험 중 상태가 달라지면 다른 arm으로 조용히 합산하지 않는다.
6. watchdog은 GPU와 app context lock을 전혀 기다리지 않는 별도 경로로 유지한다. context-lock 대기와 DXGI API 내부 정지를 서로 다른 phase로 기록한다. UI/Stop이 그 lock이나 hung worker의 무한 join 때문에 감시까지 막지 않아야 한다.

G-c를 현재 단계에서 빼는 데 동의한다. 단순히 Release를 앞당기는 것이 항상 성능상 우월하다는 전제는 두지 않는다. Microsoft도 Release/다음 Acquire 간격을 줄이는 성능 고려를 설명한다. 현재 Flush를 없애는 별건은 DONOTFLUSH query의 진행성까지 검토해야 하므로 이번 수정에 끼우지 않는다. [ReleaseFrame과 surface 수명/성능](https://learn.microsoft.com/en-us/windows/desktop/api/DXGI1_2/nf-dxgi1_2-idxgioutputduplication-releaseframe).

## 4-arm 실기 측정안

**두 변수의 2×2 비교**를 유지한다. 같은 source base·계측·codec/MFT·해상도/FPS/bitrate·게임 장면·display 설정에서 다음만 바꾼다.

| arm | G-f | G-a |
|---|---|---|
| 0 | 현재 앱 동기화/기존 timeout·idle 정책 | off |
| F | 앱 직렬화 + 실제 Acquire 0/lock 밖 양보 | off |
| A | 현재 앱 동기화/기존 timeout·idle 정책 | on |
| FA | F와 같음 | on |

F는 직렬화와 그에 필요한 wait 정책을 묶은 변경이다. 4-arm만으로 두 내부 변경의 효과까지 따로 분리했다고 하지 않는다. 0/A는 알려진 앱 동시 사용 위험이 남는 **진단용 비교 arm**이며 최종 출하 선택지로 삼지 않는다. WGC를 거쳐 MT를 바꿔 arm을 만들지 않는다.

각 block은 **새 Host process/device로 시작**한다. 동일 test binary의 arm 설정 또는 동일 base의 arm별 artifact를 고정하고 effective MT/Acquire timeout·backend·MFT mode를 남긴다. WGC fallback 뒤의 시간은 DXGI 노출에서 뺀다. 사용자 설치의 격리 기록/설정을 지워 baseline을 만들지 말고 자기 시험 상태를 사용한다.

### 시간·반복·장비

- **screening**: 재현된 홈 NVIDIA에서 arm마다 **실제 DXGI 최소 20분 + fresh Host/device 시작 10회 + 같은 Host의 재접속 10회**. 순서를 섞은 짧은 block으로 나누고 block 순서를 기록한다. 총 DXGI 노출은 최소 약 80분이며 재시작/준비 시간은 별도다.
- **확인**: 선택 후보를 홈·회사 NVIDIA에서 **각 DXGI 4시간 + fresh 시작/재접속 각각 30회**까지 확인한다. 새로운 wedge가 나오면 실패 사건으로 남기고 해당 후보의 원인/다음 단계로 넘어간다. 사용자 일정에 맞춰 여러 회로 나눌 수 있다. AMD는 별도 기능/성능 회귀용이다.
- baseline 또는 위험 arm에서 wedge나 0.2.98 수준의 뚜렷한 100~500 ms 굶주림이 재현되면 그 arm은 중단할 수 있다. 중단 전 실제 노출·실패 시각·시도 수를 보존하고, 못 채운 시간을 성공으로 합산하지 않는다. 동일 노출이 아니면 단순 사건 개수로 우열을 주장하지 않는다.
- 시간만으로 첫 세션 결함을 놓치지 않도록 cold device 시작과 warm reconnect를 별도 denominator로 센다. fullscreen/모드 전환이 필요하면 사용자 수행 시각을 기록한다. 에이전트가 사용자 게임에 임의 입력을 보내지 않는다.

이 수치는 **초기 실행 가능한 목표**다. 사용자와 장비/시간을 한 번 조율해 확정하고 첫 run 전에 고정한다. 결과를 본 뒤 무사고가 되도록 N이나 허용치를 줄이지 않는다. 4시간 0건도 영구 무사고 증명은 아니다. 예를 들어 단순 Poisson 가정의 95% 사건률 상한은 약 3/T이므로 4시간 0건이면 약 0.75회/시간 수준이며, 실제 군집/첫 세션 효과 때문에 그 가정도 조심해서 해석한다.

### 지표·선정 gate

- 사건: watchdog exit 44, 오래 멈췄다가 돌아온 call, app lock 굶주림, API 실패와 backend fallback. phase/Host-run/device 세대/arm·노출시간·시작 횟수를 함께 남긴다. exit 44만 세면 timeout 미만 stall을 놓친다.
- 성능: **호출자별** context lock wait와 lock hold, Acquire/Release API wall time, acquired-frame hold, readback completion 대기, capture→encode→present 지연 p95/p99, fresh-frame FPS/drop, CPU/GPU 사용량. lock 대기와 GPU 대기를 섞지 않는다. 지금의 holdMax/합계 로그만으로 p99를 만들 수 없으므로 동일한 histogram/표본 계측을 모든 arm에 넣는다.
- 초기 성능 gate: 같은 기기·같은 workload에서 caller별 ctxWait/hold p99는 baseline보다 **max(1 ms, baseline p99의 10%) 초과 증가 금지**, capture→present p95는 **한 target frame 간격 초과 증가 금지**. 100~500 ms 굶주림 재현은 별도 실패다. 표본이 없는 정적 구간을 p99=0으로 만들지 않는다. CPU/GPU·FPS/drop의 뚜렷한 회귀도 원인을 설명하고 채택 전에 해소한다.
- 안정성 gate: 확인 후보의 정해진 실 노출/시작 횟수에서 wedge 0, stop/recreate/backend 전환·첫 영상·포인터/해상도 동작 정상. G-f lock이 필요한데 A arm만 좋아졌다는 이유로 앱 겹침을 그대로 출하하지 않는다. F/FA의 성능·공유 경계 근거를 함께 보고 선택한다.

## G-e — 독립 복구 작업의 범위

11초를 특정 상수의 탓으로 먼저 확정하지 않는다. source에 viewer의 **kUdpControlReadTimeoutMs=12000** 등 후보는 있지만, 사건에서 그 경로가 실제 지연을 만들었는지는 미확인이다. 해당 사건의 **viewer 종류/버전·Host/supervisor 버전·시계 차이**부터 고정한다. NAS 수집 시각이나 서로 다른 PC의 wall clock 단순 차이를 stage 시간으로 쓰지 않는다.

단계별로 fault 시작 → watchdog 감지/Host exit → supervisor 새 Host → viewer peer 실패 감지 → 새 인증/연결 → picker 실제 표시 → 선택 → **새 세대의 첫 fresh frame present**를 연결한다. fault/재연결 op ID와 monotonic 측정·필요한 clock offset을 남긴다. 단순 render 반복이나 예전 마지막 frame 재표시는 영상 복귀로 세지 않는다.

**완료 목표를 두 개로 나눈다.**

1. 현재 재접속 계약이 picker 재표시라면, **wedge seam 시작부터 새 연결의 조작 가능한 picker까지 ≤10초**를 자동 복구 목표로 한다.
2. 그 뒤 시험이 명시적으로 허용된 target을 선택한 시점부터 **첫 fresh frame ≤2초**를 별도로 잰다. 사용자의 선택 대기 시간을 숨기고 “자동 영상 복귀 ≤10초”라고 보고하지 않는다. 해당 client가 원래 자동 복원을 허용하는 경로라면 그 기존 계약 안에서 fault→fresh frame ≤10초를 직접 확인할 수 있다.

**이전 window를 자동으로 다시 선택하는 새 기능은 이번 G-e에 넣지 않는다.** 이전 HWND를 새 process에 그대로 적용하거나 현재 prelogin/권한 상태를 건너뛰지 않는다. 그런 복원 정책이 필요하면 별도의 사용자 행동/target identity 설계로 다룬다. 실제 사건 client의 기존 복구 계약을 source로 확정해 위 목표의 적용 대상을 명시한다.

G-e는 독립 변경으로 검수·출하할 수 있다. 5초 watchdog을 성급히 줄이거나 모든 12초 응답 timeout을 일괄 축소하지 않는다. 긴 window-list/thumbnail 응답과 peer 생존 판정은 구분한다. 기존 session에 묶인 probe/ACK·재전송 상태와 실제 지연 원인을 기준으로, 살아 있는 정적 session을 끊지 않는 쪽으로 바꾼다. owner 종료·다른 PC tap·새 로그인·takeover 뒤 자동 재탈취는 허용하지 않고, 새 binding의 입력/clipboard 기존 경계를 유지한다.

검증은 실제 GPU hang을 만들지 않는 **시험 seam의 capture 진행 정지**로 한다. 자기 fixture에서 Acquire/Release/lock-wait phase를 분리해 총 30회(각 10회), watchdog부터 기존 supervisor/실 client 복구·표시까지 확인한다. lab의 각 유효 trial에서 위 시간 목표를 만족시키고 p50/p95/max·실패 수를 남긴다. **정적 화면 1시간 + 정상 control이 살아 있는 무영상 구간에서 오탐 0**, 기존 지연/손실·한 방향 단절·긴 응답 시험의 회귀도 확인한다. 이 검증은 실제 driver wedge 예방 효과의 증거와 분리한다.

## G-b / G-d 전환 조건

G-f/FA에서 wedge가 남거나 성능 gate를 못 맞추거나 외부 MFT 공유 때문에 동기화 경계를 설명하지 못하면 **G-b의 설계로 이동**한다. capture D1의 immediate context를 캡처 소유 thread에 묶고 readback/처리/MFT를 다른 device로 옮기는 범위를 검토한다. encoder device만 따로 만들고 readback이 D1을 계속 다중 사용하면 q9의 핵심 분리가 아니다.

공유 texture에는 GPU fence 또는 keyed-mutex의 producer/consumer 수명·지원 확인·bounded 대기·device loss/resize·stop 회수가 필요하다. CPU SetEvent는 준비 신호일 뿐 GPU 완료 증명이 아니다. 미지원 시의 명시 fallback도 설계에 포함한다. OSLink에서 event를 봤다는 이유로 그 경로를 그대로 복제하지 않는다.

G-b가 process/driver 전체 실패까지 격리하는 것은 아니다. G-d의 process 분리는 장기 fault-domain 개선으로 유지하고, G-e를 그것이 끝날 때까지 묶어 두지 않는다.

## 사용자 기기 조율과 다음 담당

현재 할 일은 **검증용의 수정 목표안 보고**다. 구현은 기능별 계획/고정 SOURCE·검증 역할로 진행한다. 실제 NVIDIA/게임 측정은 후보 hash·arm/시간표·격리된 상태·원래 실행으로 돌아갈 절차를 구체화한 뒤, 사용자에게 **어느 PC를 언제 사용할지 한 번에 조율**한다. 이 목표 판정만으로 실사용 Host 설치·재시작이나 회사/홈 PC 장시간 측정을 시작하지 않는다.

실기 하네스는 기존 input OFF/power OFF·job/종료 확인·slot·자료 소유 규칙을 따른다. 공유 emulator/사용자 clipboard나 금지된 prelogin 시험을 이 작업에 끼우지 않는다. 검증용 하나가 작업용과 사용자에게 합의본을 연결한다. 이 Codex는 제품 수정·build·GPU/Host 실행·배포·기기 조작을 하지 않았고, 이번 산출물은 이 판정서뿐이다. commit/push 없음.

지시서의 **슬롯 해제 시 기존 ka-pc 실 Host e2e 우선**은 유지한다. DW2의 실기 비교가 그 작업을 선점한다는 지시는 아니다.

사용자 보고용 요지: **먼저 캡처와 GPU 처리의 동시 사용 경계를 바로잡고 MT 보호의 효과·지연을 같은 NVIDIA 조건에서 비교한다. 복구 시간 단축은 별도로 진행한다. 작은 수정으로 안정성과 지연을 함께 만족시키지 못하면 캡처 장치를 분리한다.**
