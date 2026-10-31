# 2026-09-12 게임 원격 멈춤·반복 드래그·프레임·화질 저하 조사

후속 [코드 조사](incident_2026-09-12_code_review.md): 버튼 해제 누락, 입력 직렬 대기, 영상/ACK 수신 결합, 표시 기반 혼잡 추정과 프레임 병합을 추적했다. 최초 장애 원인과 지연 증폭 구조를 구분한다.

## 상태와 조사 범위

- 사용자 보고: 14:40:44 부근 게임 원격 멈춤, 반복 드래그 불량, 이후 심한 지연·화질 저하·60fps 대비 낮은 프레임.
- 조사: Codex가 NAS의 Host/Client/Viewer 로그와 저장소 코드를 직접 대조. 관측 범위는 2026-09-12 14:36~14:50 부근(KST). 사용자 추가 보고의 14:50 호전도 포함.
- **원인 조사 기록이며 수정·재현 시험·해결 완료 보고가 아니다.** 제품 변경, 사용자 세션 조작, 설치, 재시작, 배포, push 없음.
- 코드 대조 기준 HEAD: `8bc9d1c`. 조사 당시 별도 작업의 제품 파일 변경이 있어 해당 변경은 수정하거나 기록 범위에 포함하지 않았다.
- 사건 당시 각 기기의 실행 파일 버전·해시는 확보하지 못했다. 저장소 코드와 실행 바이너리가 같다고 보증하지 않는다. NAS에 게시된 버전이나 다른 기기의 설정으로 이를 대체하지 않는다.

## 출처와 기기 구분

로그 서버는 `192.168.0.6`, 기본 로그 루트는 `/opt/gnlink/remote60-directory/logs/shotan/`이다.
저장소 `CLAUDE.md:249-250`, `automation/gnlink_deploy.sh:43-44`의 설정을 확인해 다음 읽기 접속에 성공했다.

```powershell
ssh -i "$env:USERPROFILE/.ssh/remote60_deploy" gnlink@192.168.0.6
```

문서에 있던 `claude` 계정은 인증 거부였다. 개인키 내용·비밀번호는 수집하거나 문서화하지 않았다.

| 구분 | NAS 기기 디렉터리 | 세션 식별 / 로컬 스냅샷 |
|---|---|---|
| Host A: 14:40:44 멈춤 | `51bd6657-9b5b-41d0-8385-6557d42c3990/host.log` | client host ID `df15b73b18cda3b1`; `host.log` |
| Host B: 이후 느림·화질 저하 | `8ec6ecb1-11fb-4db3-8127-aea0c5311225/host.log` | client host ID `f6e8e54a7d5aff7c`; `host-current.log` |
| Client / Viewer | `68f79d01-d6c4-4a4a-a3ae-69da5be54826/{client,viewer}.log` | `client.log`, `viewer.log`, `viewer-current.log` |

로컬 원본 보관 위치: `.claude/investigate-0912-1440/` (Git 미추적). `*-current.log`는 후속 조사 때 받은 스냅샷이며 실시간 파일이라는 뜻이 아니다. 파일 SHA-256은 [근거 파일 목록](incident_2026-09-12_evidence_sha256.txt)에 고정했다. NAS 로그는 계속 증가·회전하므로 아래 줄 번호는 **로컬 스냅샷 기준**이다.

로그 시간은 동기화된 사건 시각이 아니다. 예를 들어 Host A의 `seq=7503` 송신은 14:40:44.011, Viewer 표시는 14:40:43.908이다. 후속 코드 조사에서 Viewer의 접두 시각은 부모 pipe reader가 기록할 때 붙고 동시 viewer가 같은 로그에 합쳐짐을 확인했다. 따라서 시계 차이뿐 아니라 수집 대기도 고려한다. 시간 문자열만 빼서 음수 지연을 계산하지 않고 seq·generation·세션과 함께 대조한다. 서로 다른 초별 통계창의 송신/수신 수치를 정확한 동일 프레임 집합으로 간주하지 않는다. 동시 세션이 겹친 구간은 session ID 추가 전 attribution의 한계가 있다.

## 판단 요약

| 문제 | 확인된 사실 / 직접 원인 | 미확정 경계 |
|---|---|---|
| 멈춤 | Host A 캡처 워커가 Release 단계에서 5.05초 정체. 감시기가 exit 44 종료를 선언하고 호스트 재기동 기록이 이어짐 | DXGI 호출 내부 정체를 유발한 드라이버·GPU·동시 접근 원인 |
| 반복 드래그 | 현재 코드에 영상 밖 버튼 해제 시 UP 전송·눌림 해제를 건너뛰는 경로 존재 | 해당 사건에서 이 경로를 밟았는지, 게임이 입력을 어떻게 소비했는지 |
| 60fps 대비 낮은 표시 | Host B에서 60fps 적용 확인. 활발한 송신 중 수신 조립 실패·복구·프레임 건너뛰기·표시 병합 관측 | 최초 손실이 네트워크인지 수신 프로세스 정체인지 |
| 화질 저하 | ABR이 severe 판단으로 해상도·비트레이트를 실제 하향 | ABR의 입력 지표별 기여도와 최적 정책 |
| 뒤이은 낮은 캡처 빈도 | Host B DXGI 새 화면 취득이 초당 2~12회로 감소 | 정상 정지 화면인지 게임 렌더링/캡처 공급 저하인지 |

**Host A의 캡처 정체와 Host B의 후속 저하를 동일 장애의 연속으로 확정하지 않는다.**

## 1. 14:40:44 부근 멈춤 — 캡처 워커 정체 확인

근거: `host.log:73818-73832`, `viewer.log:72301-72337`.

- 14:40:42.158부터 `trailing-edge kick`: 새 화면 대신 마지막 캡처 화면을 재사용.
- 14:40:44.007 `ageUs=2020154`, 14:40:46.030 `ageUs=4042424`: 재사용 중인 원본 화면의 나이가 증가.
- 그 사이 타임스탬프 없는 감시기 줄: `dxgi-worker slow phase=release phaseAgeUs=3000867`.
- 이어서 `dxgi-worker-wedge phase=release phaseAgeUs=5052670 ... loopCount=15782 ... terminating (exit 44) for supervisor relaunch`.
- 14:40:47.485~47.486 호스트 startup/listener 로그, Client 14:40:47 세션 시작 기록.
- Viewer는 해당 기간 synthetic frame을 수신·표시. 제어 RTT 약 4~5ms, `d3dPresentFail=0`, 해당 조립 통계 `dropped=0`.

코드 대조: `libs/capture/src/capture_backend_dxgi.cpp:460-470`은 Release 단계 진입 직후 `duplication->ReleaseFrame()`을 호출하고, 반환 뒤 진행 시계를 갱신한다. 로그는 이 호출 경계에서 진행이 멈춘 것을 강하게 뒷받침한다. `releaseHr=0`은 마지막 저장값이며 **정체된 호출이 성공해 반환했다는 뜻이 아니다.**

결론: 캡처 워커 정체 → 이전 화면 반복 송신 → 감시기 종료·재기동. 이후 게임 조작까지 정상 복구됐는지는 확인하지 않았다. 드라이버 내부 원인은 덤프/스레드 스택 없이 단정하지 않는다.

## 2. 반복 드래그 — 버튼 해제 누락 코드 경로, 사건 연결은 미확정

코드 대조:

- `apps/native_poc/src/viewer_window_proc.cpp:386-427`: `WM_LBUTTONUP`에서 좌표 변환 실패 시 즉시 반환한다. `mouseButtons` 해제, 원격 UP 전송, `ReleaseCapture()` 처리는 그 뒤에 있다. 패널 영역에서도 앞서 반환하는 경로가 있다.
- `apps/native_poc/src/viewer_layout.cpp:102-115`: 영상 contentRect 밖이면 좌표 변환을 거부한다.
- `viewer_window_proc.cpp:537` 이후: capture 변경/취소 시 별도 버튼 해제 경로가 있다. 따라서 눌림이 영구적으로 남는다고 단정하지 않는다.

가능한 동작: 영상 안에서 DOWN → 밖까지 드래그 → 밖에서 UP → 해제가 전송되지 않음 → 캡처 상실 등 정리 계기 전까지 원격에 눌림 상태가 남을 수 있음. 이후 누르지 않은 이동이나 다음 드래그가 의도와 달라질 수 있다. **정적 코드 경로 확인이며 실제 UI 재현은 하지 않았다.**

사건 로그에서는 14:38:46~50 드래그 구간 moveGen 약 33~44/초, moveSent 약 23~32/초, coalescing 존재, `droppedTotal=0`, 이동 ACK RTT 평균 약 3~4ms였다. 확인한 Host A 이동 통계에 `moveInjectFail=0`이며, 샘플 입력은 `mode=desktop`, 대상 게임은 `eclipsetheawakening-win64-shipping.exe`였다. 누적 `inputInjectFail=11`은 과거 값도 포함하므로 이번 드래그 실패 11회로 해석하지 않는다.

입력 로그는 모든 DOWN/UP을 남기지 않는다. `SetCursorPos`/주입 성공은 게임이 드래그를 처리했다는 증거가 아니다. 손실 없음·주입 성공만으로 드래그 정상이라고 판정할 수 없다.

## 3. 느림·프레임 저하·화질 저하 — Host B

### 설정과 시간선

| 시각(각 기기 로그 시계) | 근거 |
|---|---|
| 13:33 이후 여러 접속 | Client 세션 시작값이 `kbps=8000 fps=30`. 왜 UI 선택과 달랐는지는 조사하지 못함 |
| 14:40:56 | Host B로 접속 변경. Host A 장애와 기기 구분 필요 |
| 14:41:15 이후 | 일부 UDP 조립 통계에 dropped 발생. 이 스냅샷에서 확인한 시점이며 최초 장애 시각 확정 아님 |
| 14:44:22 | Viewer `reason=decode_queue`, 지연 추정 400623us로 congested 진입 |
| 14:44:24 | 복구 지연 추정 1.93~2.67초. 실제 디코더 함수 실행시간과 구분 |
| 14:44:44 / 49 | ABR이 1080p·6Mbps, 이후 720p·4.4Mbps로 하향 |
| 14:44:56~14:45:00 | Client 60fps·12Mbps 접속, Host B `runtime-config-applied ... bitrate=12000000 fps=60 abrOverride=0` |
| 14:45:04.522 | ABR `high_to_mid_severe`, 1080p·9Mbps |
| 14:45:16.663 | ABR `mid_to_low_severe`, 720p·6.6Mbps |
| 14:46~47 | 낮은 화질 유지, 새 캡처 공급도 감소. Viewer 표시 간격 수백 ms 기록 |

60fps 적용 근거: `host-current.log:38237`. 재하향 근거: `host-current.log:38635`. **현재 60fps 미적용이라는 진단은 틀리다.** 30fps로 시작했던 앞선 세션과 60fps가 적용된 후속 세션을 구분한다.

### 활발한 송신 중 수신·표시 감소

14:45:07 부근 독립적인 초별 통계:

- Host B (`host-current.log:38438`): `callbackFrames=61 encodedFrames=60 sentFrames=58 fpsTarget=60`, 인코딩/송신 큐가 장시간 멈췄다는 근거 없음.
- Viewer (`viewer-current.log:75679`): `completed=38 dropped=14 dropPm=269`, NACK 요청과 FEC 복구, 키프레임 요청/재동기 발생.
- Viewer (`viewer-current.log:75681`): `recvFrames=38 decodedFrames=19 skippedQueued=19 d3dPresentSuccess=9 d3dPresentFail=0`, overwrite와 paint coalescing도 발생.
- 인접한 14:45:04~08 조립 통계 `dropPm=191~300`: **조립기 프레임 드롭 지표 19.1~30%**이며 네트워크 UDP 패킷 손실률 측정값이 아니다.
- 제어 RTT는 14:45:07 약 517ms, 14:47:05 약 600ms까지 증가했다. 다른 샘플은 약 10ms였다. 왕복값은 전송 경로와 양쪽 처리 지연을 포함하므로 회선 지연만으로 해석하지 않는다.

결론: 수신 프레임 조립 실패와 복구, 뒤처진 프레임 건너뛰기, 표시 병합이 실제 표시 빈도를 낮춘다. `decode_queue`라는 이름만으로 디코더 연산 성능 부족이라고 확정할 수 없고, `d3dPresentFail=0`도 충분한 표시 FPS를 보장하지 않는다. 최초 손실 위치를 판별하려면 송수신 패킷/소켓 드롭과 수신 스레드 정체 계측이 필요하다.

### 화질 하향의 직접 원인

ABR severe 전이가 로그에 명시되어 있다. `apps/native_poc/src/host_abr.hpp:176-198,229-255`는 프레임 손실·낮은 디코딩 FPS·지연을 판단 근거로 사용한다. 목표 60fps를 유지한 채 해상도와 비트레이트가 내려간 것이다. 설정 12Mbps는 전송량의 고정 상한이 아니며 실제 순간 비트레이트는 별도다.

현재 코드의 일반 혼잡 판단에서는 공급이 희박한 초의 상향/하향 판단을 보류한다. 이것만으로 모든 복구 경로가 막힌다고 해석하면 안 된다. 실제 후속 로그에는 별도 `static_recovery` 전이도 있다(아래 참조). 실행 바이너리 일치 검증은 남아 있다. 강제 고화질만으로 수신 손실이 해결된다는 근거는 없다.

### 후반부 새 화면 공급 저하

14:46~47 Host B `dxgi-acquire`는 초당 2~12회, timeouts 약 85~90회였다. 예: `host-current.log:39840`은 content 2회, timeout 90회. 호출은 계속 돌아오고 있어 Host A의 Release 정체와 다른 양상이다. 이 시점 낮은 FPS를 전부 네트워크 탓으로 돌릴 수 없다. 정지 화면, 게임 비활성/백그라운드 제한, 캡처 공급 저하 중 무엇인지는 동시 게임 FPS·화면 움직임·포커스 증거가 없다.

## 4. 사용자 추가 보고: 14:50 호전, 반응 지연은 남음

14:51 부근 NAS에서 추가 수집한 `host-recovery.log`, `viewer-recovery.log`를 별도 보존했다. 최초 스냅샷은 덮어쓰지 않았다.

- NAS Client 최신 조회: 14:47:58 앱 로그 업로드 시작, 14:48:01 Host B에 60fps·12Mbps로 새 세션 시작. 업데이트 알림은 있었지만 설치 완료 증거는 아니다.
- Host B 14:48:02.513 `profile=mid encode=1280x720 bitrate=9000000 reason=static_recovery`.
- Host B 14:48:07.643 `runtime-config-applied ... bitrate=12000000 fps=60 abrOverride=0`.
- Viewer 14:50:35.878: **1920x1080, recvFrames=60, decodedFrames=60, skippedQueued=0, d3dPresentSuccess=40**, overwriteBeforePresent=21.
- Viewer 14:50:36.860: **recvFrames=59, decodedFrames=59, d3dPresentSuccess=42**, overwriteBeforePresent=19.
- 해당 두 초 조립 통계 **dropped=0, nackSent=0**. 14:50:37에는 dropped=1로 완전 무손실 지속을 주장하지 않는다.
- Host B 14:50:37.514: encodedFrames=59, sentFrames=59, callbackFrames=60.
- Viewer 14:50:57~59 제어 RTT는 **1502~1996us**. 이 구간 moveGen=0이므로 마우스 이동 ACK나 클릭부터 화면 반응까지의 지연 측정으로 대체할 수 없다.

판정: 사용자 체감 호전은 수신/디코딩 빈도·해상도·제어 응답 개선과 부합한다. **그 전에 새 세션이 있었으므로 같은 세션의 자연 회복이나 ABR 단독 효과로 확정하지 않는다.** 실제 표시 호출은 40~42회로 디코딩 59~60회보다 낮고 프레임 덮어쓰기가 있어 **완전한 60fps 표시 복구도 아니다.** 남은 반응 지연은 입력 생성→호스트 주입→게임 변화→뷰어 표시를 같은 행동으로 측정해야 한다. `avgLatencyUs=0`을 실제 종단 지연 0으로 읽지 않는다.

수정 없이도 좋은 구간과 나쁜 구간이 관측됐다는 점은 고정 60fps 미지원 가설을 약화하지만, 네트워크·수신 처리·게임 공급 중 최초 원인을 단독 확정하지는 못한다.

## 후속 조사와 완료 조건

1. **멈춤**: 같은 Host A에서 재발 시 워커 스택/덤프, GPU·드라이버·게임·디스플레이 전환 기록을 확보. ReleaseFrame 내부 원인과 회복 후 실제 조작을 검증.
2. **드래그**: 영상 안/밖 해제, 패널 위 해제, 빠른 반복, capture 상실을 격리 UI에서 재현. 실제 DOWN/UP과 호스트 버튼 상태를 대조하고 게임에서 반복 드래그 성공까지 확인. 제품 수정은 별도 작업.
3. **프레임/지연**: 같은 Host B·동일 seq/gen으로 송신과 수신 패킷, 소켓 드롭, 수신/디코드 스레드 정체를 동시 측정. 실제 표시 간격과 복구 대기를 함께 측정.
4. **원본 공급**: 같은 시간의 게임 FPS·포커스·화면 변화와 DXGI 획득 빈도를 비교. 정지 화면에서 낮은 캡처 빈도를 장애로 오판하지 않음.
5. **화질/설정**: 실행 바이너리 버전·해시 고정 후 UI 60fps 선택 → 세션 시작값 → runtime applied → 실제 송신/표시 → ABR 복귀까지 추적. 앞선 30fps 시작의 원인도 이 경로에서 확인.

이번 문서의 검증은 NAS 스냅샷과 코드의 대조이다. 수정 빌드·사용자 행동 재현·완전 복구·독립 검증·배포는 수행하지 않았다. 원시 로그는 토큰 등 비밀 노출 위험을 피하기 위해 Git에 추가하지 않고, 문서에는 필요한 진단 필드만 기록했다.
