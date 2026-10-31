# rel-0163 r7 — prelogin picker SOURCE 최종 재확인

2026-10-26 / 검증용 remote#yj8mrmrx / 기존 t-bpwzezfg / 제품 **eefac097791bfc0cbfefff60aba2b89d41cbdf38** / diff **5ce3ce7..eefac09** / 시험 **0854ac49a7cba1f9ce8a4055b2f9813fc41fb6dd** 참고.

**SOURCE OK. R3 token 취득의 마지막 지적을 닫고, 앞서 닫은 R1/R2/R4·write 잠금·revealedOp를 유지한다. picker 수정의 추가 SOURCE 보수 요청은 없다.** 요청 제품·시험 diff와 receiver 최종 채택/reveal 호출부를 읽고 tracked clean/whitespace rc0를 확인했다. Codex의 새 빌드·행동 실행은 없다.

## 마지막 지적 확인

- TakeFrameTicket이 gate mutex 아래 epoch와 pending/local mode를 한 snapshot으로 취득한다. receiver는 이 ticket을 **EpochChanged 대체/decoder reset보다 먼저** 받고, ticket.epoch로 decoder의 선택 epoch를 갱신한다.
- frameOp와 frameLocal은 ticket의 값으로 고정된다. 이전 dec epoch+나중 IsLiveLocal 조회를 조합하지 않으므로 취소된 local A를 ordinary로 재분류하는 경로가 제거됐다.
- 최종 CommitFrame에 같은 op/mode가 전달된다. local A가 취소/교체됐으면 기존 검증이 거부하고, ordinary ticket 뒤 새 local B가 시작된 경우도 혼합 채택을 거부한다. 정상 ordinary 스트림의 기존 경로는 유지한다.
- 유효 output 이후 generation bind·FrameBuffer write는 기존 r6의 FrameBuffer→gate 순서와 넘겨받은 unique_lock 아래 수행한다. 그 채택이 반환한 generation과 원 frameOp를 reveal에 전달한다. 실제 성공 revealedOp의 후속 frame 구분도 유지한다.
- 새 ticket 취득은 짧은 gate snapshot이며 FrameBuffer를 잡지 않는다. r6에서 확인한 lock 순서를 뒤집는 변경은 없다. 새 testAfterFrameTicket은 제품 기본 empty이며 runtime/env 연결0을 최종 출하 source 감사에 포함한다.

## 시험 근거의 귀속

추가 gate 단위는 local A ticket→취소/B→A 거부, ordinary ticket→새 local B 혼합 거부, B 자기 ticket의 정상 채택을 구분한다. E1 (6)은 ticket 직후 latch로 A 취소/B 시작을 고정해 첫 frame 미기록과 B 정상 완료를 판정하도록 추가됐다. 원인 위치를 겨냥한 대조 방향을 수용한다.

**PS39/39(version83→83·B 완료), UA28/28, M6(r6 모양)에서 version83→85 FAIL, M7 gate FAIL은 작업용 실행 보고 인용**이다. Codex/검증용 독립 PASS나 실 UAC 성공으로 바꾸지 않는다. 같은 SOURCE 판정을 위해 PS/UA/LK 전량을 다시 반복할 요청은 없다.

## 다음 담당·릴리스 경계

검증용은 기존 **t-bpwzezfg**를 이어 **eefac09의 제품 +0854ac4 시험**을 포함한 최종 고정 후보로 재빌드·재 dry-run1회하고 새 payload/manifest/SUMS·버전·gate/출하 define/hook 감사·필요 smoke를 취합한다. 새 SOURCE를 위해 별도 추상 go를 기다리지 않는다. 이전90192f4와 중간 dry-run의 hash는 새 후보 근거로 재사용하지 않는다. 버전은 미게시0.2.163을 유지할 수 있다.

검증용은 실제 picker 클릭/첫 pong 전후 intent·정지 첫 present/갱신·반복 보기/재부착·같은 연결 UA 왕복의 **제품 renderer E1**을 고정 후보에서 독립 확인하고, 원본·IDR·present/스크린샷을 연결한다. 앞선 후보에서 독립 확인한 LK 등은 product diff/영향 범위에 맞춰 필요한 smoke만 취합한다. 새로운 시험 hook 전체의 출하 runtime 호출0과 시험 define/표식0은 단순 ForTest 문자열0을 대신해 source/compile까지 대조한다.

이 SOURCE OK로 과거 EVIDENCE 보류를 자동 해제하지 않는다. 새 산출물/독립 E1을 Codex에 한 packet으로 주면 EVIDENCE 판정 후 기존 사용자 승인에 따라 서명·gnlink 배포·외부 signature/version/11hash/Android pair 불변→main/docs 일반push·remote hash→dist/최종 보고로 잇는다. PC0.2.163 먼저/APK0.2.32 별도, KA/NP는 R2이며 미포함이다. APK shared selection 보수는 기존 APK 단계에서 실제 UI ready까지 검증한다.

실 SYSTEM/Winlogon·실 UAC 예/아니오 픽셀·실 LogonUI 틀린 비밀번호 후 재시도·회사 PC/폰은 계속 별도 미검증이다. 사용자 설치·UAC 승인·서비스 변경·재부팅은 자동 수행하지 않는다. 지금 사용자 조치는 없다.

Codex 산출물은 미추적 판정서·인계뿐이고 제품/공개 문서 편집·빌드·설치·commit/push 없음. 구현 history/계획checkbox/Git MCP 커밋은 기존 task/릴리스 기록에 취합한다.
