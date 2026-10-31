# apk-0232 / ① 공용 세션 로그인 전 선택 SOURCE r1

- 판정: **NEEDS_CHANGES — N1·N2 한 묶음 보수**.
- 고정 후보: `fix/apk-prelogin-select` **e8123e4aa2bfa815b7f1d0ebe8dab75433432e10**. 제품 검토는 **5970f046f56dd8f55b98b4ab971ed7376c6d5848..627acdb6483442899c29ea282591b0401663bae8** 7파일. 시험 `c43effd` 및 `.claude/report_apk_prelogin_select_r1.md` 전문도 대조했다. 확인 시 후보 worktree HEAD 일치.
- 검토 방식: 고정 소스·호출 경로·시험 코드 읽기. 아래는 소스에서 가능한 순서의 결함이며, Codex가 APK/MediaCodec에서 실행 재현한 결과는 아니다. 38/38·실 Host 37/37·단위 249/0·빌드는 **작업용 보고 인용**이다.

## N1 (P2) 옛 local 전환이 새 선택의 완료 추적 tag를 덮는다

위치: `native_video_client_session.cpp:289-294` (`ResolveUnsendableSelect`), `:267-271` (알려진 역할의 직접 선택), `:315-327` (poll).

`BeginLocalSelectionFor(A)`는 sink 잠금 안에서 A를 검사하지만, 그 뒤 panel 전환과 `localSelectTag_.store(A)`는 같은 트랜잭션이 아니다. 특히 `ConvertSelectToLocal(A)`의 false 반환을 무시한다.

가능한 순서:

1. 첫 pong 전 queue된 A를 worker가 꺼낸다. Prelogin 판정 뒤 sink의 `BeginLocalSelectionFor(A)`가 true로 끝난 곳에서 멈춘다.
2. UI가 A를 취소하고 B를 선택한다. Prepare(B) → 알려진 Prelogin의 직접 경로 → panel B·sink B local → `localSelectTag_=B`까지 끝난다. 뒤로 가기 후 재선택이므로 UI의 선택 중 버튼 비활성화로 배제되는 순서가 아니다.
3. A worker가 재개한다. panel은 B라 `ConvertSelectToLocal(A)`는 false인데도 `localSelectTag_=A`를 저장한다.
4. 다음 poll은 sink의 B와 추적 A가 다르므로 추적을 0으로 지운다. B의 IDR가 정상 결합되어도 B의 panel 완료는 더 이상 poll되지 않는다.

결과: B의 panel은 `desktop_select_local`에 머물고 자신의 stream generation을 보고하지 못한다. Kotlin은 여전히 REQUESTING이며, 특히 SH의 `sharpenOutputConfirmed(...lastSelectStreamGeneration)`에 이전/0 세대가 들어가 화면 전환 지연·6초 복귀로 이어질 수 있다. SH를 끄면 ready 경로로 VIEWER에 들어갈 수 있으므로 모든 설정에서 반드시 같은 timeout이라고 단정하지 않는다.

필요 보수: local 시작/queue→local 전환/추적 tag 게시를 현재 op와 직렬화하고, stale A가 B의 추적을 덮거나 지우지 못하게 한다. `Convert...` 반환 검사만 추가한 뒤 검사와 store 사이를 다시 열어 두는 것으로 끝내지 않는다. poll의 sink 현재성 확인과 panel 완료도 취소·새 Prepare와의 최종 소유 경계를 확인한다. 네트워크 대기를 이 잠금 안에 넣을 필요는 없다.

제한 반례: A의 sink local 시작 직후 latch → 취소+B 시작/추적 게시 → A 재개 → B 자신의 IDR → panel B의 ACKED·gen 및 실제 APK 화면 전환. 취소만 한 경우 A의 뒤늦은 완료도 표시하지 않는다. 수정 전 실패를 남긴다.

## N2 (P2) 미결합 중 보류된 IDR가 다른 세대 결합 뒤 gate를 우회해 codec에 들어간다

위치: `selection_ack_gate.hpp:147-155` (미결합 local IDR마다 candidate 변경), `android_video_decoder.cpp:176-249` (입력·보류), `:701-710` (보류 재전달), `:741-789` (실제 queue), `:1105` (출력 세대 표기).

새 local 모드에서는 결합 전 여러 세대의 IDR가 Admit를 통과할 수 있다. 그런데 `pendingFrame_` 재전달은 gate를 재검사하지 않는다. 새 현재 프레임의 queue가 성공해도 기존 pending을 없애지 않는다.

가능한 순서(같은 크기·같은 SPS/PPS라 codec 재생성 없음):

1. local A가 미결합이다. IDR G가 Admit를 통과하지만 input buffer를 못 얻어 `pendingFrame_=G`가 된다.
2. 다음 IDR H가 들어온다. 함수 첫 Pump에서 G 재시도는 여전히 실패한다.
3. H는 미결합 gate를 통과한다(candidate H). 그 사이 input buffer가 풀려 H queue는 성공한다. `KeyDelivered(H)`로 expected=H가 된다. G pending은 그대로다.
4. 함수 끝 또는 다음 Pump에서 G를 gate 확인 없이 queue한다. 이제 expected=H인데 G 바이트가 실제 decoder에 들어간다. `KeyDelivered(G)`가 expected를 바꾸지 않는 것은 **이미 queue한 G를 차단한 것**이 아니다.

출력 쪽은 `gate_.expected()`를 출력 세대로 표기하므로 이 G 출력이 H로 취급될 수도 있다. 새 local 모드의 “결합 뒤 그 세대만 전달” 계약이 깨진다. 상위 `SessionVideoPipeline`은 완전한 IDR가 여는 시퀀스 틈에는 discontinuity reset을 하지 않으므로, 세대가 달라지면 항상 codec reset되어 이 순서가 불가능하다는 방어도 없다. 실제 기기 발생 빈도·화면 현상은 미측정이다.

필요 보수: 보류/재전달/실제 input queue 시점까지 selection 및 generation 허가를 보존하거나 다시 검사하고, 새 결합과 충돌하는 보류 프레임을 버린다. candidate 하나를 마지막 수신 IDR로 덮는 방식이 보류 프레임의 권한을 대표하지 않도록 한다. 버린 IDR의 owed 처리는 다른 유효 candidate/이미 결합한 세대를 잘못 지우지 않아야 한다. 이 Android 경로는 sink 잠금 안에서 처리되므로 PC의 FrameBuffer ticket 구현을 그대로 복제하라는 요구는 아니다.

제한 반례: 실제 Android 입력 queue 경계에 한정한 test seam 등으로 G 신규 실패 → G 재시도 실패 → H 신규 성공 → G 재시도 가능 순서를 고정한다. H 결합 뒤 G가 실제 queue/출력되지 않고, 자신의 H 출력으로만 ready가 되는지 확인한다. 단순 gate `Feed()`에서 Admit 즉시 KeyDelivered/ready를 호출하는 현재 하네스는 이 경로를 검증하지 못한다. 수정 전 실패를 남긴다.

## 수용한 부분과 다음 단계

- known `PreloginV1` + desktop 0만 local, UAC/LockCred만 있는 경우 기존 ack 유지, allowlist 불변, WindowSelect 위조 응답 없음은 방향이 맞다.
- 첫 pong 전 요청은 panel queue에 남아 송신 지점에서 같은 action tag로 결정한다. PC의 별도 deferred 등록 방식과 달라 그 lost-wake 보수를 그대로 복제할 필요는 없다. scheduler의 최초 Ping 우선 경로도 확인했다.
- sink는 Prepare/Abort/프레임 처리/실 queue를 같은 잠금으로 보호한다. PC의 별도 decode→FrameBuffer write 틈과는 다르다. 다만 N2의 pending 재전달은 별도로 닫아야 한다.
- `OnAck`는 local을 완료시키지 않고, 옛 요청은 action의 원래 tag로 전달된다. N1 보수에서 panel과 추적 게시의 현재성도 유지한다.
- 보고서가 MediaCodec 없는 시험과 실제 APK UI 미실행을 분리한 것은 타당하다. AVD 실 APK의 로그인 전 바탕화면 클릭 → 같은 연결에서 실제 새 출력·화면 표시, 취소/재선택, SH on/off, 일반/UAC ack 대조는 0.2.32 통합 EVIDENCE에서 필요하다. 실 재부팅/Winlogon·실폰 성공으로 확대하지 않는다.
- 레거시 `ControlMonitorSelect` 자체의 재설계는 이 보수에 추가하지 않는다. 다만 통합 시험에서 로그인 전 실제 APK에 선택 가능한 모니터 항목이 생기면, 그 행동의 조용한 폐기는 범위 밖이라는 이유로 출하하지 않는다.

기존 APK 0.2.32 작업의 같은 담당에게 N1·N2를 한 묶음으로 보수하고 해당 diff만 SOURCE 재확인한다. 닫힌 PC 0.2.163 리뷰·게시를 다시 열지 않는다. SH r2·UA-2 독립 작업은 진행 가능하되 최종 APK 후보는 이 보수를 포함해 고정한다. 그 뒤 검증용 독립 AVD·gate·산출물 검사 → Codex EVIDENCE → 기존 승인된 서명/게시/push 순서다.

Codex 제품 편집·빌드·테스트 실행·커밋·push 없음. 이 판정서는 미추적 운영 자료이며, 공개 history/계획/Conventional Commit은 같은 구현 작업에 취합한다. 사용자 현재 조치 없음.
