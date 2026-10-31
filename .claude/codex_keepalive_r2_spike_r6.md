# keepalive-r2 A r6 — N4 시험 경계 완료

2026-10-27 / 검증용 `remote#yj8mrmrx` / 기존 A task `t-y1lnbgnd` / 입력 report r6 전문.

**N4 SOURCE OK — r5의 a/b 잔여 닫음. 검증된 내부 임시 루트로 관련 독립 검사를 진행할 수 있다. 제품 r4 SOURCE OK/② 구현 GO는 그대로이며, 운영 NAS·자동 preconnect 출하 OFF도 유지한다.**

고정 **cf8fb706820e6f5d60c41b9730aa89cb025960ed**, diff357ee5e..cf8fb70의 test5파일 전체와 runner 연결을 읽었다. Host/native/directory server 제품 diff0, whitespace rc0, 검사 시 worktree HEAD 일치·tracked clean. 제품 재빌드·기존 N1~N3 재심사는 필요 없다.

## a. 신뢰 루트 anchor: 닫음

- 기준은 요청 경로 자체의 모양이 아니라 **D:/remote/remote/.claude/test-tmp**라는 고정 anchor다. root 후보의 부모가 이 anchor와 일치해야 하고 한 단계 이름만 허용한다. C:/work의 같은 모양 경로는 이제 허용 root로 승격되지 않는다.
- anchor가 이미 존재하고 링크가 아니며 실제 경로가 지정 위치인지 **생성 전에** 검사한다. 현재 D:/remote/remote, .claude, test-tmp는 Codex 읽기 전용 확인에서 일반 디렉터리/링크 아님이었다.
- 후보가 기존 링크이면 mkdir 전에 거부한다. 새 root는 검증된 anchor 아래 한 단계 mkdir만 하고, 생성 뒤 실제 부모/링크를 다시 검사한다. recursive mkdir로 검증 전에 바깥 부모를 만드는 경로는 없다.
- file/remove의 실제 대상 containment와 root 자체 삭제 금지는 유지된다. 이 PC의 지정 저장소에 한정된 시험 가드라는 의미이며 다른 checkout을 자동 허용하지 않는다.

## b. 수정 child의 검증 진입점: 닫음

- recovery_fault·relay_wire·account_admin·device_credential 네 child가 `testTmp.root/scratchDir`로 진입하며 raw REMOTE60_TEST_TMP 사용·worktree fallback이 제거됐다. 네 곳 모두 scratchDir prefix는 고정된 내부 이름이다. 루트 미설정이면 scratch 생성보다 먼저 중단한다.
- recursive scratch 정리는 `testTmp.remove`를 사용한다. runner는 검증된 root를 TEMP/TMP/TMPDIR·REMOTE60_TEST_TMP로 전달하는 r5 연결을 유지한다. 이 runner 경유 child의 os.tmpdir 대상도 같은 내부 루트에 놓인다.
- 표현은 정확히 유지한다: **모든 삭제 호출이 helper 호출로 바뀐 것은 아니다.** account/device/recovery의 일부 flag 파일 unlink/rm은 남아 있으나, 지금은 검증된 root가 만든 해당 고유 scratch 아래에서만 경로를 구성한다. 해당 경로의 바깥 우회가 제거된 것으로 수용하며, 무관한 모든 시험의 삭제 API를 기계적으로 교체하라는 추가 요구는 없다.

## 직접 확인과 실행 귀속

Codex는 immutable `test_tmp.js`를 Node VM에서 읽어 **모든 mkdir/mkdtemp/rm/unlink/rmdir/write 진입을 차단하는 fs wrapper**로 검사했다. 실제 파일 생성/삭제는 하지 않았다.

| 입력 | 결과 | mutation 함수 호출 |
|---|---|---|
| REMOTE60_TEST_TMP 미설정 | 설정 누락으로 거부 | 0 |
| C:/work/other/.claude/test-tmp/ka | anchor 밖으로 거부 | 0 |
| D:/remote/remote/.claude/test-tmp 자체 | 직접 task 자식 아님으로 거부 | 0 |

이는 **실제 guard 검증 로직의 비변경 실행**이며 실제 junction 생성·서버 suite 실행의 증거는 아니다. anchor 안 junction 거부/링크만 정리, child 단독 무설정 rc1/쓰기0, 정상 root의 recovery_fault·account113·device83·relay_wire 통과는 **작업용 보고 귀속**이다. 전체815 재실행을 생략한 것은 변경 범위에 맞다. 고정된 안전 runner에서 다음 실제 기능 독립 검사에 연결하면 된다.

과거 %TEMP% 삭제의 정확 pid/횟수 및 동명 기존 파일 여부 미확인 기록은 유지한다. 새 가드 통과를 과거 피해 없음/복구 성공으로 확대하지 않는다. C:/work/.../scratchX는 이전 확인대로 보존하며 이번 검토에서도 삭제하지 않았다. 이를 정리하기 위한 새 사용자 승인이나 작업 대기는 필요 없다.

## 다음 담당

검증용은 N4를 닫고, 기존 KA 정본에 **안전한 실행 루트와 현재 SOURCE 상태**를 반영한다. 별도 제품 수정/재빌드·추가 Codex guard 승인 왕복 없이 해당 고정 시험 코드로 핵심 독립 행동을 검사할 수 있다.

같은 writer의 **APK0.2.32 최종 통합/AVD가 우선**이며 이 승인으로 KA가 이를 선점하지 않는다. KA ②의 admission counter 내구성, activation 결과 상관·역순/유실/cancel, lease별 socket, proof 손실 bounded 회수, held input release·실 NAT/포트/비용/실 APK·PC는 기존 후속 그대로다. N4 검사 완료는 그 기능 EVIDENCE나 NAS 배포 승인이 아니다.

사용자 현재 조치 없음. Codex 제품 편집·빌드·서버/suite 실행·파일 삭제·commit/push 없음. 위 guard 검증만 비변경 방식으로 실행했다. 미추적 판정서/인계 외 공개 history/계획·Git MCP commit은 기존 구현 작업에 취합한다.
