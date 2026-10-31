# PC0.2.162 통합 후보 — Codex SOURCE/EVIDENCE 판정

2026-10-22 / task **integ-0162 (t-gsmwtxgn)** / 후보 **9f5491216c8eb8ef87d5f1d702e49193f15dc68b**.

**통합 구성·격리 SOURCE/EVIDENCE OK. rel/0.2.162의 실기용 후보 준비를 승인한다.** LK e728485 + DW fc4cd57 + hello d0fe74e(제품은6e89962와 같고 qemu 귀속 문서 정정)를 합친 범위다. **일반 릴리스/게시·설치·실기 성공·task 완료 판정은 아니다.** SH/새 APK는 이 PC 통합에 포함되지 않는다. 현재 통합 코드의 추가 보수 요청은 없으며 아래 보고 정정과 남은 실기/ledger를 유지한다.

## 병합을 직접 재현한 결과

- 작업용 통합 보고(r1/r1b/r2), 독립 `C:/work/gnlink-wt/verify-integ-0162/.verify/verify_integ_0162.md`, 해당 제품·시험·gate 경로를 읽었다. 양쪽 고정 HEAD/tracked clean 확인.
- Codex가 Git blob으로 별도 대조했다: main83cbd78의 apps는5063cd9와 같고, 통합 apps **94개 파일 집합이 LK∪DW∪hello∪r1b와 정확히 일치**, 빠짐/추가0이다. 원 브랜치 또는 r1b 하나에 귀속되는 **89개 blob이 그대로 동일**하다.
- 겹친5파일은 base5063cd9로 LK→DW→hello `git merge-file -p`를 독립 재현했다. **viewer_control_client.cpp의 승인된 Run 충돌 한 곳만** LK의 runGen 대입(fetch_add1회) + DW의 capability reset을 보존해 해소하면, **5개 모두 통합 blob과 원본 바이트까지 동일**하다. 재현 자료는 repo 안 `.claude/test-tmp/codex-integ-merge-9f54912/`에만 썼고 제품 파일을 고치지 않았다.
- 실제 Run은 runGen 증가1회→SetHostSupportsMonitors(false)→hostRoleKnown 초기화→L5 stale password zero/폐기 순서다. 양쪽 의미를 보존했다. docs/history의 양쪽 기록·qemu 귀속 철회(d0fe74e), gate 표식 합집합도 확인했다.
- merge commit은 두 parent를 보존한 Git CLI 사용으로 보고됐다. 앞으로 승인한 version/기능 기록 commit은 사용자 규칙대로 Git MCP로 한다. 기존 병합 이력을 다시 쓰라고 요구하지 않는다.

## 산출물·출하 분리

독립 build-vs의 **9개 exe SHA256을 모두 직접 계산해 기록과 일치 확인**했다. 주요 값:

| 산출물 | 독립 SHA256 |
|---|---|
| Stream | `b5e8a6440d6fbec0a1c09cf85f009862ce10e8b6723a7325741d116bf6c47daf` |
| Host | `e2232198444b1eefe2c7aa328417447638cb78ed6f11f322412f3aa79e28778b` |
| Viewer | `470d275d4e2e3f6ebc3ac54d97c7b378e456ea69209833a10baccdf480a5b508` |
| Client | `0d88e37bca351f2cd476563b2c932f9637d8570e6c44ef2f8047622006d0cd8f` |
| InputService | `00ecf92e58943d83756a2b0d31b171ec3671947e485cb408afdc4baa4e66daf4` |
| Updater | `f35c5f43b8503d9029326ff1b5b9821b62fef031db30daa798d14fe72f961fa0` |

Capture/ClipHelper/Setup도 같은 독립 기록의 해시와 일치했다. shell.html은 source·installer_payload·Release/ui **세 파일을 직접 계산**해 모두 `8fac49cd810e1445d36305bf5e62e9e539982fb0b671be86cb9008ee5ad62811`임을 확인했다.

읽기 전용인 `.verify/defines_check.py`를 검토 후 직접 실행했다. Release target9개 및 **ProjectReference 전이 대상·파일별 define·AdditionalOptions /D**에서 시험/변이 정의0, 필수7개 시험 target 양성 대조 ALL PASS다. 매크로 부재를 exe 문자열 grep으로 증명한 것이 아니다. gate9/self23 및 출하9exe 표식30종0/시험5exe 양성은 별도 원문 기록으로 확인했다. 통합 합집합과 hello compile 정의 검사가 서로 보완한다.

현재 버전은 **0.2.161**이다. `product_version.hpp`의 UTF-8 BOM(`efbbbf`)을 직접 확인했다. 이 해시는 아직0.2.162 릴리스 산출물 해시가 아니다.

## 동작 근거와 시험 층

- 통합 독립 논리·변이 원문/요약을 확인했다: LK lockcred54/channel15/relay41/target21/refusal4/policy17·17/menu/seal, DW store64/quarantine23/restore30/snapshot7, hello28/punch 및 두 변이 기대 FAIL. 실행rc·빌드rc0은 검증용 귀속이다.
- 독립 UI: LK44/79/28, recovery_ui84, desktop38/presentfail37/monitor44/nomonitors29, **lockscreen 교차53/0**, 서비스 대역 없는 부정 대조의 LK4항목 FAIL을 확인했다. 원문에서 새 뷰어에도 id7·id3가 분리 표시되고 목록 요청1회·글자 보류·LockCredV1 재광고가 유지되는 것을 대조했다. Busy PNG와 교차 사슬 restored BMP를 직접 열어 두 버튼과 새 스트림 화면을 확인했다.
- hello9회≤547ms·recvErrors0은 이 loopback fixture의 관찰이다. 임의 실제 망의 최대 지연을 보장하지 않는다. 같은 프로세스의 L5 stale 요청 정리와 DW의 **새 프로세스 재연결**을 혼동하지 않는다. 이번 후자는 구 프로세스의 비밀을 새 프로세스에 옮기는 경로가 없고, 전자는 기존 LK 근거와 병합 의미 보존으로 유지된다.
- **출하 Viewer 바이너리**가 실행된 사슬은 desktop/monitor/nomonitors/lockscreen이다. presentfail은 **GNLinkViewerR3Test**, LK e2e는 **시험 exe 내부의 제품 Viewer 코드**다. 셸도 GNLinkClient 출하 exe가 아닌 제품 셸 시험 빌드이며 Host/Stream/서비스도 격리 시험 빌드다. 서비스는 비-SYSTEM 대역, WTS 잠금은 가짜다. **OS 설치 실기와 구분**한다.

작업용 보고 r2의 hello 표에서 presentfail 행을 “출하 GNLinkViewer 측정”이라고 쓴 부분은 **R3Test로 정정**한다. 동일 hello 코드여도 출하 exe 실행 사실로 바꾸지 않는다. 관련 보고/메타 정정은 같은 기록에 취합하면 되고 제품 재검토를 다시 받을 필요는 없다.

## 하네스 실패·잔여 ledger

- lockscreen 첫 회는 서비스 대역 pipe readiness 대기 만료로 **제품 항목 전 실패**다. 이를 제품 성공으로 세지 않고 시험 준비 INVALID로 남긴 처리와 재실행53/0·C++ LK pipe 근거·정확한 부정 대조를 함께 수용한다. 현재 후보의 rel 준비를 막지는 않는다.
- `fs.existsSync`가 원인이라는 설명은 **가설**이다. service diag 시작1줄과10→30초 증가 뒤 재발만으로 실제 서비스 readiness/조회 오류 중 어느 쪽인지 확정하지 않는다. 같은 시험을 성공할 때까지 무한 반복하거나 대기를 계속 늘리지 않는다. **시험 준비/pipe readiness 관측 후속**으로 기록·담당을 연결한다. 하네스만 보수할 경우 해당 준비/교차 사례를 검증하고, 제품 의미가 바뀌면 새 고정 후보로 검토한다.
- directory_retry 상태 문구 간헐(t-azc166pv), viewer_udp_recovery의 기준/후보 공통 처리량 FAIL(원인 미확정/qemu 귀속 철회), SH의 수동IP/control·clipboard 실패 관찰은 별도 ledger/조사 상태를 유지한다. 이번 통합 핵심 영향 시험을 통과했다는 말과 **전체 회귀 결함이0**이라는 말은 다르다. 미승인 실험/별도 수정은 rel 브랜치에 끼워 넣지 않는다.
- 실제 SYSTEM Session0→콘솔 agent/최종 차단/LogonUI 교체·Enter·해제/재부팅/UAC·SCM(OS A~D), 회사 PC NVIDIA·실 release-phase wedge·ProgramData 영속 기록·실 두 번째 출력, Android 실기·실 LAN/NAT는 미검증이다. minidump·MT 출하·Bⓑ2 보류를 유지한다.

## 승인하는 다음 단계 — 로컬 실기 후보 준비

검증용이 정본 하나와 실제 담당/task ID를 연결해 **rel/0.2.162를9f54912에서 준비**한다. 이미 해당 release task가 있으면 재사용한다.

1. **PC 버전0.2.162 인상과 관련 기록**을 Git MCP commit으로 묶는다. `product_version.hpp`의 BOM·기존 줄바꿈을 보존하고 diff가 버전/허용된 기록뿐인지 확인한다. 계획 문서는 checkbox/상태만 갱신한다. 현재 포함 기능은 LK/DW/hello이며 새 SH APK·S5 미결을 해소했다고 하지 않는다.
2. 새 버전 commit에서 빌드/실기 후보 설치본·rollback 자료를 준비한다. 고정 산출물에 대해 검증용이 버전·hash·gate/compile 정의·shell parity·영향 smoke를 확인한다. **버전 내장4exe(Viewer/Stream/Host/Client)는0.2.162이며0.2.161 잔존이 없는지** 확인한다. InputService/Updater는 원래 version literal이 없으므로 없는 문자열을 억지로 추가하는 변경은 하지 않는다. 이전 통합 hash를 새 rel hash로 재사용하지 않는다.
3. 후보 검증 뒤 필요한 **로컬 서명/서명 검증과 배포 스크립트 `--dry-run`**까지 허용한다. 기존 PC 보호키만 사용하고 키 내용을 로그/외부로 옮기지 않는다. `--dry-run`은 게시가 아니다. **NAS 실제 업로드·manifest/sig 교체·정상 자동 업데이트 게시·git push는 이번 승인에 포함하지 않는다.** OS 실기 핵심이 아직 미검증인 실기 후보이기 때문이다. 이번에는 서버/다른 서비스·권한을 바꾸지 않는다.
4. **시험 설치본과 사용자 실기 묶음**을 구체적으로 준비해 위치·버전·build commit/hash·서명자·기존 설치 영향·되돌리기·필요한 최소 사용자 단계(A~D+회사 PC)를 한 번에 보고한다. 자동으로 사용자 Host/Client/APK를 설치/재시작·잠금/로그오프/재부팅·서비스 교체하지 않는다. 설치·실기 행동은 그 정확한 묶음에 대한 사용자 지시에 따른다. 준비가 끝나기 전에 추상적인 설치 허가부터 묻지 않는다.
5. 실기 안내에는 **새 Host는 APK0.2.30의 잠금 화면 평문 암호 입력을 차단**한다는 호환성 영향을 명시한다. SH/새 Android sealed 대응은 포함되지 않았다. logical desktop만 자동복원하고 explicit monitor/window는 picker에서 재선택하는 제한도 명시한다. 기존 개인 비밀번호를 파일/명령줄/로그에 적게 하지 않는다.

다음 결과는 **버전이 확정된 로컬 실기 후보 FINAL_PACKET**으로 받는다. 제품 기능 변경이 없으면 승인된 범위를 다시 전면 검토하지 않고 version/산출물·보고 정정·남은 실기를 확인한다. 실제 게시/실기 성공·task 완료는 별도 근거에 따른다. root 밖 worktree/junction/증거 삭제는 계속 금지다.

Codex 수행은 Git blob/3-way 독립 재현(자료는repo 안), 해시9개·HTML3개 대조, 읽기 전용 compile 정의 검사, 원본 로그/이미지 검토 및 미추적 판정서/인계 작성이다. 제품 수정·빌드/제품 시험·설치/서비스·서명/게시·commit·push는 하지 않았다. 사용자가 지금 할 일: **로컬 실기 후보 준비 보고 전까지 없음**.
