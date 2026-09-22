# 2026-09-14 원격 지연·정지 원인분리 계측 (0.2.125 후보)

## 사건과 확인 범위
- 10:16 이후 회사 뷰어→집 호스트: 릴레이/직접 P2P 모두 관측. 집 설치본은 업데이트/복구 실패로 혼합 버전, 감독 Host 없이 Stream이 남았음. 종료 근본원인과 복구 실패 파일은 기존 로그로 미확정.
- 11:12 새 직접 P2P 세션: 앞선 구간보다 프레임 조립 실패/키프레임 복구가 줄었으나 표시 간격 불균등. capGapUs는 실제로 presentGapUs의 별칭이므로 호스트 캡처 원인 확정은 철회. 키보드 지연은 move RTT로 확정할 수 없음.
- 11:35 이후 회사 뷰어→회사 게임용 PC: 사용자 보고는 0.2.124에서 게임 직후 정지. 별도 기기/세션으로 검증용 조사 중. 집의 혼합 상태를 회사 호스트에 추정 적용하지 않음.
- 조사 원본/정정 이력: `.claude/investigate-typing-1016/report.md`, 사건별 NAS 로그와 SHA256SUMS. 원본 로그는 커밋하지 않는다.

## 추가한 계측
- Viewer `[present] timingSchema=2`: 모든 표시 간격은 유지하고 상세 시각은 첫 프레임 및 평시1Hz/지연시최대10Hz 표본으로 seq/gen/frameVersion/synthetic, 호스트 capture/encode-start/end/send, 뷰어 recv/decode-start/end/queue-set/paint-start/present의 원시 QPC 기록. 기존 매 present 간격 로그는 유지해 전체 분포와 상세 표본을 구분한다. 상세 단계 분포에는 표본 편향이 있음을 분석기에 명시한다.
- `capGapUs`를 `presentGapUs`로 바로잡음. 기존 queueToPresent는 paint만 측정했으므로 해당 잘못된 이름 제거. cross-clock total/net은 계산 불가를 -1로 기록하고, 경고 조건은 같은 뷰어 클록의 recv→present 사용.
- Viewer `[input-timing]`: 키·텍스트·physical-key 이벤트의 생성/전송/응답 시각, 실패 포함 교환 소요시간. 마우스는 느린 교환/실패 때 기록. 입력 내용·키코드·좌표는 새 로그에 기록하지 않음. ACK는 OS 주입/화면 반영 보장이 아니므로 osInjectionConfirmed=0 명시.
- Stream `[input-timing]`: 이벤트·텍스트 수신완료/처리완료/ACK완료 시각. 프레임과 공통 host QPC 사용. physical-key의 호스트 처리 세부 계측은 포함하지 않음.
- Stream `[capture-timing]`: 실제 readback publish 경로의 capture/callback/publish 시각과 100ms 이상 간격. 화면 무변화와 지연을 이 간격만으로 구분한다고 주장하지 않음.
- Stream 숫자 진단 로그(wire/capture-timing/input-timing/encodedFrames)는 stdout 외에 `%LOCALAPPDATA%/remote60/diagnostics/stream-slot-N.{0,1}.log`에도 저장. 고정4bank×8MiB×2=총64MiB 상한, 재시작에도 같은 bank 재사용. .lock 파일의 배타적 핸들로 동시쓰기 보호; 4bank 모두 사용 중이면 추가 프로세스는 stdout만 유지. 임의 출력/자격증명은 복사하지 않음. 감독 Host가 없어도 로컬에 남으며 NAS 자동 업로드는 아님. 보통 1초마다 flush, 마지막 버퍼는 강제 종료 시 유실 가능. 고정 슬롯 재사용으로 교차-run 누적을 방지하며 삭제 명령은 사용하지 않음.
- Host lifecycle: 자식 online 수신/WM_DESTROY/메시지루프 종료. 강제 종료·크래시의 원인 전부를 보장하는 덤프는 아님.
- Updater rollback: 시작/정지확인/각 파일 복구/실패 operation+file+Win32 오류/등록 복구/최종 결과. 기존 health 실패 사유만 남아 복구 실패 원인을 덮는 문제를 보완. 파일 교체/복구 동작 순서는 변경하지 않음.

## 분석 및 검증
- `python automation/summarize_pipeline_latency.py viewer.log --out timing.json`: 동일클록 단계별 분포, 프레임별 원시 결과, 입력 큐/교환시간. 네트워크/전체 시간은 같은 viewerSession의 근접 clock 샘플(3초 이내)로 추정, RTT/2 불확실성 동반. 비대칭 회선에서는 실제 단방향 시간과 다를 수 있다. 계측 누락은 0ms 정상으로 취급하지 않고 입력 없음/exit2.
- 분석기 5 tests PASS: 다른 장치 시계, 큐/페인트 분리, 누락/역행시각, 실패 입력 보존.
- 업데이트 효과 220 checks/0 failed: 실제 파일 lock을 걸어 rollback 실패 파일과 Win32=32가 기록되는지 단정.
- 별도 host_diagnostic_log_test PASS: 파이프 없이 기록, 비대상 민감 로그 제외, 2개 파일 크기 상한.
- 격리 loopback 합성 1080p60→실제 GNLinkViewer: 590 schema2 present records, fixture/viewer rc0. 이는 필드 게임/키보드 실기·GPU 부하 증거가 아니다. 숨긴 창만 실행한 첫 시도는 present 0건으로 분석기 exit2(정상 판정하지 않음); 이후 별도 창 표시 시 실제 로그 확인.
- 0.2.125 설치기 Release 빌드 성공, embedded payload9/9 일치, 운영 manifest 서명과 제품 verifier Ok125/변조 SignatureInvalid 확인. r1 검증용 독립 검토 통과 후 보존상한/상세로그 기록량 보수. r2 고정 후보 검증용 PACKAGE OK 후 기존 채널 게시 및 Codex 외부 확인 완료. 사용자 설치/재시작은 미실행.

## 다음 단계
고정 후보 독립 검토 및 게시 검증 완료. 사용자 업데이트로 실제 사건을 재현한다. 현재 세션은 자동 종료하지 않는다. 단계별 지연은 계측으로 구분하되, 키가 화면에 반영되는 인과는 timestamp만으로 확정하지 않는다. main merge/push 없음.

## r2 보수 검증
- mirror 동시5개(5번째 추가bank 금지)·30회 재시작·각segment8MiB 상한 PASS.
- 실제 Viewer 합성1080p60 10초: 전체 표시 간격595개/상세 표본10개, 프로세스rc0. 관측 부재를 정상으로 처리하지 않음.
- lastError 소비처는 updater_effects.cpp:157 로그/:599 복사, updater_main.cpp:325~326 empty 여부+로그. 오류 문자열 exact-match로 동작 분기하는 소비처 없음.
- 롤백 .claude/rollback/0.2.124/ 확보: 인증된 현재 게시 pair와 동일, payload10개 size/hash 일치.
- 최종 r2 manifest SHA256 58fc1b885ca2f8e3f2cc30ea5491f7f8a87435ceb5bee67adddd77d09d9844f2, sig869489c5fcd40f747d92ddaaa9e7ef18a5071ed9369dc145f9fdb69e05115e1f.

## 게시 완료 — 2026-09-14
- 상태: **배포 완료(마일스톤) · 사용자 업데이트/실기 대기**. 진단 계측이며 지연·게임정지 해결 선언이 아니다.
- 코드 commit: `94e4d8d163b090e36fe704511fad6aed5f8abf9f` (최초계측582a9b6 포함). 검증용 rev7 FINAL_PACKAGE_VERDICT=기존채널 비상용시험 PACKAGE OK.
- gnlink_deploy.sh rc0, NAS·공개 HTTPS payload10개 hash 일치, authenticated API HTTP200/version0.2.125. 수신 manifest/sig가 위 r2 고정값과 동일; 제품검증기 Ok125·1byte변조 SignatureInvalid.
- 설치기: https://rem.shotan.net/updates/0.2.125/GNLinkSetup.exe — SHA256 `cd49ae1c9b5faea2217d4af55861c99d7f19b1de5ef63f46b1063c3a5c43616a`. 후보 worktree `dist/GNLinkSetup-0.2.125.exe` 동일.
- 서버복구쌍: `/opt/gnlink/manifest-backups/0.2.124/`, 로컬전체복구본 `.claude/rollback/0.2.124/`. 채널복원은 이미 설치한125의 자동downgrade가 아니며 이전설치기 재설치가 별도로 필요.
- 근거: `build-incident/release-0.2.125-publish.log`, `.claude/rel/0.2.125/published-https/`, `release-report.json`. 검증용 게시 후 독립외부대조 추가요청(seq2290); 여기 기록은 Codex 직접확인.
- 사용자 다음행동: 회사클라이언트와 회사게임용호스트 모두125업데이트 완료버전 확인 후 타이핑/게임 재현시각 기록. 집호스트도 조사시125완전설치 확인. 설치실패/혼합버전이면 성공으로 간주하지 않는다.
- 사용자프로세스 종료/설치/재시작, main merge, git push 모두 미실행.
