# APK E2 — decoder/control liveness 결정

2026-10-29 · **① Android 수신·제어와 느린 codec 작업의 동기 의존을 끊는 수정을 0.2.33 우선 범위로 수용한다. ② 한정 SH-off 대조와 유휴 시험 AVD 재시작을 허용한다. ③ 결정적 느린-codec 회귀 대조는 필수다.** 아래 범위로 기존 검증용 창구에서 한 지시서에 취합한다.

## 조사 근거와 확정 수준

report_e2_relay_investigation.md SHA-256 **379a52e95a537b4e6b66a6ace53211a9931b41310b84f13ffe24785c6590f2bf**를 읽고 고정 9f9a91c source를 대조했다.

- SetSurface는 LockedState 아래에서 ResetCodec/EnsureCodec(create/configure/start)를 수행한다. OnEncodedH264FrameAs도 같은 mutex와 느린 codec 경로에 들어간다.
- VideoReceiveMain은 assembled frame을 sink에 **동기 전달**하고 같은 loop에서 UDP control의 OnPacket/Tick을 처리한다. ConsumeDecoderKeyframeRequest도 decoder lock을 잡으므로, 수신 callback 한 곳뿐 아니라 control worker의 sink 호출에도 의존이 있다.
- 이 구조는 **codec 작업 때문에 control 진행이 막힐 수 있는 제품 구조적 취약점**이다. b554e76의 기존 SetSurface에도 같은 잠금 형태가 있어 이 구조 자체는 새로 도입된 것이 아님을 확인했다.

그러나 사건의 **“UI codec 생성 → 그 lock에서 RX가 정확히 몇 초 대기 → retransmit 소진”은 아직 유력한 인과 가설**이다. 전체 codec 재생성 6초와 특정 thread의 lock hold/wait 6초를 동일하게 쓰지 않는다. 직접 stage/wait 기록이 없다는 한계는 유지한다.

relay가 모든 영상을 막은 가설은 AU/delta의 sink 도달과 맞지 않는다. 다만 단순 forward source와 aggregate 계수만으로 **모든 IDR/chunk가 제때 전달됐다거나 relay/UDP/fixture 기여가 완전히 없었다**고 확정하지 않는다. 수신 loop 정지에 의한 socket backlog/drop도 구간별 결과에 포함한다. decoder/server 파일이 같다는 것은 그 파일의 동일성 근거이며 전체 호출 순서·build/fixture/부하까지 회귀가 없었다는 증명은 아니다.

## ke_r21 정정 — 부분 수용

보존 원문에서 **A의 E7g 탈취 peer_lost가 의도된 종료**이고 E7e first frame 2423/2457 ms가 통과한 것을 직접 확인했다. 따라서 ke-r23의 “선택 뒤 첫 frame 0”과 r21을 같은 실패라고 단정한 이전 표현은 좁힌다.

하지만 아래 자료도 남아 있다.

- `ke_r21/host_b/directoryhost_host.log:2356`: **08:32:27.249**, epoch 5, reason=peer-lost.
- `ke_r21/avd/steps.txt`는 E7e B first frame PASS 뒤 메뉴 미도달 3회와 08:33:42 종료를 기록한다.
- `ke_r21/avd_stdout.txt`에는 **to_list('E7e B')**의 traceback이 있다. 29/29는 그때까지 수행한 assertion의 집계이며 전체 계획의 정상 완료와 다르다.

따라서 **“r21의 peer_lost는 전부 E7g 정상 탈취뿐”으로 정정하지 않는다.** B의 후속 종료가 정상 park/end였는지 별도 실패였는지는 해당 행동/세션을 대조해야 한다. 앱 _cur.log의 마지막 snapshot에 B 종료가 없다는 것이 Host 원문을 없애지는 않는다. 두 사건의 동일 원인도 확정하지 않는다. 보고의 r21 문구를 이 범위로 바로잡는다.

## ① 제품 수정 — R2 우선, Android 한정 계약

별도 구현 task/branch로 관리하는 것은 허용한다. 다만 **분리는 출하 gate 면제가 아니다.** 현재 APK 0.2.33은 필수 ordinary 선택→첫 영상이 실패했으므로, 수정·합의된 대응과 그 실제 검증 전까지 해당 APK 출하는 보류한다. 독립적인 PC/서버 작업 전체를 이 한 건으로 멈추지는 않는다.

수정의 목표는 **수신/제어 관련 thread가 decoder의 느린 lifecycle·buffer 처리를 수행하거나 그 완료를 기다리지 않는 것**이다. codec create만 lock 밖으로 빼거나 frame callback의 lock만 바꾸는 것으로 전체 계약을 충족했다고 하지 않는다. RX가 lock을 얻은 뒤 스스로 EnsureCodec/PumpCodec를 오래 수행하는 경로와 keyframe/selection/reset hook도 함께 다룬다.

권장되는 한정 구조는 **Android sink 쪽 codec 소유 worker**다. 기존 output pump 재사용을 우선 검토할 수 있다. UI의 surface/lifecycle는 짧은 request·세대 무효화로, RX는 bounded frame 전달로, control 조회는 짧은 별도 metadata/snapshot으로 분리한다. 실제 모든 API에 별도 thread가 필수라는 규정 주장이 아니라 이 code의 소유·진행성 해결안이다. configure→start→buffer 사용 순서 등 API lifecycle는 유지한다. [NDK MediaCodec API](https://developer.android.com/ndk/reference/group/media).

필수 조건:

1. **codec API와 긴 wait를 공유하는 mutex를 RX/control/UI 진입 경로가 잡지 않는다.** codec pointer를 단순히 unlock한 채 공유하지 않고 한 소유 경계에서 serialize한다. callback 호출·Stop/join도 반대쪽의 긴 lock 아래 두지 않는다.
2. frame/command queue는 count와 bytes가 제한돼야 한다. 가득 차거나 surface가 준비되지 않으면 기존 generation/CSD/IDR 정책에 맞게 보류/폐기하며, codec가 받을 때까지 producer를 기다리게 하지 않는다. IDR의 빚은 enqueue만으로 갚지 않고 실제 codec input queue 성공에서 처리한다.
3. binding/owner/session·selection stream generation·surface 세대를 request/frame/output에 연결한다. logout/다른 PC/새 surface/선택 취소는 즉시 옛 output의 게시를 막고, 늦은 codec 생성 결과가 새 owner에 붙지 않게 한다.
4. ANativeWindow/JNI 참조와 codec의 수명은 비동기 소유에 맞게 보존·정리한다. 해제된 surface나 객체를 worker가 쓰거나, detached thread가 파괴된 sink를 쓰지 않게 한다. 느린 작업 취소와 실제 자원 해제 완료는 구분한다.
5. decode stats뿐 아니라 **control 경로에서 부르는 모든 sink hook**(keyframe 요청/owed 상태, selection result, reset 등)을 점검한다. 이미 lock 없이 publish하는 decode snapshot 방식도 참고하되 새 owner/세대가 일관된 copy여야 한다.
6. codec 실패는 영상 실패/준비 상태로 명확히 보고하고 control session의 정상 진행과 구분한다. 기존 peer-loss/retransmit 예산을 유지한 상태에서 고친다. standby에 window/decoder를 계속 유지하는 방식으로 우회하지 않는다.

이 계약 안의 Android 한정 구현·자체 대조를 기존 작업용1에 연결한다. 공통 session/PC의 수신 모델까지 바꾸거나 새 큰 lifecycle 정책이 필요하면 그 실제 확대 범위를 먼저 조율한다. 고정 Source와 영향 시험을 묶어 검토하며 매 단계 같은 설계를 재상신할 필요는 없다.

## ② 시험과 공용 emulator 재시작

**현재 boot를 보존한 SH-off E2 1회는 진단 목적으로 허용한다.** 실행 전 하네스가 실제 SH 경로를 끄는지 확인하고 effective 상태를 남긴다. AUTO_PREPARE off APK나 sharpness strength 0을 곧바로 SH pipeline off라고 부르지 않는다. E1 준비/E2 결과까지만 보고하고 나머지를 자동 성공으로 합산하지 않는다. 기존 guard/job/slot·artifact/script hash·실 clipboard 미검증 조건을 유지한다. 동작을 바꾸는 임시 product patch로 비교 arm을 만들지 않는다.

부하 조건은 boot/uptime·app/emulator 버전·각 관찰 구간 길이·1초 이상 dispatch 수와 EGL 지연 등으로 남긴다. 서로 다른 길이의 logcat에서 raw 발생 개수만 비교하지 않는다. 같은 boot여도 시간에 따른 부하가 같다고 가정하지 않는다. SH-off 성공은 trigger 분리에 도움을 주지만 liveness 수정의 대체 증거는 아니다.

**해당 emulator가 검증용이 관리하는 시험 자원임을 확인하고 유휴·slot 독점이 확보되면 정상 재시작을 허용한다.** 설치된 사용자 Host/Client나 물리 기기의 재시작 허가는 아니다. 기존 run 원문과 boot/설정을 먼저 기록하고, 해당 instance만 정상 stop/start한다. 같은 userdata/AVD 설정을 유지하고 wipe·AVD/캐시/snapshot 삭제·다른 instance 일괄 kill은 하지 않는다. 필요하면 snapshot을 삭제하지 않는 명시 cold-boot 옵션을 기록해 사용한다.

진단 순서는 **현재 boot SH-off 대조 → 필요 시 유휴 재시작 → 새 boot의 같은 기본 SH 경로 E2 1회**로 제한해 비교할 수 있다. 재시작을 원인 수정이라고 부르지 않고 run/환경을 분리한다. 아직 다른 작업이 사용 중이거나 시험 자원 identity/소유가 불명하면 그 실행 조건이 확보될 때까지 기다린다. 사용자에게 같은 시험 자원의 정상 재시작을 다시 물을 필요는 없다.

스크립트 subset/SH 설정을 바꾸면 새 snapshot/hash와 단일 호출 계획을 연결한다. OS 효과·job/파일/기기 경계를 바꾸는 변경은 기존 SOURCE 확인 경로를 따른다. 큰 전체 AVD suite를 지금 반복하는 승인은 아니다.

## ③ 느린 codec 결정적 회귀 — 필수

실제 vendor codec를 고장 내거나 PC/AVD 전체를 과부하로 만드는 대신, **codec adapter/소유 경계의 제어 가능한 barrier**를 시험 build에서 사용한다. 먼저 Host/장치 없는 component로, 그 뒤 필요한 Android 배선으로 확인한다. 생산 수신/control code와 새 scheduling code를 쓰며 별도 모델만의 PASS로 끝내지 않는다.

- codec create/configure/start를 **기존 retransmit 예산보다 긴 구간(예: 8초 또는 제어된 가상 시간)** 붙잡아도 control 송수신·ACK/probe가 계속 진행하고 peer_lost가 없어야 한다. 고정된 최소 시간과 healthy control 대조를 기록한다.
- frame producer와 control의 sink hook이 그 barrier를 기다리지 않는 것을 직접 대조한다. 기존 동기 의존 또는 mutant로는 이 대조가 실패해야 한다.
- barrier 해제 뒤 맞는 surface/owner/generation에서 새 IDR로 복귀하고 fresh frame이 나온다. fake codec 동안 프레임을 받았다는 이유만으로 영상 복귀로 세지 않는다.
- 작업 중 surface 교체·없어짐·logout/owner 교체·selection 취소, queue 포화, codec 실패 및 teardown을 확인한다. 오래된 frame/생성 결과가 새 PC에 보이지 않고 자원/queue가 누적되지 않아야 한다.

seam은 기존 시험 build 규칙에 한정하고 출하물에서 marker/활성화 경로가 없어야 한다. 시험 지연을 일반 제품의 runtime 옵션으로 노출하지 않는다. 실제 Android/NDK 배선 및 4 ABI·해당 첫 영상/SH on-off·surface 전환·KA 복귀 영향은 source 뒤 독립 EVIDENCE에서 확인한다.

## 다음 상태

검증용은 **제품 liveness 수정 task + 결정적 대조 + 한정 E2 환경 비교**를 한 유효 지시서로 연결한다. 핵심 결정은 위와 같고, 다음 보고는 고정 구현/계측 근거와 남은 불명 구간이다. r21 정정과 relay 배제 표현도 같은 정본에서 바로잡는다. ke-r23 실패·기존 안전 관측과 다른 승인 하네스 결과는 보존한다.

이번 Codex는 보고·보존 원문 일부·고정 source/공식 API 문서 읽기와 이 판정 기록만 수행했다. 제품 수정·build·Host/AVD/adb 실행·emulator 재시작·설치·운영·삭제·commit/push는 없다. 사용자 현재 조치 없음.

후속 보충(보고 66120345…)에 따른 (a)/(b) 구분과 시험 계측의 확정 범위는 `.claude/codex_apk_decoder_liveness_design_r2.md`를 따른다. R2 우선/해당 APK 출하 보류·시험 자원 허용의 큰 결정은 유지된다.
