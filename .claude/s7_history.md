
### 441) 2026-09-08 업데이트 step4-4 — "업데이트 확인" 진입점(트레이) + 확인 로직, 그리고 라우트 테스트 결함 2건 수정 (설계 3.5, 검증용 관찰 반영)
- **검증용 관찰 2건 수정(둘 다 실제 테스트 결함)**:
  - **공허하게 통과하는 단정**: `update_route_test.js` 의 `and nothing leaks in the body` 는 **응답이 아예 없으면 자동으로 만족**됐다. 즉 서버가 안 떠 있을 때 **가장 확실하게 통과**하는 단정이었고, 그건 유용함의 정반대다. 이제 `r.status !== 0` 을 함께 요구한다.
  - **연결 불가 시 빨리 실패**: 단독 실행 시 15개의 혼란스러운 실패 대신 `/healthz` 로 먼저 확인하고 **"이 테스트는 run.js 가 띄우는 서버가 필요하다"** 를 3줄로 말하고 exit 2 한다. 스위트가 서버 수명을 소유하는 것은 설계대로이므로(엔드포인트는 `REMOTE60_UPDATE_DIR` 이 있을 때만 존재한다) 테스트가 그 사실을 스스로 설명하게 했다.
- **`update_check.{hpp,cpp}` 신설** — "업데이트가 있는가" 를 묻는 일과 답하는 일을 분리한다.
  - **"확인 실패" 는 "최신" 과 다른 답이다**: `NotConfigured` / `Unreachable` / `Rejected` / `UpToDate` / `UpdateAvailable` 다섯으로 나눴다. 이걸 boolean 으로 뭉개면 **한 달째 확인에 실패한 기계가 사용자에게 "최신입니다" 라고 말하게 된다** — 이 열거형이 존재하는 이유가 그것이다.
  - **`NotConfigured` 는 네트워크를 건드리기 전에 답한다**: 신뢰키가 없는 빌드는 어떤 manifest 도 받아들일 수 없으므로 조회는 연극이고, 그때 나오는 실패는 "업데이트 서버가 고장" 처럼 읽힌다. 실제로는 "이 빌드는 아직 업데이트를 하지 않는다" 이다.
  - **호출자를 절대 막지 않는다**: 동기형은 테스트가 부를 수 있는 평범한 함수, 비동기형은 **detach 스레드**로 답을 콜백에 넘긴다. 콜백이 **워커 스레드에서 온다는 사실을 헤더에 명시**했다 — 조용히 넘기는 것이 워커가 남의 창을 만지는 경로다.
- **트레이 진입점**(`host_app_main.cpp`): `IdMenuCheckUpdate` + "Check for updates" 항목. **로그아웃 상태에서도 활성**이다 — "더 새 빌드가 있는가" 는 로그인과 무관하고, 비활성화하면 미설정 빌드가 고장난 빌드처럼 보인다. 결과는 워커가 `kUpdateCheckDoneMessage`(WM_APP+3)로 **heap 문자열 소유권과 함께 post** 하고 **UI 스레드가 MessageBox 를 띄운다**. 다섯 결과마다 문구가 다르며, 특히 `Unreachable` 은 **"최신인지 알 수 없다"** 로 적어 최신과 구별한다. 결과는 `host_app.log` 에도 남는다.
- 검증 — **`qwinsta`: `console` 만 Active, `rdp-tcp` 는 `Listen` → RDP 미접속.**
  - `remote60_update_check_test` **28 checks / 0 failed**, exit 0. 미설정 3종(**네트워크 미접촉 단정 포함**) · **Unreachable 이 UpToDate 도 UpdateAvailable 도 아님** · Rejected 3종(서명·파싱·플랫폼) · 정상 4종(신버전/동일/구버전 manifest/숫자 비교) · **비동기 2종**: 400ms 걸리는 fetcher 를 걸고 **호출이 100ms 미만에 반환**하는 것과, **실패해도 콜백이 온다**는 것("확인 중…" 에서 영원히 멈추는 UI 방지 — 서버가 죽었을 때가 바로 사용자가 보는 때다).
  - 디렉터리 스위트 전체 exit 0, 라우트 **17 checks**(reachability 1 추가). **단독 실행은 exit 2 + 3줄 안내**로 즉시 중단.
  - `remote60_host_app` 빌드 exit 0.
  - 업데이트 관련 7종 동시 재실행 전부 exit 0: version_compare **139** · manifest **41** · state_machine **57** · effects **98** · http **36** · registration **45** · check **28** = **444 checks / 0 failed**.
  - **라이브 `GNLinkHost`(5156)·`GNLinkInputService`(10820)·`GNLinkStream`(19384) PID 불변 확인.** 빌드 산출물은 `build-local/` 에만 생기며 설치본은 건드리지 않았다.
- 미검증·미착수: **빌드한 `GNLinkHost.exe` 를 실행하지 않았다**(라이브 호스트와 충돌하므로) → **트레이 메뉴를 사람이 눌러 본 적이 없다.** 진입점의 배선은 빌드와 단위 테스트까지이고 **UI 실동작은 미검증**이다. `REMOTE60_UPDATE_MANIFEST_URL` 미설정 + 신뢰키 부재이므로 현재 이 항목은 항상 `NotConfigured` 로 답한다. **클라이언트 셸 시작 시 비동기 확인은 미착수**. `Relaunch`/`HealthCheck` production, 업데이터의 `RegisterInstall` 배선도 미착수.
- 변경 파일: `apps/native_poc/src/update_check.hpp`·`update_check.cpp`·`update_check_test.cpp`(신규) · `src/host_app_main.cpp` · `apps/native_poc/CMakeLists.txt` · `apps/directory/test/update_route_test.js` · `docs/history.md` · `docs/구현계획.md`.
- 버전 인상·설치본 생성·설치·라이브 조작·서버 배포·push 없음.
- 상태: 검증용 검사 대기. **전체 완료 아님.**
