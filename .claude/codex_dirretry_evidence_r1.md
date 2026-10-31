# directory-retry r2 EVIDENCE

2026-10-28 / Codex remote#tt7fcvky → 검증용 remote#7fsfy22z.

**제한 유지보수 범위 EVIDENCE OK — 34f22de의 진단 계약·완료 stage 기반 검사·안전한 시험 cache 경계를 수용한다.** 포트 확보 간헐의 보편적 해소나 실제 운영 directory/필드·R2 통합/게시 완료를 선언하지 않는다.

## 고정점/직접 확인

- source **34f22defb536aedf48fe5143b5adc6d62d6987f1**, 검사 시 HEAD 일치·tracked clean. SOURCE 정본 `codex_dirretry_src_r2.md` 유지.
- 독립 `C:/work/gnlink-wt/dirretry-r2/build-verify`, Release. configure/build 원문은 해당 소스 경로에서 실제4개 target을 compile/link한 기록이다. 작업용 빌드와 분리돼 있다.
- Codex가 현재 파일 SHA-256을 직접 계산해 인계 prefix와 대조했다.

| 실행 파일 | SHA-256 |
|---|---|
| remote60_directory_retry_test.exe |61ecf9412f2baeefe2f0658b3b1f0ea56ad215b1f8e27cc27eecbe3599e01cd3 |
| remote60_directory_retry_mutant_timeout_only_test.exe |a5f56320b993d6d4121056016acac5dce96b2a470e6a6270729c5b6abf8a15a3 |
| remote60_directory_retry_mutant_no_legacy_attempt_test.exe |21500faf6e2c1cabf87745e7fd5db8ef2224958fab7a5b7d9063ce088c371aff |
| remote60_directory_retry_mutant_no_cycle_gate_test.exe |b0a762d95db33f619bde022433b7ecac1ac3747bf5ee30a1144abed7681bf877 |

위 값은 이번 확인 시점의 파일 hash다. 실행 전후 양쪽 hash를 Codex가 새로 확보했다는 의미는 아니다. 시험 실행은 **검증용 귀속**이며 Codex가 긴 suite를 재실행하지 않았다.

## 원문과 행동 결과

`verify_dirretry/VERIFY_DIRRETRY_34f22de.md`, configure/build, retry1~3 및 mutant3개 원문을 읽었다. 각 정상 회차는 **all retry checks passed(177 checks,0 failed)**로 끝난다.

- **inactive 단계**:17초 느린 observer에서도 cycle1의 heartbeat/register1/1, cycle2·3 skip, cycle4의2/1·같은 token online이 유지된다. stage 기록과 live count가 섞이던 Codex 재현 경계가 독립 검사에서도 닫혔다.
- **cached-silent**: HTTP 광고 없음과 legacy default의 실제 timeout을 함께 표시한다. cooldown의 동일한 이유/재송신, 이후 광고 설치→관찰·heartbeat·public 주소/online 회복을 fast/slow 모두 확인한다. gate가 timeout으로 풀려 진행한 pass도 없다.
- **pin/광고/HTTPS 구분**: 별도 timeout·미조준 이유를 유지한다. 기본 fallback을 금지해 상태를 안정화한 보수가 아니다.
- **실제 legacy-adjacent 성공**: health 실패/무광고 두 사례가 세 회차 모두 실제로 실행돼 관찰·heartbeat/게시 성공을 확인했다. fixture setup을 건너뛰어 PASS한 것이 아니다.
- **cache 경계**: 정상 원문이 `D:/remote/remote/.claude/test-tmp/verify-dirretry`를 명시한다. C: exe 옆 cache를 판정 근거로 사용한 결과가 아니다. SOURCE 때 직접 확인한 root 거부/내부 anchor 전제도 유지한다.

부정 대조:

| 변이 | 실제 실패/의미 |
|---|---|
| TIMEOUT_ONLY |2건. fast/slow의 “광고 없음+실제 timeout 두 사실”이 단순timeout으로 덮이는 것을 검출한다. |
| NO_LEGACY_ATTEMPT |12건. legacySends0, cooldown/진단 및 실제 fallback 성공 실패를 검출한다. |
| NO_CYCLE_GATE |4건. **inactive-slow2/1** 및 느린 cached-silent의 cycle4 광고/회복 실패다. 별건 setup 실패로 변이가 실패한 것이 아니다. |

이 기대 FAIL을 정상 후보의 회귀 실패에 합치지 않는다. 같은 수라는 이유만이 아니라 실패한 **행동/단계**가 원 지적과 맞는지 대조했다.

## 남기는 한계/통합

- 포트 확보 간헐은 이번 독립3회에서 재발0이다. random/256회 fixture 개선의 SOURCE 수용과 실제 두 행 실행은 인정하되 **모든 미래환경에서 해소됐다고 닫지 않는다**. 이전run2의 setup 미도달·정확한 당시 원인 미확정·Windows 제외 범위/연속cursor 측정은 각 기록으로 보존한다. 이후 실패 시 실제bind 오류/port 전제를 남기며 다른 프로세스를 추정으로 종료하지 않는다.
- 실제 운영 directory/필드의 retry·사용자 Host 창 표시 및 통합 산출물 영향 범위는 운영 반영 전 기존 릴리스 검증에 연결한다. loopback HostAgent 진단 검사를 실서비스/실 NAT 성공으로 확대하지 않는다.
- migration 시험의 같은T2는 여전히 별도 미수정이다. 이 판정으로 그 시험을 C: exe 옆 생성/삭제 방식으로 실행하지 않는다. 과거 바깥 삭제 정정도 소급 PASS/무해로 바꾸지 않는다.
- 기준에 포함된 KA/NP와 그 뒤의 다른 고정 보수는 R2 통합에서 합쳐 충돌/관련 회귀를 확인한다. directory-retry 한정 OK가 branch 전체·자동 준비/서명/게시를 승인하는 것은 아니다. maint의 별도M1도 독립이다.

검증용은 이 유지보수 단위의 SOURCE/EVIDENCE를 기존 통합 정본·담당 task에 연결하고, 의미 있는 묶음의 history/계획·Git MCP commit에 취합한다. 닫힌 단계 검사를 매 commit 전량 반복할 필요는 없으며 다음 검사는 실제 통합으로 바뀐 영향 범위에 맞춘다.

Codex 제품 수정·빌드·시험/프로세스 실행·삭제·설치·운영 서버 조작·commit/push 없음. 직접 확인은 저장 원문·Git 상태·파일 hash 대조다. 이 미추적 판정서 외 관련 공개 기록은 검증용 창구에서 처리한다. 사용자 현재 조치 없음.
