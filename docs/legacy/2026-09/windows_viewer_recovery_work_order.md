# Windows 뷰어 정지·지연 수정 작업 지시

요청일: 2026-09-07. 사용자 요청: 확인된 내용을 문서화하고 `remote` Claude 세션에 구현을 위임하되, 작업 전에 한 번 더 크로스체크한다.

근거 문서: [2026-09-06 원인 분석](stream_freeze_diagnosis_2026-09-06.md). 분석 기록 커밋: `5e32126`, 당시 제품 기준: `bae7d99`(0.2.99). 착수 시 현재 HEAD와 작업 트리 변경을 먼저 확인한다.

## 1. 구현 전 독립 교차 확인

제품 소스 수정 전에 아래를 직접 확인하고, 각 항목을 **확인 / 반증 / 조건부**로 분류하여 코드 위치·재현 결과와 함께 요청한 Codex 세션에 회신한다. 기존 분석의 결론을 그대로 승인하지 않는다. 반증된 항목은 수정 대상에서 빼고 원인을 다시 설명한다. 확인된 항목의 구현에는 사용자 재승인을 추가로 요구하지 않는다.

1. **실행 경로:** 실제 `GNLinkViewer`의 CMake 입력 파일, startup Hello/HelloAck 기능 협상, `VideoReceiver::run_udp()`를 따라가 Windows 경로에 NACK 협상·송신이 연결됐는지 확인한다. `ClientSessionController` 및 shared_core 테스트와 구분한다.
2. **복구 재요청:** `Congested && !keyFrame` early return, 일반 key wait, 무수신 timeout에서 재요청 타이머가 실제로 실행되는지 확인한다. 첫 복구 IDR이 도착하지 않는 상태로 60초 상당의 입력을 주입한다.
3. **완성 IDR 중복 요청:** seq gap을 가진 완성 IDR이 decoder reset뿐 아니라 새 IDR 요청까지 일으키는지 확인한다. 정상 IDR 수락과 IDR 디코드 실패를 구분한다.
4. **정적→활동 전환:** synthetic 수신 시각, 실제 콘텐츠 수신 시각, 실제 presented capture 앵커의 의미를 확인한다. 2초 synthetic 구간 뒤 6ms 실제 프레임 묶음이 가짜 혼잡을 만드는지 확인한다.
5. **현장 근거의 한계:** 저장 viewer 로그의 최신 종료는 09-05 15:53이다. 11:20 장기 정지의 마지막 congestionState는 normal이고 이후 제어도 peer-lost가 됐다. 위 상태 머신 결함이나 특정 D3D 락을 그 사건의 유일한 원인으로 확정하지 않는다.

로컬 재현 자료: `.claude/freeze-diagnosis-20260905/`의 `viewer_gate_probe.cpp`, `assembly_gap_probe.cpp`, `CMakeLists.txt`, `gate-probe-result.log`, `assembly-probe-result.log`. 프로브의 exit 0은 **문제 재현 성공**을 뜻하므로 수정 후 회귀 테스트로 승격할 때 기대값을 바꿔야 한다.

기존 재현값: Congested에서 60초 P 3,600개 폐기/추가 IDR 요청 0회; synthetic idle 뒤 계산된 backlog 2,006,000us; 무손실 seq 1→3 IDR에서 Completed+key+droppedPreviousIncomplete. 최신 저장 세션의 실제 reason=2 요청 90/90회는 완성된 key+gap 직후였다(throttled 1건 제외).

## 2. 확인된 항목의 수정 순서

1. **Windows NACK 및 조립 순서:** 실제 Windows 경로에서 기능 협상 결과를 보존하고 NACK 정책을 구동한다. 누락 청크 검출, 무수신 시간 경과, 재정렬 유예, 큰 IDR의 아직 전송 중인 tail, 라운드/바이트 상한, 구버전 peer를 함께 다룬다. 최신 AU가 먼저 완성되어 이전 미완성 AU를 버리는 assembler 정책과 NACK의 복구 유예가 충돌하지 않도록 한다.
2. **복구 상태 머신:** Congested·일반 key wait·영상 무수신 모두 프레임 개수와 독립된 시간 기준으로 재시도한다. 요청은 제한된 빈도로 합치며 매 P프레임마다 IDR을 요청하지 않는다. 현재 완성 IDR이 복구점이면 추가 IDR 요청을 생략하고, 디코드 실패는 해당 실패 경로에서 처리한다.
3. **정적 복귀:** 실제 콘텐츠의 idle 시계와 전체 수신/heartbeat 시계를 구분한다. 첫 실제 표시 기회 전의 짧은 burst를 오래된 backlog로 오인하지 않도록 하되, 실제 지속 적체 감지를 약화시키지 않는다.
4. **영구 정지 진단·회복:** recv→assembly→decode 진입/반환 진행과 제어 채널 생존성을 구분할 수 있게 한다. 제어 실패·영상 장기 무진행 시 조용히 마지막 화면만 유지하는 상황을 검출하고, 원인이 확인된 범위에서 제한된 복구/재연결과 상태 알림을 설계·검증한다. 영상 디코드 정체가 제어 수신까지 막는 문제를 단순 keyframe 타이머로 해결했다고 보고하지 않는다.
5. **호스트 UAC/readback은 별도 검증:** 0.2.99의 worker 계측과 100/0·8/0·8/2000 A/B로 capture→publish 및 입력 반응을 비교한다. 실기를 못 하면 미검증으로 남긴다. 원인 확인 없이 multithread 보호를 OFF로 복원하거나 대규모 디바이스 분리를 먼저 적용하지 않는다.

## 3. 완료 조건

- 실제 Windows 제품의 수신 경로를 실행하는 통합 테스트로 NACK 협상과 손실 복구를 확인한다. shared_core/다른 세션 경로 테스트만 PASS인 결과로 완료 처리하지 않는다.
- P 청크 손실, 복구 IDR 청크 손실, 큰 IDR tail, 조립 재정렬, source 정지, Congested에서 첫 IDR 손실, 제어 응답 소실을 다룬다. 재요청이 사라지거나 IDR 폭주로 바뀌지 않아야 한다.
- 정상 완성 IDR 앞 seq gap, IDR 디코드 실패, synthetic→real 전환, 실제 backlog, 구버전 peer 회귀를 포함한다.
- 입력→새 콘텐츠 표시 지연, 복구 소요 시간, IDR/NACK 횟수, CPU 및 실제 capture→publish를 구분해 보고한다. FPS가 낮다는 이유만으로 정적 화면을 실패로 판단하지 않는다.
- 변경에 맞는 빌드·테스트를 수행하고 필요한 설치본을 생성한다. 설치본 생성과 현장 설치 후 검증은 구분한다.
- 저장소 규칙에 따라 `docs/history.md`에 변경·검증·잔여 항목을 기록하고, `docs/구현계획.md`는 상태만 갱신하며, 작업 관련 파일만 Git MCP로 커밋한다. 다른 세션의 변경을 되돌리거나 포함하지 않는다.
- 최종 회신: 교차 확인 결과, 커밋, 변경한 실제 실행 경로, 실행한 테스트와 실패 주입 결과, 실기 수행 여부, 남은 미확정 원인.

수행 방식: 대상 확인 후 A2A task로 위임한다. 답장 주소는 전달 메시지의 `[A2A reply_to]`에 있는 identity+sessionId로 고정한다. 긴 답변이 미리보기로만 보이면 `.claude/a2a_codex_reply.md`에 이어 쓰고 짧게 위치를 알린다.
