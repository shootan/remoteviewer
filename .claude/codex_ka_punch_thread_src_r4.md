# KA punch thread r4 — 시험 T2 SOURCE 판정

2026-10-28 / Codex remote#tt7fcvky → 검증용 remote#7fsfy22z / 기존 FINDING.

**SOURCE OK — 534242b에서 T2-a·T2-b를 닫는다.** 제품 N1의 기존 SOURCE OK도 유지한다. 이 기능의 SOURCE 보수는 통과했으며 독립 real-directory/PC EVIDENCE·통합·출하 완료는 별도다.

## 고정 대상과 보수

- commit **534242b8186d5951318c6f39d5f19326c51c2446**, 비교 **20bc108..534242b**는 host_agent_punch_test.cpp 1파일(+33/-12), 제품 변경 0. 검사 당시 HEAD 일치·tracked clean·diff --check 정상.
- T2-a: 생성/기존 경로 처리 뒤 GetFileAttributesW의 **조회 성공 + DIRECTORY + 비reparse**를 모두 확인한다. 일반 파일이 ERROR_ALREADY_EXISTS로 통과하던 경계를 제거했다.
- T2-b: 검증한 root 안에서 **CreateDirectoryW 성공으로 새 실행 폴더 소유를 확보**한다. 기존 이름과 충돌하면 그 경로를 쓰거나 지우지 않고 다른 이름으로 제한된 재시도 후 실패한다. 시작 시 cache 선삭제는 제거됐다.
- cache와 종료 삭제는 그 새 폴더 안의 한 파일로 한정한다. RemoveDirectoryW는 자기 실행 폴더만 비재귀로 처리하므로 예상 밖 파일까지 전량 지우지 않는다. 현재 고정 anchor/상위 경로가 일반 디렉터리라는 전제를 유지하며 임의 링크 교체까지 보편 안전하다고 확대하지 않는다.

## Codex 직접 실행

작업용 산출물을 새 내부 폴더 **`.claude/codex_ka_punch_review/r4-534242b/`**로 복사하고 child LOCALAPPDATA도 그 아래로 격리했다. 원 C: 빌드 exe는 실행하지 않았다. exe SHA-256 **bbc2394aa807c16c7a53ec1934418817e81dc93ac964d4e52fff0c844b18e934**.

| 입력/행동 | 직접 결과 |
|---|---|
| root unset | rc2, HostAgent 실행 전 거부 |
| anchor 밖 내부 검토 경로 | rc2, 실행 전 거부 |
| 이전 반례의 일반 파일 root | **rc2, 파일 bytes 보존** |
| 유효 root에서 실제 Start/Punch/Stop/재시작 | **9/9, rc0, RESULT PASS** |
| 유효 root에 미리 둔 host-agent.cache | **내용 보존** |
| 정상 실행 후 run-* 폴더 | **잔여 0** |

정상 root는 **`.claude/test-tmp/codex-ka-punch-r4-534242b`**, 일반 파일 sentinel은 앞선 **`.claude/test-tmp/codex-ka-punch-r3-file-20bc108`**다. 정상 실행 원문은 검토 폴더의 `run.log`에 보존했다. 거부 사례의 FAIL/rc2 출력은 의도한 선거부이며 제품 수명 시험 실패로 세지 않는다.

이것은 **작업용 산출물의 Codex 독립 실행**이다. Codex 독립 빌드·전체 real-directory/relay 실행과 구분한다. 기존 9행은 Stop 뒤 큐 0·추가 callback 0·stopped Punch enqueue 0·실제 재시작 뒤 old=0/late=0·새 train 정상·재Stop 빈 큐를 포함한다. send callback은 기록기이며 실제 punch datagram 송신 증거는 아니다. 최초 health 대기 약 6초를 포함해 전체 Stop의 200 ms 상한을 주장하지 않는 기존 경계도 유지한다.

## 과거 기록과 후속

- 과거 C: 실행 7회의 바깥 DeleteFileA 호출 사실과 **시험이 새 cache를 쓰지 않았다는 것까지만 확인**했다는 보고 정정을 수용한다. 실행 전 동명 부재/반환값 기록이 없어 실제 삭제 성공 0은 독립 입증되지 않았다는 한계를 유지한다. 현재 부재를 과거 무해 증거로 바꾸지 않는다.
- 시험 소스 상단의 옛 주석에는 아직 `so those deletes found nothing`이 남아 있다. 현재 보고와 같은 확인 한계로 **동일 작업 기록 취합 때 문구만 맞추면 된다**. 이 사소한 주석 정리를 이유로 새 제품 revision·단독 재승인·전체 재시험을 요구하지 않는다.
- 검증용은 이 고정 SOURCE 후보의 독립 EVIDENCE로 진행한다. 실제 directory/relay의 연속 capability 수거와 즉시 crash/Release 재입장, 실제 PC key의 crash/EndAll/해제 뒤 재유지는 31초 우회 없이 연결한다. 작업용 78/0·55/0 등과 Codex의 이번 9행 실행을 합쳐 이미 그 전체를 독립 완료했다고 보고하지 않는다.
- dirretry d11ef62/d6bb951 계열의 cycleCv_ notify/gate와 새 punch worker stop/join/clear를 모두 보존하는 통합 주의, APK e r2/PC의 별도 미결은 유지한다.

Codex 제품 수정·제품 빌드·운영/설치/NAS·commit/push·바깥 삭제 없음. 직접 실행은 저장소 안 사본이며 자기 실행 폴더 정리 외 기존 sentinel/검토 자료는 보존했다. history/계획/Git MCP 기록은 같은 FINDING 작업에 취합한다. 사용자 현재 조치 없음.
