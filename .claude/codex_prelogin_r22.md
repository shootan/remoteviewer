# prelogin r22 — UI 근거 및 PC 단독 릴리스 범위

`t-r639inm8 / r22 / kind=review-result / reply_to=remote#pvbntb3z` · 2026-10-20

**① UI EVIDENCE OK(제품 동등 시험 범위), 버전 후보 준비 진행 가능. ② APK는 게시된 0.2.30 그대로 유지, 이번 범위는 PC용 prelogin v1. ③ main 통합·PC 0.2.160 후보 준비는 수용하되 게시 전 구형 클라이언트 호환 근거와 고정 패키지 검수를 연결한다.** 지금 게시·설치·재부팅을 승인한 판정은 아니다. 사용자 게시 지시 단계는 제안대로 유지한다. 새 APK 동반을 임의로 필수화하지 않는다.

대상은 `406f563471789bc297f0427565ceba9564bf6f5d`이다. r20/r21 SOURCE 및 E1/E2 판정·수용 한계 8건은 유지한다.

## ① UI 근거의 의미

직접 읽은 독립 summary_vui22는 toggle 32/0·reboot_prompt PASS·login 79/0·ready_chain 18/0 및 정상 종료를 기록한다. toggle 원본에는 가시성·활성·중앙 가림 없음, UIAutomation 토글→제품 admin 요청→기록/시작형식 대역의 결과, 관리자 거부 대조, sign-out이 마지막 Disable로 끝남이 있다. prompt의 확인 1/취소 0/닫기 0, login의 제품 툴바/비밀번호 창→봉인→실제 호스트 수신→시험 EDIT 입력 및 부정 대조·로그/파일 비밀 부재도 원본을 읽었다.

PNG 5장을 직접 열어 Host 토글 켜짐/설명·sign-out 해제 확인 중 문구, 재부팅 확인/취소와 경고, 툴바의 다시 시작·Windows 로그인·Ctrl+Alt+Del, 비밀번호 창을 확인했다. 이로써 기존의 UI 미실행 상태는 해당 제품 동등 시험 범위에서 해소된다. 작업용의 나머지 13개 실행 요약도 읽었으나 Codex 직접 실행이라고 쓰지 않는다. 작업용 최종 보고서는 이후 기록에 취합한다.

**실환경과의 차이는 남는다.** toggle은 asInvoker HostUiTest+시험 pipe/기계 저장소/SCM 효과 대역이며, login은 MigrationTest host를 Default desktop의 시험 창으로 향하게 하고 종료 권한을 제거한다. 실제 SYSTEM/Winlogon·LogonUI 비밀번호 필드·SCM 설치/시작형식·UAC·cold reboot·사용자 전환 성공은 아니다. sign-out PNG의 “해제 확인 중” 한 장이 최종 해제 증거인 것도 아니다; 이번 시험에서 마지막 Disable을 확인한 원본 동작 로그가 별도 근거다.

또한 foreground 로그는 **각 실행 전/후 GMux라는 표본**이다. 시험 소스는 실제 입력 때 시험 창을 잠시 전경으로 가져온다. “전 구간 GMux / 포커스 탈취 0”이라는 연속 관측 주장으로 확대하지 않는다. 이 보고 문구 정정 때문에 UI 시험을 다시 돌릴 필요는 없다.

## ② APK 0.2.30과 PC 단독 게시

게시 APK의 기록상 기준은 `b4fd204`(versionCode29/versionName0.2.30), SHA256 `e6a6eecbf129f92998d5c4d4e2844057485315489c001023b24f5966a93608cf`이다. 실제 호환 시험에서는 이 게시 아티팩트와 대조한다. main에 Android 소스가 함께 들어오는 것과 그 소스로 APK를 게시하는 것은 별개다. **새 prelogin Android 소스를 같은 0.2.30 이름으로 재빌드/교체하지 않는다.** Android manifest·sig·APK URL의 실제 바이트는 유지한다.

소스에서 확인한 구형 동작:

- b4fd204의 ClientSessionController는 연결 시 window list를 예약하고, 앱의 접속/목록 재진입도 RequestWindowList를 호출한다. 새 role gate·문자 송신 전 필터가 없다.
- 구형 scheduler는 list 요청을 문자 입력보다 먼저 선택한다. 따라서 정상 시작 경로에서는 새 prelogin host의 `ControlWindowListRequest` 거부가 먼저 일어나 연결이 실패할 것으로 예상한다. **이것은 소스 추론이며 실제 게시 APK 시험 결과가 아니다.**
- 새 host의 `host_control_session.cpp`는 prelogin에서 허용하지 않은 type을 payload handler 전에 거부하고 해당 링크를 닫는다. InputEvent는 별도의 문자/탐색키 필터도 적용한다. 인증된 prelogin ingress·서버 신원·봉인 자격 경로도 별도 층이다. allowlist 하나만으로 모든 안전 조건을 입증했다고 쓰지 않는다.
- 서버에서 거부하는 것은 구형 앱의 **송신 전 문자 차단**과 다르다. `prelogin_input_gate.hpp` 자체도 “호스트가 거부할 때는 이미 wire를 건넜다”는 이유로 viewer 필터를 요구한다. 따라서 서버 allowlist를 근거로 구형 APK의 prelogin 완전 호환·문자 송신 방지까지 선언할 수 없다.

**릴리스 범위 선택:** 이번 PC v1에서 Windows 로그인·재부팅 등의 새 기능을 지원하는 클라이언트는 새 PC viewer다. UI에도 “휴대폰에서의 로그인 입력은 다음 버전부터”라는 한계가 보인다. 기존 APK는 로그인된 일반 Host 사용을 계속 지원하되, **prelogin 호스트 접속/로그인은 미지원이고 연결이 거부/종료될 수 있음**을 릴리스/실기 안내에 명확히 적는다. 휴대폰 prelogin 사용을 이번 성공 기준으로 바꾸려면 사용자 APK 유지 지시와의 새 범위 조율이 먼저다.

**게시 전 좁은 호환 확인은 필요하다(후보 준비와 병행 가능).** 실제 게시 APK0.2.30을 격리 AVD에서 쓰거나 그 아티팩트와 소스 차이를 고정한 구형 native session을 사용해 다음을 확인한다. 새 406f563 session의 android-session PASS는 구형 APK의 근거가 아니다.

1. 새 PC의 일반 Host에 기존 APK가 연결·목록/선택·park 재사용을 수행하는 기존 동작이 유지됨.
2. 새 prelogin host에는 구형 초기 list가 거부되고 무한 대기/루프 없이 실패를 알리거나 종료함. 시험용 비밀 아닌 sentinel 입력을 조기 큐잉한 경우에도 문자 payload가 먼저 송신되지 않는지 wire에서 관찰하고, 호스트 OS 입력/봉인 자격 처리/재부팅 부작용이 없음을 구분해 기록함. 실제 비밀번호는 사용하지 않음.
3. 그 실패 뒤 worker/서비스가 살아 있고 지원 PC viewer가 새 세션으로 정상 접속할 수 있음. 기존 계정·기기 사용 중 프로세스는 조작하지 않음.

이 근거가 예상과 다르면 “서버가 막으니 안전”으로 닫지 않는다. 기존 APK를 유지한 채 안전하게 거부할 admission/호환 경계를 보완하거나 PC 후보 범위를 재조율한다. 클라이언트 편의를 위해 host allowlist·문자 필터를 풀거나 창 목록을 임시 허용하는 것은 승인하지 않는다. 현재 알려진 코드 때문에 APK 동반을 강제할 필요는 없지만, **호환 시험 없이 즉시 게시 OK도 아니다.**

## ③ main·버전 후보·서명/게시 흐름

현재 직접 확인한 main은 `ec395cac70741a51c795b2188ae1656570cb8892`이며 후보와의 merge-base는 `0537eb9`다. 0537eb9→ec395ca 차이는 `docs/작업목록.md` 한 줄이다. root에서 현재 checkout된 `refactor/viewer-split`의 별도 커밋을 main 또는 후보에 넣지 않는다.

- 검증용이 main ec395ca 위에 **검수한 integ 후보 406f563까지만** merge하고 문서 상태 양쪽 의미를 보존한다. merge commit의 두 parent와 제품 diff가 검수 후보와 같음을 확인한다. index.commit으로 두 번째 parent가 사라지는 경우를 정당한 merge로 처리하지 않는다. 이후 PC 버전 0.2.160 및 기능 단위 history/계획 상태를 반영하고 고정 commit을 만든다. Android 버전/게시 아티팩트는 그대로다. 이 판정은 push 허가를 추가하지 않는다.
- `gnlink_release.sh --worktree <전용checkout> --version 0.2.160 --dry-run`은 PC windows.manifest/payload를 실제 빌드·패키징·gate하지만 서명/게시하지 않는다. 새 build/release 경로를 사용하고 기존 후보를 지우거나 덮어쓰지 않는다. 그 후보에서 출하 gate·시험 훅 배제·installer/payload 일치·필요 smoke/호환 결과와 파일 해시를 고정하고 검증용 명시 OK를 기록한다.
- **같은 release 디렉터리로 release.sh를 다시 `--sign --deploy` 실행하는 제안은 그대로는 불가하다.** 스크립트는 기존 build/release 경로 재사용을 거부한다. 검증한 dry-run 후보를 보존한 채 사용자 게시 지시 후 `gnlink_release_sign.ps1`로 **그 동일 manifest/payload**에 서명하고, 후보 빌드의 `remote60_verify_release`(정상 수락+변조 거부) 및 서버측 verifier를 실행한다. 서명 전후 manifest hash·payload checksum/payload-set을 확인해 고정 후보가 바뀌지 않았음을 남긴다. 개인키는 보호 저장소 안에서만 사용한다.
- 이어 그 서명된 release dir을 `gnlink_deploy.sh --release-dir ...`의 verify-only·dry-run·실게시로 넘기고 windows manifest/sig·버전·아티팩트·외부 HTTPS 해시를 확인한다. 별도 release.sh 재빌드 경로를 선택한다면 새 산출물을 다시 고정·검수해야 한다; 이전 dry-run의 runtime/hash 증거를 새 바이너리에 그대로 붙이지 않는다.
- 게시 전후 Android manifest/sig와 APK0.2.30 hash 불변을 확인한다. 버전/빌드 commit/PC 아티팩트 hash/URL/직접 확인한 행동/남은 실기를 분리 기록한다. 현재와 별개의 사용자 설치·재시작·서비스 등록·재부팅은 자동 실행하지 않는다.

지금 진행 가능한 다음 담당은 검증용: 기존 task에서 작업용 UI 보고 취합·구형 호환 확인·main/PC 후보 준비를 연결한다. 그 후 사용자 게시 지시가 오면 고정 후보의 서명·게시·외부 검증으로 진행한다. 실제 SCM/UAC·Winlogon 로그인·cold reboot·빠른 사용자 전환·sign-out 후 재부팅 접속 해제는 사용자 실기 경계로 남긴다. 자동 가능한 호환/출하 후보 검증을 생략하고 “UAC만 남음”으로 닫지는 않는다.

## 증거 고정과 수행 한계

- 작업용 summary_r22 SHA256 `0d8d930396fa681ccb4766dd1be28aee2dc56cf5ed7521b931aa66b528d925a2`.
- 검증용 summary_vui22 `a92949f8a0e7756fd39e87254b358051c9562f24bb7739590bfa6f15c9139fe1`, 누적 검증 보고 `d81438894bc901a945622069b44b48c1cf3eff74c9e314886e0213c5a9579bb6`.
- 현재 독립 GNLinkStream `bc1867c814bc236488be9f4c82f168e8450bfbc67940f9bcd3ea4767283c3e6e`; HostUiTest `68f06d2561fad59353313a63f3056bb6a6223152b3f2a3f8db18e260b4c40153`; StreamMigrationTest `a83b849e26cc2c5ac9d4ef115085e4a2a4837a456fc133d3fccab14251d8a449`.

Codex 직접 수행: 화면 5장 보기, 원본 로그/소스/구형 코드·scheduler/버전·main 차이·스크립트/해시 읽기. UI/빌드/구형 APK 실행·merge·버전 변경·서명·배포는 수행하지 않았다. 검토서 외 제품/공개 문서 변경 없음으로 새 commit/push 없음.
