# rel-0164 — old APK lane 대체 결정

2026-10-29. **Arm S + Arm P의 합성 근거로 기존 lane을 대체하는 안을 수용한다.** 두 arm이 각자의 범위에서 통과하고 다른 통합 관문도 충족되면 PC/서버 분리 릴리스의 호환 gate를 판정할 수 있다. “게시 APK로 새 서버에 실제 로그인했다”는 주장으로 합치지는 않는다. 이번은 도구/실행 계획 승인 범위이며 아직 해당 EVIDENCE나 출하 OK는 아니다.

확인한 source: APK `b554e762613e5de82584564a96713589c1c28e06`, 새 통합 후보 `53fced4c61f6592cb2c10348b43ad359b492bdfd`의 Host 진입, 기존 분리 결정과 canonical CLAUDE.md의 「테스트 빌드와 출하 빌드는 달라도 된다」. 같은 제품 코드의 test signer·fixture endpoint를 허용하고, 시험/출하 차이와 한계를 기록하는 규칙에 맞는 대체다.

## Arm S — 구 제품 source의 directory/relay 호환

고정 b554e76의 제품 UI/native/session/기능 정책을 그대로 사용하고 시험 directory endpoint만 기존 debug property로 지정하는 방향을 수용한다. **decoder liveness·새 KA e·X fence 등 후속 제품 코드를 이 build에 섞지 않는다.** 새 server/runtime 모듈과 새 Host는 최종 통합 후보의 해시로 고정한다.

단 “차이가 DIRECTORY_URL 하나”라는 설명은 현재 Gradle만으로는 맞지 않는다.

- endpoint override는 debug variant에서만 가능하므로 debug/test signer·debuggable·컴파일/패키징 차이가 존재할 수 있다. bitwise 같은 게시 APK라고 하지 않는다.
- b554e76 build.gradle.kts의 debug override는 `DIRECTORY_MIGRATABLE_ORIGINS`도 기본 빈 문자열로 만든다. 출하값과 맞추려면 기존 `gnlink.testMigratableOrigins`에 `https://rem.shotan.net`을 명시한다. 의도적으로 다르게 두면 두 번째 유효 설정 차이로 기록한다. 신규 source 수정은 필요 없다.
- BuildConfig의 endpoint·migration/update 관련 값, native flags/SH seam 유무·ABI·signer·build type/SDK 도구를 effective diff 표로 남긴다. HTTP fixture를 쓴 결과는 실제 HTTPS/TLS/DNS 경로 검증이 아니다. endpoint에서 파생되는 update/log 경로도 시험에서는 달라질 수 있으므로 자동 update/불필요한 업로드를 이번 arm의 성공으로 세지 않는다.

이 차이가 해당 wire/사용자 행동에 영향을 주지 않는 근거와 매핑을 제출한다. 새 server에서 로그인/hosts/connect·ordinary direct/relay·화면 선택/영상/control·기존 park/resume·새 PC standby 무간섭/명시 takeover 중 실제 수행한 항목을 나눠 기록한다. 원문의 ①–⑦ 번호는 각 arm의 실제 UI/transport/판정과 연결하는 표로 고정하고, 다른 흐름을 실행하고 같은 번호만 PASS로 쓰지 않는다.

## Arm P — 게시 binary의 정상 수동 직접 연결

게시 APK SHA-256 `1a3eee63…815ab` 원본을 수정 없이 사용한다. 실제 전체 hash와 설치한 package/version/certificate를 기록한다. 새 Host의 **정상 사용자 모드**와 자기 격리 시험 port/주소에 한정한다. b554e76의 launch host/videoPort/controlPort 입력과 loginManualButton은 기존 수동 경로이며, JNI nativeConnect는 directory capability 없이 ClientSessionConnectArgs를 구성한다. 53fced4의 정상 Host에는 이 token-less 수동 Hello를 받는 기존 경로가 있다.

이는 directory 인증을 테스트한 것이 아니라 **기존 정상 모드 수동 연결을 테스트**하는 것이다. prelogin은 token-less 첫 Hello를 거부하며 그 보안을 풀지 않는다. secure/logon desktop·관리자 권한 우회·사용자 설치 Host에 직접 접속하는 시험도 아니다.

특히 b554e76의 manual args 기본값은 **requireTcpControl=true / controlOverUdp=false**다. 따라서 이 arm은 설정한 TCP control listener와 video UDP port를 사용하는 직접 연결이다. 자동으로 directory UDP control/relay와 같다고 간주하지 않는다. Host 쪽의 실제 control transport/입장 경로를 기록한다. 새 Host의 인증 directory·standby activation·NP relay 조합은 S와 별도 suite가 담당한다.

수동 경로에서 실제 가능한 선택/영상/control·목록/재접속·상대 무간섭만 수행한다. manual home scene이 CONNECT인 점과 directory PC 목록의 차이를 보존한다. manual UI에 없는 항목은 NOT RUN/해당 없음 사유를 남기며 숨은 handler/가짜 capability를 추가해 맞추지 않는다.

### P의 운영 로그인 방지

**수동 버튼을 누르기만 하면 운영 로그인도 막힌다고 가정하지 않는다.** onCreate의 initDirectoryUi가 먼저 beginAutoLogin을 시작하고, 수동 버튼 handler는 그 작업의 epoch를 취소하지 않는다. 저장된 로그인/owed sign-out가 있으면 별도 요청 가능성이 있다.

승인된 시험 AVD의 분리된 앱 상태/설치 절차를 먼저 고정해, 이 arm에서 운영 origin의 saved login·pending sign-out/자동 복귀가 없음을 보장한다. source의 NO_CREDENTIAL/다른 origin 보존 경로와 관측 로그를 확인한다. 사용자 실제 앱 데이터를 조사·복사·삭제하거나 운영 계정으로 시험하지 않는다. debug/release signer 차이 때문에 설치 전환이 필요하면 시험 AVD에서의 처리/상태 분리를 기록하며 사용자 물리 폰·앱은 건드리지 않는다. 이 조건을 확보하지 못하면 P는 실행하지 않고 blocker를 남긴다.

## 실행과 판정 경계

- 검증용이 독립 실행하고 두 arm의 source/build/APK hash·server/Host hash·script snapshot·환경·rc·실제 단계·NOT RUN을 따로 보존한다. 작업용1의 도구 작성은 허용하며 새 launch/기기/파일 경계 변경은 기존 SOURCE 확인 절차로 묶는다.
- 새 Host는 공용 input none + power off의 launch/final 두 줄 gate, job 종료/EOF, repo test root·슬롯·격리 fixture target을 사용한다. APK에서 보이는 “입력 켜짐”은 Host OS 주입 성공의 근거가 아니다. 실제 입력/clipboard·사용자 desktop 조작은 하지 않고, clipboard 공유 미확인 항목은 기존대로 제외한다.
- 같은 SDK adb 절대 경로/hash·승인 시험 serial/boot·안정성 확인을 적용한다. hosts/DNS·CA/SSL 우회·공유 adb/사용자 프로그램 종료·운영 서버 로그인은 이 대체안에 포함하지 않는다.
- S/P 중 peer_lost/첫 영상 실패가 나오면 기존 알려진 APK 결함이라고 자동 면제하지 않고 새 Host/서버 기여 여부를 해당 arm의 단계로 판정한다. 제어 손실을 영상 진행만으로 정상 처리하지 않는다.
- directory suite와 NP 구 요청/구 viewer 대조를 함께 사용하되 “845” 등의 과거 합계 자체로 호환을 승인하지 않는다. 최종 통합 server의 legacy 계약이 실제 통과한 근거를 쓴다.

권장 보고 문구: **“0.2.32 원 Source의 fixture endpoint build로 새 서버의 directory/relay 경로를 확인하고, 게시 APK 원본은 새 Host와 수동 직접 연결로 확인했다. 게시 APK 원본의 고정 HTTPS 주소를 통한 새 서버 로그인·relay는 배포 전 직접 검증하지 않았다.”** 두 결과를 합쳐 “게시 binary의 새 서버 end-to-end 직접 검증 완료”라고 쓰지 않는다.

미검증 gap을 위와 같이 명시하는 조건으로 lane 대체를 허용한다. 이 gap 때문에 DNS/CA 우회나 운영 로그인을 새 필수 단계로 만들지 않는다. 실제 server 게시 후 기존 분리 결정의 외부 HTTPS/API/manifest·runtime 상태 확인은 그대로 수행하며, 이번 대체만으로 그 운영 경로가 검증됐다고 하지 않는다. 서버 먼저/실패 시 PC 게시 0·Android pair 불변·push 보류와 APK 0.2.33 보류는 모두 유지한다.

검증용은 기존 rel-0164 지시서 하나에 이 대체와 arm별 매핑을 합쳐 진행한다. 이번 Codex는 고정 source/정책 읽기와 판단 기록만 했으며 build·Host/AVD/adb 실행·사용자 앱 변경·운영 로그인·삭제·commit/push는 없다. 검토 artifact만 작성했으므로 제품 commit은 만들지 않았다. 사용자 현재 조치 없음.
