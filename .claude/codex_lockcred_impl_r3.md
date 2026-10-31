# lockscreen-cred r3 — Codex SOURCE/EVIDENCE 검토

2026-10-22 / task **t-rs9nhsdv** / 후보 **53506ab445aa6e5cab7a4c08c50ec05886c1ccbe** (제품e6e49d8, 단위5dfba94, e2e b6ff40a, 문서53506ab).

**NEEDS_CHANGES — 잔여 두 항목만 보수한다.** L2-R 원래 expiry와 L1-R 늦은 cache 재생성은 닫는다. Busy16의 원인 구분·무입력·보유 요청 무간섭·수동 재시도 UI도 독립 근거를 확보했다. 다만 같은 sealed 요청 재전송이 typed 보유 마감을 다시 늘리는 경로와 Busy 문구의 최대 시간 표기가 남는다. 전량 재검사나 새 task 대신 아래 R1/R2를 기존 task에 취합한다.

## 확인한 근거

- 작업용 r3 보고와 검증용 `C:/work/gnlink-wt/verify-lockcred/.verify/verify_lockcred_r3.md`, r2→r3 제품/시험 diff 및 관련 원문을 읽었다. HEAD·tracked clean·CR-at-EOL 허용 diff-check 확인.
- 독립 build-vs hash 직접 계산/일치: InputService `dcd98c971ea89782c278df85b361199f214d5357301654155a011bf5cd59decc`, Stream `a254a976bcd91db5f2022fefb54fdba3fc167fc164e863d8d3281bee3eefeb41`, Viewer `faae692b483752a4212034d9bf5712808e79da23e1e3ee3f83a9baf181adf6b5`.
- `.verify/r3/*.out` 실제 요약 줄에서 relay37×3, lockcred54, service12, channel15, target21, refusal4, policy17/17, prelogin host43, seal/menu/pipe PASS, gate9/self19 및 seam 출하0/시험6·4를 확인했다. 실행rc와 환경은 검증용 귀속이며 Codex 재실행/빌드가 아니다.
- 독립 UI lock-screen44×2 / prelogin79 / not-prelogin28 원문을 확인했다. 실제 시험 파이프의 Host A pending(stage3) → viewer Busy16 → 칸 untouched·A 무간섭 → A 연결 종료 → 수동 재시도 typed8·바이트 교체 → stage9 순서를 대조했다. Busy/일반 오류 PNG 두 장을 직접 열었다.
- 일반 오류 창의 PostMessage 주입은 **문구 분기 대조**이며 relay 일반 오류 생성 증거는 relay 시험(없는 pipe/무응답→InternalError12)에 있다. sealed Busy는 relay process 시험 + 공통 결과 문구 코드로 확인한 범위다. 이를 실제 두 PC/OS 실기로 합성하지 않는다. 이 범위 차이만을 이유로 새 UI 전량 시험을 요구하지 않는다.

## 닫는 항목

| r2 항목 | r3 판정 |
|---|---|
| L2-R | **OK.** `min(original expiresMs, now+20s)`와 기존 만료 경계 유지, 만료 뒤 Ready 무입력/만료 전 대조 확인 |
| L1-R | **OK.** challenge 시 txGen 부여, 완료 시 같은 live transaction 확인, EndSession 제거. challenge/status 취소 후 잔존0·다른 세션 보존 근거 확인. 영구 tombstone 없음 |
| L5-R 큐/교환 | 큐와 무관한 절대 마감 검사 및 남은 exchange budget, 종료 결과/재연결 status 금지 보완 확인. **동일 sealed 재전송의 마감 재설정은 R1 잔여** |
| UX-R | Busy16·purpose 유지·실제 pipe busy와 일반 오류 분리·공통 안내·수동 재시도 보완 확인. **최대 시간 문구만 R2** |

N2 단독 변이가 통과하고 N2b(두 방어 제거)가 실패했다는 보고는 방어 중복으로 설명된다. 단독 변이가 실패한 것으로 쓰지 않으며 이것만을 위해 시험을 추가하지 않는다. r2 고아 LifecycleTest2개는 검증용 시작 시 부재로 해소 기록을 수용한다.

## R1 — 같은 typed transaction의 sealed 재전송이 절대 마감을 다시 설정 (필수)

`host_unlock_relay.cpp:590~619`의 `SealedAsync`는 **terminal 결과만** 중복 전송을 생략한다. 같은 transaction이 이미 `Injected, terminal=0`인 동안에는 동일 sealed 요청을 다시 큐잉한다. 서비스 `secure_unlock_service.cpp:274~280`는 이를 재입력 없이 현재 상태로 응답하며, 잠긴 상태가120초 안이면 다시 Injected를 반환한다.

relay의 `host_unlock_relay.cpp:540~546`는 그 중복 응답에도 무조건 `hold_for(typedHoldMs_, ...)`를 호출하고, 284~290행은 `holdUntilMs = now + ms`로 마감을 새로 만든다. **이미 시작한 동일 typed 단계의130초가 중복 응답마다 다시 시작된다.**

구체 반례: t=0 첫 Injected → 마감130초. t=119초 동일 sealed 재전송 → 서비스는 아직 Injected 반환 → relay 마감249초. 그 뒤 요청/폴링이 끊기면 서비스는 `secure_input_service_main.cpp:863`의 ReadFile에서 기다리므로, service의120초 상태 판정이 자동으로 pipe를 반환해 주지도 않는다. 한 번의 재전송만으로 기존 transaction이130초를 넘게 점유한다. 이것은 소스에서 확인한 경로이며 실제 PC에서249초 시험을 실행했다는 뜻은 아니다.

**보수:** 첫 typed 진입에만 typed 마감을 정하고, **동일 cookie/requestId/txGen의 중복 sealed/Injected 응답은 기존 마감을 보존**한다. 새 transaction과 challenge→첫 typed의 정상 단계 전환은 구분한다. 중복을 서비스로 보내지 않고 현재 상태를 돌려주는 방식도 멱등/종결 의미를 유지한다면 가능하다. 어느 방식이든 deadline을 뒤로 미는 replay를 허용하지 않는다.

**최소 반례:** 기존 시험 hold2초로 첫 Injected → 약1초 뒤 같은 sealed 재전송 → 여전히 Injected → 최초2초 기준 pipe 반환/UnlockUnconfirmed·다음 Host 통과. 지연 응답/여러 duplicate와 기존 terminal 중복 대조를 포함하고, 마감 갱신 변이가 실패해야 한다. service 단위의 재입력0도 유지한다. 실제130초 UI 반복이나 무관 GPU 전량 시험은 필요 없다.

## R2 — “길어야 2분”을 보장할 수 없음 (필수, 작은 문구 보수)

`viewer_unlock.cpp:567~568`의 Busy 문구는 요청 종료/취소 뒤 재시도 안내로서는 적절하지만, **“길어야 2분”은 보장하지 않는다.** typed 단계 자체의 relay 상한이130초이고, Busy 시점이 challenge 단계면 남은 challenge/입력 처리 뒤 typed 단계로 넘어갈 수도 있다. R1도 현재 연장 경로다. 서비스 상태 대기120초와 상대가 Busy를 본 시점부터의 총 대기를 같은 수치로 안내하면 안 된다.

최소 보수는 숫자 약속을 삭제해 **“그 요청이 끝나거나 취소된 뒤 다시 누르세요.”**로 하는 것이다. 정확한 총 대기 상한을 새로 설계할 필요는 없다. 실제 elapsed 근거 없이 다른 최대 시간을 추정해 넣지 않는다. 관련 현장 묶음/최신 보고의 같은 문구도 맞춘다.

기존 결과 문구 함수의 Busy/일반 오류 분리와 연결된 독립 UI 한 회의 판독 가능 캡처로 확인하면 된다. 이미 검증한 전체 로그인 UI 모드를 이 작은 문구 변경 때문에 전량 반복하지 않는다. R1의 relay 독립 반례와 같은 고정 후보로 연결한다.

## 남은 범위/다음 담당

검증용이 **R1/R2 한 유효 보수 지시**를 기존 t-rs9nhsdv/작업용1로 연결하고 고정 후보의 relay 반례·영향 UI·출하 gate/해시를 확인한다. 통과한 L1/L2를 다시 전면 개편하거나 추가 병렬 지시를 만들지 않는다. 제품 보수 기록/계획 상태/Git MCP commit은 같은 task에 취합한다.

OS A~D(실제 Session0 SYSTEM→콘솔 child·agent 최종 차단·실 LogonUI 교체/Enter/해제·재부팅·UAC·APK), 실제 두 Host PC 간 Busy, S1 간헐은 계속 미검증/미해결이다. 새 APK sealed 대응/S5·버전 인상·서명·게시·사용자 설치·push 승인 없음. 최종 상태는 여전히 보수 중이며 일반 릴리스/실기 성공이 아니다.

Codex 변경은 미추적 검토서·인계 메모뿐이다. 제품 수정·빌드·실서비스/설치/UI 조작·commit·push 없음.
