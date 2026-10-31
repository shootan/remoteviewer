# ua2-apk r1 — SOURCE 검토

2026-10-26 / 검증용 `remote#yj8mrmrx` / 입력 `.claude/report_ua2_apk_r1.md` 전문.

**SOURCE: NEEDS_CHANGES — 표시 계약 N1~N3을 한 묶음으로 보수한다.** 전역 저장값 재전송 제거와 GDI 1..3 배선은 수용한다. 최종 APK 게시 승인은 아니다.

- 고정 후보 **5f73ad121367f964f7b255b0982d9664618b593b**. 제품 **4df7db7b4969f7e570d7b31df8e42c21a54aadda**, SOURCE diff **6d5c032..4df7db7** 9파일. 시험 5f73ad1의 단위·AVD 코드도 대조했다.
- Codex 직접 확인은 고정 소스, 저장된 `ua2_7/ua2_tap_lines.txt`·Host 로그와 `ua2_3_after_pick.png` 열람이다. AVD 13/13·단위257/0·공용 세션 회귀는 **작업용 실행 보고 인용**이며 Codex 독립 실행이 아니다.

## N1 (P2) 완료 여부를 requested 값 비교만으로 추정한다

위치: `DesktopBackendPresentation.kt:present`, `HostStatus.of`, `MainActivity.kt:requestSessionDesktopBackend/updateDesktopBackendButtons`.

현재 `applying = asked != null && status.requested != asked`뿐이다. `FLAG_NOT_YET_APPLIED`는 사유 문구에만 쓰고, JNI가 전달한 seq는 HostStatus에서 버린다. 따라서 다음 두 방향이 모두 틀린다.

1. **끝난 요청을 영구 대기로 표시**: Prelogin 호스트에 WGC/GDI 요청을 보내면 `host_stage_backend`는 DXGI 전용이라 요청을 처리하되 거부한다. 해당 요청의 handled key와 requested=DXGI, active=DXGI, BEFORE_LOGIN, **NotYetApplied 없음**이 답으로 온다. 앱 asked=WGC는 계속 다르므로 모든 후속 조회에서도 APPLYING이다. “로그인 전 DXGI만”이라는 사유가 붙어도 “적용 중”은 잘못이다. 거부/현재 제한을 확정 표시하고 다시 선택 가능해야 한다.
2. **아직 끝나지 않은 요청을 완료로 표시**: 이전 snapshot의 requested가 우연히 새 asked와 같거나(같은 값 재누름·A→B→A), 최신 답에 NotYetApplied가 있는데 requested는 같으면 APPLIED로 분류한다. 이 답이 최신 선택의 처리를 입증하지 않는다. NOT_YET_APPLIED인 자체 단위 사례도 현재 APPLIED를 거부하는 assert가 없다.

필요 보수: 현재 UI intent와 실제 전송/응답의 소유를 연결하고, **현재 요청이 처리됐는지**와 **지금 사용 중인 방식**을 분리한다. 기존 wire seq와 로컬 intent/응답 태그 등으로 충분하며 새 Host 프로토콜을 요구하지 않는다. 코드 값 동일성만으로 완료를 추정하지 않는다. NotYetApplied는 처리 대기, 현재 요청을 처리한 뒤의 사유 있는 거부는 terminal, 이후 선택의 답이 아닌 늦은 응답은 이전 상태로 취급한다. idle/window 대상으로 요청을 저장한 상태도 실제 desktop 전환 성공으로 만들지 않는다.

제한 검증: prelogin WGC 거부→대기 해소·DXGI 안내, 같은 값 재요청의 NotYetApplied→처리 확인 뒤 완료, A/B/A 중 지연된 앞 응답→최신 intent 유지. 적어도 pure/native 경계의 결정적 반례와 실제 APK 상태 문구 한 사례를 고정한다. 이때 현재 확인된 active 값은 별도로 보여도 된다.

## N2 (P2) 창 대상의 실제 캡처와 비활성 desktop backend 값을 혼동한다

위치: `DesktopBackendPresentation.kt:present`의 lit/active, 적용 문구, AVD `scenario_ua2` 판정.

이것은 저장 근거에도 드러난다. Host seq7은 **requested=3, effective=3, active=1, flags=0x5**이고 직전 로그는 `desktop-backend-stored ... desktopActive=0`이다. 창은 WGC 경로인데 `backend.active`는 desktop 경로의 이전 DXGI 값이다. APK 스크린샷은 DXGI 버튼을 켜고 **“요청: GDI · 실제 적용: DXGI (창 캡처는 WGC)”**라고 쓴다. GDI 전환 실패가 아니라, 창 대상으로 보고 있어 바탕화면 요청을 보관한 상태다.

필요 보수: WINDOW_TARGET이면 **현재 창 캡처 WGC**와 **이 연결의 바탕화면 요청 GDI(바탕화면 선택 시 적용)**를 명확히 구분한다. dormant desktop active=DXGI를 현재 실제 캡처인 것처럼 점등/표기하지 않는다. 요청·effective 정책값·현재 사용 경로는 서로 다른 의미다. UI는 어느 의미의 버튼/줄인지 일관되게 정하며, host flag로 판단할 수 있으므로 이번 수정 때문에 PC0.2.163 wire를 바꿀 필요는 없다. 재시작 실패/격리/로그인 전 제한도 각각 현재 actual과 요청의 차이를 그대로 설명한다.

AVD의 현재 assert는 사유 검사에서 `or host_lit == 'DXGI'`로 창 WGC 사유 누락을 허용하고, GDI 탭 뒤에는 “요청: GDI” 존재만 본다. 위 모순을 통과시키므로 기대한 상태 전체를 검사하도록 좁힌다. **GDI 요청 1건 전달**은 이미 유효한 근거지만 **GDI desktop 전환 성공**은 아니다.

## N3 (P2) 구 Host·capability 미확인 상태에도 세션 한정을 보장하는 공통 안내

위치: `settings_desktop_backend_hint`, `DesktopBackendPresentation.present`, `nativeGetDesktopBackendStatus`의 hostAnswers, `FLAG_SESSION_SCOPED`.

구 Host에서 status 줄은 확인 불가라고 하지만, 위의 공통 hint는 여전히 **“이 PC의 이번 연결에만 적용 · 다음 연결은 PC 기본값으로 시작”**을 보장한다. 특히 요청 후 no_answer_asked 줄은 일반 “확인 불가”로 축약되어 범위 보장이 없다는 설명도 사라진다. APK 저장값을 지워도 **구 Host의 전역 override 수명**까지 고쳐진 것은 아니다.

또한 hostAnswers=false는 첫 pong 이전의 미확인 상태도 포함한다. 현재 JNI bool만으로는 “아직 capability를 못 받음”과 “이 Host는 응답 미지원”을 구분하지 못한다. 연결됨을 먼저 관측하면 새 Host도 잠시 구 Host로 설명될 수 있다.

필요 보수: capability **미확인/지원/미지원**을 구분해, 미확인은 확인 중, 미지원은 결과·세션 한정 모두 보장 불가로 설명한다. 세션 한정 보장 hint는 확인된 지원/SESSION_SCOPED 의미와 일치시킨다. 요청 후에도 legacy 범위 불명 안내를 유지한다. 미지원 Host에 새 90 조회를 보내지 않는 현재 규칙은 보존한다. FLAG_SESSION_SCOPED 없는 답도 세션 한정이 검증된 답처럼 사용하지 않는다.

제한 검증: 첫 pong 지연 중 확인 중→지원 확인, 구 Host에서 변경은 기존 wire로 보내되 조회0·선택 완료 점등0·연결 한정 단정0, 구 Host→새 Host 재연결 시 이전 상태·요청 전파0. 일반 설정 화면의 실제 문구를 함께 확인한다.

## 수용한 변경·근거 범위

- `SavedEndpoint`에서 desktopBackend를 읽지 않고 `saveEndpoint`에서 옛 키만 지운다. sharpness·quality/fps는 별도 저장으로 유지한다. 접속 자동 재전송과 Apply의 backend 요청을 제거했고 수동/디렉터리의 새 연결에서 sessionDesktopBackend를 비운다. 기존 전역 WGC가 다른 Host로 자동 전파되는 원인에 맞는 변경이다.
- Query는 backend=0이며 `queryPending_`로 보관해 pending 변경 요청을 덮지 않는다. cap 없는 peer에는 query를 보내지 않고, park inhibit 및 실제 새 연결의 backend snapshot 초기화는 유지된다. 같은 park/resume에서 메모리 요청을 비우는 코드는 추가하지 않았다.
- GDI=3은 기존 Host 요청 범위·JNI enum과 맞으며, 공용 세션 두 군데의 1..2 제한을 1..3으로 고친 것은 필수 배선 보수다. 새로운 GDI 캡처 권한을 추가한 것은 아니다. “이전 모든 배포에서 한 번도 닿지 않았다”는 전 배포 이력 단정은 현재 소스 확인보다 강하므로, 보고는 확인한 이전 경로가 3을 거부했다는 사실로 한정한다.
- 저장 로그에서 GDI 탭→요청→Host stored seq7→세션 종료 reset→새 epoch 기본 DXGI를 확인했다. default 복귀 및 폰 WGC 후 PC 영향 제거는 이미 PC163 Host의 세션 소유 계약과 연결되지만, 이번 APK 시험은 실제 desktop 전환·두 Host 교차 실행까지 증명하지 않는다.
- 조회는 `elapsed >= 2000ms`를 status poll에서 검사한다. 저장 Host 로그 간격은 약2.2~2.3초이므로 “2초 이하 보장”은 현재 구현보다 강하다. **약2초 주기(상태 poll 지연 포함)**로 보고를 고치면 되며, 이를 이유로 새 상시 타이머를 요구하지 않는다.

## 다음 담당·통합 확인

기존 UA2 APK **t-ui7wvkxk / 같은 작업용1** 정본에 N1~N3을 취합해 제한 보수·반례 → 해당 SOURCE diff로 회신한다. picker N1/N2·SH B 보수는 각 정본의 미결로 유지하며 새 병렬 writer를 만들지 않는다. PC163 게시를 다시 열지 않는다.

최종 고정 APK0.2.32에서 검증용이 일반 UI 선택→호스트 요청/이유/표시, 재접속·Host 교차 전파0, legacy/첫 pong 지연, query/변경 상관 및 기존 SH 저장 보존을 독립 확인한다. desktop 실제 전환은 격리 fixture/시험 데스크톱 등 가능한 경계를 검토하되, 사용자 현재 화면을 임의 캡처/설치/재시작하지 않는다. 자동 실행이 불가능한 부분은 정확히 분리하고 request 전달을 actual desktop 전환 성공이라고 쓰지 않는다.

Codex 제품 수정·빌드·AVD 실행·commit/push 없음. 이 미추적 판정서 외 공개 기록·Git MCP commit은 같은 구현 작업에 취합한다. 사용자 현재 조치 없음.
