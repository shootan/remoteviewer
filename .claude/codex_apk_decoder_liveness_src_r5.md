# APK decoder liveness r5 — N3 SOURCE

2026-10-29. **ac587dd SOURCE OK. 잔여 N3 재시도 상한 지적을 닫는다.** r4에서 수용한 N1/N2/D1과 shared trace의 기존 범위 판정은 유지한다. 실제 Android EVIDENCE/terminal UX·control peer_lost 종결·APK 0.2.33 출하 승인은 아직 아니다.

## 고정과 검토

- HEAD `ac587dde7ad87f35e00547e3992677a5554c9c3d`, 부모 `0a108b39830664e28ae36d79372912eae894b9bf`. tracked clean(`apk_out/` untracked), diff --check 통과.
- 보고 `report_decoder_liveness_r1.md` r5, SHA-256 `55175a23f28a2168f7e27d9970462c4c0a79b0f329a4ce39e39893482d7e6846`.
- 이번 diff는 decoder cpp/hpp, liveness test, gate 주석 4파일이다. selection gate의 동작 변경은 없고 N1 예약 계약을 다시 바꾼 것이 아니다.

생성 실패 뒤의 frame/timer가 EnsureCodecLocked의 같은 retry slot·counter를 사용한다. 일반 delta는 기한 전 생성을 하지 않고, timer도 같은 조건을 따른다. **IDR은 기존 즉시 회복 계약 때문에 기한 전 시도가 가능하지만 같은 예산을 소비한다.** 예산 소진 후 codecTerminal은 IDR/delta/timer 모두를 막는다. 성공 또는 명시 lifecycle의 ResetCodecRetry로 새 실패 episode가 시작되는 구조를 수용한다. wire/transport/owed 8회 예산을 늘리지 않았다.

정확한 횟수는 **최초 실패 1 + 추가 시도 최대20 = 연속 실패21회**다. IDR 예외 때문에 terminal이 반드시 20초 뒤에 된다고 보장하지 않는다. “항상 1초 간격”도 delta/timer에 대한 설명이며 IDR 예외를 함께 쓴다. 모든 생성이 성공하고도 codec queue/output이 별도 실패하는 모든 경우까지 이번 생성 실패 예산이 보장한다고 확대하지 않는다.

## Codex 독립 반례 대조

자기 review 폴더에 고정 decoder/gate/pending slot/fake NDK를 freeze해 빌드했다. 제품 source는 수정하지 않았다. 기존 candidate/owner 대조와 새로운 frame/timer/IDR/terminal 흐름의 결과는 다음과 같다.

```text
CONTRACT first queued generation binds: PASS bound=101
CONTRACT owner change during dequeue: PASS
CONTRACT two seconds, one IDR plus 40 deltas:
failures=3 timerRetries=2 PASS
CONTRACT IDRs share budget: PASS failures=21 retry=20
CONTRACT terminal IDR/delta do not restart: PASS
CONTRACT new surface resets terminal and queues IDR: PASS
```

0a108b3에서 같은 delta arm은 41회 실패/counter0이었다. ac587dd에서는 3회/2로 제한되고, 이어 새 IDR들을 보내도 총21회에서 terminal이 됐다. fake create가 다시 성공하도록 바꾼 뒤에도 terminal에서 IDR/delta로 생성하지 않았으며, 새 surface를 접수한 뒤 현재 IDR이 queue됐다. timer만 계수하고 frame 경로가 무제한 시도하던 반례를 닫는다.

기록:

- `.claude/codex_decoder_liveness_review/review_r5.py`
- `r5_ac587/{candidate_probe.cpp,android_video_decoder.cpp,android_video_decoder.hpp,selection_ack_gate.hpp,decoder_pending_slot.hpp,fake_ndk.cpp,build.log,candidate_probe.log}`
- build rc0 / probe rc0.
- exe SHA-256 `877c10a5608dfcf596a33896562f01ec27b0600adee5df51ce8a8e49c79cb2f1`.
- log SHA-256 `36e05ba5447f0951d12f9323d8e8ef3cec99ca3000d909403524cc2a63f44943`.

작업용의 68/68·IDR49/49·기존 소비 시험·NDK4ABI는 제출 보고에 귀속한다. 이 전체를 Codex가 독립 재실행했다고 하지 않는다. 이번 직접 대조는 위 좁은 실제 sink/fake NDK 범위다.

## 다음 진단 실행 — 기본 SH / SH-off 각 1회 허용

새 고정 trace APK를 만들고 hash·native ABI/flag·effective endpoint·실행 script snapshot을 기록한 뒤, **기본 SH E2와 실제 Settings SH-off E2를 각 1회, 첫 영상 후60초 관찰**로 진행할 수 있다. 아직 APK를 만들지 않은 것은 SOURCE 반려 이유가 아니며 해당 build/확인은 이 승인 안에서 이어간다. 다음 정상 build 단계마다 새 go를 받을 필요는 없다.

단 **현재 읽은 r8 스크립트에는 `KE_PLAN=e2-shoff`와 `full`만 있다. `full`로 E3 이후까지 실행하지 않는다.** 기본 SH arm은 E1+E2+60초만 수행하는 명시 subset을 고정한다. 기존 launch/guard/job·기기/파일 경계를 바꾸지 않는 subset/관측 분기는 같은 task 범위로 준비하고 diff/hash와 실제 선택 plan을 보고한다. 기본/SH-off는 별도 run ID와 결과로 남기며 자동 반복하지 않는다. source/script가 달라진다면 영향을 구분하고 실제 실행 hash를 연결한다.

두 arm의 조건:

1. 새 APK가 실제 ac587dd의 JNI/native를 담았는지 고정한다. trace debug APK의 `liveness-trace` 4ABI 포함, plain/release의 0/4 및 release trace 속성 거부 경계는 그대로다. SH 시험 seam/후속 무관 제품 수정은 켜거나 섞지 않는다. 이전 7a79b09 APK hash를 재사용하지 않는다.
2. 검증용이 기존 측정 슬롯을 조율한다. SDK adb path/hash·시험 serial/boot와 **30초 안정 관찰**을 기록하고, listener 교체/offline 반복이면 INVALID로 남기며 kill-server/다른 앱 종료나 blind 재실행을 하지 않는다. 기존 허용한 시험 AVD의 정상 재시작은 필요성·유휴/독점 조건 아래 별도 환경 arm으로 기록한다.
3. local fixture server/Host/harness도 실제 hash를 고정한다. relay trace는 local fixture에만 켜고, current run/flow·방향·transport seq/ACK/close와 app/Host를 clock offset으로 대조한다. 계측 send 성공을 원격 수신으로 확대하지 않는다. 공용 input none/power off 두 줄 launch/final gate, job/EOF·repo 경계·clipboard 미조작은 유지한다.
4. 기본 SH는 requested/effective/fallback을 남긴다. 실제 fallback direct로 흘렀다면 “SH on 렌더 검증”이라고 쓰지 않는다. SH-off는 Settings OFF/direct·sharpen_start 0의 기존 구분을 유지한다. 두 arm의 선택 전 준비 시간·부하 차이도 기록한다.
5. 첫 영상과 60초 영상/control 진행을 분리한다. 가장 먼저 발생한 세션 종료/오류 시각을 잡아 뒤 메뉴 탐색 실패를 최초 원인으로 부르지 않는다. 실패 시 원문/clock/trace를 보존하고 그 arm을 종료한다. 1회 무발생으로 r21 B/r25의 control-only peer_lost 원인을 닫지 않는다.

새 코드에서 terminal UI/명시 재시도가 실제로 새 surface lifecycle를 만드는지는 후속 독립 EVIDENCE 항목이다. 4MiB를 넘는 유효 IDR의 실제 지원 영향과 메모리 관측도 남아 있다. 건강한 E2만 통과했다고 이 실패 UX나 모든 codec/surface 경합까지 완료로 표시하지 않는다. APK 0.2.33 출하 보류는 해당 필수 증거와 control 종료 조사 판정까지 유지한다.

PC 0.2.164와 old-apk-lane은 이 branch를 포함하지 않는 기존 분리 경계를 유지하며 독립적으로 진행한다. 검증용은 같은 liveness task의 다음 담당으로 새 artifact·한정 실행 결과를 연결한다.

Codex는 source와 자기 순수 fake NDK probe만 실행했다. 실제 Host/Viewer/AVD/adb·GPU/OS 입력·설치·운영·삭제·제품 변경·commit/push는 없다. 검토 artifact만 작성했으므로 제품 commit은 만들지 않았다. 사용자 현재 조치 없음.
