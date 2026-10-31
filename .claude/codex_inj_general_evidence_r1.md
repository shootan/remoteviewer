# 일반 8종 — 검증용 독립 실행 r1 EVIDENCE

2026-10-29 · **구성 단위 EVIDENCE OK — b8dcc93의 지정 일반 8종만 수용한다.** admission의 두 모드를 포함해 9개 명령, 19개 Host launch다. fatal 및 ordinary record e2e는 통과하지 않았으며, 제품 통합/출하·실제 사용자 OS 입력·prelogin UI의 승인이 아니다.

## 고정과 Codex 확인

- 보고: `.claude/verify_inj/VERIFY_RECORD_ba2eaf2.md` SHA-256 **32730ba623bdc2b7408a480c9db8f4b38d19dd2cefde602d993d9b6285ec1f7f** 일치. r1 실행은 검증용의 2026-10-29 17:21~17:26 기록이다.
- source: verify-inj-b8 HEAD **b8dcc930ea1a2e99951ec7fbcd5fa339d5a04c3b**, verify-inj HEAD **ba2eaf28ff515bea8f83617dade88e33f17b378a**를 직접 확인했다. 둘 다 tracked 변경 없음. 일반 8의 실행 범위는 기존 b8 SOURCE 판정과 일치한다.
- `r1/hashes.txt`의 **14개 실행 파일을 현재 제공 artifact와 전부 직접 SHA-256 대조**했다. GNLinkStream **bcdc5fee91a4d5e5c26e9e6ef5b22f4a0e72609e74ae48c25d25d82dfdbad59a**, MigrationTest **9087a81db35ffc0d46253a6be63818779546ea9d02ffdba8e841d5abfe03267b** 및 8개 하네스가 맞다.
- 원문 `r1/*.log`의 functional assertion, RESULT, final guard와 `summary.txt`의 rc를 직접 대조했다. 검토 시점 로그/summary/manifest 13개 SHA-256은 `.claude/codex_test_injection_review/verify_inj_r1_audit.json`에 보존했다.
- console Active/RDP Listen, 시작 전 시험 Host 목록 0이라는 보존 파일을 읽었다. slot의 CreateNew/자기 파일 정리·설치 제품 무접촉은 검증용 실행 보고 및 launcher의 대상 제한 근거로 유지한다. Codex가 그 당시 OS 전역 활동을 독립 관측한 것은 아니다.
- Codex는 이번 검토에서 시험을 재실행하지 않았다.

## 수용한 구성 단위 결과

| 실행 | 프로그램 결과 | 최종 판정된 Host launch |
|---|---|---:|
| host_standby_admission_e2e | rc 0, 60/0 | 2 |
| 같은 하네스 --product-client --real-directory | rc 0, 72/0, old-host 없음 | 2 |
| host_punch_reply_e2e | rc 0, 108/0 | 6 |
| host_migration_e2e | rc 0, 37/0 | 4 |
| host_reboot_delegate_e2e | rc 0, 23/0 | 1 |
| android_session_reboot_e2e | rc 0, 18/0 | 1 |
| bwe_observe_e2e | rc 0, 15/0 | 1 |
| host_control_deadline_e2e | rc 0, 17/0 | 1 |
| host_two_dispatcher_e2e | rc 0, 24/0 | 1 |

**각 launch**의 final guard가 complete output에서 **child end·EOF·input/power OFF 두 줄·무상충**을 확인한 것으로 기록돼 있고, 미판정 running launch는 0이다. 프로그램의 check 수와 별도 final-guard PASS 행 수를 섞어 세지 않는다. 정상 성공 후 staging cleanup은 검수한 source의 동작이며, 이 승인 때문에 과거 unsafe 실행을 소급해 안전 실행으로 바꾸지 않는다.

단순 PASS 합계 외에 다음 실제 구성 동작을 확인했다.

- admission은 standby가 session을 선점하지 않는 것, generation/replay/explicit activation, 별도 장치의 간섭 거부를 검사했다. product-client/real-directory 모드는 loopback fixture의 실제 directory/relay와 제품 controller로 activation·영상·같은 lease/socket 복귀를 검사했다. 여기의 real-directory는 운영 NAS 배포/변경 증거가 아니다.
- punch reply는 capability 전달·endpoint 변환과 불일치 endpoint 거부, 늦은 capability 및 제한된 prelogin protocol 경우를 검사했다. 이것을 금지된 viewer_prelogin_login_e2e의 재개나 실제 logon desktop 조작 성공으로 부르지 않는다.
- migration은 carried/rejected/down 등의 cache·directory 전환을 시험 seam/자기 staging에서 검사했다.
- reboot delegate는 제한 token/SeShutdownPrivilege 부재와 fake service OS seam의 권한 거부·위임 횟수를 확인했다. android_session_reboot는 공용 session controller의 요청/응답/중복·NotAuthorized 동작이다. 실제 PC reboot나 AVD Android UI를 수행했다는 뜻이 아니다.
- deadline은 응답하지 않는 preview helper 뒤 약 1051 ms deadline과 그 뒤에도 정상 control ping/반복 요청이 살아 있는 것을, two-dispatcher는 TCP preview와 UDP session이 서로 직렬 대기하지 않는 것을 검사했다. BWE는 viewer report를 Host가 수신·계측하는 해당 관찰 범위다.

일부 로그의 PowerShell NativeCommandError 행은 이번 원문에서 native stderr의 session 통계/진단을 감싼 출력이다. 이를 해당 프로그램의 rc와 혼동하지 않는다. 여기서 수용한 실행은 기능 RESULT/rc 0과 final guard가 함께 있다.

## 분리 — fatal은 INVALID, record는 FAIL

**fatal 전체 EVIDENCE는 미통과다.** std/winrt의 의도된 startup 예외/exit 49 관측은 보존하지만, none 정상 대조는 exit 11·OFF 줄 부재로 final gate가 INVALID(rc 2)를 냈다. “FATAL line이 없음” assertion이 PASS를 찍어도 정상 startup의 양성 대조가 된 것은 아니다. 전체 gate가 이 경우를 거부한 점은 fail-closed 근거이며, fatal 기능 전체 성공 근거는 아니다.

보존된 `.claude/test-tmp/verify-inj/fatal-11144-608191265-0/fatal_none.log`에서도 power OFF 뒤 h264 unsupported와 encoded-experiment 활성화 요구를 직접 확인했다. 해당 startup 실패를 정상 경로 성공으로 세지 않는다.

**ordinary record e2e도 미통과다.** record-only 선언 전 연결 0, wake/monitor-power 기록과 입력 dispatch 6개 관측은 남길 수 있다. 하지만 expected session epoch가 0이고 기록 matcher가 epoch 1을 다른 session으로 판단해 2 FAIL이다. Host 원문이 실패 후 삭제돼 **판독 결함이 원인이라고 확정할 수는 없다.** effect 기록의 epoch 1만으로 “이 session의 올바른 epoch 1”을 역으로 정해 PASS로 만들지 않는다. epoch 원천·session 연결·매칭 순서를 원문과 함께 확인해야 한다.

이 두 실패는 일반 8의 fixed artifact/source·서로 독립된 실행 결과에 영향을 주는 근거가 아니다. 일반 8을 다시 전량 실행할 필요는 없다. 반대로 일반 8의 OFF 실행 성공은 record-only 경계의 전체 효과 매칭 성공을 대신하지 않는다.

## D1/D2와 다음 실행 범위

작업용1에 연결한 D1/D2 방향을 수용한다. 같은 안전 하네스 작업의 고정 보수로 묶는다.

- D1: expected epoch는 이 Host/이 session의 권위 있는 원문에서 얻고, 미획득·출력 불완료는 INVALID로 중단한다. 0/상수/기록 matcher의 실제 effect epoch를 expected 값으로 대입하지 않는다. assertion FAIL/INVALID에서도 Host 전체 log·상태·원문을 자기 run에 보존하고, 판정 뒤 삭제해 원인을 잃지 않는다.
- D2: none의 encoded-experiment 설정을 갖추고 정상 startup의 구체적 긍정 신호·예상 종료 상태를 검사한다. no-FATAL 하나로 정상이라고 하지 않는다. startup-fault의 허용 예외와 정상 none의 OFF 두 줄/완료 검증은 유지한다.

**새 고정 보수본에서는 이 두 시험만** 필요한 component 대조와 독립 재실행으로 이어가면 된다. 기존 record-only artifact/loopback/clipboard OFF·job/slot 및 fatal의 허용 case 경계를 유지한다. 새 source/artifact hash, 좁은 diff, 원문·실패 자료 보존과 두 실행의 결과를 한 packet으로 연결한다. OS 효과·root/cleanup·job/권한·금지 prelogin 범위를 바꾸는 변경이면 해당 경계 SOURCE를 먼저 검토하고, 일반 8의 재검사는 그 변경이 실제 영향을 줄 때만 한다.

old-host NOT RUN, real4 실제 입력과 viewer_prelogin_login_e2e 금지, PC/APK UI·실 NAT·비용/성능 및 최종 통합 미검증은 유지한다. 그 외 기존 task의 실행 가능한 작업을 이 두 시험의 수정 때문에 중단하지 않는다.

이번에는 source/제공 artifact·원문 로그 읽기, audit hash와 판정 기록만 수행했다. 제품 변경·시험 재실행·설치·운영·사용자 입력/clipboard 조작·삭제·commit/push는 없다. 사용자 현재 조치 없음.
