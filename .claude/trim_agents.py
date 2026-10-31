# -*- coding: utf-8 -*-
"""AGENTS.md keeps the top-level rules; CLAUDE.md holds the detail.

Only the duplicated paragraphs move out. The deletion limit, the workflow, scope control, the
three roles and the deployment completion bar all stay where they are.
"""
import io

p = 'AGENTS.md'
d = io.open(p, encoding='utf-8').read()

# ---------------------------------------------------------------- point at the detailed doc
old = u'''# AGENTS.md

## 가장 상위 정책'''
new = u'''# AGENTS.md

**상위 원칙·역할·완성 기준만** 여기 둔다. 구체 절차(보고·테스트 의존성·사용자 행동 완료 조건·
테스트 빌드 분리·연속 수행·검증 독립성·로그·배포 명령)는 **`CLAUDE.md` 가 정본**이고, 같은 내용을
두 곳에 적지 않는다.

## 가장 상위 정책'''
assert d.count(old) == 1, 'header anchor'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- roles: keep the flow, drop the copy
old = u'''- **검증용 Claude (검토·검사)**: Codex 계획을 독립적으로 검토해 이견·위험·누락은 근거와 함께 Codex 에 수정 요청하고, OK 면 Codex 의 재승인 없이 작업용에 직접 A2A task 로 위임한다. 계획 변경이 필요하다고 판단한 부분은 Codex 의 수정·확정 전에는 작업용에게 위임하지 않는다. 변경 없이 OK 한 독립 범위는 바로 위임할 수 있다. 구현 결과·diff·검증 근거를 독립 검사한다(검증 목적의 빌드/테스트 실행과 검토 기록 작성 가능; 기록은 `.claude/` 등 저장소 미추적 위치). 저장소 추적 파일(제품·문서)을 수정·커밋하지 않으며, 자기 수정물을 자기 승인하지 않는다.
- **작업용 Claude (실행)**: 검증용이 OK 한 계획을 구현한다 — 제품 코드·설정·문서의 실제 파일 변경, 빌드·테스트 실행, 버전/설치본 생성, 작업 기록(`docs/history.md`, `docs/구현계획.md`)과 Git MCP 커밋. 완료 검사는 먼저 검증용에 요청하고, 검증용의 수정 요구는 같은 계획 범위 안에서 반영해 재검사를 받는다. 계획·범위 변경이 필요하면 검증용을 통해 Codex 와 조율한다.'''
new = u'''- **검증용 Claude (검토·검사)**: 계획을 독립 검토하고, OK 한 범위는 Codex 재승인 없이 작업용에 직접 위임한다. 구현 결과를 독립 검사한다. **저장소 추적 파일을 수정·커밋하지 않으며, 자기 수정물을 자기 승인하지 않는다.**
- **작업용 Claude (실행)**: 검증용이 OK 한 계획을 구현한다 — 파일 변경, 빌드·테스트, 버전/설치본, 기록, Git MCP 커밋.
- 두 역할의 구체 행동·보고 형식·검증 독립성 기준은 **`CLAUDE.md`** 를 따른다.'''
assert d.count(old) == 1, 'roles anchor'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- completion: add the behaviour bar
old = u'''- ⚠️ **빌드 성공·파일 생성·서버 복사만으로 완료 처리하지 않는다.** 6단계까지 근거가 있어야 완료다.'''
new = u'''- ⚠️ **빌드 성공·파일 생성·서버 복사만으로 완료 처리하지 않는다.** 6단계까지 근거가 있어야 완료다.
- ⚠️ **사용자 행동이 되지 않으면 배포해도 완료가 아니다.** 요청된 행동(실행·로그인·선택·클릭·성공/실패)이
  실제로 되는지가 완료 조건이고, 소스 grep·handler 존재·문구 표시·테스트 총합은 그 증거가 아니다.
  판정 기준은 `CLAUDE.md` "사용자 행동 우선 완료 조건".'''
assert d.count(old) == 1, 'completion anchor'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- drop what CLAUDE.md now owns
old = u'''- **역할**: 작업용 Claude 가 `gnlink` 권한으로 준비·서명·게시·스크립트 실행까지 한다. 검증용 Claude 는 계획·구현·배포 결과를 검사한다.
  **`gnlink` 권한으로 불가능한 구체 작업만** NAS 세션에 넘기고, 넘길 때는 **정확한 범위·실패 근거·백업/rollback** 을 함께 준다.
  권한이 있는 배포를 NAS 에 중계하거나 매번 재승인받지 않는다. **계정 권한의 광역 완화와 다른 NAS 서비스 변경은 이 정책의 허가 범위가 아니다.**'''
new = u'''- **역할**: 작업용 Claude 가 `gnlink` 권한으로 준비·서명·게시까지 한다. **`gnlink` 로 불가능한 구체 작업만**
  NAS 세션에 넘기고, 넘길 때 **정확한 범위·실패 근거·백업/rollback** 을 함께 준다. 권한이 있는 배포를
  중계하거나 매번 재승인받지 않는다. **계정 권한의 광역 완화와 다른 NAS 서비스 변경은 허가 범위가 아니다.**
  배포 명령·순서·기술적 주의는 `CLAUDE.md` "릴리스 배포".'''
assert d.count(old) == 1, 'deploy role anchor'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('agents ok')
