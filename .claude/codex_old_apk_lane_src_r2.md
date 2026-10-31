# rel-0164 old APK lane r2 SOURCE / update GET 결정

2026-10-29. 후보 `7714c8597aaa1c10f804fc2f6438cea80ed0c7f8`, base `53fced4c61f6592cb2c10348b43ad359b492bdfd`. tracked clean(미추적 시험 산출물 있음), diff --check 통과. 제품 동작 파일 변경은 없고 fixture/하네스/tool/test 변경이다.

**무인증 운영 update manifest GET 1회는 허용한다. 시험 SOURCE는 NEEDS_CHANGES — 아래 N1/N2를 고친 뒤 S/P를 실행한다.** 검증용의 5 target 독립 빌드와 기기 없는 대조는 계속할 수 있다. 이 보류를 다른 PC 통합 검증 전체의 중단으로 넓히지 않는다.

## 고정점

- 보고의 현재 SHA-256은 `262830eeac1a429d0b7e3dbca27d0279becdcab8d74d5ea7977e7b163f629b56`다. 인계의 1e7b08ee 이후 갱신된 「S 뒤 P 실행 순서와 데이터 초기화 범위」 등을 포함해 읽었다.
- snapshot SUMS `5ae2c649427ed58a4ff7a1e8d43a5e79ca4e0ab164a712082f1ed08440424535`의 11개 파일은 **LF 정규화 내용으로 모두 일치**했다. oa_adb.py/oa_adb_redact_test.py/oa_avd.py/stub_harness.py 네 파일은 checkout CRLF 때문에 raw byte hash는 다르다. 실제 실행 파일의 raw hash와 canonical LF hash를 구분해 기록하면 되고 제품 차이라고 오인하지 않는다.
- 게시 P APK 직접 hash: `1a3eee63a992371f7ad455deedfabbc0e0b627c1c7432fa9f9251b8183d815ab`.
- 시험 S APK 직접 hash: `50080b2bd586ee66ce446ab8f647ce4e5c11fafc22593d15bef7e948d2816962`.
- 두 APK의 native SH seam marker는 모두 0/4로 직접 확인했다. 이 회차에 signer/SDK 차이를 별도 재측정했다고 하지는 않는다.

## 운영 update GET — 허용 범위

b554e76의 derived update 경로는 `/api/update/manifest?platform=android` 응답 하나에서 manifest와 signature를 읽는다. 빈 session이면 credential header가 없고, 시작 시 check는 결과/업데이트 제안을 표시한다. download/install은 별도 사용자 동작의 경로다.

따라서 **P의 승인된 시험 AVD·초기화된 앱 1회 시작에서 발생하는 이 무인증 읽기 요청을 허용**한다. 로그인·계정/세션 생성이나 운영 배포 승인이 아니며 테스트 때문에 DNS/CA를 바꿀 필요 없다. 자동 다운로드/설치·update 버튼 클릭·자격 증명 전달·운영 로그 업로드로 확대하지 않는다. 시작 횟수와 update_check 결과를 기록하고, 앱 동작상 예상과 다른 요청/업데이트 흐름이 나오면 해당 실행을 그 범위에서 판단한다.

이는 source상 한 번의 논리 check 허용이다. 실제 network request 횟수를 proxy/서버에서 직접 셌다고 주장하지 않는다. precondition을 충족시키기 위해 반복 재시작하거나 로그인하는 것은 이 예외가 아니다.

## N1 / P1 — pm clear 실패를 무시한 뒤 P 앱을 시작함

위치 `oa_avd.py:314–316` 및 `p_first_connect:193` 부근.

현재는 `rc,out,err = ka.run('shell','pm','clear',PKG)`를 **기록만 하고** installed APK 조회·am start까지 진행한다. `pm clear`가 nonzero/timeout/실패 응답이어도 시작을 막는 조건이 없다. 따라서 이전 vault/owed 상태가 남아 있는 앱이 먼저 onCreate의 autoLogin을 실행할 수 있다. 뒤에 NO_CREDENTIAL/로그를 확인하고 force-stop하는 것은 이미 나간 요청을 되돌리지 못한다.

**필요 보수:** 양 arm 모두 설치 상태/반환과 특히 **pm clear rc=0 + 명시적 정상 Success 응답**을 확인한 뒤에만 앱을 시작한다. 실패·timeout·비정상/빈 stdout은 INVALID exit2로 끝내며 **am start 0, tap 0**이어야 한다. 실패한 초기화를 성공으로 간주해 재시도하지 않는다. 기존 P의 시작 후 NO_CREDENTIAL·sign_in_store 0 확인은 추가 방어로 그대로 유지한다.

실제 기기 없이 fake adb로 rc!=0/Success처럼 보이는 출력, rc0/실패·빈 출력, timeout/exception을 대조하고 그 뒤 launch가 0임을 확인한다. 현재 stub orchestration 17개는 Python 기기 단계 내부의 이 조건을 덮지 않는다. 사용자의 실제 앱 데이터를 만지는 시험은 하지 않는다.

초기화 범위는 기존 승인처럼 **공용 시험 AVD emulator-5554의 com.remote60.androiddirect 하나**다. keystore 자체의 삭제 여부를 폭넓게 증명할 필요는 없고, 앱 vault/owed의 source 저장 위치가 비워져 어떤 자격 증명도 제출할 수 없다는 전제와 성공 결과를 확보한다. 다른 패키지·AVD 전체·사용자 물리 앱은 범위 밖이다. signer 불일치 때 자동 uninstall로 확장하지 않고 기존 명시적 시험 패키지 처리 방침을 지킨다.

## N2 / P2 — standby holder의 종료/로그 오류가 exit0으로 숨겨질 수 있음

위치 `ka_standby_holder_tool.cpp:217–231`.

Release 뒤 15초에 viewer가 끝나지 않아 `released=false`여도, `proc.End()`가 종료/EOF/로그 실패를 `child_fail`에 기록해도, 마지막 반환은 died/ready만 본다. **ready=true·died=false라면 두 실패 경로 모두 rc0 가능**하다. 공용 Child에는 `children_end_all()`/누적 failure 판정이 있는데 tool 결과에 연결되지 않았다. 상위 job이 나중에 active0을 확인하는 사실이 이 tool의 EOF/보존 실패나 정상 Release 실패를 PASS로 바꾸지는 않는다.

**필요 보수:** tool의 최종 결과 전에 공용 child 종료·EOF·로그 판정을 완료하고 누적 오류를 exit nonzero에 반영한다. Release 송신 실패·정상 종료 deadline 초과도 별도 실패로 세고, job 강제 회수는 cleanup으로만 표시한다. 정상 ready/관찰 결과와 cleanup 완료를 나눠 적는다.

기기/실 Host 없이 자식 대역 또는 순수 결과 판정 대조로 ready 뒤 released=false·Child 판정 오류·정상 종료를 구분해 확인한다. Host의 launch/final input/power guard나 기존 fixture job 종료 확인은 그대로 둔다.

## 수용한 범위와 보고 정밀도

- S의 기존 migration property를 출하값에 맞춘 보정, P의 TCP control 직접 연결과 PC 목록/park 미지원 구분, 별도 P-5d 종료/P-6r 새 연결 매핑은 수용한다. 실제 기기 결과는 아직 없다.
- 게시 APK 자체가 debug/signer가 같다는 새 관측을 기록하고, 이전 “반드시 서명자가 달라 uninstall 필요” 단정은 폐기한다. 이것이 product source를 바꿔도 된다는 뜻은 아니다.
- `oa_apk_diff.py`는 metadata·특정 문자열/marker count·hash/길이를 비교한다. **이 도구만으로 DEX bytecode 전체/ELF 명령의 차이가 오직 두 항목뿐임을 증명하지는 않는다.** 별도 비교 근거가 있으면 경로/hash를 함께 인용하고, 없으면 “source/build provenance와 관측된 차이”로 표현한다. 동일 Source/property 통제는 기존 허용 범위이므로 이를 이유로 전량 역어셈블리 작업을 새 필수로 만들지 않는다.
- 되받은 base.apk 원본 hash 검사는 유지한다. 단순 version/signature 일치로 자동 완화하지 않는다. 값이 달라지면 우선 INVALID로 두고 구체 원인을 확인한다.
- ⑦은 holder가 ready를 유지하며 살아 있었는지의 관측이다. 아직 따로 측정하지 않은 모든 입력/decoder/resource·오랜 NAT 안정성을 그 상태 enum 하나로 증명하지 않는다. 실제 폰의 영상/control 진행과 Host 로그를 함께 본다.

## 다음 단계

N1/N2를 **같은 old-apk-lane task의 한 보수**로 묶어 source/기기 없는 거부 대조를 제출한다. 제품 변경은 요구하지 않는다. 그 뒤 새 hash·snapshot·검증용 독립 build를 고정해 S→P 각 1회, 별도 슬롯으로 실행한다. 이 판정은 현재 7714c85에서 실제 S/P 실행을 허용하는 SOURCE OK가 아니다.

기존 Host input none + power off launch/final gate·job 종료/EOF·repo/승인된 시험 경로·adb hash/serial/boot·UDF/clipboard 미조작·운영 로그인 금지·APK/Android pair 불변·push 보류를 유지한다. 무인증 update 읽기 예외만 위처럼 허용했다. APK 0.2.33 liveness 보류와 PC의 다른 독립 lane은 각자의 상태로 계속한다.

이번 Codex는 고정 source와 APK/hash/snapshot을 읽고 대조했으며 기기/Host/Viewer/adb/운영 API는 실행하지 않았다. 제품 수정·설치/초기화·삭제·운영 배포·commit/push도 없다. 미추적 검토 기록만 작성했으므로 제품 commit은 만들지 않았다. 사용자 현재 조치 없음.
