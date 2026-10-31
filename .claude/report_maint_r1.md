# report maint r1 — 배포 스크립트 실패·로그 문구 + retry 시험 포트 고정 장치 간헐 (작업용2, 2026-10-28)

task maint / r1 / kind=maintenance (t-8yjh8ocy).
- 브랜치 `fix/maint-r1` = main 6fed2a3, worktree `C:/work/gnlink-wt/maint-r1`.
- 운영 서버·NAS 무변경(배포 시험은 로컬 모의 모드). 게시·push 없음.

## 1) 배포 스크립트 문구 — commit **b78ea39**

**먼저 사실 정정**: CLAUDE.md 가 지적한 두 문구는 **main 에서 이미 바뀌어 있었다.**
- 09-23 커밋 65fcee7(RV-14) 이후 바뀐 것:
  - 교체 실패 시 `.tmp` 를 지우지 않고 **exit 4** 로 끝난다.
  - 메시지는 "the published pair may be partially replaced -- restore the backup pair" 다.
  - 재시작 로그는 "no restart: only manifest files changed; server code and environment are unchanged" 다.
- CLAUDE.md 의 `:435~:445` 와 인용 문구는 **09-11 당시** 스크립트 기준이었다(`git show d93aeab:automation/gnlink_deploy.sh` 로 확인).
- `:396` 의 "the previous release is still the published one" 은 **교체 전** 검증 실패 경로라 맞는 문구다. 그대로 둔다.

**그래서 남은 실질만 했다**
- **교체 실패의 구분**(`publish_pair`): 두 rename 중 어느 것이 됐는지 **읽기만 하는 검사**(어느 `.tmp` 가 남았나)로 메시지를 셋으로 나눈다. **exit 4 는 셋 다 그대로**이고 자동 원복은 없다. `swap-state <상태>` 줄을 stderr 에 추가한다.

| 상태 | 메시지 |
|---|---|
| `manifest-only` | manifest 는 새 것, sig 는 옛 것 → "the published pair does not match (clients refuse it); restore the exact backup pair by hand" |
| `neither` | "the previous pair should still be published -- check both files against the backup's SHA256SUMS" |
| `unknown` | 기존 취지("may be partially replaced -- restore the exact backup pair") |

- **재시작 로그**: "no restart: only the manifest pair changed -- a restart is needed only when server.js, a module it requires, or its env changes".
- **CLAUDE.md**: '릴리스 배포 (NAS)' 절의 옛 줄 번호와 "후속 2건" 을 현 스크립트 사실로 고쳤다. 같은 commit 이다.

**시험** — `gnlink_deploy_test.sh` 에 case 13 을 추가했다.
- PATH 앞에 둔 `mv` shim 이 이름 하나만 거부하고, 나머지는 진짜 `mv` 에 넘긴다. 스크립트는 실제 것 그대로다.
- 13a: sig rename 실패 → exit 4, `manifest-only`, 실제로 새 manifest 가 게시되고 sig 는 `.tmp` 뿐.
- 13b: manifest rename 실패 → exit 4, `neither`, 아무것도 게시 안 됨, `.tmp` 둘 다 남음.
- 13c: 정상 게시 → swap-state 없음, 새 재시작 문구.
- **결과: 13 의 10건 전부 PASS.**
- **부정 대조**(수정 전 스크립트 + 같은 시험): 문구 검사 4건 FAIL, 사실 검사(exit 4·실제 파일 상태) PASS → **동작은 같고 문구만 바뀌었음**을 보여 준다.

⚠️ **회귀 시험 자체가 main 에서 이미 깨져 있다** (보고, 이번에 고치지 않음).
- 기존 고정 장치 `.claude/rel/0.2.109-r2`(기본값)와 `0.2.123` 을 지금의 payload 검사가 거부한다("the manifest does not name GNLinkClipHelper.exe"). 그래서 main 6fed2a3 그대로도 **FAILED (31)** 다(PASS 26).
- 그래서 `FIXTURE_BASE` 를 `GNLINK_TEST_FIXTURE_BASE` 로 덮어쓸 수 있게 하고(시험만), 최신 서명본 `C:/work/gnlink-wt/rel-0163/.claude/rel-final/0.2.163` 으로 돌렸다.
  - 수정 전: PASS 48 / FAIL 9.
  - 수정 후: PASS 58 / FAIL 9 — **FAIL 목록 동일(diff 0)**.
- 남은 9건은 0.2.109 모양을 가정한 사례들이다. 0/11 개수·ui 하위 경로·불변성 변조 대상·case 12 "옛 주소 서명본" 등이 새 fixture 와 맞지 않는다. 고정 장치 갱신은 별도 정비 후보다. 제안: 현 서버용 최소 fixture 를 시험 안에서 만들어 서명하는 방식(시험 키) — 결정이 필요하다.
- 실행 위치: 시험은 `$REPO/.claude/deploy-test.*` 를 만들고 스스로 지운다. 그래서 저장소 밖(worktree)에서 돌리지 않았다. `D:/remote/remote/.claude/test-tmp/maint*`(수정본·수정 전 본 복사)에서만 실행했다.

## 2) retry 시험 인접 UDP 포트 간헐 — 원인과 보수 (`fix/directory-retry-r2` **34f22de**)

**원인 (실측)**
- `FakeDirectory::StartLegacyAdjacent` 는 TCP `bind(0)` 으로 P 를 받고 UDP P+1 을 bind 하는 시도를 32회 한다.
- 이 PC 에서 TCP `bind(0)` 은 **연속 번호**를 준다. PowerShell 로 40회 해서 51809…51848 이 나왔다.
- UDP 에만 Windows 예약 범위가 있다: `netsh int ipv4 show excludedportrange protocol=udp` → 50000–50059(관리), **60182–60381**, **62131–62230**. TCP 쪽 예약은 49677–49978 등으로 서로 다르다.
- 그래서 OS 의 TCP 할당 위치가 UDP 예약 범위 바로 앞에 있으면, 32번이 **연속으로 함께 실패**한다.
- **재현**(실제 OS, 같은 규칙을 PowerShell 로): TCP 할당 위치를 60170 까지 밀고 옛 규칙을 돌리면 P=60171~60180 은 성공, **P=60181 부터 연속 실패(22/32)**.
- 10-28 run2 의 "32회 실패" 와 부합한다. 다른 프로세스의 UDP 점유도 보탤 수 있다.
- 예약 범위는 Hyper-V/WinNAT 가 부팅 때 동적으로 잡는다. 그래서 시점마다 다르다(**가능성**: 이번 값은 10-28 측정치다).

**보수 (시험 장치만, 판정 불변)**
- `directory_fake_server.hpp`: 동적 범위 [49152, 65533] 에서 **기준 포트를 무작위로** 골라 TCP B, UDP B+1 을 둘 다 `BindAt`. 최대 256회(시도끼리 독립).
- 찾지 못하면 여전히 호출자가 **loud FAIL** 한다(건너뛰지 않음). 문구는 "no free tcp/udp adjacent pair after 256 random tries".
- 같은 재현 조건(할당 위치 60170)에서 새 규칙은 **1회 만에** 쌍을 찾았다.
- 이 헤더를 쓰는 다른 시험(migration·punch_reply e2e·viewer_cancel e2e 등)은 `StartLegacyAdjacent` 를 부르지 않으면 영향이 없다. 빌드로 컴파일을 확인했다.

## 3) 결과·commit (retry 반복 결과로 채움)

| 항목 | 위치 | 결과 |
|---|---|---|
| 1) 배포 문구 | `fix/maint-r1` **b78ea39** (main 6fed2a3 위, clean) | 위 §1 |
| 2) 포트 장치 | `fix/directory-retry-r2` **34f22de** (d2f1243 위) | retry 177/0 ×2, legacy-adjacent 두 행 실제 실행 PASS |

- **2) 를 dirretry-r2 위에 둔 이유**: 같은 시험 파일이다. 또 그 브랜치의 retry 시험만 cache 를 저장소 안 시험 root 에 둔다(Codex T2 보수 d2f1243). main 판 시험은 exe 옆(C:)에 만들고 지운다.
- 처음엔 maint-r1 에서 main 판으로 3회 돌렸다(133/0 ×3, migration 71 PASS). 그 실행이 **저장소 밖 빌드 폴더에서 fixture 생성·삭제**를 했다. 자세한 내용은 `report_dirretry_rebase_r1.md` r2 절에 있다. 그 미커밋 변경은 maint-r1 에서 되돌리고 dirretry-r2 로 옮겼다.
- **미검증**:
  - 원래 간헐 조건(UDP 예약 범위 바로 앞 TCP 할당 위치)에서 **C++ 시험 그 자체**로 재현하지는 않았다. 같은 규칙을 PowerShell 로 재현했다.
  - 새 규칙의 실패 확률은 계산하지 않았다(256회 독립 시도).
- **삭제 후보 추가**(목록만): `C:/work/gnlink-wt/maint-r1`(worktree)·`maint-r1-build*`, `D:/remote/remote/.claude/test-tmp/maint`·`maint-base`(배포 시험 sandbox).


## r2 — Codex `codex_maint_src_r1.md` M1 + 고정 장치 정비 (2026-10-28)

**고정 후보 `fix/maint-r1` b1fd9c7** (b78ea39 위 1 commit). 제품 변경은 배포 스크립트의 진단뿐이다. 동작·exit code·원복 없음은 그대로다.

**M1 보수**: 교체 실패 때 두 가지를 따로 말한다.
- `swap-state` — 단계 추정이다. 어느 `.tmp` 가 남았는지 읽기만 해서 정한다.
- `pair-state` — 지금 게시된 pair 다. 읽기 전용으로 가져와 이번 릴리스와 바이트를 비교하고, 게시 전과 같은 검증기·키로 서명을 검증한다. 값은 `verified-this-release` / `verified-another-pair` / `does-not-verify` / `incomplete` / `unknown` 이다.
- "클라이언트가 거부" 는 `does-not-verify` 일 때만 쓴다. exit 4 는 모든 경우에 유지한다. 자동 원복과 정리는 없다.
- 임시 사본은 `$REPO_ROOT/.claude/deploy-tmp/pair.*` 에 두고 바로 지운다. 스크립트가 원래 쓰던 위치다.

**고정 장치 (Codex GO)**: 시험이 직접 만든다.
- payload: 제품이 교체하는 이름마다 작은 파일 하나. 이름은 payload gate 와 같은 소스에서 읽는다(11개).
- manifest: 크기·해시·url(rem.shotan.net — case 12 의 "옛 주소" 계약 유지).
- 서명: 릴리스 서명 스크립트에 **실행마다 만드는 일회용 키**로 서명한다. 공개키는 배포 child 에만 `GNLINK_PUBLIC_KEY_HEX`(검증기에 원래 있던 override)로 넘긴다.
- **운영 키는 읽지 않는다.** 제품의 고정 키와 모든 검사는 그대로다.
- 시험 시작 때 이 고정 장치가 payload gate 를 통과하는지 먼저 확인한다(실패하면 FAIL).
- `GNLINK_TEST_RELEASE` 로 실제 서명 릴리스를 넘기는 옛 경로도 남아 있다.

**case 13**:

| 사례 | 상황 | 기대 |
|---|---|---|
| 13a | 기존 pair 없음 · sig rename 실패 | manifest-only / **incomplete** |
| 13b | 다른 서명 pair(0.2.110) 위 · sig rename 실패 | manifest-only / **does-not-verify** · 옛 sig 유지 · backup 존재 |
| 13c | **같은 pair 재게시** · sig rename 실패 (M1) | manifest-only / **verified-this-release** · ERROR 줄에 "refuse" 없음 · 실제 서명 검증 OK |
| 13d | 다른 pair 위 · manifest rename 실패 | neither / **verified-another-pair** · `.tmp` 둘 다 보존 |
| 13e | read-back 실패(시험용 `cat` shim) | **unknown** · 어느 쪽 주장도 없음 |
| 13f | 정상 게시 | 두 줄 모두 없음 · 재시작 문구 |

**결과**
- `gnlink_deploy_test` **80/80 PASS, rc 0**. main 6fed2a3 은 옛 고정 장치로 FAILED 31, r1 의 0.2.163 덮어쓰기는 FAIL 9 였다.
- **부정 대조**(b78ea39 스크립트 + 새 시험): 6 FAIL. 13c 에서 M1 의 오진 문구가 그대로 재현된다.
- 실행 위치: 저장소 안 `D:/remote/remote/.claude/test-tmp/maint`(수정본 사본), `maint-base`(대조). worktree 에서는 돌리지 않았다 — 시험이 `$REPO/.claude/deploy-test.*` 를 만들고 지우므로.

**CLAUDE.md**: 배포 절에서 단계와 pair 상태를 구분했다. `manifest-only` 를 짝 불일치로 일반화하지 않는다.

**기타**
- retry 포트 장치는 dirretry 34f22de 에만 있다(SOURCE OK). 이 브랜치에는 없다.
- 삭제 후보: `C:/work/gnlink-wt/maint-r1/automation/__pycache__`(내 python import 로 생김), 시험 sandbox 두 곳.

---

## r3 (2026-10-28, 작업용2) — Codex M2: 수행하지 못한 검사는 unknown

**고정 후보**: `fix/maint-r1` **919dbfb**(b1fd9c7 위 1 commit, tracked clean). 시험한 파일(worktree, CRLF) sha256:
`gnlink_deploy.sh` `fc4ebd25866eb09118f54d4083f66c39a0455ce5073853871434f94abf25249d`,
`gnlink_deploy_test.sh` `9917aad2b69897297b409c58bda4d9934fac5d4ea498ffa944ed6e4d8245eb09`,
`gnlink_verify_manifest.js` `c913a50542f1f28869bb82ecdc0f1c25448f234c89f6607274e3a8c18c35148b`
(sandbox 사본과 worktree 가 같은 해시. git blob 해시는 줄끝 정규화 때문에 다르다).
범위: 제품 스크립트 2 + 시험 1 + CLAUDE.md. 제품 코드(C++·서버 모듈) 변경 없음.

**보수**
- swap 단계 조회와 존재 조회는 **exit 0 + 단어 하나**(`gnlink-swap-*`, `gnlink-pair-present|missing`)로 답한다. nonzero(ssh 255·원격 명령 실패)나 다른 출력은 `unknown` 이다. `incomplete` 는 조회가 돌아서 `missing` 이라고 답했을 때만이다.
- `gnlink_verify_manifest.js` 의 종료 코드를 나눴다: **3 = REFUSED**(문서가 검증되지 않음을 말하는 유일한 코드), **4 = 검사 불가**(파일·키를 읽지 못함, 쓸 수 없는 키, 예외, 변조본을 받아들인 검증기), 2 = usage. 성공만 보는 호출자(배포 전 검사, release 스크립트)는 동작이 같다.
  - 키 확인은 CLI 에서 먼저 한다. 공용 `loadManifest` 는 쓸 수 없는 키를 "signature did not verify" 로 돌려주기 때문이다(서버 모듈이라 바꾸지 않았다). ⚠️ `crypto.verify` 내부 예외는 서버 모듈 안에서 false 로 접히므로 3 으로 보일 수 있다. 서버 모듈을 고치지 않는 한 남는 한계다.
- `does-not-verify` 는 검증기 exit 3 일 때만이다. 127·1·4 는 `unknown` 이다. exit 4 는 그대로이고 자동 원복·정리는 없다.
- 문구: `unknown` 은 "아무것도 알 수 없음 — 직접 확인하고, 검증되는 짝이 아니면 backup pair 로 원복". `verified-another-pair` 는 "게시돼야 할 짝이 아닐 때만 원복".
- CLAUDE.md: 같은 구분을 적고 상태별 원복 단계를 나눴다(this-release 는 확인 후 재실행, another-pair 는 대조 후 필요할 때만, does-not-verify·incomplete 는 원복, unknown 은 직접 확인).

**시험**(저장소 안 sandbox `D:/remote/remote/.claude/test-tmp/maint`, 수정본 사본, 기본 생성 fixture + 일회용 시험 키)
- 수정본: `gnlink_deploy_test: PASS`, **rc=0, PASS 92 / FAIL 0 / SKIP 0**(r2 80 + 새 12). 로그 `.claude/test-tmp/maint_r3_run1.log`.
- 새 case:
  - 13g: 존재 조회 255 → `unknown`, 주장 없음, pair 바이트 불변.
  - 13h: 단계 조회 255 → `swap-state unknown`.
  - 13i: 검증기 127 → `unknown`, 주장 없음, pair 불변.
  - 13j: node exit 1 → `unknown`.
  - 13k: 검증기를 실제로 실행해 거부 3, 정상 0, 읽기 불가 4, 쓸 수 없는 키 4.
  - 기존 13a(진짜 missing → incomplete), 13b(진짜 거부 → does-not-verify), 13c/13d/13e/13f 는 유지되고 PASS 했다.
- **부정 대조**: 같은 시험에 b1fd9c7 의 deploy.sh·verify js 를 넣으면 **rc=1, FAIL 7**: 13g, 13h, 13i(`does-not-verify`), 13j(`does-not-verify`), 13k×3(exit 1). 로그 `.claude/test-tmp/maint_r3_neg.log`. 대조 뒤 수정본을 되돌렸고, 해시가 worktree 와 같은 것을 확인했다.
  - ⚠️ 13g·13h 는 옛 코드에서 **결함 때문에 실패한 것이 아니다.** 옛 조회문에는 shim 이 찾는 표지 문자열이 없어서 fault 가 주입되지 않았고, 다른 이유로 FAIL 했다(13g 는 `verified-this-release`, 13h 는 `manifest-only`). 옛 코드의 오분류 자체는 Codex probe 가 재현한 것이다. 13i·13j·13k 는 정상적인 부정 대조다.
- 실행하지 않은 것: ssh·curl(이 회귀가 대신하지 못함), 운영 NAS, 외부 signed release 경로(GNLINK_TEST_RELEASE).
- 금지 준수: 운영 키·서버·NAS·push·게시 없음. 삭제 없음(시험이 자기 WORK 를 sandbox 안에서 지움).

---

## r4 (2026-10-28, 작업용2) — Codex M2 잔여 2개

**고정 후보**: `fix/maint-r1` **e58bb29**(919dbfb 위 1 commit, tracked clean). 시험한 worktree 파일 sha256:
`gnlink_deploy.sh` `ecae5f38fcf105a3d7499745c1300b13245c705136b9f8923d4d2522be592155`,
`gnlink_deploy_test.sh` `67735713609c5c627b46466dc16244fb42883c3b862778978172f459198e2093`,
`gnlink_verify_manifest.js` `ca67846e3bcc87f0782d3e01f5d031b8d0006819c7a516bb293b77cff54c85aa`.
CLAUDE.md 는 commit 에만 있다(시험 대상 아님).

**보수**
- **M2-b**: swap 단계와 pair 존재를 **manifest 디렉터리 읽기 전용 나열 한 번**으로 읽는다.
  - 명령: `find DIR -mindepth 1 -maxdepth 1 -printf '%Y %f\n' && echo gnlink-list-end`.
  - find 가 0 으로 끝나고 출력이 끝 표지로 끝날 때만 "나열됨"이다. 그 밖(255, 내부 항목을 읽지 못한 find 의 nonzero, 표지 없음)은 단계·pair 모두 `unknown` 이다.
  - `incomplete` = 나열됐고, 둘 중 하나가 일반 파일로 없을 때다. `%Y` 는 symlink 를 대상 기준으로 본다.
  - `test -f` 는 더 쓰지 않는다. "없음"과 "볼 수 없음"이 같은 false 이기 때문이다.
- **M2-a**: CLI 가 공용 모듈의 `SignatureInvalid` 를 **같은 입력으로 `crypto.verify` 를 한 번 더 실행해 확인**한다. false 면 3(거부), 예외나 결과 불일치면 4(검사 불가)다. 128 hex 가 아닌 서명은 crypto 없이 거부(3)다(공용 모듈과 같다). 공용 서버 모듈은 바꾸지 않았다.

**시험**(sandbox `.claude/test-tmp/maint`)
- 수정본: **rc=0, PASS 97 / FAIL 0 / SKIP 0**(로그 `.claude/test-tmp/maint_r4_run1.log`). 주입한 fault 마다 표지 파일을 남기고, case 가 그 표지를 확인한다(주입 도달 확인).
  - 13g: 나열 조회 255 → pair·단계 모두 unknown, 도달, pair 불변.
  - 13h(새): 나열은 다 출력했지만 find 가 exit 1 → unknown, 도달, pair 불변.
  - 13i·13j: 검증기 127 / 1 → unknown, 도달.
  - 13k: 실제 CLI 와 실제 공용 모듈에 `--require` preload 로 `crypto.verify` 가 예외를 던지게 했다 — 유효 pair → **4**, 불일치 pair → **4**(둘 다 throw 도달). 진짜 거부 3, 정상 0, 서명 형식 아님 3, 읽기 불가 4, 쓸 수 없는 키 4.
  - 기존 13a(진짜 missing → incomplete), 13b(진짜 거부 → does-not-verify), 13c/d/e/f 유지.
- **부정 대조**: 919dbfb 의 deploy.sh·verify js 를 넣으면 **rc=1, FAIL 6**(로그 `.claude/test-tmp/maint_r4_neg.log`). 대조 뒤 수정본을 되돌렸고 해시가 worktree 와 같다.
  - 13k crypto throw ×2(옛 코드 exit **3**): 진짜 재현이다.
  - 13g·13h ×4: 옛 코드에는 `find`/끝 표지가 없어 fault 가 **도달하지 않아** 실패한 것이다(표지 확인으로 그렇게 드러난다). 옛 `test -f` 접힘 자체는 Codex 의 원문 probe 가 재현한 것이고, 이 대조의 FAIL 을 그 재현으로 세지 않는다.
- 한계: 이 회귀는 ssh·실제 NAS 권한 장애를 대신하지 못한다. 원격 find 가 GNU find(`-printf`)라는 전제다 — NAS 는 Ubuntu 라 GNU find 이지만 이번에 ssh 로 확인하지는 않았다. `-printf` 가 없으면 nonzero → unknown 으로 떨어진다(안전한 방향).
