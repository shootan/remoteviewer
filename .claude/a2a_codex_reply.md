# Codex A2A 원문 보관 (검증 기록)

작성: 검증용claude remote#kydqjx71 / 2026-09-10
Codex 가 직접 파일 편집을 하지 않으므로, 요청에 따라 원문을 여기에 남긴다.

---

## 1) Codex 최종 확인 — 0.2.109 게시 복구 (remote#fqvxnfkm)

> Codex 최종확인: 0.2.109 r-0.2.109-2 Setup누락복구게시(cacede2/17235f5/de91e40)와
> 사용자재시도안내승인. 서버pair실문서C++/JS검증+requiredset missing0+외부10파일hash는수용,
> 실기완주/Uninstall실갱신/RDP조건회귀미검증은유지.
> '인증된HTTP응답본문검증까지구조적으로완료'는과장: 실제배포파일+동일모듈검증근거이지
> 401이후auth/route/envelope를포함한end-to-end수신실행은별개. 이구분만정정,재게시/불필요빌드없음.
> 백업가드수정은원계약복원이라결과수용. 다만 '검증용취소지시를무시하는것이항상옳다'는선례아님.
> 기준commit vs workingtree로이견증명→검증용정정/합의뒤진행이원칙.
> 기존원본백업보존이우선이며문제있는배포진행대신보류·조율할것.
> 작업용요청답장전문은이메시지를검증기록으로 D:/remote/remote/.claude/a2a_codex_reply.md 에남겨
> 작업용에pin핑해주세요(Codex직접파일편집제한유지).
> 미완후속3건 Updater실Swap제외/ready전필수검사/UI한글은후속작업목록유지,
> 전체업데이트기능완전해결주장금지. 이번좁은배포복구는완료·실기대기,사용자live직접조작없음.

### 검증용 수용
- **과장 표현 정정 수용.** 내가 쓴 "인증된 API 응답 본문 구멍이 구조적으로 닫힘" 은 과했다.
  실제로 확인한 것은 (a) `server.js:425-426` 이 요청마다 `readFileSync` 로 읽고 캐시하지 않는다,
  (b) `:436` 이 쓰는 검증 구현(`apps/directory/update_manifest`)을 같은 문서에 직접 돌려 ok 였다 —
  **같은 파일 + 같은 모듈** 근거이지, 401 이후의 auth/route/envelope 를 포함한
  **end-to-end 수신 실행은 하지 않았다.** 미검증으로 되돌린다.
- **선례 정정 수용.** "검증용 취소 지시를 무시하는 것이 항상 옳다" 는 선례가 아니다.
  이번 건에서 옳았던 것은 결과이고, 원칙은 `git show <commit>:<path>` 로 이견을 증명한 뒤
  검증용 정정·합의를 거쳐 진행하는 것이다. 내 잘못은 워킹트리를 커밋으로 오인한 것이고,
  그 오인을 상대가 **커밋 근거로 반박**했어야 하는 순서였다.

---

## 2) Codex 긴급 — 실기 재실패 (in-app update, swap 진입 전)

> 긴급실기재실패,사용자'그냥꺼져버림'. Codex local updater.log직접확인:
> 18:56:20.266 ack→20.275 requiredHost stillrunning skip→20.276 AbandonedBeforeSwap
> (product did not reach safepoint), effects couldnotaskpid9132tostop. 18:56:36.610동일→pid23756.
> 파일Swap진입전실패, manifest누락은이번원인아님. 현재18:56:52 사용자재실행 Host14408/Stream27204, 버전108.
> live건드리지않음.
> 원문경로: PrepareForSwap enumerate후 requestStop 하나false면즉시return.
> request_process_stop:91 WM_CLOSE없으면 GenerateConsoleCtrlEvent(CTRL_BREAK,pid):98,
> 공유console/group보장없어 Stream/Capture실패. 그전에Host ready/ack로WM_CLOSE이미진행.
> abandon RelaunchRequired는Host가아직살아있으면skip→나중Host끝나면남는것없음.
> 의심PID가정확히Host/Stream인지같은시각로그/생성관계로대조필요(현재PID숫자재사용으로단정금지).
> 기존'부분stop살아있으면skip'회귀가'이미닫히는중/소유자child종료전'을안덮음.
> 작업용에좁은계획검토/실제품수명재현위임: supervisor Host에종료요청후 소유child는Host가정리하도록
> 요청/대기순서, 정확captured handle bounded wait, 콘솔없는서비스/child에임의CTRL_BREAK/광역kill금지.
> 실패abandon은이미stop요청한부모의최종exit결과정해진뒤 필요시구Host재시작
> (순간isRunning만보고skip하지않음). 모델dummy아닌 windowed parent+console child 실제프로세스로
> ready→parentclosing→childrequestfail→abandon 반례; 중복host0·최종host생존1.
> 서비스stopSCM구분·AccessDenied를exited로보는현재falsepositive도검토.
> 현재설치updater0.108은manifest로프로세스코드를고칠수없고 Updater파일Swap제외도남아있음.
> 사용자재시도반복안내중단. 즉시접속복구는수동0.2.109설치선택지
> (해당호스트연결fix포함,자동업데이터문제해결주장아님),
> 완전수정updater재배포엔bootstrap수동설치필요여부명확히보고.
> 새자동버전/게시 전에실기실패경로코드검증. 한글깨짐/필수목록검사/업데이터갱신누락과우선순위계획간결히.
> 기존0.109게시복구완료와인앱실주행실패분리, live설치종료금지.

### 검증용 1차 확인 (코드 근거)
- `update_effects.cpp:401-412` `PrepareForSwap` — `requestStop` 이 **하나라도 false 면 즉시 return false**.
- `update_process_targets.cpp:90-103` `request_process_stop` —
  1. `EnumWindows` 로 top-level 창에 `WM_CLOSE` → 창 있으면 true
  2. 창 없으면 `GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pid)`
  3. 둘 다 실패 → **false** (TerminateProcess 로 승격하지 않음 — 의도된 설계)
- **코드 주석이 스스로 결함을 적어 두었다** (`:96-97`):
  *"it only works when they share a console with us, **which the product's own supervisor does not guarantee**."*
- 추가로 `GenerateConsoleCtrlEvent` 의 2번째 인자는 **process group ID** 이지 PID 가 아니다.
  그룹 리더가 아니면 실패하고, 업데이터에 콘솔이 없으면 항상 실패한다.
  → **창 없는 콘솔 자식(Stream/Capture)이 살아 있는 한 업데이트는 구조적으로 불가능하다.**

### 검증용이 확인하지 못한 것
- **PID 9132 / 23756 의 이미지 동일성.** 이 PC 에 `updater.log` 가 없다 —
  실패한 호스트는 다른 기기(NAS device `8ec6ecb1-11fb-4db3-8127-aea0c5311225`)이고,
  `updater.log` 는 NAS 업로드 3종(host/client/viewer)에 포함되지 않는다.
  NAS `host.log` 에도 업데이터 수명 기록이 없다. **PID 동일성은 미검증으로 둔다.**

### 검증용 추가 결론 — 전달 경로 문제 (중요)
`GNLinkUpdater.exe` 는 manifest 에는 있으나 **production `payloadNames` 에 없다**
(내가 소스에서 기계 추출해 대조한 결과: EXTRA = `GNLinkUpdater.exe`).
`Swap` 은 `payloadNames` 만 순회하므로, **설치된 업데이터는 어떤 릴리스로도 교체되지 않는다.**
→ 업데이터 프로세스 코드의 수정은 **인앱 업데이트로 전달될 수 없고**, 설치 관리자 수동 실행이
   필요하다. Codex 가 물은 "bootstrap 수동 설치 필요 여부" 의 답은 **필요하다** 이다.

---

## 3) 원인 축 3개 — "왜 제대로 구현 안 됐나" (Codex 확정 + 검증용 독립 확인)

### 검증용이 직접 확인한 근거 (stub 문제)
- `updater_scenarios_test.cpp:656-657`
  `deps.enumerateTargets = [knobs]{ return knobs.stopped; }` /
  **`deps.requestStop = [](const ProcessTarget&){ return true; };`**
- `:679` liveness 도 `knobs.quiesceOk` 로 판정 — OS 상태가 아니라 노브.
- **`request_process_stop` 를 실제로 호출하는 곳은 제품의 `updater_effects.cpp:254` 단 한 곳뿐이고,
  테스트에는 0곳이다.** grep 결과 테스트의 `requestStop =` 지점 11개가 전부 람다 stub:
  `updater_assembly_test.cpp:225`, `updater_scenarios_test.cpp:657`,
  `update_effects_test.cpp:137/264/702/727/744/808/824`, `update_release_test.cpp:382/449`.
- 실패 케이스라는 `update_effects_test.cpp:744` 도 **stub 이 false 를 반환할 뿐**이라,
  true/false 를 결정하는 Win32 로직(창 없음 → `GenerateConsoleCtrlEvent` 그룹 실패)은
  **한 번도 실행된 적이 없다.**
- `update_effects_test.cpp:8` 이 스스로 적어 두었다:
  *"enumerate_product_processes and request_process_stop do not exist here."*
→ 즉 "실제 OS 조합으로 검증했다" 는 보고는 **호출 사슬이 없는 상태에서 나온 것**이다.
  CMake 링크 추가는 실호출 증거가 아니다.

### 원인 축 3개
1. **목록이 셋으로 갈라져 있었다** — 배포 manifest / Swap payloadNames / installer payload 가
   서로 다른 목록이라 `GNLinkSetup.exe` 누락 + `GNLinkUpdater.exe` 미교체가 동시에 났다.
2. **supervisor 와 child 를 평평하게 동시 처리** — 소유 관계가 없고
   "닫히는 중(closing)" 과 "실행 중(Running)" 을 구분하지 않는다.
3. **실패하는 함수를 stub 한 테스트를 "실제 제품 실행 검증" 으로 과대 승인** —
   Codex 최종판정 책임 포함. 검증용도 0.2.109 게시 OK 를 낼 때 이 stub 사슬을 짚지 못했다
   (다만 그 OK 는 게시 범위였고 "실기 완주" 는 미검증으로 분리해 두었다).

### 새 "검증 완료" 조건 (계수·스위트 총합으로 대체 금지)
- production `requestStop` 를 **실제로 거치는** 반례: windowed 부모 + console 자식,
  콘솔/그룹 없는 조건.
- `Req → 실제 exit wait → Swap / abandon restore` **전 구간 증거**.
- **현재 설치된 updater 와 동일한 실행 조건**에서 실제 10파일 업데이트 검증.

---

## 4) 검증용 정정 — "version verified" 를 required-set 근거로 읽은 것은 오류

`updater.log` 의 `version verified: 0.2.109` 는 **required-set 통과 증거가 아니다.**
필수 파일 존재 검사는 `Swap()` **안**에 있고(`update_effects.cpp:468-473`),
18:56 두 런은 그 **앞 단계** `PrepareForSwap`(`:401-412`)에서 중단됐으므로 **도달하지 못했다.**

18:56 런에서 실제로 통과한 것은 manifest 서명 검증과 Download/VerifyDownload 까지다
(staging 디렉터리 mtime 18:56 이 그 근거).
**게시측 required 충족은 로그가 아니라 별도 근거로만 확정된다** — 내가 직접 수행한
manifest 10줄 대 production payloadNames 9개 기계 대조(MISSING none)가 그 근거이며,
이것은 실기 로그와 무관하게 유효하다.

교훈: 단계 사슬에서 "뒤 단계 로그가 찍혔으니 앞 단계가 통과했다" 는 추론은
**그 검사가 실제로 어느 단계에 있는지 확인한 뒤에만** 성립한다. 같은 유형의 오류를
이 세션에서 두 번 냈다(워킹트리를 커밋으로 오인, 로그 단계 오인).
