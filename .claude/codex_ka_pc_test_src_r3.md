# KA PC 시험 r3 — crash-close SOURCE

2026-10-29 · **SOURCE OK: 5f0e7a3dddc84eeaecba8a46227320d2961ce6f4.** 구성원 목록의 root + 시스템 conhost 예외를 아래 한정 범위로 수용한다. 고정 보수본의 managed만 새 run으로 실행할 수 있다. 실제 managed EVIDENCE가 통과한 것은 아니다.

## 고정과 변경 범위

- aecbdfd 위 Child helper·그 시험·managed caller **3파일 +238/-6**. 고정 HEAD, tracked 변경 없음을 확인했다.
- report_ka_pc_impl_r1.md 「시험 r3」 포함 SHA-256 **c07efe0dfc6210ab50ce249b1f797abba041a35b41c5a9f963c2451881b415b2** 일치.
- aecbdfd 대비 product shell/macro/viewer·UI·공용 input/power guard diff 0을 직접 확인했다. 정상 child의 job 종료·EOF·sink/trailer 성공 기준은 유지된다.

## conhost 예외 결정

**active count가 정확히 1이어야 한다는 문자적 조건 대신, 시험 대상 application root 하나와 시스템 console host만 있다는 판정을 수용한다.** 임의의 application descendant 허용으로 넓힌 것이 아니다.

새 코드는 JobObjectBasicProcessIdList의 전체 목록 조회 성공과 완전성을 확인한다. root PID가 있어야 하며, 그 외 member는 query 가능한 image 전체 경로가 **GetSystemDirectoryW + conhost.exe**와 일치해야 한다. 이름만 conhost인 다른 경로나 조회 불명·다른 descendant는 거부한다. root 소속은 같은 원래 process handle로 IsProcessInJob을 확인하고, 종료도 그 handle의 signal로 확인한다. PID를 다시 찾아 종료하지 않는다.

Windows의 console 상속은 별도 application 작업자 수와 구분할 필요가 있다. [Console 생성/상속 문서](https://learn.microsoft.com/en-us/windows/console/creation-of-a-console). **이 환경에서 root + conhost가 실제로 job에 있는지는 아래 직접 self-test에서 관측**했다. 문서가 모든 환경의 job member 수를 보장한다고 해석하지 않는다.

## crash-close 기록과 실패 분기

CrashCloseJob의 명시적 상태에서 kill-on-close 재조회·root membership·허용 member 목록·root가 아직 살아 있음·CloseHandle 성공·그 뒤 root 종료 signal을 기록한다. 어느 항목이 빠져도 producer 종료 성공으로 세지 않는다.

- 증거 없는 null job은 계속 거부한다.
- close 거부는 원래 job handle을 남겨 End의 cleanup에 쓴다. 그 cleanup 성공을 실패한 crash 단계의 증거로 대신하지 않는다.
- monitor가 붙은 Host 경로는 이 단계 자체를 거부한다. 현재 실제 caller는 Activate 전 ready viewer-C의 crash 단계다.
- End는 해당 crash 기록과 별개로 기존 reader EOF·read 오류·sink/trailer 보존 검증을 유지한다. 정상 job이 있는 child는 기존 active-process count 0 확인 경로다.

trailer의 증거는 **close 이전의 root/console 구성 + close 성공 + root 종료 signal + EOF/log 보존**이다. 닫힌 job을 다시 조회해 aggregate active=0을 관측했다는 뜻이 아니며, conhost 각각의 종료 signal을 별도로 기다린 증거도 아니다. 이 범위로 표현하는 현재 evidence 줄을 수용한다. 다른 Host/여러 application descendant에 이 대체 판정을 쓰지 않는다.

## Codex 직접 확인

고정 source/helper 5파일을 자기 review 폴더로 추출해 별도 빌드했다. 실행 root와 compiler TMP/TEMP는 repository 내부의 자기 새 폴더다. **ka_pc_e2e_child_test: ALL PASS, rc 0, PASS check 32개**를 직접 확인했다.

- 새 crash 단계의 실제 trailer: **root + 1 console host(s) only**를 기록하고 producer end confirmed, EOF, log kept.
- 손으로 job close/null만 한 옛 단계는 root가 끝나도 NOT confirmed였다.
- 실제 다른 self descendant가 있으면 거부했다.
- close 실패 대조는 job을 cleanup용으로 유지하고 NOT confirmed였다.
- monitor가 붙은 self-sleeper 대조는 crash 단계를 거부했고 기존 정상 End를 유지했다.
- 순수 기록 대조는 필요한 7개 항목 중 하나라도 없으면 거부했다.

출력의 child FAIL 행은 위 의도된 음성 대조다. 최종 self-test 실패나 실제 managed 실행의 FAIL로 섞지 않는다. 이 시험은 자기 self-sleeper/job/pipe와 WebView 생성 없는 preflight만 사용한다. **GNLink Host/Viewer·WebView·GPU·AVD는 실행하지 않았다.**

자료: `.claude/codex_ka_pc_review/test_r3_5f0/`의 동결 원문·build.log/run.log·child_5f0.exe.

- exe SHA-256 **ba6003af7606994d148579be805317d1751ef3a9aabb85f5b36e43f61f46a6a4**
- 실제 run: `.claude/test-tmp/codex-pc-child-r3-1793265511499237200/run-27700-611331984`

## artifact와 다음 실행

제공 binary의 전체 hash를 직접 확인했다.

| 산출물 | SHA-256 |
|---|---|
| managed e2e | 7bb6b7902c5b48749b5885f4aadf6ed0175ad03a85fd5bc0dc2e2b5fbbe656aa |
| rebuilt shell e2e | f4b67f281700f3dec192090d0aa623ed141f7faa2a1e78302fcd50f5a7f22d9d |

GNLinkClient **f14c5942…**, GNLinkViewerKaTest **bdff9363…**, GNLinkStream **f35143b2…**의 전체 hash도 기존 값과 일치했다.

검증용은 기존 슬롯 순서에 맞춰 **managed만 새 tag/run으로 단일 호출 1회** 실행하도록 연결할 수 있다. 이 SOURCE 범위의 별도 go는 필요 없다. r1의 failed managed log/trailer는 보존하고 새 결과를 따로 기록한다.

정상 shell 동작·제품·UI·guard 의미가 바뀌지 않았으므로 **aecbdfd의 shell 148 후보를 다시 전량 실행할 필요 없다.** 그 로그는 실행한 옛 test binary 9d0baa96…와 연결해 보존한다. rebuilt shell f4b67f28…를 이미 실행했다고 라벨을 바꾸지 않는다. 최종 PC 독립 검사·통합/출하, old-host NOT RUN과 다른 task의 미결은 그대로다.

이번 Codex는 source/hash 읽기·자기 self-sleeper 단위 실행·판정 기록만 수행했다. 실제 Host 시험·설치 제품/사용자 입력·clipboard·운영·설치·삭제·제품 편집·commit/push는 없다. 사용자 현재 조치 없음.
