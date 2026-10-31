# keepalive-r2 A r5 — N4 시험 경계 재검토

2026-10-27 / 검증용 `remote#yj8mrmrx` / 기존 A task `t-y1lnbgnd` / 입력 report r5 전문.

**N4: NEEDS_CHANGES — 루트 anchor와 자식 시험 진입점 두 잔여. 제품 SOURCE OK/② 구현 GO는 r4 그대로 유지한다. NAS·자동 preconnect 출하 OFF도 그대로다.**

고정 **357ee5e896c4b3118682aef22d5477ba4d25ed96**, diff f4ddc9a..357ee5e는 `apps/directory/test/` 7파일뿐이다. 제품 Host/server diff0을 직접 확인했고 전체 변경·실제 child 호출/cleanup도 읽었다. whitespace rc0. Codex suite/삭제/부정 경로 실행은 하지 않았다.

## N4-a (P2): 경로 모양이 허용 저장소를 증명하지 않음

`test_tmp.root()`는 `.claude/test-tmp/<이름>` 모양이면 mkdir하고, realpath도 **동일한 모양인지**만 검사해 cached root로 채택한다. **D:/remote/remote 안이라는 검사가 없다.**

예를 들어 `C:/work/other/.claude/test-tmp/ka`는 모양과 realpath 모양 모두 맞는다. 이곳이 root로 채택되면 `file/remove`는 그 바깥 root 안의 파일 삭제를 정상 허용한다. `C:/work/.../scratchY`를 거부한 현재 대조는 모양이 틀린 사례이므로 이 우회를 검증하지 못한다. Codex는 이 경로를 만들어 실행하지 않았다.

또 생성 전 검사는 lexical shape뿐이고, 실제 경로 검사는 mkdir 뒤다. root나 기존 조상 junction이 다른 위치의 `.claude/test-tmp/<이름>`으로 이어져도 현재 검사는 통과할 수 있다. `remove`에서 마지막 대상의 lstat만 보는 것은 **root 자체의 신뢰**를 복구하지 못한다.

필요 보수:

- 허용 base를 **이 저장소의 주 checkout `D:/remote/remote/.claude/test-tmp`**에 결합한다. linked worktree C:/work에서 실행해도 그 worktree의 `.claude`를 임의로 새 삭제 허용 루트로 삼지 않는다. Git common-dir로 주 저장소를 확인하는 등 이동 가능한 구현은 가능하나, 요청받은 임의 경로의 모양만으로 자기 자신을 허용 base로 승인해서는 안 된다.
- candidate는 그 base의 고유 task 자식 하나여야 한다. `mkdir` 전 lexical containment와 이미 존재하는 조상의 실제 경로/링크를 검사해 바깥으로 이어지는 경로를 거부한다. 생성 뒤에도 실제 위치를 확인한다. 링크 거부를 약속한다면 root/조상 검증도 그 의미와 맞춘다.
- 이후 file/remove의 실제 대상 검사와 root 자체 삭제 금지는 유지한다. 이 helper를 일반적인 파일시스템 sandbox라고 과장할 필요는 없으며, 현재 시험이 쓰고 지우는 범위를 정확히 제한하면 된다.

제한 대조: **모양은 유효하지만 저장소 밖인 candidate**, 허용 base 아래의 root/조상 링크가 바깥으로 이어지는 후보, 정상 내부 task, root 자체/`..` 삭제 거부. 새 반례 때문에 실제 바깥 폴더를 만들거나 정리하지 않는다. 먼저 검증 로직을 닫고 검사 단계의 write/delete 호출0을 확인한다. 더 많은 실제 삭제 실험은 필요 없다.

## N4-b (P2): 수정된 scratch child 네 개는 검증 helper를 우회함

`account_admin_test`, `device_credential_test`, `recovery_fault_test`, `relay_wire_test`는 `REMOTE60_TEST_TMP`를 **path.resolve만** 하여 바로 mkdir/mkdtemp한다. 미설정이면 linked worktree의 `.claude/test-tmp`로 fallback한다. helper의 root 검증은 호출하지 않는다. 이후 raw unlink/rm 및 recursive scratch cleanup도 있다.

따라서 run.js로 올바른 env를 전달한 경로는 개선됐지만, 이 수정된 child를 단독 실행하면 무효 env·미설정을 검증하지 않고 바깥 자료를 쓰거나 삭제할 수 있다. 보고서의 “미설정이면 아무것도 안 씀”은 **현재 run.js/relay_env_bound 진입점에만** 해당하며 이 네 개까지의 보장이 아니다.

필요: 이번에 수정한 네 child도 동일한 검증된 root helper를 통해 진입하고, fallback으로 저장소 밖 cleanup 경로를 되살리지 않는다. 유효 루트/소유 scratch를 검증한 뒤 생성·정리를 수행한다. 나머지 os.tmpdir 기반 child는 검증된 runner가 같은 TEMP/TMP/TMPDIR를 전달하는 기존 흐름을 유지해도 된다. 관련 없는 UI runner 전부를 다시 쓰라는 요구는 아니다. 문서·독립 검사 명령도 검증된 진입 경로로 통일한다.

제한 대조: 네 진입점에 루트 미설정·유효 모양의 바깥 루트를 주면 **첫 생성 전에 중단**, 정상 내부 루트에서 필요한 시험이 정상 동작. 유효 run.js 경로를 이미 통과했으므로 각 소스 보수마다 815건 전량 재실행을 반복할 필요는 없다. 마지막 고정 안전 runner에서 한 번 관련 핵심 검사를 확인한다.

## 수용한 변경·운영 사실

- run.js가 먼저 helper를 호출하고 child env에 TEMP/TMP/TMPDIR 및 REMOTE60_TEST_TMP를 함께 넘기며, runner 자체의 데이터/로그/update 정리를 helper로 옮긴 방향은 맞다. 정상 설정 `D:/remote/remote/.claude/test-tmp/ka-r5-n4` 경로의 실행 개선은 인정한다. 남은 지적은 잘못된 루트를 “허용 root”로 채택하지 않는 것과 우회 진입점이다.
- 저장된 `ka_r5_dir_suite.txt`의 ALL PASS를 읽었다. 815/0·%TEMP% 변화0·부정 대조 결과는 **작업용 실행 귀속**이며, Codex 독립 실행 결과가 아니다. 정상 루트 1회 통과가 N4-a/b의 거부 조건을 증명하지는 않는다.
- 이전 `%TEMP%` 삭제 경로 목록·여러 실행의 정확 pid/횟수 미기록·시작 force 삭제 시 동명 기존 파일 가능성을 배제할 수 없다는 한계는 그대로 기록한다. “전부 자기 파일임이 증명됨”으로 정정하지 않는다. 현재 remote60-*0은 과거 피해 부재를 증명하지 않는다. 추가 바깥 cleanup은 하지 않는다.
- `C:/work/gnlink-wt/ka-r3/scratchX`는 Codex가 **읽기 전용 확인**했고 현재 일반 빈 폴더(항목0)다. 삭제하지 않았다. 이를 치우기 위한 새 사용자 승인 요청이나 바깥 삭제는 이 검토의 필수 단계가 아니다. 원래 자료 보존/사용자 판단 항목으로 둔다.

기존 담당에게 **N4-a/b만 같은 정본에서 보수**하도록 연결한다. 제품 blob/고정 exe는 그대로 쓰며 제품 전량 rebuild·r3 N1~N3 재심사는 불필요하다. 지금 작업용1의 우선은 picker 최종 경계이며 KA가 이를 선점하거나 새 병렬 writer를 만들지 않는다. 다음 회신은 시험 guard diff와 제한 반례면 된다. 사용자 현재 조치 없음. Codex 제품 수정·빌드·suite/삭제 실행·commit/push 없음. 이 미추적 판정서 외 기록·Git MCP commit은 같은 작업에 취합한다.
