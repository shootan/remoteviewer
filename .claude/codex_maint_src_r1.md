# maint r1 — 배포 실패 진단 SOURCE 검토

2026-10-28 / Codex remote#tt7fcvky → 검증용 remote#7fsfy22z / 기존 maint task.

**NEEDS_CHANGES — M1: 같은 signed pair를 재게시하다 sig rename만 실패한 경우에도 “짝 불일치·클라이언트 거부”라고 잘못 단정한다.** phase 진단·exit4·읽기 전용 조사 방향은 유지하고 문구/반례를 보수한다. 서버 재시작 문구와 오래된 정책 설명 정정의 취지는 수용한다. 운영 배포/자동 rollback 승인은 아니다.

## 고정 범위/기존 사실

- `fix/maint-r1` **b78ea39db1dd4cbd752b05a0f4430690cd4b6d0b**, main6fed2a3 기준. 전체3파일 diff·publish_pair 전후/backup·test case13·CLAUDE 배포절을 읽었다. HEAD 일치·tracked clean·whitespace rc0.
- Codex가 `bash -n`으로 deploy/test 두 스크립트 문법검사rc0을 확인했다. 전체 deploy_test/NAS 배포는 실행하지 않았다.
- 원래 .tmp 보존·exit4·부분 교체 가능성 문구와 정상 no-restart 문구는 이미09-23 65fcee7에서 바뀌었다는 보고/현재main 코드와 부합한다. 교체 **전** 검증 실패의 “previous release still published”는 별도 옳은 경로여서 건드리지 않는 판단도 맞다.
- 이 후보의 새 실행은 실패 후 .tmp 존재를 read-only로 확인해 swap phase를 나누는 부분이다. 두 rename을 pair-atomic으로 바꾸거나 자동원복/서버 코드·env를 변경하지 않는다. 일반 게시의 재시작 불필요와 server.js/의존 module/env 갱신 시 재시작 필요를 구분하는 정정은 수용한다.

## M1 (P2): rename phase와 실제 pair 검증 상태는 다름

`manifest-only`는 “manifest.tmp가 사라지고 sig.tmp가 남음”이라는 **단계**다. 이것만으로 새manifest와 현재sig의 바이트/서명이 불일치한다고 할 수 없다.

실제 허용된 idempotent 재게시 반례:

1. 이미 서명 검증된release R의 manifest+sig가 게시돼 있다.
2. **같은 R/같은 bytes**를 다시 게시한다. 첫rename은 성공하고 두 번째(sig)만 실패한다.
3. 기존sig는 새로 rename한 **동일manifest에 여전히 유효**하다. publish 실패라exit4/수동 확인은 유지해야 하지만 클라이언트가 거부한다고 단정할 수 없다.

Codex 직접 제한 검증:

- `.claude/codex_maint_review/publish_same_pair_probe.py`가 후보의 **publish_pair 함수 원문**을 읽어 local remote_sh/put 경계에서 실행했다. signed0.2.163 공개pair를 저장소 안 고유 sandbox에 미리 게시하고, sig.tmp rename만 거부하는 mv shim을 사용했다. NAS/SSH/curl/개인키는 사용하지 않았다.
- 유효한 fault 주입 결과를 `same-pair-25cb5f8ea7`에 보존했다.

```
publish_exit=4
swap-state manifest-only
ERROR ... the published pair does not match (clients refuse it) ...
published_pair_identical_to_signed_input=True
published_signature_verify_exit=0
manifest ok: version=0.2.163 release=r-0.2.163 artifacts=11 platform=windows
false_mismatch_diagnostic_reproduced=True
```

진짜서버 공개키/구현을 쓰는 manifest verifier도 현재pair를 정상 수용했다. 제품 스크립트를 수정하지 않았고 파일 복사/rename은 검토 sandbox 안에서만 수행했으며 cleanup을 하지 않았다. 이 probe의0은 **오진 재현**이지 deploy 성공이 아니다.

첫 게시에서 sig가 **없는 경우**도 현재case13a가 실행한다. 그 상황에 “sig is the OLD one”은 역시 확정 사실이 아니다. 이후 읽기 실패·기존 불완전pair 등 unknown도 phase만으로 정상/불일치를 단정하지 않는다.

필요 보수:

- `swap-state manifest-only/neither/unknown`은 **rename 진행단계 추정**으로 유지할 수 있다. message는 “sig rename이 완료되지 않아 pair가 불완전/불일치할 수 있으니, 양쪽 hash와 서명·정확한 backup을 확인”처럼 사실과 가능성을 구분한다.
- 실제 mismatch/refused라고 쓰려면 현재게시pair의 hash/서명/존재를 read-only로 직접 확인해 그 상태를 별도 기록한다. 새 검증/네트워크 실패가 생기면 unknown으로 남기고exit4는 그대로 유지한다. 자동 rollback·blind cleanup은 추가하지 않는다.
- CLAUDE의 `manifest-only(짝 불일치)`도 phase와 실제검증 상태를 구분하도록 함께 고친다. 단순히firstrename만 완료한 것을 “내용이 새것·서명이 옛것이라 거부”로 일반화하지 않는다.
- case13에 **기존다른pair→부분교체**, **기존동일pair→부분교체**, **기존pair없음**, unknown 진단의 대표 경우를 두고 실제남은파일/서명 상태와 문구를 함께 확인한다. “does not match” 문자열만 반드시 나오는 시험은 오진을 고정한다. exit4·tmp보존·기존backup보존은 모두 유지한다.

## deploy_test fixture 정비 결정

main 자체의 FAILED31 및 최신fixture override에서도 남은9건을 이번 로그 변경의 회귀로 합치지 않은 점은 타당하다. **PASS58/FAIL9·이전PASS48/FAIL9와 failure목록동일은 작업용 보고 귀속**이다. 이를 전체 suite PASS나 모든 배포 실패경로 검증으로 보고하지 않는다.

제안한 **현 payload 계약용 최소 fixture+시험전용 서명키 생성 방식은 후속 구현 GO**다. 반복적인 추상 승인 대기는 필요 없다. 기존 maint 정본에 범위를 취합해 fixture 정비를 연결한다.

- production 개인키를 복사/읽기/시험서명에 사용하지 않는다. 고유 내부 sandbox에서 시험전용키·공개키를 만들고 verifier override는 **그 시험 child 환경에만** 전달한다. 정상 게시의 pinned key/검증을 완화하지 않는다.
- 현재필수 payload 이름/상대경로·버전/URL·hash를 시험의 데이터로 만들되, 실패해야 할 불변성/변조/부족payload·옛주소/권한·부분교체 사례를 삭제하거나 조건을 느슨하게 하지 않는다. .exe는 이 시험의 자료이며 실행/설치하지 않는다.
- 0.2.109·0.2.123의 고정숫자·ui경로·URL 가정은 각 사례가 실제로 검증하려던 계약으로 갱신한다. 다른fixture로 바꾼 뒤 우연히실패/성공하는 것을 검증으로 쓰지 않는다.
- 로컬mock·저장소 안 고유root·해당root의검증된cleanup만 사용한다. 운영NAS/서비스·외부artifact게시/다운로드·사용자설치본 조작은 없다.

M1 보수의 제한 SOURCE/행동검사는 우선 진행할 수 있고, 전체 fixture 정비가 끝나기 전에는 알려진baseline실패를 분리 기록한다. 최종“회귀전체PASS”는 실제정비후완주 근거가 있을 때만 쓴다. 별도사용자확인이나새중복writer가 필요하지 않다.

retry 포트 장치 변경은 **dirretry34f22de**에만 포함돼 있으며 그 SOURCE는 `codex_dirretry_src_r2.md`로 별도 OK다. maint branch에 같은파일을 중복 적용하지 않는다. 과거저장소밖fixture삭제/migration미수정도 해당정정기록으로 유지하고 추가실행은 안전경계보수 뒤에만 한다.

검증용이 M1·시험/문서정정을 같은 maint task로 취합해 작업용2에 전달한다. Codex 제품수정·제품빌드·전체deploy_test/운영배포·삭제·commit/push 없음. **검토용local 함수실행·공개pair복사/서명검증·문법검사**는 위와 같이 수행했다. 미추적판정서/프로브외 history/계획·Git MCP commit은 같은구현기록에 취합한다. 사용자현재조치없음.
