# rel-0164 old APK lane r5 — viewer child env

2026-10-30. **953aef2 SOURCE OK. viewer h264 시작 전제 누락을 닫고, 재빌드한 holder로 S→P 각1회 실행을 허용한다.** 기존 N1/N2/배열 보수·격리·무인증 update check의 한정 허용을 유지한다. 실제 old APK 호환 EVIDENCE/출하 판정은 결과 뒤다.

## 고정

- HEAD `953aef2c984d557c1c04e1f43647483ab6728216`, 부모949fe07. tracked clean(미추적 시험 산출물 있음), diff --check 통과.
- 보고 r5 SHA-256 `418772532f0ee0c572b2b74c58dc23a8413bc4959aef082a3de51cc553361559`.
- snapshot SUMS `b194ae3f6a479ab70f83b7f3a4046f8e0233f7b65c25743c1c7f6aa387c6848b`: **20파일의 commit blob 및 raw checkout hash를 모두 직접 대조**했다.
- diff는 env helper/holder/verdict test/보조 check script의4파일이다. 제품/Host/Viewer 동작 변경은 없고 실제 새 build 대상은 시험 holder다.

## 보수 수용

holder는 LOCALAPPDATA를 격리한 기본 env block에서 **FORCE=1과 offscreen X/Y를 child 전용 block에 직접 대체/추가**한다. 부모에 값이 없거나0/다른 값이어도 child에 필요한 값이 들어간다. 이 변경을 위해 호출자/전역 환경의 상태를 우연히 요구하지 않는다. case-insensitive 중복 변수를 제거하고 다른 항목/격리 LOCALAPPDATA를 유지하며, 실제 사용 block은 double-NUL로 끝난다.

Codex가 고정 env/verdict header와 test를 자기 review 폴더에 복사해 독립 빌드·실행했다. **9 checks / 0 failed, rc0**. 위치 `.claude/codex_old_apk_review/r5_953/{ka_standby_holder_env.hpp,ka_standby_holder_verdict.hpp,ka_standby_holder_verdict_test.cpp,build.log,verdict.log}`. 이 순수 대조는 실제 Viewer 실행이 아니다.

실제 oa_fixture 경로는 기본 host lookup과 정상 env 설정을 사용한다. `--host-id`/`--omit-viewer-encoded-env`는 보조 대조용이며 S/P 명령에 넣지 않는다. ka-pc 자체 e2e가 기존처럼 FORCE를 주는 것까지 함께 바꾼 수정은 아니다. 제품 payload에 이 tool/check를 넣는 승인도 아니다.

## 보존된 실제 holder/Viewer 대조의 수용 범위

Codex는 작업용의 다음 두 run 원문/state·Viewer 로그·end trailer를 직접 읽었다(새 Viewer를 실행하지 않음).

- `holder-check-neg-20261030003939-43452`: FORCE 없음, Init 뒤 Viewer가 codec을 거부하고 자체 종료. **holder4/ready0/died1/childFailures0**. end confirmed·EOF/log kept다.
- `holder-check-pos-20261030003947-6440`: child block FORCE1, Init에서 stop까지 약6.25초, codec 거부 없이 실제 Viewer의 managed standby 시작을 확인. Release 뒤 종료, **holder3/ready0/died0/releaseSent1/released1/childFailures0**. end confirmed·EOF/log kept다.

Host가 없는 nonexistent host-id 대조이므로 양성의3은 예상한 “never ready”다. 이를 tool의 정상 ready0 exit로 고치거나 실제 Host standby/폰 호환 성공으로 세지 않는다. 이 대조가 증명하는 것은 **실제 h264 Viewer의 startup 전제를 child env로 충족하고 Init/Release 수명 경로로 진행했다는 점**이다. state의 env 줄은 부모가 만든 child block 검사이며 child의 GetEnvironmentVariable 값을 별도로 직접 읽은 것과도 구분한다.

보조 `oa_holder_viewer_check.ps1`의 PASS만 인용하지 않고 이번처럼 expected rc3/4·childFailures0·정리 원문을 함께 본다. 이 script의 일반 예외 경로는 holder 종료를 finally에서 항상 회수하지 않고 PASS식도 rc/child failure를 모두 보지 않으므로, **미래에 재실행한다면 기존 verified job 외피와 엄격한 종료/예상rc 판정을 사용**한다. 이번에는 이미 정상 수집된 위 원문을 확인했으므로 이 보조 script의 전면 보수/반복 실행을 S/P 재개 조건으로 새로 만들지 않는다. 실제 S/P는 기존 oa_slot/fixture job 경로다.

## 실제 S/P 재개

검증용의953aef2 독립 holder 재빌드를 사용하고 **전체 hash**를 보고한다. 작업용 e9ae9415…와 독립 build65b937cd…가 다르다는 사실은 build 귀속을 분리해 기록한다. 과거9a3f126d… holder를 재사용하지 않는다. 나머지4 target은 변경 없이 기존 고정값을 확인한다.

기존 완전한 절대경로·새 run·자기 슬롯 아래 S1회→P1회로 진행한다. 정상 단계마다 새 go를 받지 않는다. `OA_PM_CLEAR_TIMEOUT_SEC=60`, SDK adb hash/serial/boot, source snapshot·server/Host hash·install/pm clear 성공 전 start0·P NO_CREDENTIAL 추가 gate, APK 원본/되받은 hash, Host input none/power off launch/final 두 줄·child EOF/log·job residual0를 유지한다.

FORCE는 h264 기능 전제이며 Host OS 입력/전원 OFF를 대신하지 않는다. 실제 사용자 앱/Host 설치본·physical phone·clipboard는 건드리지 않는다. P의 운영 update manifest 무인증 논리 check1회만 허용하며 로그인/download/install로 넓히지 않는다.

실제 holder가 ready가 되어야 S/P 본 관문의⑦ 근거가 된다. 초기화 실패·예상 밖 자식 종료는 INVALID/해당 실패로 남기고 입력/영상 성공을 추정하지 않는다. 첫 두 회차의 배열·env INVALID 기록을 보존한다. 실제 AVD run의 기능 실패/peer_lost는 기존 APK 결함으로 자동 면제하지 않는다. 두 arm의 결과를 합쳐 게시 APK가 새 운영 HTTPS directory/relay를 직접 검증했다고 쓰지 않는다.

다음 담당은 검증용의 실제 S/P 결과다. rel-0164 통합/배포 관문, Android pair 불변·push 보류, APK0.2.33의 별도 출하 보류는 모두 유지한다.

이번 Codex는 고정 source/hash·기존 대조 원문 검토와 순수 env/verdict9건만 실행했다. 실제 Host/Viewer/AVD/adb·GPU/OS 입력·설치/초기화·운영 API·삭제·제품 변경·commit/push는 없다. 미추적 검토 기록만 작성했으므로 제품 commit은 만들지 않았다. 사용자 현재 조치 없음.

## 세 번째 INVALID 통지 — standby ready 미도달

검증용 통지상953aef2/독립 holder65b937cd…의 S/P는 각각66–67초 후 INVALID이며 AVD 미시작·기기 조작0, 슬롯00:56:21–00:58:34 자기 해제다. codec startup은 통과했지만 reserved→preparing→unsupported 후 ready가 없어 holder3/released1/childFailures0으로 끝났다. 이 실행 상세는 검증용 통지에 귀속하며 Codex가 새로 독립 실행했다고 하지 않는다.

환경 보수 SOURCE와 초기화/종료 판정은 유지한다. 다만 실제 새 Host/server 조합의 standby admission이 확인되지 않아 old-apk 호환 gate와 rel-0164 출하 판정은 미닫힘이다. 제품/fixture/설정/경로 원인은 미확정이다. 서버 reserved/pinned은 Host 수신·인증·Pong의 증거가 아니며 무응답 unsupported를 구 Host/영구 미지원의 확정으로 쓰지 않는다.

검증용이 이미 지시한 기기 없는 실제 fixture+holder+Host 대조를 같은 task로 잇는다. ready 확인 전 S/P를 반복하지 않는다. 기존 슬롯·OFF guard·job/EOF·새 run과 실패 Host 로그를 유지한다. 필요하면 local flow/단계 계측으로 전달과 Host 거부를 구분하되 capability/secret 원문을 기록하지 않는다. 고정 원인/보수와 ready 근거가 오면 해당 경계만 재확인한다. 사용자 조치 없음.