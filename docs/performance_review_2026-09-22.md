# Remote / GNLink 성능 개선 조사 — 2026-09-22

## 결론

**개선 여지가 있다. 가장 먼저 다룰 것은 네트워크 용량에 맞는 송신 제어와 입력의 직렬 대기다.** GPU 연산 자체만 빠르게 만들어서는 좁은 회선에서 반복되는 정지를 해결하지 못한다. 이어서 수신 큐, GPU→CPU 왕복, 서버의 동기 디스크 작업을 줄이고, Android의 주기 조회·JNI·썸네일 할당을 개선하는 순서가 합리적이다.

이 문서는 **정적 소스 분석 결과**다. 아래의 비용 발생 구조는 코드로 확인했지만, 현재 기기의 CPU/GPU 사용률·프레임 지연·배터리를 새로 측정하지 않았다. 따라서 개선율, 실제 병목 1위, 특정 장애의 원인, 1080p60 달성 여부를 확정하지 않는다. 현재 배포·설치 상태도 이번 조사에서는 조회하지 않았다.

## 1. 기준과 조사 범위

- 기준: 로컬 main `bed73ed` (`product_version.hpp`: Windows **0.2.134**). 분석 브랜치 `audit/performance-20260922`.
- 현재 루트 체크아웃 `refactor/viewer-split@1261ef5`는 최신 main이 아니다. 그 워킹트리의 미커밋 파일은 보존했다. 과거 `host-pc-recovery` 경로가 현재 유효한 worktree가 아님도 확인했다.
- `apps/`, `libs/`, `automation/`의 추적된 소스·설정 파일 **586개 / 142,742줄**을 인벤토리·패턴 검색했다. 파일별 SHA256·줄 수·검색 건수는 [CSV](performance_inventory_2026-09-22.csv)에 있다. 생성물·외부 SDK·바이너리·문서·일부 텍스트 벡터는 집계 제외다. 패턴 수에는 주석도 포함하므로 결함 개수로 읽으면 안 된다.
- 전 파일 수작업 정독을 주장하지 않는다. 전체 목록과 비용 패턴을 먼저 조사하고, 아래 근거 파일의 호출자·분기·실행 경로를 집중 검토했다. 테스트는 구현 의도를 대조하는 자료이며 이번에 실행한 결과가 아니다.
- 제품 코드 수정, 빌드, 벤치마크, 사용자 앱 기동/종료, GPU 부하, NAS 변경, 배포, main 병합, push는 수행하지 않았다.
- 별도 `feat/connect-bandwidth@60aca38`에 **C1 LAN Punch 응답 수정**이 있다. main에는 아직 없고 이 문서의 기준에 섞지 않았다. 그 브랜치의 수정·검증 결과를 합칠 때 O02를 재판정해야 한다.

| 영역 | 조사 파일 | 줄 수 | 주요 경로 |
|---|---:|---:|---|
| Windows native | 403 | 110,831 | Host 감독자·Stream, PC Client/WebView·Viewer, 제어·미디어·업데이트 |
| Android GNLink | 55 | 12,211 | Activity, JNI, 공유 세션 코어, MediaCodec, 로그·업데이트 |
| 디렉터리/릴레이 서버 | 24 | 5,470 | 인증·접속·UDP relay·로그·저장소 |
| 캡처/공통 라이브러리 | 12 | 1,511 | DXGI acquire·capture runtime·입력 프로토콜 |
| automation | 91 | 12,718 | 기존 성능/재접속/soak/릴리스 검증 도구 |
| shared fixture | 1 | 1 | 매니페스트 검증용 HTML; 벡터 TXT는 수치 제외 |

### 실행 경로

```text
GNLinkHost(감독자)
  └─ GNLinkStream: DXGI/WGC/GDI 캡처 → staging/readback → 크기/색 변환
       → MFT H264 → 송신 큐 → 페이싱/FEC/NACK → 직접 경로 또는 NAS relay
       ↕ 별도 TCP / UDP 제어 Serve: 입력·Ping·창목록·격리 썸네일

PC GNLinkClient(WebView shell) → GNLinkViewer
  UDP ingress → 조립/NACK → MFT decode → 최신 프레임 슬롯 → D3D present

Android MainActivity ↔ JNI ↔ ClientSessionController(공유 C++)
  수신/조립 → MediaCodec → Surface; 상태/썸네일은 JNI로 UI 전달
```

## 2. 우선순위와 기존 작업 연결

P1은 사용자 체감·정지 위험 때문에 먼저 계측/설계할 대상, P2는 해당 비용이 실제로 높을 때 적용할 대상, P3은 규모가 작거나 조건부인 대상이다. **P1이라고 모두 확정 장애인 것은 아니다.** 각 항목의 ‘관측’은 정적 확인, ‘영향’은 검증할 가설이다.

| ID | 우선 | 영역 / 개선 후보 | 규모 | 기존 작업과 관계 |
|---|---|---|---|---|
| O01 | P1 | 대역 추정·ABR·페이싱·복구 트래픽 예산 통합 | 큼 | C0 확장, C6 연계 |
| O02 | P1 | LAN 직접 경로 선택과 실제 응답 가능성 | 중 | C1, 별도 브랜치 진행 중 |
| O03 | P1 | 입력 ACK 직렬 대기 해소 | 큼 | C4/R02, C5 연계 |
| O04 | P1 | PC UDP 큐를 패킷 수뿐 아니라 체류 시간으로 관리 | 중 | 수신 경로 후속 |
| O05 | P1 | 서버 로그 I/O가 릴레이를 막는 구조 분리 | 중 | 기존 서버 원장과 병합 관리 |
| O06 | P1 | 썸네일 deadline 동안 같은 제어 세션이 기다리는 비용 | 중~큼 | C8; 격리 자체는 이미 완료 |
| O07 | P2 | surface encode에서도 수행되는 BGRA readback 제거 검토 | 큼 | GPU surface/캡처 백로그 |
| O08 | P2 | surface encode 선택을 FPS·tail·GPU별 계측으로 | 중 | O07 선행 조건 |
| O09 | P2 | 변경 영역을 이용해 정적 화면 비용을 앞단에서 절감 | 큼 | C10/캡처 계측 연계 |
| O10 | P2 | PC NV12 surface의 SRV 재사용 | 소~중 | 새 국소 개선 후보 |
| O11 | P2 | NACK 캐시의 전체 AU 복사·개수 기반 상한 개선 | 중 | C6 하위 |
| O12 | P2 | NACK 시간 예산을 RTT/전송 상황에 맞게 조절 | 중~큼 | C6; Windows/Android 정책 구분 |
| O13 | P2 | Android 3ms 출력 펌프를 알림/기한 기반 대기로 | 중 | 모바일 CPU·전력 |
| O14 | P2 | Android 상태 스냅샷의 JNI·JSON 중복 처리 절감 | 중 | 기존 가변 polling 위 후속 |
| O15 | P2 | Android 썸네일 JNI 복사·Bitmap 할당 절감 | 중 | picker/UI |
| O16 | P2 | Host 로그 소비자의 행별 flush와 파이프 역압력 | 중 | 관측성 유지 필요 |
| O17 | P2 | PC 새로고침 요청 합치기·동시 worker 상한 | 소~중 | shell 네트워크 부하 |
| O18 | P2 | 서버 store 저장을 순서 보장 비동기화 | 중 | D1 내구성 요구 보존 |
| O19 | P3 | UDP 송신 scratch 버퍼 재사용 | 소 | 메모리 할당 프로파일 후 |
| O20 | P3 | 제어 idle 2ms 주기 깨우기 축소 | 중 | O03 scheduler 연계 |
| O21 | P3 | PC HTTP session/connection 수명 재사용 | 중 | 인증·origin 분리 필수 |
| O22 | P3 | Android 로컬 로그 writer의 유계 배치·수명 관리 | 소~중 | 업로더 큐와 별개 |
| O23 | P3 | WebView 호스트 목록 변경분만 갱신 | 소 | 대량 호스트에서만 우선 |
| O24 | P1 | 전체 파이프라인 지표를 한 기준선으로 묶기 | 중 | C10·기존 automation 활용 |

## 3. 네트워크·제어 경로

### O01 — 대역 추정과 전체 송신 예산

- **관측:** `host_abr.hpp:20`의 `AbrInputs`는 FPS·지연·decode tail·drop·송신 프레임 수 등이며 실제 가용 대역 추정 입력이 없다. 1초 단위 결정, 4초 warmup, 연속 pressure 조건이 있다. `host_startup_config.cpp:148`은 기본 peak 500%, floor 40Mbps, IDR peak 100Mbps. `host_net_io.cpp:94`에서 이를 AU 전송 시간으로 바꾼다. **이미 페이싱이 있으므로 ‘페이싱 부재’가 아니다.**
- **영향 가설:** 사용자 평균 비트레이트가 낮아도 순간 burst는 좁은 Wi-Fi/WAN 큐를 압박할 수 있다. 인코딩이 잘돼도 수신 피드백이 오기 전 수초간 품질이 붕괴할 여지가 있다. 이것만으로 과거 특정 정지의 원인을 확정하지 않는다.
- **개선:** 수신 도착 간격/지연 변화·전송량·RTT·큐 체류 시간을 연결한 추정기를 설계하고 사용자 설정을 상한으로 적용한다. 미디어+FEC+NACK+IDR가 **하나의 링크 예산**을 공유하도록 한다. 초기 보수적 시작, 상향 probing, 하향 hysteresis를 함께 둔다.
- **주의:** 40/100Mbps 값을 무작정 낮추면 IDR 전송 시간이 길어지고 송신 큐가 다시 차는 역효과가 있다. 인코더 IDR 크기·NACK hold·조립 상한까지 동시에 검증한다. 프로토콜 피드백 확장은 구버전 협상 필요.
- **검증:** 제한 회선 4/8/12/25Mbps, RTT 1/30/80ms, 순간 용량 하락, 정적→고화질 영상 전환. 프레임 p95/p99 간격·queue age·실제 링크 bytes·회복 시간·품질 복귀를 비교한다.

### O02 — LAN 경로

- **관측:** `connect_candidates.hpp:80`에 private/public 후보 구성은 이미 있고, `directory_rendezvous.cpp:189`의 `PunchAny`는 매 라운드 모든 후보에 보내고 먼저 수신된 일치 endpoint를 고른다. `directory_session_bootstrap.cpp:151`에서 사용한다. 선호 목록 순서만으로 LAN 승리를 보장하지 않는다.
- **개선:** 현재 진행 중인 `60aca38`의 Host Punch 응답을 먼저 검증한다. 후보별 송신/응답 시간, 선택 이유를 근거로 직접 경로 우선의 짧고 유계인 선택 창이 추가로 필요한지 판단한다. LAN IP가 있다는 이유로 무조건 선택하면 다른 사설망에서 실패한다.
- **검증:** 동일 LAN 두 PC, 서로 다른 사설망, relay-only 환경. 연결 성공률·첫 화면 시간·선택 endpoint·NAS relay bytes를 함께 확인. **새 중복 구현을 시작할 항목이 아니다.**

### O03 — 입력 직렬화

- **관측:** `viewer_control_client.cpp:409-420`는 action마다 `execute_control_action`을 호출하고 응답이 와야 다음 action으로 간다. `native_video_client_shared_core.cpp:463`의 scheduler가 Ping·상태·입력 등을 고른다. 입력 move 합치기는 이미 존재한다.
- **개선:** 마우스 이동은 최신 상태 중심, 키/button down/up은 순서·ACK를 보존하는 작은 유계 in-flight 창으로 분리한다. 우선순위만 바꿔서는 이미 기다리는 응답을 추월하지 못한다. 호스트 dedup·세대·해제 보장·구버전 협상을 포함한 프로토콜 설계가 필요하다.
- **검증:** RTT별 move 생성→송신→호스트 적용, drag 연속성, 키 누락/중복/고착 0, Ping/clipboard/thumbnail 동시 부하. 기존 252ms 수치는 작업목록의 과거 사례이며 이번 측정값이 아니다.

### O04 — PC ingress 큐

- **관측:** `viewer_udp_ingress.hpp:65-95`의 Packet은 1,600바이트 고정 배열이고 deque 상한은 1,024개다. `Pop:52-59`는 lock 안에서 packet 이동과 출력 복사를 한다. 고정 배열의 move는 payload 소유권 전달이 아니다. 제어 패킷은 이 큐 전에 처리한다는 장점이 있다.
- **영향:** 약 1.6MB 상한은 메모리 상한이지 지연 상한은 아니다. 이론상 1.6MB/12Mbps는 약 1.1초의 전송량이다(실제 체류 시간 측정치가 아니며 패킷 크기·소비 속도에 따라 달라진다).
- **개선:** 재사용 slab/ring 인덱스로 전달, lock 구간 축소, 수신 시각과 oldest age 계측, byte+age 상한을 함께 적용한다. 임의 H264 조각 드롭은 참조 체인을 깨므로 조립기의 loss/IDR 복구와 연결한다.
- **검증:** decode 지연 주입·burst·재접속 중 control 유지, allocations/s·lock p99·큐 oldest age·로컬 drop과 네트워크 loss 분리.

### O11/O12 — NACK의 복사와 시간 예산

- **O11 관측:** `host_encoded_sender.cpp:30-90`의 StoreAu는 전체 payload를 복사하고, RetransmitAu도 캐시 lock 안에서 전체 AU를 다시 복사한 뒤 **재전송 byte budget을 검사**한다. `host_encoded_sender.hpp:96` 상한은 24 AU다. 이미 재전송 토큰 버킷이 있으므로 무제한 송신이라고 평가하지 않는다.
- **O11 개선:** immutable AU 공유 소유권 또는 pool, 전체 bytes+나이 상한, 거부될 요청은 대형 복사 전에 필터. 캐시 참조는 lock 밖 송신 때도 유효해야 한다. 작은 missing chunk 한 개 요청·반복 거부 요청에서 copy bytes와 lock 시간을 측정한다.
- **O12 관측:** `udp_video_nack.hpp:31-35` 기본값은 gap 25ms / tail 120ms / round 25ms / 최대 3회. `viewer_context.hpp:38` hold 120ms, Windows `viewer_video_receiver.cpp:67-74`에서 협상 후 적용. Android 공유 세션도 NACK 송신은 있으나 같은 in-order hold 배선은 아니다(`native_video_client_session.cpp:669,922`).
- **O12 개선:** 고정 대기 대신 smoothed RTT·AU 전송 완료 예상·남은 표시 예산을 반영하되 최소/최대값을 둔다. 아직 오는 tail을 loss로 오인하지 않고, 이미 포기한 AU가 늦은 패킷으로 부활하지 않는 기존 tombstone/saturation 정책을 보존한다.
- **검증:** RTT 1~100ms, 0.1/1/3% loss, burst loss, 큰 IDR, 마지막 조각 지연, 정적 화면. 복구 성공률뿐 아니라 표시 지연과 추가 bytes를 평가한다. **NACK 미구현이라는 과거 항목은 현재 사실과 다르다.**

## 4. Host 캡처·인코딩·썸네일

### O06 — 격리는 됐지만 thumbnail 응답은 여전히 동기

- **관측:** `host_control_session.cpp:168-243`의 Serve 안에서 `capture_thumbnail_isolated`가 끝날 때까지 기다린다. `host_thumbnail_helper.cpp:253`은 요청마다 process를 만들고, wait/teardown 뒤 반환한다. 프로세스 전체 cap 1, Busy/Canceled 무과금, 3회/2초/60초 정책은 이미 구현돼 있다. `viewer_control_client.cpp:560`도 idle action에서 한 썸네일씩 동기 요청한다.
- **개선:** 제어 응답은 캐시/‘아직 없음’으로 빠르게 반환하고, 별도 유계 작업이 isolated helper를 실행해 캐시를 채우는 구조를 검토한다. 반복 캡처는 짧은 TTL·현재 picker·보이는 카드 중심으로 제한한다. 정상 helper 재사용은 spawn 비용 실측 뒤 선택하고, 정체 helper를 폐기할 수 있는 경계는 유지한다.
- **주의:** timeout 1초는 모든 teardown까지 항상 1초라는 뜻이 아니다. kill 확인·lingering 경로를 포함해야 한다. 캐시는 HWND/PID/생성시각/크기/세대와 수명 제한 필요. CPU 스레드로 PrintWindow를 되돌리면 안 된다.
- **검증:** 창 1/10/30개, 강제 정체 helper, TCP+UDP 동시 요청 중 Ping·입력 p99, 첫 목록·건강한 카드 표시 시간, 자식 상한·종료·업데이트 3단 소유권 회귀.

### O07 — GPU surface 경로의 readback

- **관측:** `d3d_capture_readback.cpp:570-593`은 NV12 surface 생성에 성공해도 BGRA staging copy와 query/Flush 경로를 지나고, `:783-824`에서 BGRA payload를 얻는다. `host_stage_encode_send_h264.cpp:194,294`가 이후 surface 입력을 택한다. 따라서 인코더 입력이 surface라는 사실만으로 전체 capture 경로가 zero-readback인 것은 아니다.
- **활성 조건:** `host_capture_session.cpp:204-210`에서 HW encoder와 `REMOTE60_NATIVE_NV12_SURFACE` opt-in이 모두 있어야 켜진다. **기본 제품 경로가 이미 surface encode라고 가정하면 안 된다.** O07/O08은 이 선택 경로의 안전성·성능을 확보한 뒤 적용할 후보이며, 현재 기본 경로의 이중 NV12 변환을 확정한 것이 아니다.
- **개선:** surface-only 프레임 표현을 분리하고 readback 없이 publish 가능한 경로를 설계한다. BGRA 의존 static gate·bootstrap·fallback을 추적해 필요할 때만 CPU payload를 만든다. GPU lease와 encode 완료·epoch 전환의 소유권이 핵심이다.
- **규모 감각:** BGRA 1080p는 8,294,400B, 60fps에서 한 번의 프레임 복사만 약 **498MB/s**, 4K60은 약 **1.99GB/s**의 논리 바이트다. 실측 GPU/메모리 트래픽이나 절감 보장값이 아니다.
- **검증:** readback bytes/frame, context wait·Map·memcpy p95, GPU Copy/Video Encode 시간, AMD/Intel/NVIDIA별 장시간 lease 고갈·device loss·CPU fallback. ‘GPU니까 더 빠르다’는 가정으로 기본값을 바꾸지 않는다.

### O08 — surface 경로 판정

- **관측:** `host_stage_encode_send_h264.cpp:315-333`은 30개 입력의 `encodeCallUs` 평균이 16ms 초과면 surface를 끈다. 상수는 고정이고 측정은 호출 시간 중심이다. 소스 주석의 68ms/4.5ms는 과거 사례이지 이번 실측이 아니다.
- **개선:** 현재 frame budget의 일부와 p95/tail, 입력 수락→AU 출력 지연, readback까지 포함한 전체 비용으로 판정한다. GPU/driver/backend별 기록과 세션 내 유계 재평가를 검토하되 장애 반복 probe는 막는다.
- **검증:** 30/60fps, 시작 직후 warmup, GPU 경쟁 부하, 일시 느림/지속 느림을 구분해 선택 오판과 전환으로 인한 공백을 측정한다.

### O09 — 정적 화면 판정을 캡처 앞단으로

- **관측:** `host_stage_gate_static.cpp:103-107`의 change 샘플링은 CPU payload를 받은 뒤 수행된다. DXGI backend는 AcquireNextFrame을 쓰지만 해당 소스에서 dirty/move rect 메타데이터 활용은 찾지 못했다.
- **개선:** DXGI 변경 메타데이터를 힌트로 사용해 불필요한 full readback·색 변환을 줄일 수 있는지 검토한다. WGC/GDI는 별도 정책이 필요하다. 커서·작은 글자·crop 이동·최종 프레임·복구 IDR를 놓치지 않아야 한다.
- **검증:** 정지 문서, 커서만 이동, 한 글자 수정, 스크롤, 창 이동, 다중 모니터/DPI. CPU 절감과 첫 변화 표시 지연을 함께 확인한다.

### O16 — Host 로그 역압력

- **관측:** `host_app_main.cpp:402-414`는 자식 stdout 파이프를 읽고 행별 AppendLogLine을 호출한다. `host_app_log.cpp:56-84`는 mutex 아래 fputs+fflush. 파일 핸들 재사용·회전 동기화는 이미 개선됐다.
- **개선:** 유계 로그 큐와 짧은 배치 flush, 장애·종료 핵심 행 즉시 보존, 중복 통계의 rate-limit을 검토한다. 디스크가 느리면 소비자가 파이프를 충분히 비우지 못해 자식 로그 쓰기에 영향을 줄 수 있으나 현재 발생 여부는 미측정이다.
- **검증:** 로그 폭주+느린 디스크 fixture에서 pipe drain·frame gap·drop count·회전 무결성. 관측 비용을 줄인다는 이유로 원인 추적 로그를 없애지 않는다.

### O19 — 송신 임시 버퍼

- **관측:** `host_net_io.cpp:122,191,254`는 AU 송신/재전송 때 datagram·parity vector를 생성한다.
- **개선:** 송신 스레드 소유 scratch 재사용 또는 고정 크기 버퍼를 검토한다. 두 송신 경로가 공유하는 전역 mutable 버퍼는 금지. FEC 내용 초기화는 반드시 유지.
- **검증:** allocations/s·송신 CPU와 tail 개선이 실제인지 확인. 기본 MTU의 작은 할당이라 O01보다 우선할 이유는 아직 없다.

## 5. PC Viewer / GNLinkClient

### O10 — 매 present마다 두 SRV 생성

- **관측:** `viewer_nv12_renderer.hpp:482-534`의 `render_surface`는 매 호출 local ySrv/uvSrv를 만들며 `CreateShaderResourceView`를 두 번 수행한다. `viewer_present.cpp`에서 surface 경로가 호출된다. 이미 GPU decode surface를 직접 표시하는 경로가 있으므로 ‘PC가 전부 CPU 복사’라는 평가는 틀리다.
- **개선:** device generation·texture COM identity·subresource별 유계 SRV 캐시. decoder 재구성/device loss에서 비우고 texture pool 수명과 참조 회수를 검증한다.
- **검증:** SRV 생성 횟수, CPU present p95, GPU memory·COM 참조 누적, resize/stream generation/crop/device reset. 60fps일 때 호출당 2개면 초당 약 120개 생성이라는 계산이며 실제 present 빈도는 별도 측정한다.

### O17 — shell 작업 합치기

- **관측:** `client_shell_main.cpp:856-875`의 refresh는 호출마다 `gWorkers.Launch`; `async_worker_group.hpp:23-42`는 종료 worker를 회수하지만 활성 작업 수 상한은 없다. ownerEpoch는 다른 로그인 결과를 막지만 같은 epoch 안의 중복 refresh를 합치는 장치는 이 함수에 없다. UI busy가 일부 호출을 제한하므로 평상시 무한 증가라고 단정하지 않는다.
- **개선:** 요청 종류별 single-flight, 마지막 refresh 한 번만 예약, login/connect/종료의 우선순위와 취소를 유지. 같은 epoch 내 오래된 결과가 새 결과를 덮지 않도록 refresh serial도 고려한다.
- **검증:** 늦은 서버+반복 클릭/브리지 요청에서 활성 worker 수·중복 HTTP 수·UI 응답·결과 순서.

### O20 — 제어 idle wakeup

- **관측:** `viewer_control_client.cpp:565`는 할 일이 없으면 `WaitForInput(2)`; 입력 큐 CV는 있지만 scheduler의 다음 시각 대신 고정 2ms로 깬다. 최대 약 500회/초는 이상적 polling 빈도 계산이며 실제 측정치가 아니다.
- **개선:** 입력뿐 아니라 Ping·tune·selection·종료가 wake를 발생시키고 다음 deadline까지만 기다리도록 한다. 단순히 2ms를 100ms로 늘리면 조작 지연이 증가한다.
- **검증:** idle CPU·wakeups/s, 최초 입력/ping 지연, lost wakeup, 종료·재접속.

### O21 — HTTP 핸들 수명

- **관측:** `winhttp_transport.cpp:78-99`는 교환마다 session/connect/request를 생성한다. 요청 deadline과 TLS posture는 이미 있다. 매 요청 반드시 새 TCP/TLS handshake가 발생한다고 단정하지 않는다.
- **개선:** origin별 session/connect 재사용 가능성을 측정한다. request 단위 timeout, proxy 변경, 계정 전환, 쿠키·토큰 경계와 취소 수명을 유지한다.
- **검증:** 로그인→목록·heartbeat·업로드의 cold/warm latency, 실제 TCP/TLS 연결 수, 핸들 수, 401/account switch·서버 변경 회귀.

### O23 — shell DOM

- **관측:** `ui/shell.html:316-346`는 renderHosts마다 전체 innerHTML 삭제 후 모든 버튼을 만들고 연결 상태 변경 때도 다시 렌더한다. 고주기 영상 경로가 아니며 소수 호스트에서는 비용이 작을 수 있다.
- **개선:** hostId 기반 노드 재사용·변경 상태만 갱신. 리스트 가상화는 수백 개에서 프로파일로 필요성이 확인될 때만.
- **검증:** 1/20/200개 host fixture, 렌더 시간·포커스·스크롤·접속 버튼 중복 실행.

## 6. Android GNLink 앱

### O13 — 디코더 출력 펌프

- **관측:** `android_video_decoder.cpp:125-135`는 3ms sleep마다 mutex를 잡고 `DrainOutputLocked`; `:794`는 timeout 0으로 output 조회한다. 예약 표시를 지키기 위한 구조이며 이 대기 자체가 영상 오류라는 뜻은 아니다.
- **개선:** codec output 알림과 보류된 프레임의 다음 release deadline을 결합한다. idle/정지 상태에서는 길게 자고 새 출력·Surface 교체·종료 때 깨운다. NDK 비동기 callback 사용 가능 버전·vendor 동작을 확인하고 현재 동기 경로와 혼용하지 않는다.
- **검증:** 화면 정지/영상 재생 각 wakeups·CPU·소비 전력/발열, present gap p95/p99, 마지막 프레임 drain, Surface 재생성/백그라운드 복귀.

### O14 — 상태 조회의 중복 직렬화

- **관측:** `MainActivity.kt:889-896`은 이미 화면별 250/500/750/1000ms polling이다. ‘항상 250ms’라는 주석은 현행 코드와 다르다. 그래도 `:3054-3072`에서 상태·오류·전체 창목록 JSON·video debug·generation 등을 각 JNI로 읽는다. `native_bridge.cpp:666-668`에서 snapshot 복사→JSON 생성, Java에서 재파싱한다.
- **개선:** 단일 versioned snapshot, 변경 없는 창목록은 재전달/파싱 생략, debug는 진단 UI가 필요할 때만. 알림 기반 전환은 lifecycle·스레드 경계를 설계한 뒤 적용한다.
- **검증:** JNI 호출/복사 bytes·UI frame time·GC·idle CPU, selection timeout·clipboard·복구 상태 표시 누락 여부. 이미 된 가변 polling/adapter 변경 감지는 보존한다.

### O15 — 썸네일 복사·할당

- **관측:** `native_bridge.cpp:681-694`: native thumbnail copy→Java ByteArray. `MainActivity.kt:647-665`: 새 Bitmap·direct ByteBuffer를 만들고 pixels를 복사한다. **버전이 변한 썸네일에만** 수행되는 guard가 이미 있어 매 tick 모든 썸네일을 복사한다고 말할 수 없다.
- **개선:** 동일 크기 Bitmap/direct buffer 재사용, 변경된/보이는 카드만 처리, 큰 목록은 UI 밖 변환 후 generation을 확인해 게시. 소유 중인 Bitmap을 화면 사용 중 recycle하지 않는다. 과거 wrapped-array crash 회피 계약은 유지한다.
- **검증:** 창 5/30/100개·빠른 변화·스크롤에서 GC/allocated bytes·main-thread p95, native/Java lifetime·색상·크기 검증.

### O22 — 로컬 로그 writer

- **관측:** `SessionDiagnosticsLog.kt:30-45`는 매 로그를 single-thread executor에 넣고 mkdir/appendText/rotation을 수행한다. 실행 대기열에 명시적 byte/count 상한이나 종료 메서드가 없다. 이는 `LogUploader.kt`의 별도 512KiB 제한 큐와 다르다.
- **개선:** 배치 writer, pending 상한·drop 집계, 파일 핸들 수명 관리, Activity 파괴 시 배출/종료 계약. 진단 읽기 작업과 쓰기 순서는 보존한다.
- **검증:** 느린 저장장치·로그 폭주·화면 회전/Activity 재생성에서 pending heap·thread 수·UI 응답·로그 손실량. 정상 사용에서 누수가 발생했다는 실측 주장은 아니다.

## 7. 서버 경로

### O05 — 로그 저장과 relay의 이벤트 루프 공유

- **관측:** `server.js:417-422` 로그 POST는 mkdirSync·rotate·appendFileSync를 호출하고, `:359-382` 보존기간 sweep도 재귀 동기 I/O다. UDP relay `:1228-1240,1322-1348`가 같은 Node 프로세스에 있다. 로그 폭주·NAS 디스크 지연 때 네트워크 callback을 늦출 수 있는 구조다.
- **개선:** per-file 순서를 보장하는 유계 비동기 쓰기 큐, 비동기 rotation, sweep을 작은 배치로 나누거나 별도 작업자로 이동. HTTP 성공 응답 시점과 쓰기 실패 반환 계약을 명시한다. 단순히 fs.promises로 치환하면 같은 파일 회전/append 순서가 깨질 수 있다.
- **검증:** relay 스트림과 최대 허용 로그 업로드·rotation 동시 실행, event-loop-delay p95/p99·relay packet gap·queue bytes·429/500 응답. 현재 NAS가 이 코드와 같다는 주장은 하지 않는다.

### O18 — store 전체 동기 저장

- **관측:** `server.js:564-591`는 200ms debounce 또는 즉시 저장으로 전체 store JSON.stringify/writeFileSync/renameSync. 요청 경로 인증 KDF는 이미 async이며 동시에 최대 4개(`:602-614`)다. 남아 있는 scryptSync 정의만 보고 ‘로그인이 동기 KDF’라고 판정하면 잘못이다.
- **개선:** 단일 writer가 versioned snapshot을 순서대로 기록하고, 자격 발급 요청은 해당 버전의 저장 성공을 기다린 뒤 성공을 반환한다. 느린 디스크에서 무한 pending이 되지 않도록 제한한다. 계정/host 규모가 커지면 transactional storage를 별도 검토.
- **검증:** 동시 register/login/heartbeat+디스크 오류+재시작에서 token 내구성·503/rollback 보존, event-loop delay. 저장 전에 200을 반환해 속도만 높이는 수정은 금지.
- **배포 구분:** 기존 작업목록 D1은 서버 `27d9e74` 미배포라고 기록한다. 이번에는 라이브 파일 대조를 안 했으므로 ‘여전히 미배포 확정’이 아니라 **배포 전 재확인 필요**로 남긴다.

## 8. 이미 있는 최적화 — 중복 구현 금지

| 기능 | 현행 코드 근거 | 이번 판단 |
|---|---|---|
| GPU readback buffer pool·대기 backoff·memcpy lock 축소 | `d3d_capture_readback.cpp:64,662,805` | 전체 프레임 memcpy를 context lock 밖으로 옮기는 제안은 이미 반영됨 |
| CPU BGRA→NV12 luma SSE2 | `mf_h264_codec.cpp:992` | ‘색 변환 전부 scalar’ 아님. 추가 SIMD는 profile 뒤 |
| 저지연 MFT 설정 | `mf_h264_codec.cpp:1419-1422` | low-latency flag 추가만으로 해결한다고 제안하지 않음 |
| 송신 페이싱용 재사용 high-resolution timer | `host_net_io.cpp:60-89` | pure busy-yield 제거는 이미 반영; 짧은 tail은 남음 |
| 미디어 queue 상한·IDR/epoch 보호 | `host_sender_queue_policy.hpp`, `host_encoded_sender.cpp` | 무작정 latest P-frame만 보내면 참조 깨짐 |
| PC 수신/제어 분리 | `viewer_udp_ingress.hpp:23-25` | decode 중 제어 유지 의도를 보존 |
| PC GPU decode surface present | `viewer_video_receiver_frame.cpp:277-299`, `viewer_present.cpp` | CPU NV12 업로드는 fallback 경로로 구분 |
| 썸네일 process 격리·cooldown·Busy 무과금 | `host_thumbnail_helper.cpp`, `host_thumbnail_budget.hpp` | 응답 없는 창 무한 PrintWindow 직접 호출은 구버전 문제 |
| Android 화면별 polling·변경 시 adapter 갱신 | `MainActivity.kt:889,3255` | 이번 후보는 그 이후의 JNI/할당 비용 |
| 서버 요청 KDF async·동시 상한 | `server.js:602-614` | 코드와 라이브 배포 상태는 별개 |
| Windows NACK/조립 hold·포기 AU 재수락 방지 | `viewer_video_receiver.cpp`, 공유 assembler | 기존 C6를 새 미구현 목록처럼 취급하지 않음 |

## 9. O24 — 실행 전에 필요한 성능 기준선

기존 `automation/summarize_pipeline_latency.py`, `summarize_wan_capture.ps1`, `verify_native_video_runtime.ps1`, `soak_native_video_reconnect.ps1`의 지원 입력을 먼저 확인해 재사용한다. ‘스크립트가 있다’는 것과 ‘이번 측정을 실행했다’는 것을 구분한다.

1. **출처 고정:** 각 PC/폰 모델·GPU/driver·모니터 Hz/HDR·전원모드·콘솔/RDP·source commit·실행 바이너리 hash·설정·선택 endpoint·버전을 기록한다. raw/TCP 결과를 제품 H264/UDP 결과로 대신하지 않는다.
2. **행동:** 연결→창목록→선택→첫 화면→30초 정적→스크롤/타이핑→고화질 영상→대역 하락→복귀→재접속. 로컬 fixture와 실제 WAN은 별도 결과로 남긴다.
3. **단계 지표:** capture 공급 FPS, callback→encode, input→AU, 송신 queue age·wire 간격, ingress oldest age, 조립 hold, decode, publish→실제 present. 모든 단계에 seq+stream generation을 연결한다.
4. **자원:** 프로세스/스레드 CPU, wakeups, GPU engine별 시간, logical copy bytes, allocation rate/heap, Android GC·전력, server event-loop delay·디스크 지연.
5. **지연 해석:** 서로 다른 PC의 QPC를 그냥 빼지 않는다. offset 추정과 오차를 명시하거나 동일 기기 단계별 시간/seq 비교로 제한한다. 평균만으로 정지를 숨기지 않고 p50/p95/p99와 최대 gap·freeze 횟수를 함께 본다.
6. **A/B:** 같은 장면·설정·기간·환경으로 고정 횟수 실행, 개선 전/후 순서를 번갈아 배치. 실패도 보존하며 PASS까지 반복하지 않는다. CPU 감소와 품질/지연 악화를 맞바꾼 것은 별도로 기록한다.

### 작업 순서 제안

- **1차:** O24 기준선 + 진행 중 C1/O02 결과 인수. O01은 대역/페이싱 설계, O03은 입력 프로토콜 설계를 준비한다. 이 둘은 병목 축이 다르다.
- **작은 실험:** O10 SRV 캐시, O17 refresh 합치기, O05 서버 로그 배치의 측정 가능한 프로토타입을 각각 분리한다. 한 릴리스에 전부 넣지 않는다.
- **중기:** O04 ingress·O11/O12 복구 예산, O13~O15 Android 비용을 해당 기기 실측으로 선택한다.
- **큰 변경:** O07/O09는 GPU lease·fallback·최종 화면 보존 시험이 마련된 뒤. O06 비동기 썸네일은 제어 응답 지연이 실제 문제일 때 프로토콜/캐시 계약부터 정한다.

## 10. 한계와 참고

- 09-15 GPU 타임아웃·LDPlayer 관련 사건이나 17:15 영상 끊김의 근본 원인을 이번 정적 분석으로 새로 확정하지 않았다. 과거 ‘연관’ 자료를 현행 0.2.134의 재현 증거로 바꾸지 않는다.
- 보안·인증·UAC·업데이터 정합성은 성능을 위해 약화할 대상이 아니다. 업데이트는 영상 hot path와 분리해 기존 작업목록을 유지한다. CMake/automation은 검증 지원 범위를 살폈으며 이번 목적은 빌드 시간 최적화가 아니다.
- 참고 API 근거(2026-09-22 조회): [Node 동기 파일 API는 이벤트 루프를 차단](https://nodejs.org/api/fs.html), [D3D11 immediate context 동시 사용 제약](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-intro), [Android NDK MediaCodec API](https://developer.android.com/ndk/reference/group/media). 이 문서는 해당 API 특성만 참고했으며 제안의 실제 속도 향상을 보증하는 자료로 쓰지 않았다.

**수행된 작업:** 최신 main 고정 소스 조사·전체 인벤토리·24개 개선/계측 후보 정리. 제품 구현과 성능 실측은 미수행이며, 다음 단계는 우선 후보의 기준선 수집이다.
