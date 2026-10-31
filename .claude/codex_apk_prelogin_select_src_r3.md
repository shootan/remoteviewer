# apk-0232 / ① picker SOURCE r3

2026-10-27 / 검증용 `remote#yj8mrmrx` / 입력 `report_apk_prelogin_select_r1.md` r3 전문.

**제품 SOURCE OK — 잔여 N1 최종 경계 닫음. r2에서 닫은 tracker 덮기·N2 queue 허가는 유지한다. E1 시험 대역 배선은 보완 후 통합 EVIDENCE로 연결한다. 게시/실 APK 경합 재현 성공 판정은 아니다.**

제품 **9afcec7f14c9e3a83d7f81b9f41cdcb6b3f85b1e**와 시험 **3a17444851d54d53f32dc07b5f4cfbf9b134ba0e** 두 commit 전체 diff, Android sink/JNI 및 panel·controller의 관련 호출을 검토했다. 각각 whitespace rc0. 검사 시 worktree HEAD=3a17444, tracked clean. UA2 r3·SH r4 미완료 보수는 이번 판정에 포함하지 않는다.

## N1 최종 소유 경계: 닫음

- `PollLocalSelection`은 별도 CurrentTag/Bound 읽기 뒤 panel을 기록하지 않고, **CommitLocalSelectionFor(tag, callback)** 한 번을 사용한다.
- 실제 Android override는 **Prepare/Abort와 같은 LockedState(mu_)** 안에서 pending tag와 local bound generation을 확인한 뒤 callback을 호출한다. callback의 tracker CAS 및 `ApplyLocalSelectedFor`가 끝난 뒤에야 sink 잠금이 풀린다. 단순 마지막 재읽기를 더한 구현이 아니다.
- 취소/새 Prepare가 먼저 들어가면 tag/local 결합이 불일치해 callback이 실행되지 않는다. commit이 먼저 들어가면 panel 기록이 먼저 끝나고, 그 뒤 취소가 sink의 pending/ready를 무효화하거나 B Prepare가 새 tag를 설정한다. **취소가 완료된 뒤 A의 새 selected 기록이 생기는 틈은 닫혔다.**
- 이미 먼저 기록한 A의 panel 결과가 마지막 대상의 과거 상태로 남는 것과, 취소 뒤 새로 A를 완료 기록하는 것은 구분한다. P9의 합격 기준은 후자 차단이며, 취소 뒤 sink current selection/ready0도 확인한다. B는 자신의 IDR gen701/tag92로 완료해야 한다.
- Unsupported sink의 기본 callback 구현은 Abandoned로 닫힌다. 호환성을 이유로 기본 구현을 옛 “읽고 잠금 밖에서 callback” 형태로 바꾸지 않는다.

## 잠금 순서 감사

현재 제품 호출 경로에서 새 역전은 발견하지 않았다.

- 직접 local 시작: park→local→sink(함수 반환 시 해제), 이어 panel. queue 전환: local→sink, 이어 panel.
- poll 완료: **local→sink→panel**. callback은 panel 모델의 짧은 상태 갱신/CAS만 하며 sink·local·park·session mu를 재진입하거나 네트워크/JNI를 호출하지 않는다. panel 치수 helper도 snapshot만 읽는다.
- panel 모델의 관련 함수는 자체 mu 안에서 상태만 갱신하고 sink/controller callback을 호출하지 않는다. Android Prepare/Abort·codec/output pump·LockedState 종료의 decode snapshot publish도 local/park/panel로 역호출하지 않는다.
- poll은 sink callback 반환 후 local을 풀고 snapshot/session mu를 잡는다. JNI Abort는 sink 함수가 반환되어 잠금이 풀린 뒤 controller의 rekey 해제를 호출한다.
- 시험 hook은 시험 define 안에 있고, testHookMu로 hook을 복사한 뒤 그 잠금은 풀고 실행한다. 출하 target의 해당 define/표식 부재는 최종 산출물 gate/컴파일 설정 검사에 계속 포함한다.

이 판단은 현재 고정 소스 경로에 대한 것이며 향후 callback이 sink를 재호출해도 안전하다는 보장은 아니다. 인터페이스의 “commit은 sink를 다시 부르지 않는다” 계약을 유지한다.

## P9 근거와 E1 시험 배선 보완

Codex는 저장 `psel_r3.txt`·`psel_r3_after.txt`의56/0과 `psel_r3_mut.txt`의 P9 두 FAIL을 직접 읽었다. r2 모양 변이는 **at ui done=0 → final window_selected**, 보수는 **at ui done=1 → final window_selected**로, 완료와 UI 취소/Prepare의 순서 차이를 검사한다. role_park13/shared_core·빌드/복원 해시는 작업용 보고다. Codex 새 실행은 없다. GateSink는 Windows용 codec/sink 대역이며 실제 Android LockedState가 실행된 증거라고 확대하지 않는다. Android 구현의 경계는 이번 소스 대조, 실제 APK의 표시 행동은 통합 AVD로 나눈다.

**E1(시험 전용 필수 배선):** `viewer_prelogin_login_e2e_test.cpp:984~1003`의 `--android-select`용 GateSink는 BeginLocal/LocalBound를 구현하지만 **새 CommitLocalSelectionFor override가 없다**. 기본 Abandoned가 호출되어 local tracker가 지워지므로, 이 고정 소스에서 과거37/37 결과를 재사용할 수 없다. Codex가 그 exe를 실행해 실패시킨 것은 아니며 소스 호출 경로로 확인한 누락이다.

이 시험 sink에도 실제 Android와 같은 **자체 mu 안의 tag/bound 검사+callback** 배선을 추가하고 필요한 해당 target만 재빌드해 실제 Host `--android-select` 행동을 다시 확인한다. 제품 코드를 다시 고칠 이유는 아니며, 같은 검증용이 시험 diff를 확인하고 최종 EVIDENCE에 함께 기록하면 된다. 이 명백한 시험 연결 보완 때문에 별도 제품 SOURCE 승인 왕복이나 전체 PC 재빌드를 만들지 않는다. 실제 APK AVD의 로그인 전 바탕화면 선택→decoder→최종 화면 표시·취소/재선택/SH on/off는 그대로 필수다. 실 OS Winlogon·실폰/입력 버퍼 고갈은 별도 미검증을 유지한다.

같은 담당은 E1을 기존 picker 시험 작업에 취합하고 UA2 r3·SH r4 잔여를 이어간다. 그 뒤 통합 제품/시험 commit·APK 해시를 새로 고정하여 검증용 독립 행동·gate·서명자/버전/ABI 확인→Codex EVIDENCE→기존 승인된 게시 절차로 연결한다. 현재 APK0.2.32 게시 완료나 전체 제품 OK는 아니다. PC163 게시 재검토·새 병렬 writer는 없다.

사용자 현재 조치 없음. Codex 제품 수정·빌드·시험 실행·commit/push 없음. 미추적 판정서/인계 외 공개 기록/계획·Git MCP commit은 같은 구현 작업에 취합한다.
