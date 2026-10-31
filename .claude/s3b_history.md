
### 437) 2026-09-08 업데이트 step3 보강 — PID 재사용 방어, 모의 대체를 실제 OS 동작으로 교체 (Codex 확정 + 검증용 remote#0jkgf453 보강 지시)
- 배경: 검증용의 보강 지시 — **"모의 효과 순서 테스트만으로 실제 OS 검증을 대체하지 말 것"**. 격리는 **대상을 분리하라**는 뜻이지 **가짜로 대체하라**는 뜻이 아니다. #436 이 격리를 강조하다 일부를 콜백으로 대체해 둔 것을 실제 OS 동작으로 바꿨다. 그리고 프로세스 대상 식별을 PID 단독에서 **실행 경로 + 생성 시각 + 핸들**로 강화하라는 지시를 반영했다.
- **PID 재사용 방어(실제 결함 수정)**: `ProcessTarget{pid, imagePath, creationTime}` 을 도입했다. PID 는 재사용되므로 **열거와 종료 사이에 원래 프로세스가 죽고 다른 프로세스가 그 번호를 물려받으면 무관한 프로세스를 죽인다.** 이제 `Quiesce` 와 `request_process_stop` 이 핸들을 연 뒤 `GetProcessTimes`/`QueryFullProcessImageNameW` 로 **신원을 대조**하고, 불일치면 "이미 사라졌다" 로 처리한다(그것이 기다리던 결과이므로 오류가 아니다). 생산 열거기는 **자기 자신을 대상에서 제외**한다.
- **레지스트리·서비스 격리(지시 추가분)**: `UpdateEffectsConfig` 에 `registryRoot`·`serviceName` 을 **기본값 없이** 추가했다. 지금은 아무도 읽지 않지만, 앞으로 `RegisterInstall` 을 쓸 때 실제 Uninstall 레지스트리 키와 `GNLinkSecureInput` 서비스 이름을 **하드코딩할 수 없게** 만든다 — 미설정이면 `validate()` 가 거부한다.
- **모의 → 실제 OS 로 교체한 시나리오**:
  - **재실행 실패**: 람다가 false 를 돌려주는 것이 아니라, 존재하지 않는 경로로 **실제 `CreateProcessW`** 를 호출해 실패시킨다 → `UpdatedButNotRelaunched`, 신버전 파일 유지(롤백 안 함) 확인.
  - **롤백 실패**: 교체 성공 뒤 복구 대상 파일을 **공유 0 으로 실제로 잠가** `MoveFileEx` 를 OS 수준에서 실패시킨다 → 롤백이 실패를 보고하되 **복구 가능했던 파일은 복구된 것**까지 확인(첫 문제에서 전부 포기하면 신버전이 더 많이 남는다).
  - **동시 실행**: 스레드가 아니라 **실제 두 번째 프로세스**(같은 exe 를 `--hold-lock` 로 재실행)가 명명 뮤텍스를 쥔 상태에서 `AcquireLock` 실패 → 상태기계가 `NothingToDo`, 다운로드 미진입, 그 프로세스 종료 후 락 재획득까지 확인. 파이프로 "held" 를 받고 진행해 sleep 추측을 없앴다.
  - **PID 재사용**: 살아 있는 프로세스를 **틀린 생성 시각**으로 기술해 `Quiesce` 가 즉시 성공하고(타임아웃을 기다리지 않음, 300ms 미만 실측) **그 프로세스를 건드리지 않음**을 확인. **대조군**으로 같은 프로세스를 **올바른 신원**으로 주면 실제로 기다리다 실패하는 것까지 확인해, 앞 케이스가 다른 이유로 통과하지 않았음을 보인다.
  - `process_identity_matches` 직접 검사 4종(자기 자신 일치 / 생성 시각 다름 / 경로 다름 / 신원 미확보는 절대 불일치).
- **권한 흐름**: 테스트가 자기 토큰의 elevation 을 읽어 **"NOT elevated 로 실행됨"** 을 출력하고, 설계의 "호스트가 띄운 업데이터는 2차 UAC 불필요" 주장이 **여기서 검증되지 않았음**을 명시한다. 관리자 권한 실증은 미검증으로 남긴다(지시대로 막히지 않고 진행).
- 검증 — **`qwinsta`: `console` 만 Active, `rdp-tcp` 는 `Listen` → RDP 미접속.**
  - `remote60_update_effects_test` **98 checks / 0 failed**(#436 의 64 → 98), exit 0.
  - 4종 동시: version_compare **139/0** · manifest **41/0** · state_machine **57/0** · effects **98/0**, 전부 exit 0.
  - **테스트 전후 라이브 `GNLinkHost`(5156)·`GNLinkInputService`(10820)·`GNLinkStream`(19384) PID 불변 확인.**
- 미검증(그대로): **실제 제품 프로세스 종료 0회 · 실제 설치 경로 교체 0회 · 실제 네트워크 다운로드 0회 · 관리자 권한 실행 실증 없음 · 실제 레지스트리/서비스 등록 없음**(`RegisterInstall` production 구현 자체가 없다). `enumerate_product_processes`/`request_process_stop` 은 컴파일만 됐고 실행된 적 없다.
- 변경 파일: `apps/native_poc/src/update_effects.hpp`·`update_effects.cpp`·`update_process_targets.hpp`·`update_process_targets.cpp`·`update_effects_test.cpp` · `docs/history.md` · `docs/구현계획.md`.
- 버전 인상·설치본 생성·설치·라이브 조작·서버 배포·push 없음.
- 상태: 검증용 검사 대기. 다음은 자율 진행 범위(JS/Kotlin manifest 소비자 · 서버 발행 배선 · UI 진입점). **전체 완료 아님.**
