# GNLink 정적 화면 지연·고화질 영상 정지 원인 분석

분석 기준: `bae7d99`(0.2.99). 제품 소스 수정 없이 실제 Windows 뷰어의 호출 경로, NAS 로그, 기존 테스트 및 별도 재현 프로브를 확인했다.

**결론: 캡처 지연, Windows 수신 경로의 복구 기능 누락, 복구 상태 기계의 결함이 겹친다.** 호스트의 readback 대기를 줄이는 변경만으로 PC 뷰어의 장기 정지까지 해결되지 않는다. 특히 NACK 수정이 실제 Windows 실행 파일에 연결되지 않았다는 점은 기존 완료 보고와 구분해야 한다.

## 확인된 결함

| 구분 | 확인 결과 | 영향 |
|---|---|---|
| P1 Windows NACK 미연결 | `GNLinkViewer`는 기능 협상을 요청하지 않고 NACK 전송 루프도 실행하지 않음 | 손실 청크만 복구하는 0.2.95/96 변경이 PC 뷰어에는 적용되지 않음 |
| P1 혼잡 복구 재요청 누락 | Congested 상태에서 들어온 P프레임을 재요청 처리 전에 반환 | 최초 요청한 키프레임이 없거나 완성되지 않으면 자체 재요청이 멈춤 |
| P2 완성된 키프레임에도 추가 요청 | seq gap을 처리할 때 현재 수신 프레임이 이미 완성된 키프레임인지 확인하지 않음 | 불필요한 decoder reset·IDR 요청과 대형 프레임 전송 반복 |
| P2 정적 화면 복귀의 가짜 혼잡 | 합성 프레임은 수신 간격 시계를 갱신하지만 실제 present 시각은 갱신하지 않음 | 짧은 실제 프레임 묶음이 오래 밀린 큐로 오인되어 복구 시작 |

### 1. 실제 Windows 뷰어에는 NACK 복구가 연결되지 않았다

- [viewer_startup.cpp:371](../apps/native_poc/src/viewer_startup.cpp#L371)는 `UdpHelloOptions`를 만들지만 `requestNack=true`를 설정하지 않는다. 이 필드의 기본값은 [native_video_client_tcp_control.hpp:73](../apps/native_poc/src/native_video_client_tcp_control.hpp#L73)의 `false`다. HelloAck의 기능 비트도 이 호출에서는 받지 않는다.
- Windows의 실제 수신 루프는 [viewer_video_receiver.cpp:48](../apps/native_poc/src/viewer_video_receiver.cpp#L48)의 `VideoReceiver::run_udp()`다. 여기에 `OldestIncomplete`, `VideoNack`, 재전송 유예·라운드 처리가 없다. recv timeout에서도 제어 채널의 `Tick()`만 호출한다.
- NACK 구현은 [native_video_client_session.cpp:522](../apps/native_poc/src/native_video_client_session.cpp#L522)의 별도 `ClientSessionController` 경로에 있고, 같은 파일 823행에서만 `requestNack=true`를 설정한다.
- [CMakeLists.txt:150](../apps/native_poc/CMakeLists.txt#L150)의 Windows `GNLinkViewer` 타깃은 `viewer_video_receiver.cpp`를 사용한다. 반면 238행의 `shared_core_test`는 `native_video_client_session.cpp`를 포함한다. 따라서 그 테스트의 성공은 실제 PC 뷰어에서 NACK이 동작한다는 증거가 아니다.

수정 방향: 협상 상태와 NACK 정책을 Windows 수신 경로에 연결하고, 패킷 처리·무수신 timeout 양쪽에서 구동한다. 협상만 켜는 변경으로는 부족하다. 또한 현재 assembler는 더 최신 프레임이 완성되면 이전 미완성 조립을 버리므로, 재전송을 기다리는 순서·유예 정책도 실제 수신 루프와 함께 검증해야 한다.

### 2. 혼잡 상태에 들어가면 키프레임 재요청에 도달하지 못한다

- [viewer_frame_gate.cpp:280](../apps/native_poc/src/viewer_frame_gate.cpp#L280)는 `Congested && !keyFrame`이면 즉시 `DropCongested`를 반환한다.
- 뒤쪽 337~349행의 `waitForKeyFrame` 재요청(reason 3)은 이 경우 실행되지 않는다. 302~334행의 시간 기반 재복구도 `Recovering` 상태에만 적용된다.
- `VideoReceiver::run_udp()`의 무수신 timeout에는 영상 복구 타이머가 없다. 새 완성 프레임이 전혀 오지 않는 경우에도 별도의 키프레임 재요청 시계가 돌지 않는다.

**실제 FrameGate 코드 재현:** 정상 IDR 수락 후 decoder failure 경로로 Congested에 진입시켰다. 이후 60초 상당의 P프레임 3,600개를 주입했을 때 3,600개가 모두 폐기되고, 최초 요청 이후 추가 키프레임 요청은 **0회**였다. 이는 host가 자율적으로 보내는 다음 IDR이 완성되면 풀릴 수 있지만, 그 IDR도 손실되거나 source가 멈추면 스스로 재시도하지 못하는 결함이다.

수정 방향: 프레임 개수와 독립된 복구 deadline을 두고, Congested·일반 key wait·무수신 상태를 모두 제한된 빈도로 재시도한다. 계속 실패하면 사용자에게 연결 상태를 알리고 재연결할 수 있어야 한다. 단순히 모든 P마다 요청하면 이전 IDR 폭주로 돌아간다.

### 3. 이미 키프레임을 받았는데 또 키프레임을 요청한다

- [native_video_client_shared_core.cpp:856](../apps/native_poc/src/native_video_client_shared_core.cpp#L856)는 완성 프레임 번호가 이전 번호+1이 아니면 `droppedPreviousIncomplete=true`를 반환한다. 이 값에는 실제 네트워크 손실뿐 아니라 호스트가 보내기 전에 프레임을 버린 경우도 포함된다.
- [viewer_video_receiver.cpp:173](../apps/native_poc/src/viewer_video_receiver.cpp#L173)는 현재 프레임이 완성된 키프레임이어도 먼저 `handle_udp_discontinuity()`를 호출한다. 이 함수는 decoder reset과 `request_keyframe(2)`를 수행하고, 이후에야 현재 키프레임을 디코드한다.
- [host_stage_encode_send_h264_au.cpp:315](../apps/native_poc/src/host_stage_encode_send_h264_au.cpp#L315)의 키프레임 enqueue는 대기 중인 프레임을 지우므로, 손실 없는 링크에서도 정상적인 키프레임 재동기화에 seq gap이 생길 수 있다.

**로그 확인:** 09-05 15:44~15:53의 실제 `keyframe-request seq=... reason=2` 전송 로그는 **90회**이며, **90회 모두** 직전 3줄 안에 `stage=assembly key=1 droppedPrev=1`이 있다. 별도의 throttled 로그 1건은 전송 횟수에서 제외했다. 예: viewer seq=133은 15:44:45.540에 키프레임 조립 완료, .542에 또 키프레임 요청, .558에 그 키프레임 디코드 완료다.

**assembler 재현:** seq=1 IDR 다음에 seq=3 IDR을 손실 없이 입력하면 `Completed + key + droppedPreviousIncomplete`가 함께 반환된다. 따라서 번호 누락만으로 실제 패킷 손실을 확정하면 안 된다.

수정 방향: 현재 완성된 IDR이 이미 복구점이면 필요에 따라 decoder를 재동기화하되 추가 IDR 요청은 생략한다. 해당 IDR 디코드 실패는 별도 오류 경로에서 처리한다.

### 4. 정적 화면에서 실제 화면으로 돌아올 때 가짜 혼잡을 만든다

- [viewer_video_receiver_frame.cpp:164](../apps/native_poc/src/viewer_video_receiver_frame.cpp#L164)의 `note_packet()`은 합성 프레임도 포함한 수신 시계를 갱신한다.
- [viewer_frame_gate.cpp:142](../apps/native_poc/src/viewer_frame_gate.cpp#L142)의 idle 재앵커는 직전 수신 간격이 250ms를 넘어야 작동한다.
- 반면 0.2.97부터 합성 프레임은 실제 콘텐츠의 presented capture 앵커를 갱신하지 않는다. 두 시계의 기준이 달라졌다.

**실제 FrameGate 코드 재현:** 실제 화면 표시 뒤 2초간 100ms 간격의 합성 프레임을 처리하고, 새 실제 프레임 3개를 2ms 간격으로 입력했다. 렌더러가 다음 60Hz 표시 기회를 갖기 전인 6ms 안에 `decodeQueueLag=2,006,000us`로 계산하여 reason 1 요청 및 Congested 진입이 발생했다. 이것은 오랫동안 정체된 디코드 큐를 만들어 넣은 실험이 아니다. 비교 기준에 남아 있던 정적 시간 때문에 발생했다.

실기에서 동일 원인이 매번 발생했다고 단정할 입력별 synthetic trace는 없다. 다만 최신 저장 세션에서도 stale-reference recovery는 0인데 reason=decode_queue 혼잡 진입은 7회 남아 있고, 진입 시 streamLag는 0~34.5ms인데 decodeQueueLag는 346~572ms였다. 이 결함과 맞는 신호지만 각 이벤트의 원인은 추가 확인이 필요하다.

수정 방향: 실제 콘텐츠 도착 시각과 모든 패킷/합성 heartbeat 시각을 구분한다. idle 복귀 후 실제 표시 기회를 고려해 혼잡 여부를 판단하고, 기존의 실제 큐 적체 감지는 유지한다.

## UAC 뒤 호스트 지연: 발생 구간은 확인, 내부 대기 원인은 미확정

09-05 동일 세션의 비교:

| 시각 | 호스트 관측 |
|---|---|
| 15:45:59 | 60fps, readback pending 관찰 평균 4.193ms, peak 5.808ms |
| 15:46:27 | DXGI 실패 후 WGC 시작 |
| 15:46:30.633 | WGC→DXGI 복귀 |
| 15:46:31.102 | 첫 지연 프레임 publish, stamp 보정 265ms |
| 15:46:59 | readback pending 평균 71.581ms, peak 598.250ms |
| 15:48:29 | pending 평균 500.052ms, peak 1,016.511ms |

특히 host seq=3945의 진단 레코드는 `captureToQueueUs=1,000,131`, `captureUnmapWaitUs=499,824`, `captureUnmapUs=17`, `encUs=3,416`이다. 이 레코드의 캡처 입력 단계가 약 1초 걸리므로 일반적인 인코딩 연산 시간만으로는 설명되지 않는다. 단, async 인코더의 현재 입력 통계와 출력 AU는 서로 다른 프레임일 수 있어 같은 seq의 전체 지연 분해로 합산하면 안 된다.

화면이 거의 바뀌지 않는 동안 낮은 FPS 자체는 change-driven 캡처의 정상 동작일 수 있다. 그러나 새 입력에 따른 실제 publish가 수백 ms~1초 늦어지는 것은 별도 문제다.

`AcquireNextFrame` 대기의 내부 락 경합은 [Sunshine의 대응 코드](https://github.com/LizardByte/Sunshine/blob/master/src/platform/windows/display_base.cpp)에 독립 근거가 있다. 하지만 GNLink에서 WGC가 보호 상태를 바꿨는지, 순수 GPU 작업인지, runtime/driver 또는 context 대기인지까지는 당시 로그가 구분하지 못한다. 0.2.98/99의 계측과 8ms timeout+2ms 양보는 이를 확인·완화하기 위한 변경이며 실기 성공을 입증한 상태가 아니다.

## 'PC 클라이언트를 껐다 켜야 풀린다'는 장기 정지 기록

- 09-05 11:20:42.749 viewer는 seq=3261 표시 후 새 영상 수신·디코드 통계가 끊긴다.
- 11:20:49.566 제어 교환이 6.476초 후 실패하고 `closed=1 reason=peer-lost`를 기록한다.
- 호스트는 이 사이에도 영상과 키프레임을 송신했다. 예: 11:20:48.489 seq=3586 키프레임 223,876 bytes. 이후 호스트 제어 세션도 `peer-lost`로 종료된다.
- 11:21:50.517에도 viewer는 같은 seq=3261만 다시 표시한다.

따라서 이 장기 정지는 '호스트가 새 영상을 전혀 만들지 않았다'로 설명되지 않는다. 마지막 관측 congestionState는 normal이므로 위 Congested 재요청 결함만으로 이 특정 사건을 설명할 수도 없다.

수신 루프가 영상 조립·동기 디코드·제어 패킷 처리와 heartbeat를 함께 맡는다(`viewer_video_receiver.cpp:78-98`, `viewer_video_receiver_frame.cpp:242`). 영상 경로가 오래 막히면 제어 처리도 지연될 수 있다. 또한 [viewer_control_client.cpp:323-343](../apps/native_poc/src/viewer_control_client.cpp#L323)은 제어 실패 시 루프를 종료하며 자동 재연결을 수행하지 않는다. 재시작이 새 제어·조립·디코더 상태를 만드는 것은 증상과 부합한다.

다만 저장 로그에는 정지 순간의 수신 스레드 스택과 양단 패킷 기록이 없어, 최초 방아쇠가 UDP 전달 중단인지, 디코더/D3D 호출 정체인지, 다른 수신 스레드 경합인지 확정할 수 없다. 실패 로그가 없다는 이유로 임의의 mutex deadlock을 단정하지 않는다. 다음 실기는 recv→assembly→decode 진입/반환 heartbeat와 제어 채널 상태를 프레임 진행과 독립적으로 기록해야 한다.

## 검증 범위와 다음 수정 순서

1. Windows 실제 바이너리의 NACK 협상·재전송 구동과 keyframe 복구 deadline을 연결한다. 완성된 IDR 앞 gap은 추가 요청 없이 복구하도록 처리한다.
2. 동일 PC 뷰어 경로로 손실 주입: P 청크 손실, 복구 IDR 청크 손실, source 정지, Congested 진입 후 최초 IDR 손실, 제어 응답 소실을 검사한다. 별도 ClientSessionController 테스트만으로 완료 처리하지 않는다.
3. 합성 프레임→실제 콘텐츠 복귀 회귀를 고정한다. FPS 숫자보다 입력→새 콘텐츠 표시와 실제 큐 깊이를 검증한다.
4. 호스트 UAC 지연은 100/0, 8/0, 8/2000 A/B 및 worker 단계 계측으로 분리한다. 보호 OFF 강제 복원은 하지 않는다.
5. 제어/영상이 진행되지 않는 상태의 제한된 재시도·사용자 알림·재연결 정책을 추가하고, 영상을 멈춰도 제어 수신이 계속되는지 검증한다.

기존 `viewer_frame_gate_test`, `native_video_client_shared_core_test`는 PASS였다. 이번 별도 프로브는 제품의 실제 FrameGate·assembler 소스를 링크하여 위 세 가지 문제 경로를 재현했다. 프로브 소스와 출력은 로컬 `.claude/freeze-diagnosis-20260905/`의 `viewer_gate_probe.cpp`, `assembly_gap_probe.cpp`, `gate-probe-result.log`, `assembly-probe-result.log`에 있다. 제품 파일과 기존 테스트는 수정하지 않았다.

로그 범위: host `8ec6ecb1`, viewer `68f79d01`, 09-05 저장 세션. 09-06 21:33 NAS 재확인에서도 해당 viewer의 최신 기록은 09-05 15:53:24 종료였다. 09-05 조사 당시 설치 레지스트리는 0.2.97이었으며, 0.2.99 설치 후 동일 증상이 재현된 최신 양단 로그는 확보되지 않았다. 따라서 이 보고서는 현재 코드의 확인된 결함과 저장된 사건의 증거를 구분하며, 0.2.99 실기 결과를 추정하지 않는다.
