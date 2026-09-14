# 2026-09-14 원격 지연·정지 원인분리 계측 (0.2.125 후보)

## 사건과 확인 범위
- 10:16 이후 회사 뷰어→집 호스트: 릴레이/직접 P2P 모두 관측. 집 설치본은 업데이트/복구 실패로 혼합 버전, 감독 Host 없이 Stream이 남았음. 종료 근본원인과 복구 실패 파일은 기존 로그로 미확정.
- 11:12 새 직접 P2P 세션: 앞선 구간보다 프레임 조립 실패/키프레임 복구가 줄었으나 표시 간격 불균등. capGapUs는 실제로 presentGapUs의 별칭이므로 호스트 캡처 원인 확정은 철회. 키보드 지연은 move RTT로 확정할 수 없음.
- 11:35 이후 회사 뷰어→회사 게임용 PC: 사용자 보고는 0.2.124에서 게임 직후 정지. 별도 기기/세션으로 검증용 조사 중. 집의 혼합 상태를 회사 호스트에 추정 적용하지 않음.
- 조사 원본/정정 이력: `.claude/investigate-typing-1016/report.md`, 사건별 NAS 로그와 SHA256SUMS. 원본 로그는 커밋하지 않는다.

## 추가한 계측
- Viewer `[present] timingSchema=2`: 첫 프레임부터 seq/gen/frameVersion/synthetic, 호스트 capture/encode-start/end/send, 뷰어 recv/decode-start/end/queue-set/paint-start/present의 원시 QPC 기록. 기존 매 present 로그를 확장하여 정상 프레임도 비교 가능.
- `capGapUs`를 `presentGapUs`로 바로잡음. 기존 queueToPresent는 paint만 측정했으므로 해당 잘못된 이름 제거. cross-clock total/net은 계산 불가를 -1로 기록하고, 경고 조건은 같은 뷰어 클록의 recv→present 사용.
- Viewer `[input-timing]`: 키·텍스트·physical-key 이벤트의 생성/전송/응답 시각, 실패 포함 교환 소요시간. 마우스는 느린 교환/실패 때 기록. 입력 내용·키코드·좌표는 새 로그에 기록하지 않음. ACK는 OS 주입/화면 반영 보장이 아니므로 osInjectionConfirmed=0 명시.
- Stream `[input-timing]`: 이벤트·텍스트 수신완료/처리완료/ACK완료 시각. 프레임과 공통 host QPC 사용. physical-key의 호스트 처리 세부 계측은 포함하지 않음.
- Stream `[capture-timing]`: 실제 readback publish 경로의 capture/callback/publish 시각과 100ms 이상 간격. 화면 무변화와 지연을 이 간격만으로 구분한다고 주장하지 않음.
- Stream 숫자 진단 로그(wire/capture-timing/input-timing/encodedFrames)는 stdout 외에 `%LOCALAPPDATA%/remote60/diagnostics/stream-PID-start.{0,1}.log`에도 저장. 프로세스별 8MiB×2 순환; 임의 출력/자격증명은 복사하지 않음. 감독 Host가 없어도 로컬에 남으며 NAS 자동 업로드는 아님. 보통 1초마다 flush, 마지막 버퍼는 강제 종료 시 유실 가능. 오래된 프로세스의 파일 자동 삭제는 하지 않음.
- Host lifecycle: 자식 online 수신/WM_DESTROY/메시지루프 종료. 강제 종료·크래시의 원인 전부를 보장하는 덤프는 아님.
- Updater rollback: 시작/정지확인/각 파일 복구/실패 operation+file+Win32 오류/등록 복구/최종 결과. 기존 health 실패 사유만 남아 복구 실패 원인을 덮는 문제를 보완. 파일 교체/복구 동작 순서는 변경하지 않음.

## 분석 및 검증
- `python automation/summarize_pipeline_latency.py viewer.log --out timing.json`: 동일클록 단계별 분포, 프레임별 원시 결과, 입력 큐/교환시간. 네트워크/전체 시간은 같은 viewerSession의 근접 clock 샘플(3초 이내)로 추정, RTT/2 불확실성 동반. 비대칭 회선에서는 실제 단방향 시간과 다를 수 있다. 계측 누락은 0ms 정상으로 취급하지 않고 입력 없음/exit2.
- 분석기 4 tests PASS: 다른 장치 시계, 큐/페인트 분리, 누락/역행시각, 실패 입력 보존.
- 업데이트 효과 220 checks/0 failed: 실제 파일 lock을 걸어 rollback 실패 파일과 Win32=32가 기록되는지 단정.
- 별도 host_diagnostic_log_test PASS: 파이프 없이 기록, 비대상 민감 로그 제외, 2개 파일 크기 상한.
- 격리 loopback 합성 1080p60→실제 GNLinkViewer: 590 schema2 present records, fixture/viewer rc0. 이는 필드 게임/키보드 실기·GPU 부하 증거가 아니다. 숨긴 창만 실행한 첫 시도는 present 0건으로 분석기 exit2(정상 판정하지 않음); 이후 별도 창 표시 시 실제 로그 확인.
- 0.2.125 설치기 Release 빌드 성공, embedded payload9/9 일치, 운영 manifest 서명과 제품 verifier Ok125/변조 SignatureInvalid 확인. 검증용 독립 검토 전이며 게시/설치/재시작 미실행.

## 다음 단계
고정 후보 독립 검토 및 게시 검증 후 사용자 업데이트로 실제 사건을 재현한다. 현재 세션은 자동 종료하지 않는다. 단계별 지연은 계측으로 구분하되, 키가 화면에 반영되는 인과는 timestamp만으로 확정하지 않는다. main merge/push 없음.
