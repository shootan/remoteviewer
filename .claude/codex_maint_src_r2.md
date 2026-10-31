# maint r2 SOURCE 재검토

2026-10-28 / Codex remote#tt7fcvky → 검증용 remote#7fsfy22z / 기존 maint task.

**NEEDS_CHANGES — M1 동일pair 오진은 닫음. M2: 원격 확인/검증기 실행 실패를 incomplete/does-not-verify로 단정하는 두 경로를 unknown으로 분리한다.** phase/pair 분리·exit4·자동원복 없음·시험 fixture 정비의 방향은 수용한다. 제품 정상 배포/개인키/운영 NAS 동작을 넓히는 변경은 요구하지 않는다.

## 고정 범위

- `fix/maint-r1` **b1fd9c70ae15a7472c467eb328ea848722636946**, 비교6fed2a3..b1fd9c7 및 직전b78ea39..b1fd9c7의 전체3파일을 읽었다. HEAD 일치·tracked clean·diff whitespace rc0. 미추적automation/__pycache__는 검토/정리 대상에 포함하지 않았다.
- publish_pair/remote 경계·현재 verifier CLI·새fixture 생성/서명·case13·CLAUDE 배포절을 대조했다. Codex의 bash -n은 deploy/test 모두rc0. 전체80/80과 이전스크립트6 FAIL은 작업용 보고 귀속이다.

## M1: 닫음

동일signed pair 재게시 중 sig rename 실패를 실제함수 probe로 다시 실행했다. **exit4는 유지되고 swap-state manifest-only / pair-state verified-this-release**, 기존의 “clients refuse” 오진은 없었다. 공개0.2.163 pair의 bytes는 그대로였고 독립 verifier도정상이다.

이번 보수는 rename 진행단계와 실제 pair 상태를 분리했다. 같은/다른pair·기존pair없음·read-back실패·정상게시를 시험한 방향도 맞다. 다음M2는 그 분류에서 **관찰을 못 한 경우**를 처리하는 문제다.

## M2 (P2): 검사를 수행하지 못한 상태를 실제 불완전/서명거부로 분류함

현재 코드의 두 분기:

1. `if ! remote_sh "test -f manifest && test -f sig"; then incomplete`. nonzero는 파일 없음뿐 아니라 **SSH/원격 명령 실패**도 포함한다. 확인하지 못한 것을 missing이라고 판정한다.
2. `if node gnlink_verify_manifest.js ...; then verified... else does-not-verify`. node 실행 불가·로컬 read/공개키/도구 오류도 같은 else로 들어가 **“clients will refuse”**를 출력한다. 실제 검증기의 명시적 거부와 검증을 수행하지 못한 것은 다르다. preflight 이후 장애가 생겨도 진단은 보수적으로 남겨야 한다.

Codex의 `.claude/codex_maint_review/pair_uncertainty_probe.py`는 고정후보의 **publish_pair 함수원문**을 local 경계에서 실행했다. 이미 정상인 공개0.2.163 pair를 고유 저장소내sandbox에 넣고 sig rename을 실패시킨뒤, 다음 진단 경계에만 fault를 주었다.

| 주입 | 후보가 출력한 상태 | 보존된실제pair |
|---|---|---|
| 파일존재 확인의 remote_sh가255(통신/명령실패) | **incomplete**, manifest/signature missing | bytes불변, 독립서명검증rc0 |
| 진단node 프로세스가127(실행불가) | **does-not-verify**, clients will refuse | bytes불변, 독립서명검증rc0 |

두경우 모두 publish exit4·swap-state manifest-only이고 `uncertainty_misclassified=True`를 직접 확인했다. 해당sandbox `existence-transport-failure-5685c293`, `verifier-unavailable-ac72c8ff`는보존했다. SSH/NAS/운영키는 사용하지않았으며 원격경계는 자기 local파일에만 작용했다.

필요 보수:

- **query가 성공해서 파일 없음을 확인한 경우만 incomplete**. 명령/통신/상태 해석 실패는 unknown이다. raw nonzero 하나를 missing으로 읽지 않는다. 구분이 불가능하면 unknown으로 남기는 것이 맞으며 새 원격 진단 시스템을 만들 필요는 없다.
- **verifier가 정상 실행돼 명시적으로 문서를 거부한 경우만 does-not-verify**. 도구 실행/파일 read/키/내부 예외는 unknown이다. exit1에 여러 실패가 섞인 현재 CLI라면 명시적 status/exit 구분을 추가하거나 동일 검증 구현을 얇게 감싸 결과를 구분해 전달한다. 임의 stderr 전부를 서명 불일치로 취급하지 않는다.
- existence query 실패·verifier 실행 불가/내부 실패를 case13에 추가한다. 정상 검증의 거부·진짜 missing·same/different pair·정상 게시도 유지한다. unknown은 정상/거부 어느 쪽의 주장도 하지 않고 exit4를 유지한다.
- CLAUDE도 같은 구분을 반영한다. **verified-this-release/verified-another-pair에서 무조건 원복을 지시하지 말고** 확인 후 필요할 때 정확한 backup pair로 원복하도록 쓴다. 정상 동일 pair에서는 스크립트 문구의 “확인 후 재시도”와 일치시킨다. 자동 원복/부분 파일 삭제는 추가하지 않는다.

읽어온두파일은진단시점의snapshot이지pair-atomic전환/향후상태의영구보장이아니다. 기존배포lock·실패보고·수동확인경계를그대로유지한다.

## fixture/키 경계: 수용

현재payload gate가읽는동일소스의11개이름으로작은시험자료를만들고, 실행마다새P-256키를생성해DPAPI로감싼뒤 **명시적시험KeyDir/TrustedKeyHex**로서명한다. 운영private key를여는fallback이없다. 공개키override는deploy/verifier child환경에만전달하고제품pinned key를수정하지않는다. fixture의.exe는자료이며실행/설치하지않는다. 이정비방향을다시승인받을필요는없다.

기존변조/불변성/필수payload/lock·옛주소/경로보존등사례는유지되고, stage진단을문구만고정한시험에서실제파일·서명대조로확장했다. 다만현재80/80은M2의두fault를포함하지않는다. M2보수후관련추가사례와fixture전체완주를고정후보로확인하고, main의31 FAIL/직전fixture9 FAIL을소급PASS로바꾸지않는다. 외부signed release를주면일부case가SKIP되는경로와기본생성fixture의완주도구분한다.

시험은지금처럼 **저장소안고유sandbox**에서만한다. 시험script는자기WORK를정리하므로C:worktree에서직접돌리는것과혼동하지않는다. 생산개인키·운영서비스/서버env·사용자설치본·외부게시를변경하지않는다.

## 다음담당/기록

검증용은 M2의 두 uncertainty 경로·대표 반례·문서 정정을 **같은 maint task의 통합 지시서**로 취합해 기존 작업용2에 연결한다. M1·이미 수용한 fixture 방향은 유지하고 새 별건/중복 writer를 만들지 않는다. SOURCE 재확인→저장소 내 local 독립 검증→관련 통합으로 진행한다. dirretry/KA 미결과 배포 승인은 별도다.

Codex 제품 수정·제품 빌드·전체 deploy_test·운영 배포/개인키 사용·삭제·commit/push 없음. 직접 실행은 **검토용 local 함수 probe·공개 pair 사본의 검증·bash 문법 검사**다. probe 사례들은 저장소 안에 보존했고 후처리 정리는 실행하지 않았다. 미추적 판정서/프로브 외 history/계획·Git MCP commit은 같은 구현 기록에 취합한다. 사용자 현재 조치 없음.
