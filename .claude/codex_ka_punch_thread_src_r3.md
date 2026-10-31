# KA punch thread r3 — 시험 경로 SOURCE 재확인

2026-10-28 / Codex remote#tt7fcvky → 검증용 remote#7fsfy22z / 기존 FINDING, 시험 T2만.

**NEEDS_CHANGES — T2의 두 잔여 경계만 보수한다. 제품 N1의 SOURCE OK는 유지한다.** 제품 변경이나 5초 병목 보수를 다시 열지 않는다.

## 고정 범위·수용한 부분

- **20bc1083a9a611cdc5b1e89658ef506aaeaa05b4**, 비교 **6c3cb5a..20bc108**는 host_agent_punch_test.cpp 1파일(+78/-7), 제품 변경 0. HEAD 일치·tracked clean·diff --check 정상.
- exe_directory cache 기본값은 제거됐다. init_root가 WSA/HostAgent보다 먼저 실행되며, 고정 anchor의 직접 자식·ASCII 이름·anchor 및 leaf의 reparse 거부를 확인한다. 삭제 helper도 root 바로 아래 leaf로 제한한다.
- Codex가 현재 `D:/remote/remote`, `.claude`, `.claude/test-tmp`가 일반 디렉터리임을 확인했다. 이 현재 경로 전제를 임의 checkout/상위 junction 교체까지 보편 안전하다고 확대하지 않는다.
- 작업용의 정상 내부 root 9×3·unset/outside 거부는 작업용 실행으로 구분한다.

## T2-a — 기존 일반 파일을 유효 root로 받아들임: 직접 재현

CreateDirectoryW가 실패하고 ERROR_ALREADY_EXISTS면 통과시키지만, 뒤에서는 `is_reparse(root)`만 검사한다. **FILE_ATTRIBUTE_DIRECTORY 확인이 없다.** 기존 일반 파일도 ERROR_ALREADY_EXISTS이고 reparse가 아니므로 gRoot에 들어간다.

Codex가 새 내부 폴더에 작업용 exe를 복사하고 child LOCALAPPDATA도 그 안으로 지정해 다음 경계를 직접 실행했다. 원 C: 빌드 exe는 실행하지 않았다.

| 입력 | 직접 결과 |
|---|---|
| REMOTE60_TEST_TMP unset | rc2, 실행 거부 |
| 고정 anchor 밖의 내부 검토 경로 | rc2, 실행 거부 |
| anchor 바로 아래 **일반 파일**을 root로 지정 | **HostAgent Start/Run까지 실행, 9/9, rc0** |

마지막 입력은 Codex가 직접 만든 sentinel **`.claude/test-tmp/codex-ka-punch-r3-file-20bc108`**이다. sentinel bytes는 보존됐다. 바깥 파일 삭제가 재현됐다는 주장이 아니라, **유효 폴더가 아닌 입력에서 네트워크/시험 전에 거부한다는 계약이 성립하지 않음**을 확인했다.

실행/로그 보존: **`.claude/codex_ka_punch_review/r3-20bc108/regular-file-root.log`**, exe SHA-256 **9b164760021e7a861baa2b793e16f78d0c8eccdb1d393c4686eb58e6a32c4fe9**. 이는 작업용 산출물의 Codex 독립 실행이며 독립 빌드는 아니다.

필요: 디렉터리 생성/기존 경로 처리 뒤 실제 attributes를 읽어 **조회 성공 + DIRECTORY + reparse 아님**을 모두 확인한다. 기존 일반 파일은 rc2로 거부하고 그대로 보존해야 한다. 경로가 없으면 내부 폴더를 만들 수 있다는 현재 정책은 유지할 수 있다.

## T2-b — PID 이름과 시작 시 삭제는 실행 소유 확인이 아님

현재 이름은 `host-agent-punch-<PID>.cache`이며 **시작 시 delete_own_file을 여전히 호출**한다. PID는 재사용되고, init_root는 기존 root도 수락한다. 따라서 전 실행의 파일이나 미리 존재한 같은 이름이 있으면 현재 실행이 만든 적 없어도 삭제한다. prefix/leaf 검사는 삭제 위치를 제한할 뿐 파일 소유를 증명하지 않는다.

필요: 시작 시 임의 동명 파일을 삭제하는 대신 **새 실행이 자기 경로를 배타적으로 확보**한다. 예를 들어 충분히 고유한 이름을 CREATE_NEW로 예약하고 충돌 시 새 이름/명시 실패로 처리하거나, 충돌 없이 만든 고유 실행 폴더 안에 둔다. 자기 생성/예약 성공 뒤의 파일만 종료 때 정리한다. 기존 동명 파일은 보존해야 한다. 이는 r2에서 요청한 “고정 동명 기존 파일을 먼저 지우지 않음·새 실행 소유만 정리”의 잔여 보수다.

## 과거 실행 기록의 해석

보고의 **r2 실행/변이 합계 7회가 C: Release에서 저장소 밖 DeleteFileA를 호출했다**는 정정을 기록한다. 해당 시험이 응답 없는 directory에서 새 cache를 저장하지 않았다는 설명은 코드와 일치한다.

다만 “시험이 파일을 생성하지 않음”과 “삭제 호출 전에 동명 기존 파일도 없었음”은 다른 사실이다. 원 코드가 DeleteFileA 반환을 기록하지 않았고 현재 파일도 없다는 것만으로 **실제 삭제 성공 0·기존 파일 피해 없음까지 Codex가 독립 확인했다고 쓰지 않는다**. 삭제 결과/실행 전 부재에 대한 별도 근거가 있으면 같은 보고에 연결하고, 없으면 그 부분은 보고 주장과 확인 한계로 구분한다. 과거 바깥 호출을 소급 승인하는 것도 아니다. 새 바깥 정리·권한 질문은 필요 없다.

## 다음 단계

검증용은 같은 시험 T2 보수에 두 경계를 함께 취합한다. 정상 내부 root, unset/outside, 기존 일반 파일 거부·보존, 기존 동명 파일 보존/자기 예약 파일 정리를 확인한다. 제품 N1은 이미 SOURCE OK이며 fixture 수정 때문에 무관 제품 suite를 매번 전량 반복할 필요는 없다. 고정 후보 독립 EVIDENCE와 real-directory/PC 즉시 재유지, dirretry cycleCv_ 통합 주의는 기존 판정을 유지한다.

Codex 제품 수정·빌드·운영/설치/NAS·commit/push·바깥 삭제 없음. 직접 실행과 sentinel은 저장소 안 자기 검토 자료이며 보존했다. history/계획/Git MCP 기록은 같은 FINDING 작업에 취합한다. 사용자 현재 조치 없음.
