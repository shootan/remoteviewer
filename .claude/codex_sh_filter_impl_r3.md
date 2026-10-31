# SH r3 — Codex SOURCE/EVIDENCE 검토

2026-10-22 / task **t-b7ss11ov** / 후보 **c494dc656b9aff372fd4d03997c8a1e44e514eea**.

**SOURCE / 기능별 EVIDENCE OK — 코드 보수·출력/기하 검토를 닫는다.** r2 R1/R2와 독립 touch_host 보완을 확인했다. 다만 **control 실패 관찰 미해결 / S4 AVD 미달 / 실폰 미검증**이 남아 전체 기능 완료·연결 안정성 OK·폰 실기만 대기·통합/출하 승인으로 올리지는 않는다. 현재 SH 코드에 새 보수 지적이나 같은 시험의 추가 반복 요청은 없다.

## 닫는 항목

| 항목 | 판정 |
|---|---|
| R1 release seam 차단 | **OK.** release는 자기 source set의 noseam, native seam은 debug 한정. resolved task graph에서 Release 작업을 거부. 독립 assembleRel/aR/assembleRelease/build/assemble/bundleRel 거부 및 debug+seam·noseam release dry-run 허용 대조 확보 |
| R2 첫 frame metadata | **OK.** ring의 ns/meta/size→key 공개 뒤 buffer release, release 실패 시 해당 key 회수. handoffNs는 release 호출 직전 발행 시각으로 명시. 고친 순서 첫 출력 latchesBefore0, r2 순서의 늦은 entry는 첫 출력 미확인/안전 fallback이라는 독립 반례 확보 |
| H1/H2 출력·수명 | **보완 확인.** 독립 hold11/0·VIEWER never7/0: BLOCKED 중 producer0·요청 기억·늦은 해제 뒤1회 복귀·SurfaceTexture1회 정리, neverDraw 정확 안내/원본 새 화면 |
| H3/H5.3 통계·크기 | 이전 구간/nearest-rank 보수 유지, 독립 resize8/0. 새 fixture가 발견한 no-ring quarter-turn step의 불필요 swap도 수정 확인 |
| R3 shader fixture | **해당 fixture 범위 OK.** 실제 생산 renderer의 BufferQueue→OES→shader→ImageReader 출력, bottom padding/crop·mirror·rot90·no-ring 반례 및 실제 unclamped/noswap 부정 대조 확보. CPU 기대 수식만 실행한 시험이 아님 |

fixture는64×48→64×40 cropRect와 transform을 각각 공급한 것이다. 임의의 모든 crop origin/transform 조합이나 실제 Codec2 색 변환·SM-S948N1088을 증명했다고 확대하지 않는다. sourceOverride는 decoder ring이 없는 fixture 공급 경계이며 실제 APK decoder ring→renderer 크기 경로는 독립 resize/race로 따로 확인한다.

## 직접 대조한 근거와 귀속

- 작업용 `.claude/report_sh_filter_impl_r3.md`, 독립 기록 `C:/work/gnlink-wt/verify-sh/.verify/verify_sh_r3.md`, r2→r3 관련 제품/빌드/gate/fixture/하네스 diff를 읽었다. 고정 HEAD·tracked clean·CR-at-EOL 허용 diff-check 확인.
- 독립 APK/fixture Host hash 직접 일치:
  - noseam **0d44725b882837ec30bebed168a48d6b433385be8cbc5322491ffd843b9be74a**
  - shseam **f8218612a6990a6d45caa45fe19bea8b1ef84fa265dde92ebca6a0adcd28aca9**
  - Host MigrationTest **0771e768a2ae4c459fdebaae0b047fced4e4465915a0357e8674574a6f152f56**.
- APK는 검증용의 독립 빌드다. Host는 작업용 DW 빌드에서 hash로 고정해 복사한 fixture이므로 검증용이 Host까지 새로 빌드했다는 뜻은 아니다. 이 공유 fixture의 한계를 명시한 독립 Android 실행 근거로 수용한다.
- 독립 `.verify/run_*.log`와 gate 원문 확인: compare25, hold11, viewer_never7, resize8, race5, shader25, latency2 수집, noseam gate7/shseam gate2FAIL. 종료코드와 dry-run 전체 조합 결과는 검증용 기록 귀속이다. 단위249/0은 작업용 인용이다.
- shader crop max오차2.0/2.4, mirror/rot90 약0.45, magenta0, unclamped37.0/noswap34.85는 부정 대조로 검출됐다. race의 구식 순서 fallback은 수정 버전 실패가 아니라 의도한 원래 결함 대조다.
- 비교 crop `compare_A_off_weak_strong.png`를 직접 열어 단계별 획 가장자리 변화와 위치를 확인했다. 이것을 모든 콘텐츠의 halo 없음·사용자 선호·손실 정보 복원으로 일반화하지 않는다.
- Codex는 소스·원문 로그·해시·캡처 대조를 했으며 AVD/제품 빌드·실행·기기 조작을 직접 하지 않았다.

## touch_host 독립 추가 — 기하 검증 닫음, control 실패 분리 유지

작업용18/0×2에 더해 검증용이 동일 c494dc6 독립 APK/고정 Host로 두 회 실행했다. 추가 기록 `verify_sh_r3.md`의 "touch_host 독립 (r3 추가)"와 repository 안 `.claude/test-tmp/verify-sh-touch-r3/` 원문을 직접 대조했다. C:/work 자료 삭제 없이 repo 안 출력 경로를 사용했다.

- **첫 회 rc1, FAILED6 checks/5failed:** GUARD는 통과했으나 첫 탭 전 연결 실패. Host window-select pending→applied(10:28:00.632)→TCP disconnected(10:28:00.633), 앱 control loop failed. 좌표는 전부 미판정이고, 제품 control 실패 관찰로 유지한다. 환경 INVALID/PASS로 세지 않는다.
- **두 번째 rc0, ALL PASS18/0:** owned 패턴 창 적용/background_message GUARD 후 앱 native 큐→Host→창의 MouseDown/Up 수신이 off/.20/.35 각각(670,569), 크게보기(.20)는(669,569)로 **모두Δ0px**다. `attempt2/run_touch_host.log`와 8줄의 `touch_host_received.txt`를 직접 확인했다. 크게보기 PNG도 열어 확대 상태 및 화면 이동/전체보기 버튼을 확인했다. 실제 SendInput/실폰 시험이 아니다.

**남았던 독립 터치 기하 근거는 확보됐으므로 닫는다.** 첫 회 실패는 이 성공으로 없어지지 않는다. 앱/Host의 개별 로그 시각을 읽었지만 PC/AVD 시계의 정확한 오프셋을 실측했다고 하지 않는다. worker의 clipboard sync failed와 이번 control loop failed는 문구/발생 단계가 다르며 동일 원인으로 합치지 않는다. 약10초 뒤 UDP 세션 종료 관찰도 성공/실패 두 회에 모두 있었으므로 그것만으로 이번 TCP 실패 원인을 확정하지 않는다. 성공 회차의 실제 PC clipboard 변경 관찰 뒤 실패가 없었다는 점도 원인 증명이 아니다.

검증용은 **control 실패·worker clipboard 실패·수동IP10초 현상을 원인 미확정 관찰로 구분한 ledger와 다음 담당을 유지**한다. 보존된 로그에서 제한적인 원인 분리는 계속할 수 있으나 SH에 Host/control 개편을 추가 승인하지 않는다. 별도 hello 수정의 동일 원인/해결 효과라고 단정하지 않는다. 관련 연결 신뢰성의 영향/잔여 상태를 최종 통합 후보에서 분명히 보고해야 한다. 이를 이유로 SH 코드·이미 확보된 기하/출력 시험을 무한 반복할 필요는 없다.

## S4와 남은 실기/기록

독립 whole-segment p95는 off74.7/72.8ms, .20=87.9ms, .35=87.8ms로 **AVD SwiftShader +13.1~15.1ms, +4ms 기준 미달**이다. latency2PASS는 수집 통과다. 독립 기록의 “어느 쪽도 S4 판정 근거가 아니며 폰 실측으로만 판정”은 **실폰 S4 판정으로 전용하지 않는다**는 의미로 좁혀 적는다. AVD 자체의 기준 미달 근거는 유효하며 지우지 않는다. r2의 +16.7~18.7ms와 직접 성능 개선/회귀 비교도 하지 않는다.

r2 identity의 최종 전체 picture mean0.179/p99=3/max223 정정은 수용한다. 과거 부분 영역0.10/max10을 최종 전체 결과로 쓰지 않는다. 최신 기능 기록/history·계획 상태도 수정/독립검증/자동 잔여·실기 한계가 맞게 같은 SH task에서 마무리한다.

실폰 화질·S4/발열·전력·Codec2 1088 crop·실 GPU 실패·회전 터치·실 Host SendInput, 실제 release 빌드/서명은 미검증이다. 자동 보완 후에도 상태는 테스트 후보·실폰 실기 대기이며, S4 미충족 상태를 기능 완료로 승격하지 않는다. S5(버전/keepalive-multi/A6) 미결, APK0.2.30/29·설치/서명/게시/push 범위 불변이다.

다음 담당은 검증용이다. **c494dc6를 SH 코드·기하 검토 입력 후보로 고정**하고 기존 t-b7ss11ov의 독립 추가 결과·기능별 상태·미해결 control/S4/실기 한계를 같은 기록에 취합한다. 작업용2에 같은 제품 보수를 다시 지시할 필요는 없다. hello/LK/DW와 통합할 경우 새 commit/hash·공통 gate 합집합·영향 검증을 고정한다. 사용자 현재 조치 없음. Codex 변경은 미추적 검토서/인계뿐으로 별도 제품 변경·commit·push 없음.
