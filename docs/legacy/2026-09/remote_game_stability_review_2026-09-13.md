# 안정화 후보 독립 검토 보수 — revision 2

검토 task: `t-nbc17io0`, 검증용 `remote#wl89o85z`, 구현/보수 Codex `remote#03k4xd5j`.
**상태: revision 2 재검토 요청 준비. main merge/배포는 수행하지 않았다.**

## revision 1 판정

검증용은 소스/제출 근거를 독립 대조하고 `NEEDS_CHANGES`를 반환했다. **코드의 merge blocker는 없고 A/B 문서 보수 후 머지 가능**, **배포는 C의 실제 GPU 부하 측정 전 불가**라고 구분했다. revision 1 테스트 수치는 제출 로그를 확인한 것이며 검증용이 재실행한 수치가 아니었다.

## 보수 결과

| 항목 | revision 2 처리 |
|---|---|
| A: 두 벌의 문서 | 기준 본문이 같음을 확인한 뒤 주 트리의 추가 이력/계획과 후보 추가분을 모두 보존해 후보의 history/계획에 통합. incident 3종은 후보 상위집합 유지, evidence 목록 동일. 주 트리 원본은 무수정 |
| B: F-23 미수정 표찰 | main `1261ef5`의 ledger를 가져와 switch 뒤 기본 반환 수정·미지 WM_TIMER 검사·잔여 상태를 갱신. F-22 머리말도 본문의 기존 수정 완료(history #412)와 일치시킴 |
| D: WSAEINTR | ingress의 recv/select 오류 분류에서 WSAEINTR 재시도를 보존. timeout/would-block/oversize는 계속 비종료, connection-reset은 기존처럼 종료 |
| E: 짧은 소비 버퍼 | Pop에서 이미 꺼낸 패킷을 버릴 때 dropped 카운터 증가. 실제 2바이트 datagram→1바이트 소비 버퍼로 검사하고 다음 패킷 수신도 확인 |
| F: 스레드 주석 | ingress만 소켓을 읽으며 OnPacket을 공급하고, 여러 스레드의 Tick은 channel mutex로 직렬화됨을 실제 코드와 맞춤 |
| G: 환경변수 64자 | 동적 UTF-16 환경 snapshot/RAII 복원 사용. 4096자, 미설정, 빈 값의 실제 Windows 환경변수 왕복 검증. snapshot 실패 시 원본을 덮어쓰지 않음 |
| H: 자동 격리 해제 의견 | **유지하기로 결정**. 원인 미확정 상태에서 일정 시간 정상 실행은 DXGI 회복 증거가 아니므로 자동 재진입을 추가하지 않음. 새 child 기본 backend의 WGC 격리는 supervisor 재시작 시 초기화되며 원래 환경값 복원. 드라이버 회복 판단/선택적 재시험은 후속 정책 |
| C: GPU 작업 증가 | **배포 blocker 유지**. content마다 최신 픽셀 소유는 유지해야 하지만 버려질 프레임의 색공간 변환/전처리를 늦추는 설계와 실제 GPU 부하 측정은 아직 수행하지 않음. 144Hz 입력·30fps 소비 등의 조건에서 검증 필요 |

## Git 통합 경계

dirty 주 트리에서 직접 checkout/merge하면 미추적/미커밋 사본 때문에 거부될 수 있다. 이는 **clean main worktree에서의 통합까지 금지하는 Git 조건은 아니다.** 주 트리 사본을 임의 정리하지 않고, 향후 별도 clean main 작업 트리에서 통합해야 한다.

후보 기준은 문서 전용 커밋 하나를 포함한 **`1261ef580d5a1dbeec1a14723f2d1564834c0a95`**로 전진시켰다. 사전 조건은 후보 branch/HEAD 일치 및 staged 변경 없음이었다. 인덱스/후보 branch 기준만 변경했고 기존 working file SHA-256은 전부 동일함을 확인했다. **main 포인터나 주 작업 트리를 수정한 것이 아니다.** F-23 ledger가 양쪽에서 별도 추가된 것처럼 충돌하지 않도록 현재 main을 기준으로 보수했다. 새 제품 커밋은 아직 없다.

다른 Host/PC 감사 작업의 이력/체크리스트는 삭제하지 않았다. 그것이 다른 작업의 제품 변경이나 문서 병합까지 승인한다는 뜻은 아니다. 해당 원장 링크/소유 작업의 통합 여부는 실제 merge 시 다시 확인한다. 증거와 합본 제안은 `.claude/review-integration/`에 보존했다.

## 보수 검증

- `review-r2-build.log`: ingress/환경 테스트 및 Viewer/Host 빌드 성공.
- `review-r2-results.json`: UDP ingress와 환경 snapshot 테스트 exit 0 및 각 exe 해시.
- `review-r2-ui-result.log`: **42 passed / 0 failed**, F-23의 미지 WM_TIMER 기본 반환 검사 포함. 실제 WndProc 테스트이며 원격 게임 실기를 대신하지 않음.
- `review-r2-gate.log`: missing 2, zero-present/freeze/stutter 1, healthy 0의 양성/음성 대조.
- 4096자·미설정·빈 환경값 복원은 실제 Windows API 실행. WSAEINTR는 공유 분류 함수의 fault 값 검사이며 OS에서 실제 네트워크 중단을 발생시킨 실기는 아니다.
- 원래 1080p Viewer 표시/UDP 복구 전량 기록은 **revision 1** 증거다. GPU 부하 미측정을 감추거나 revision 2에서 재실행한 것처럼 표기하지 않는다.

원래 manifest는 `build-incident/candidate-manifest-r1.json`에 보존한다. revision 2의 현재 코드·문서·실행 파일 해시는 `build-incident/candidate-manifest.json`을 따른다. 검증용에 새 해시를 전달한 뒤 코드/문서를 다시 고정하고 같은 task에서 최종 merge 판정을 받는다. Git MCP commit·main merge·배포 승인은 이 문서 자체로 대체하지 않는다.

## 문서 보수 2 — BOM 및 입력 보존

뒤늦게 도착한 revision 1 제안본 검토에서 history의 UTF-8 BOM 손실이 확인돼 복원했다. **history 본문 바이트는 그대로이고 선두 EF BB BF만 추가**했으며, 계획 파일의 BOM 없음 상태는 유지했다. 주 트리 원본 두 파일은 byte hash가 그대로다. 제품 소스/실행 파일은 revision 2와 동일하며 다시 빌드하지 않았다.

보수 전 revision 2/주 트리 입력은 `.claude/review-integration/inputs/`에 raw copy로 고정했다. raw SHA-256과 BOM 제거·CRLF 정규화 SHA-256을 구분한 `bom-repair-manifest.json`으로 검증한다. 최초 제안본은 덮지 않고, BOM 보완 제안은 `bom-corrected/history.md`에 따로 둔다.

후보 문서 적용은 revision 1 검토 종료 후 보수 지시(seq2217)와 revision 2 준비 단계에서 수행했다. 최초 제안 생성 당시의 '원본 무변경' 상태와 이후 후보 보수 상태를 구분한다. 주 작업 트리 원본은 두 단계 모두 무변경이었다.

문서 보수 전 전체 manifest는 `candidate-manifest-r2a.json`에 보존한다. 현재 manifest의 `review_revision=2, documentation_revision=2`는 코드 재변경이 아니라 이 문서 보수를 구분한다.
