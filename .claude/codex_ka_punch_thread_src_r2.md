# KA Host punch thread r2 — SOURCE 재검토

2026-10-28 / Codex remote#tt7fcvky → 검증용 remote#7fsfy22z / 기존 keepalive-r2 FINDING.

**제품 SOURCE OK — 6c3cb5a에서 N1 큐 수명을 닫는다. 다만 제출물 전체는 새 시험의 T2 경로 보수 전까지 NEEDS_CHANGES다.** 제품 보수를 다시 설계하거나 이미 닫은 N1을 재검토하라는 뜻은 아니다. 아래 시험만 안전한 실행 경계로 고친 뒤 독립 EVIDENCE로 이어간다.

## 고정 범위

- 후보 **6c3cb5a11a5d819a5b2d2d19940ab4b95bc412bf**, 비교 **a7773e6..6c3cb5a**. HEAD 일치·tracked clean·diff --check 정상.
- 실제 diff는 Host hpp/cpp와 **CMake·새 host_agent_punch_test.cpp를 포함한 4파일(+156)**이다. 제품 변경 9줄과 시험 전용 hook·target·시험을 모두 대조했다.

## N1 — 닫음

Stop은 running=false 및 worker wake 뒤 punch thread와 directory cycle **둘 다 join한 다음** punchMu_ 아래 큐를 비운다. producer가 늦게 Punch까지 진행한 경우도 join 완료 뒤의 폐기로 회수된다. Punch 자체도 queue lock 아래 running을 확인하므로 stop 상태의 추가 enqueue를 거부한다. Start의 빈 큐 보장은 보조 장치다.

이 순서는 기존의 “worker만 먼저 끝난 사이 producer가 다시 넣음”과 “Stop 잔여 train이 다음 send_로 송신됨”을 막는다. 새 Start는 직렬 Stop 반환 뒤라는 기존 호출 수명으로 해석하며, 동시에 임의의 Start/Stop을 여러 스레드가 호출해도 안전하다는 새 보장은 하지 않는다. socket은 Stop 반환 뒤 닫는 기존 제품 순서를 유지한다.

시험 hook은 새 시험 target에만 `REMOTE60_HOST_AGENT_TEST_HOOKS`로 정의되며 출하 class에는 노출되지 않는다. 작업용의 Stop clear 제거 3 FAIL·stopping guard 제거 1 FAIL은 각 보수 경계를 겨냥한다. Start clear 제거가 살아남은 것은 Stop clear와 겹치는 belt라는 설명과 일치하며, 독립 검증을 했다는 주장으로 바꾸지 않는다.

## Codex 직접 실행 — 저장소 안 사본에서 9/9

원 빌드 경로의 exe는 아래 T2 때문에 실행하지 않았다. 작업용 빌드 산출물 **remote60_host_agent_punch_test.exe**를 새 폴더 **`.claude/codex_ka_punch_review/r2-6c3cb5a/`**로 복사하고, child의 LOCALAPPDATA도 그 폴더 아래로 지정해 직접 실행했다. 기존 폴더가 있으면 덮어쓰지 않도록 먼저 거부했고 부모 절대 경로를 확인했다.

- 결과 **rc0, 9 checks / 0 failures, RESULT PASS**. 해당 폴더 `run.log`에 보존했다.
- train 중간 Stop 뒤 pending=0, Stop 뒤 추가 callback 0, stop 상태 Punch의 enqueue 0, 실제 같은 HostAgent의 두 번째 Start 뒤 **old=0 / late=0**, 새 target과 proof의 train 정상, 두 번째 Stop 뒤 빈 큐를 확인했다.
- exe SHA-256 **a3c2881b182656f5c7a447f7f39c08e7e515b90ab650dfc2d0a6a54bdb49bd0b**.
- 이것은 **작업용 산출물의 Codex 독립 실행**이며 Codex 독립 빌드나 전체 real-directory/relay 실행은 아니다. send callback은 기록기이고 실제 UDP 송신이 아니다. 시험의 실제 Start/Run은 localhost의 응답 없는 HTTP endpoint를 사용한다.
- 이 실행의 첫 health 조회는 약 6초였다. 큐/worker 정리는 확인했지만 전체 Stop이 200 ms 이내라는 성능 보장은 하지 않는다. 기존 HTTP 대기까지 이번 N1이 취소한다고 주장하지 않는다.

## T2 — 새 시험이 exe 옆 고정 cache를 무조건 삭제함

`host_agent_punch_test.cpp`는 GetModuleFileNameA로 exe 디렉터리를 구해 **`host_agent_punch_test.cache`를 시작과 종료에서 DeleteFileA**한다. root guard나 해당 실행의 소유 확인이 없다. 기본 산출물은 `C:/work/gnlink-wt/ka-r3/build/apps/native_poc/Release/`에 있으므로 거기서 실행하면 **저장소 밖 cache 삭제 명령**이 된다. 시작 시 삭제는 기존 동명 파일까지 대상으로 한다.

이는 이미 directory-retry에서 보수한 T2와 같은 경계다. 이번 Codex 실행은 exe 자체를 검증한 내부 새 폴더로 옮겼으므로 삭제 대상도 그 자기 폴더 안이었지만, **시험 소스의 기본 동작이 안전해진 것은 아니다**. 작업용의 앞선 9×3·변이 실행 위치와 실제 cache 처리 여부는 현재 보고만으로 확정하지 않는다.

필요한 보수는 시험에 한정한다:

- cache를 검증된 **REMOTE60_TEST_TMP의 저장소 안 고유 실행 root**로 옮기고, 미설정/바깥 root는 파일·네트워크 동작 전에 명시적으로 거부한다. 기존 directory-retry의 고정 anchor/경로 및 reparse 검사를 재사용할 수 있다.
- 임의의 고정 동명 기존 파일을 시작 때 먼저 지우지 않는다. 새 실행이 소유한 경로/파일만 만들고 정리한다. 비ASCII root가 가능하면 wide 경로 처리도 보존한다.
- 해당 시험/변이의 과거 실행 위치·cache 삭제 사실은 **같은 보고에** 정확히 기록한다. 새 바깥 정리나 삭제 권한 질문은 필요 없다.
- 보수 뒤 unset/outside 거부와 내부 root의 수명 9행을 확인한다. 제품 큐 변경이나 78개 무관 항목을 이 시험 경로 수정 때문에 전량 다시 고칠 필요는 없다.

## 통합 주의 정정과 다음 단계

검증용의 정정을 수용한다. directory-retry의 HostAgent cycleCv_ 변경 자체는 **d11ef62/d6bb951 계열**이고, **34f22de는 이를 포함한 고정 후보 tip**이며 그 마지막 commit은 시험 fixture 포트 보수다. 앞선 판정의 “dirretry 34f22de도 같은 파일을 수정”은 후보 전체 범위라는 의미로 읽고, 마지막 commit 자체가 cycleCv_를 추가했다고 읽지 않는다.

통합 Stop에는 **cycleCv_.notify_all()**과 새 punch worker의 wake/join, producer join 뒤 큐 clear를 모두 보존한다. 시험의 cycle gate가 독립 punch worker 전체의 정지 barrier가 아니라는 주의도 유지한다. 원 5초 병목 보수 방향과 N1 제품 SOURCE는 수용했으며, APK e r2/PC 전체 SOURCE 승인을 대신하지 않는다.

시험 T2 보수 후 같은 기능의 고정 후보를 독립 검증하고, real-directory/relay 및 실제 PC key의 즉시 crash/EndAll/해제 뒤 재유지를 31초 우회 없이 연결한다. 과거 부분 FAIL은 보존하고 최신 전체 유효 실행과 구분한다. 기록·history/계획·Git MCP commit은 같은 FINDING 작업에서 취합한다.

Codex 제품 수정·제품 빌드·운영/설치/NAS 조작·commit/push 없음. 직접 실행은 위 내부 사본의 수명 시험이며 자기 cache 삭제 외 바깥 삭제는 하지 않았다. 사용자 현재 조치 없음.
