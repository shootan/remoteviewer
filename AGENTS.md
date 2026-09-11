# AGENTS.md

**상위 원칙·역할·완성 기준만** 여기 둔다. 구체 절차(보고·테스트 의존성·사용자 행동 완료 조건·
테스트 빌드 분리·연속 수행·검증 독립성·로그·배포 명령)는 **`CLAUDE.md` 가 정본**이고, 같은 내용을
두 곳에 적지 않는다.

## 가장 상위 정책
* repository root path를 벗어나는 범위는 절대 삭제커맨드를 실행하지않는다. 필요시 권한을 물어본다. 

## Mandatory Workflow (Always)
After each meaningful task is completed, do all of the following in order:
1. Update `docs/history.md` with:
   - goal
   - files changed
   - validation/build/test result
   - next action
2. Update `docs/구현계획.md` by checkbox/progress status only (do not append history narrative).
3. Commit only task-related changes with a clear Conventional Commit message, using git MCP tools.
4. In the final report, include:
   - commit hash
   - what was changed
   - what was validated

## Scope Control
- Do not include unrelated files in commits.
- Do not revert user changes unless explicitly requested.
- If no file change is required, state why and skip commit.
- Keep role separation strict:
  - write history narrative only in `docs/history.md`
  - update only checklist/progress status in `docs/구현계획.md`

## Agent Role Separation (2026-09-07, 3역할 절차)
사용자 지시: "두뇌는 Codex, 손발은 Claude" 에 검증 단계를 더한 세 역할. 흐름은
**Codex 계획 → 검증용 Claude 계획 검토·확정 → 작업용 Claude 구현 → 검증용 Claude 검사·OK → Codex 최종 확인** 이다.
- **Codex (설계·감독)**: 요구사항 정리, 원인·설계 분석, 계획·범위·우선순위·완료 기준 작성, 검증용과 이견 조율, 진행 감독, 최종 근거·산출물 확인과 사용자 보고. 파일 변경·빌드·테스트 실행·커밋을 직접 하지 않는다. 읽기 전용 코드/diff/로그 검토는 감독 업무로 허용한다. 과거의 "A2A 완료 구현 리뷰 금지" 정책(철회, `docs/history.md` #388)은 되살리지 않는다.
- **검증용 Claude (검토·검사)**: 계획을 독립 검토하고, OK 한 범위는 Codex 재승인 없이 작업용에 직접 위임한다. 구현 결과를 독립 검사한다. **저장소 추적 파일을 수정·커밋하지 않으며, 자기 수정물을 자기 승인하지 않는다.**
- **작업용 Claude (실행)**: 검증용이 OK 한 계획을 구현한다 — 파일 변경, 빌드·테스트, 버전/설치본, 기록, Git MCP 커밋.
- 두 역할의 구체 행동·보고 형식·검증 독립성 기준은 **`CLAUDE.md`** 를 따른다.
- **완료 판정**: 전체 구현 task 는 조사·빌드 대기·검토 중에는 working 으로 둔다. 검증용의 명시적 "구현 OK" 와 Codex 의 최종 확인 전에는 완료로 표시하지 않는다(개별 subtask 완료 보고와 구분). 작업용은 검증용을 건너뛰어 Codex 에 최종 완료 확인을 요청하지 않는다(진행 보고는 자유). 결함이 남으면 NEEDS_CHANGES, 환경 탓에 검사하지 못한 부분은 "미검증" 으로 분리한다. 단독 실행 PASS 는 재현된 회귀 FAIL 을 상쇄하지 않는다.
- **역할 식별·주소**: 역할은 GMux 버스 탭의 displayName(검증용claude / 작업용claude)과 세션 시작 시 `a2a_whoami` 로 확인한다. 세션 ID·resume UUID 같은 ephemeral 값은 이 문서에 적지 않는다. 답장·위임은 identity+sessionId 로 pin 한다.
- 이 절은 위의 최상위 삭제 제한, Mandatory Workflow, Scope Control 을 바꾸지 않는다. 두 Claude 세션의 구체 행동·보고 기준은 `CLAUDE.md` 의 해당 절을 따른다.

## 배포 완료 기준 (2026-09-10)

사용자 지시: *"gnlink 권한으로 안 되는 것만 NAS Claude 한테. 배포 자동화 스크립트 만들어놓으면 문제없잖아. 이제부터 버전 올라가서 빌드되면 자동배포하는 것까지가 완료고 실기 테스트 대기 상태."*

- **완료 흐름** — 이 순서를 다 지난 것만 "완료" 다:
  1. 버전 확정/인상 → 2. 빌드 → 3. 관련 검증과 **검증용 Claude 의 명시적 OK**(대상 commit·파일 해시 고정)
  → 4. 릴리스 서명 → 5. **`gnlink` 계정으로 자동 배포**(`automation/gnlink_deploy.sh`)
  → 6. **외부 검증**(HTTPS 도달·manifest 서명·버전·아티팩트 hash·필요 API 계약) → 7. Codex 최종 근거 확인
  → 8. **"배포 완료 · 실기 테스트 대기"**.
- ⚠️ **빌드 성공·파일 생성·서버 복사만으로 완료 처리하지 않는다.** 6단계까지 근거가 있어야 완료다.
- ⚠️ **사용자 행동이 되지 않으면 배포해도 완료가 아니다.** 요청된 행동(실행·로그인·선택·클릭·성공/실패)이
  실제로 되는지가 완료 조건이고, 소스 grep·handler 존재·문구 표시·테스트 총합은 그 증거가 아니다.
  판정 기준은 `CLAUDE.md` "사용자 행동 우선 완료 조건".
- ⚠️ **"자동 배포" 는 컴파일만 통과하면 미검토 코드를 게시한다는 뜻이 아니다.** 3단계(검증용 OK)는 건너뛸 수 없다.
  다만 그 검증을 통과한 **통상 릴리스는 게시 재승인이나 반복적인 go 대기가 필요 없다.**
- **역할**: 작업용 Claude 가 `gnlink` 권한으로 준비·서명·게시까지 한다. **`gnlink` 로 불가능한 구체 작업만**
  NAS 세션에 넘기고, 넘길 때 **정확한 범위·실패 근거·백업/rollback** 을 함께 준다. 권한이 있는 배포를
  중계하거나 매번 재승인받지 않는다. **계정 권한의 광역 완화와 다른 NAS 서비스 변경은 허가 범위가 아니다.**
  배포 명령·순서·기술적 주의는 `CLAUDE.md` "릴리스 배포".
- ⚠️ **`git push` 는 여전히 보류다.** 여기서 "자동 배포" 는 **NAS 게시**를 뜻하며 **push 허가가 아니다.**
  skill·도구의 push 지침보다 사용자의 standing 보류가 우선한다. 배포 문서에는 **push 미실행**을 명시한다.
- **개인키는 이 PC 의 보호 저장소 밖으로 나가지 않는다.** NAS 로 가는 것은 **공개키·서명·파일뿐**이다.
- **실패·권한 부족·미검증은 working 또는 명시적 blocked** 로 둔다. 배포 성공처럼 완료 처리하지 않는다.
- 배포 문서에는 **버전 · build commit · 파일 hash · 게시 URL · 직접 검증한 것 · 남은 실기 항목**을 분리해 적는다.
- 사용자 Host/Client/APK 의 **설치·재시작·실기 조작은 자동 배포와 별개**다. **요청 없는 자동 설치는 하지 않는다.**
