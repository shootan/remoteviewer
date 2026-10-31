# old APK lane — 6345512 SOURCE / 조사 r2 / 다음 실행

2026-10-30 · Codex remote#tt7fcvky → 검증용 remote#7fsfy22z

이 문서가 c468c27만을 본 `codex_old_apk_lane_src_r7d.md`의 SOURCE 부분과 다음 지시를 대체한다. 동일 old-apk-lane task로 이어 가며 별도 구현 task나 구 Host 실행을 지금 추가하지 않는다.

## ① 고정 SOURCE: 548d199..6345512 — 한정 보수 필요

시험 파일만 바뀐 범위와 diff --check를 확인했다. 제출 diff SHA-256은 `488a3b948ea14d0a2c0e71a0832cfbdf4837c7c7519ae5fef25f2b6cd2f84d60`, SUMS는 `1dfa30851f88d08076409c42eb161c2ae1eab9b3a1ef7612166dc08bf25e4e8d`로 일치한다. author의 현재 미커밋 시험 수정은 후보에 섞지 않았다.

**이전 N1(④ False 무시)은 닫는다.** 고정 main의 해당 문장을 그대로 실행하고 효과 함수만 대역화한 대조에서 S/P 모두 input_check=False이면 return1, menu0이었다. 소스 문자열 존재 검사와 별도로 확인했다. 이전 N2의 동일 viewer 검사는 scene/상태/새 pick/Host 종료를 탐지하는 방향으로 반영됐다. 기존 IME shown/unknown 거부와 HOSTS에서만 확인 후 BACK1회도 유지된다.

**남은 N2 · 로그를 못 읽었을 때 동일 연결 검사가 열린다.**

same_viewer는 `since(marker)`와 `host_log()[offset:]`가 모두 빈 문자열이면 all(empty)=True로 통과한다. 실제 applog는 pull/읽기 예외를 잡아 `''`를 반환하고, host_log도 읽기 실패 때 `''`를 반환한다. 따라서 다음 두 상태가 모두 **True**가 된다.

- 정상 pick 후 Host 로그를 읽을 수 없거나 길이가 기존 offset보다 짧아짐.
- 앱 로그 pull/읽기 실패로 현재 앱 상태를 알 수 없음.

직접 고정 함수 대조에서 둘 다 재현했다. 정상 변경 없음은 True, scene HOSTS와 Host ended는 False였으므로, 일반 변화 검사와 이 실패 경계를 구분했다. 예전처럼 scene 변화 검사가 전혀 없는 것은 아니며, **자료 부재를 변화 부재로 오인하는 좁은 잔여**다.

보수 범위는 다음으로 한정한다.

- pick marker를 만들 때 앱 cursor와 같은 run Host 기준 자료가 실제로 확보됐는지 확인한다. 확보 실패를 빈 marker로 정상 등록하지 않는다.
- tap 직전에는 앱 snapshot 읽기 성공과 marker의 연속성을 확인한다. marker가 없어진 경우 전체 로그를 새 변화분으로 가정하지 않는다. Host snapshot도 읽기 성공·기준 offset 이상·해당 run/기준의 연속성을 확인한다. 읽기 실패·축소/교체·판단 불가면 tap0/해당 arm 중단이다.
- “변화 없는 유효 snapshot”과 “snapshot이 없음”을 구분한다. 모든 기존 helper 호출자를 크게 바꾸거나 제품 APK에 seam을 넣을 필요는 없다. 이 검사에 쓸 명시 snapshot 결과를 둘 수 있다.
- 읽기 실패/축소·marker 상실 음성, 정상 무변화 양성, scene/Host 종료 음성, S/P ④ 실패 뒤 후속 menu0을 실제 함수/흐름으로 확인한다. 소스 문자열 매칭만으로 이 경계를 닫지 않는다.

Codex 기록: `.claude/codex_old_apk_review/r7d_634_checks.py`, `r7d_634/checks.json`. 이는 고정 함수·main 문장과 효과 대역만 쓴 순수 확인이다. 검증용의 pick31/pmclear12/redact7/orchestration22 rc0 보고는 별도 귀속으로 보존한다. 그 성공을 Codex가 전량 재실행했다고 하지 않는다.

**현재 6345512의 판정은 SOURCE NEEDS_CHANGES다.** 위 잔여와 아래 예정된 SH/trace/진단을 같은 하네스 후속 고정본에 합친다. 이미 닫은 ④ 반환 보수를 다시 지휘하거나 제품 기능 변경으로 확대하지 않는다.

## ② 조사 r2와 PC 0.2.164의 blocker 종료 기준

정본 `oa_s_disconnect_investigation_r2.md` SHA-256 `5fad53797e51fff669de4e42e1c60d2bfbf88f4f99fa4f0c77c658da9ae0e858`가 r1을 대체한다. 시각 차를 범위로 두고, 수신이 뒤에서 따라잡은 사실과 H1/H2의 미설명을 적은 점은 수용한다. 지금도 결론은 **미확정 호환 blocker**다.

다음 표현은 보정한다. Host seq7은 마지막으로 **로그에 식별되는** 요청이며, 정상 ControlPing handler는 매 ping을 로그로 남기지 않는다. `reason=none`과 약10.16초는 10000ms read timeout에 부합하지만 이 종료 사유를 직접 기록한 것은 아니다. 따라서 이를 곧바로 “그 뒤 메시지 수신0이 확정”이라고 적지 않는다. 영상 in의 증가도 ACK가 반드시 처리됐다는 증거는 아니며, 반대로 그 증가를 무시하고 receive가 전 기간 멈췄다고 하지 않는다. 이번 head/ACK/drop/lastError는 여전히 미확인이다.

구 APK에는 codec을 sink lock 아래에서 만들고 ACK를 같은 RX에서 처리하는 기존 구조가 있다. 이번 가설을 지지하지만 이번 증상을 “구 Host에서도 같은 결함”으로 확정하지 않는다. IME 원격 전송 FIFO와 로컬 editor action도 구분한다. password IME_ACTION_DONE→performLogin이라는 로컬 경로가 있어, FIFO 설명만으로 모든 IME 부작용을 배제하지 않는다. 이 경로가 이번에 실행됐다는 증거도 없다.

**실제 구 Host 0.2.163 A/B를 지금 출하 전 필수로 추가하지 않는다.** 안전 OFF 경계가 없는 게시본 구 Host를 이 PC에서 실행하는 금지는 유지한다. 먼저 다음 SH-off S/P 한 묶음으로 호환 행동을 확보한다.

- 두 arm에서 자신의 ③④ 및 S park/reuse, P disconnect/new-connect/standby 공존이 실제로 성공하고, 연결 유지·guard·정리 근거와 wire/서버 계약에서 새 불일치가 없으면 **SH-off 설정에서의 old-APK 호환 lane 근거**로 수용할 수 있다.
- 그 결과만으로 기본 weak 실패의 원인을 확정하거나 기본값 동작까지 검증됐다고 하지 않는다. 정상 설정의 제한과 아직 미확정인 약한 선명도 경로를 최종 게시 판단/보고에 명시한다. 이번 SOURCE 문서가 rel-0164 출하를 승인하거나 기존 실패를 소급 PASS로 만드는 것은 아니다. 최종 EVIDENCE에서 trace·오류 문구·소스 계약과 다른 통합 관문을 함께 보고 blocker의 정당한 종료/조건부 수용 여부를 판단한다.
- SH-off에서도 종료/형식 이상이 나오면 blocker를 유지한다. 곧바로 같은 run을 반복하지 않는다. 그때 구 source 대조가 꼭 필요하면 안전 guard만 더한 구 source 시험 build 또는 승인된 격리로 고정한다. 실 게시 구 바이너리의 안전 경계를 해제하지 않는다.

현재 구 Host 추가 대조를 먼저 기다리느라 SH-off 대조 준비를 멈출 필요는 없다. 동시에 “새 Host가 원인이란 증거0”을 “새 Host는 원인이 아님”으로 바꾸지 않는다.

## ③ 다음 S/P 실기 조건 — 기존 결정 유지

고정 후속 하네스의 위 잔여와 진단 절차를 확인한 뒤 **S → P 각1회, SH-off**다. 단계마다 추가 허가를 받는 흐름으로 만들지 않는다. 실제 실행은 검증용이 자기 슬롯에서 맡는다.

- APK·5 exe·SDK pin/시험 AVD/package·install/clear·P NO_CREDENTIAL·input none/power off launch/final·EOF/job 종료·새 run/자기 slot은 기존 그대로다. 운영 로그인·사용자 앱·adb 서버/다른 emulator 재시작은 하지 않는다.
- 두 arm은 각각 clear 뒤 공개 UI로 `settingsSharpOffButton`을 선택한다. requested=off/direct와 sharpen_start0을 확인한다. weak→fallback effective=off는 이 대조가 아니다. 안전한 UI 경로가 없으면 NOT RUN으로 남기고 prefs 강제 변경/새 APK로 대체하지 않는다.
- 첫 frame 뒤 입력 없이 약30초 영상/연결 진행을 보고 ④로 간다. 이후 기존 lane 행동을 수행한다. 동일 viewer/IME/view bounds가 판단 불가하면 입력0이다.
- 로컬 fixture relay는 방향·종류·시각/송신 결과를 기록한다. **53fced4의 서버에는 REMOTE60_RELAY_TRACE 구현이 없으므로 env1만으로 계측되지 않는다.** 기존에 검토한 trace 계측을 후보 server에만 덧댄 시험 사본의 trace-only diff/hash와 양성 대조를 고정한다. 다른 branch의 server 전체를 교체해 NP fence나 후보 계약을 잃지 않는다. 운영 배포0이며, ACK/data/head 식별이 없는 집계로 클라이언트 ACK 처리를 확정하지 않는다.
- 예상치 못한 종료나 ④ 실패 때 후속 입력/시나리오는0이다. 가능하면 즉시 UI 오류 문구 dump/화면과 app/Host/relay 원문을 읽기 전용으로 보존하고 정리한다. 비밀 입력이나 private 화면 내용을 버스에 복사하지 않는다.
- load/boot/adb 상태와 1회 범위를 기록한다. 첫 nonzero는 batch를 멈춘다. 정리 확인 후 P 독립1회는 기존 허용 범위이나 공통 안전/설정 전제도 실패했다면 P NOT RUN이다. S 반복/추가 weak arm은 허용 범위에 넣지 않는다.

다음 담당은 기존 작업용1의 같은 task다. 검증용이 이 잔여·진단/SH 조건을 한 지시서에 합쳐 고정 SOURCE와 관련 거부 대조를 잇는다. 보수/빌드 준비는 계속 가능하며 현 후보로 실기는 아직 재개하지 않는다. Codex는 실제 Host/Viewer/adb/AVD·OS 입력·설치·게시·push를 실행하지 않았다. 제품/추적 문서 변경이 없는 전문 검토라 제품 commit은 만들지 않았다. 사용자 현재 조치 없음.
