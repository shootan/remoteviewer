# prelogin-field r3 — dec6006 검토

`task prelogin-field / r3 / kind=review-result / reply_to=remote#izxg3q0o` · 2026-10-20

**SOURCE OK / 보완 범위 EVIDENCE OK. R2-1·R2-2를 종결하고 0.2.161 버전/출하 후보 준비로 진행한다.** 이번 delta에서 추가 필수 소스 보수 요청은 없다. 버전→dry-run 고정 패키지 검수→사용자 게시 지시→동일 후보 서명·게시·외부 검증→사용자 로그아웃 실기 한 묶음의 제안에 동의한다. 실제 rc49 원인·SYSTEM/Winlogon 실기 성공이나 현재 배포 완료 판정은 아니다.

고정 대상 `dec600631e8a0273bbd11615d5ad5e6c93dd3ca5`(parent90289e8), 작업용·검증용 HEAD 일치/ tracked clean을 직접 확인했다. r3의6파일 변경, 제품/시험 호출선, 보고와 원본 로그·해시를 읽었다. D1/D4/D2에 관한 r2 검토 범위와 한계는 유지한다.

## R2-1 종결 근거

pipe reader는 읽기·행 구성·짧은 큐 조작만 하고 디스크/forward를 수행하지 않는다. writer가 별도로 WriteLine/Flush/forward를 한다. 일반 로그 큐는4096줄/1MiB에서 overflow를 drop·계수하며, writer의 파일/forward 대기가 reader의 mutex를 보유하지 않는다. reader가 끝나면 read end를 닫아 후속 생산자 쓰기가 실패하도록 바뀌었다. Drain의 시간 계산도 진입부터 시작하고 writer 완료 대기는 그 deadline을 사용한다.

독립 원본 worker_log 로그에서 writer를 계속 멈춰 놓은 상태의 약4MB 출력4594ms, Drain(1500)=false/1500ms, hook 이후 child의 자력 exit15(총8109ms), drop2964줄을 확인했다. reader 종료 대조는 출력0ms·Drain false/0ms·child 자력 exit15/234ms다. parent가 kill한 것을 성공으로 세지 않는다. 기존 결합을 복원한 mutant R은4FAIL/20초 kill, read end를 남긴 C는3FAIL/20초 kill인 작업용 원본도 읽었다.

따라서 앞서 지적한 **writer의 정체/reader 종료 때문에 생산자와 종료 hook이 무한 대기하는 반례**는 코드와 시험으로 해소됐다. 실제 저장장치가 멈춘 상황을 실측한 것은 아니고 writer 경계의 stall로 공급한 근거다. OS 스케줄링·전체 초기화 I/O까지 포함한 절대적인 realtime 상한으로 확대하지 않는다.

큐 포화에서는 일부 로그를 버리는 설계다. `Drain=true`를 drop된 줄까지 전부 디스크에 있다는 보장으로 쓰지 않는다. 현재 정상 FATAL 보존 성공과 정체 시 false/종료 지속을 구분한다. 헤더의 “Everything printed…/never waits longer” 표현도 이 관측/설계 범위로 해석하며 다음 관련 편집 때 정확히 맞추면 된다. 이 문구 보정이나 모든 스케줄의 측정을 새 게시 관문으로 추가하지 않는다.

## R2-2 종결 근거

Install은 event/pipe·reader/writer를 준비한 뒤 stdio를 바꾸고, fd/표준 핸들 상태를 보관해 실패 시 Rollback으로 복구한다. fd·pipe/event·forward·thread·file/held chain 정리와 실패 후 재설치가 있다. Install은 noexcept/예외 회수이고 main의 설치 호출도 catch 안이다. 원래 표준 핸들이 없는 stream은 NUL로 남기는 정의된 동작이다. 세션 간 상속 또는 unsafe 로그 파일 폴백을 추가하지 않았다.

독립38/0 원본에서 stdout만 redirect한 뒤 stderr 실패, event 실패, thread 시작 실패 각각을 주입해 설치 실패/미설치·fd와 표준 객체 복원·파일 배타 열기·후속 약2MB의 원래 stdout 도달·재설치/child 정상 종료를 확인했다. 무표준핸들 child도 설치/printf/cerr/Drain/자력 종료가 통과했다. rollback을 제거한 mutant B의6FAIL·재설치 뒤0xC0000409는 작업용 원본 근거다. 해당 복구를 시험하지 않은 r2의 정상 경로 PASS와 구별된다.

실제 worker main 경로의 새 fatal 시험도 읽었다: --prelogin→첫 단계 throw→머리줄 다음 단계명 포함 FATAL→자력 exit49이며, 로컬 로그와 원래 stdout 양쪽에 남는다. 별도 sink helper만 호출한 근거가 아니다.

## 영향 검증 및 출하 분리

검증용 별도 build-vs 원본 summary에서 worker_log38/0·fatal14/0·punch102/0·lifecycle21/0·monitor_fallback173/0·fec136/0 등 exit0와 종료, fixed_server9·encoder_poll ship gate를 확인했다. **실행20회에는 worker_log가2회 들어 있다**(서로 다른 시험20종이라고 합산하지 않는다). 작업용 SendInput pe3의4종/실제 mutant 실패와 시험90289e8 매핑은 기존 근거로 유지하며 r3 바이너리를 같은 SendInput으로 다시 실행했다고 바꾸지 않는다.

보호 파일/회전 구현 자체는 r3에서 변경되지 않았다. 기존 parent chain·trusted owner/DACL·reparse/hardlink 거부·SY/BA 전용·runtime2MiB×2·비밀 미기록 시험 범위를 유지한다. 시험 owner predicate가 실제 SYSTEM/권한 시험을 대신하지 않는 한계도 유지한다.

현재 독립 GNLinkStream의 바이트에서 stream-test-seam·WinRT trap reached·GNLINK_STREAM_TEST_LOG_ROOT가 모두 부재하고 MigrationTest에는 모두 존재함을 직접 대조했다. 제품의 새 정적 fault hook은 기본값 null/false이고 출하 runtime env로 활성화되지 않는 호출선이다. 버전 인상 후 실제 출하 패키지에도 시험 exe/훅/약화 설정이 없는지 고정 artifact에서 확인한다.

## 릴리스/실기 연결

검증용은 기존 task에서 버전0.2.161 및 기능 단위 기록을 취합하고, 새 commit·패키지 해시·버전/제품 동등 시험 차이를 고정해 dry-run 출하 검수를 연결한다. r3의 기존0.2.160 버전 개발 산출물을0.2.161 출하물과 동일하다고 쓰지 않는다. 새 패키지의 필요한 smoke/서명 verifier·payload/installer 일치·외부 HTTPS 확인은 기존 릴리스 gate대로 진행한다. dry-run 후보에 직접 서명해 동일 바이트를 유지하는 앞선 흐름을 그대로 쓴다. 사용자 게시 지시 단계와 APK0.2.30 유지도 변경하지 않는다.

다음 실기는 준비가 모두 끝난 뒤 사용자 업데이트/UAC·로그아웃→로그인 전 PC viewer 접속→worker 생존/로그·Windows 로그인/Host 인계→트레이/업데이트 메뉴를 한 묶음으로 확인한다. 아직 SYSTEM/Winlogon rc49 원인, 실제 SY/BA 로그·session0→console 기동, 승격 Host의 TaskbarCreated/Explorer 타이밍은 미확인이다. 실패하면 해당 회차의 단계/HRESULT와 보호 로그를 승격된 수집으로 회수한다. 지금 사용 중인 제품/서비스·입력·로그아웃을 자동 조작하지 않는다.

추가 사용자 GO 없이 지금의 후보 준비를 계속할 수 있으며, 다음 담당은 검증용 remote#izxg3q0o이다. 보완 범위 검수는 완료했고 사용자 실기 성공까지 완료했다고 부르지 않는다.

## 근거 고정

- 작업용 보고 SHA256 `4f40bdaef7f36f40d67edafb6b2a134756fcdc1d3eb3b8166fae05f66b5170ed`
- 검증용 보고 `4537b6902aacf3979a545451a757e8881e5eac084d96617893eaac3690482eaf`
- 독립 summary_vfield3 `9edc19226072d9748aefbeef3ce49c340fc4a40761845177e404607a0fc94954`
- 독립 GNLinkStream `92bfc82a59cf518ed06fbb0c721118e495df4f65b54ef0006f7f96fd0a3877d2`
- 독립 GNLinkInputService `1738b2d394556504a42f38eca4ad2369cbe5bed990963c77882d346bd4e955e4`

Codex 직접 수행은 소스/diff·보고/원본 로그·HEAD/clean·바이너리 표식/해시 확인과 `git diff --check 90289e8 dec6006` exit0이다. 별도 child의 Win32/CRT 핸들 동작도 관찰했으나 이를 제품 실패 주입의 독립 재실행으로 세지 않는다. 제품 빌드·SendInput·서비스/설치·실기 실행은 하지 않았다. 검토서 외 제품/공개 문서 변경이 없어 새 commit/push는 없다; 기능 기록은 기존 릴리스 기록에 취합한다.
