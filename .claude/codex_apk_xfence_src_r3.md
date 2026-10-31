# APK X fence r3 — SOURCE 재검토

**NEEDS_CHANGES — r2의 기존 owed release 두 경로는 닫는다. 아직 송신되지 않은 down과 입구에서 거부된 up의 반대 비대칭을 보수한다.** capability 감소 뒤 park/drain의 bounded 실패/teardown 수용은 유지한다.

## 고정·수용 범위

- 후보 **7c5d027de764044b750d72b0114fe58242258b11**, 비교 **50340ce..7c5d027**의 ledger header·시험 두 파일 +180/-27. tracked clean·diff --check 정상.
- report SHA-256 **97ec2a6ebc506dd6e9fdefcebbd2aad33c1ac32609f1f02b20b6ca8bf2d4259d** 일치. controller cpp/hpp·shared_core·JNI는 변경 0이다. base e 변경 전체 또는 APK 0.2.33 출하 판정이 아니다.
- **owed_를 독립시킨 변경을 수용한다.** 입구 거부/sender Dropped가 실제 송신 완료 down의 빚을 더 이상 지우지 않으며, X cap 복귀 시 up이 두 경계를 통과한다. OnSentAction의 up 완료에서만 owed를 지운다.
- 기존 parkMu_ 보호·연결 Reset 호출은 그대로다. no-X에는 owed release 예외를 열지 않는 점, entry/sender held bit가 실제 held/owed를 숨기지 않는 점도 수용한다.
- 작업용의 67/67×6·구 header 7 FAIL·21 target/소비 회귀는 보고 인용이다. Codex는 고정 header의 아래 제한 직접 probe만 실행했으며 실제 wire suite/NDK/JNI/AVD/Host는 실행하지 않았다.

## 닫은 반례의 직접 확인

고정 header에 송신/ACK 완료를 대역으로 제공하고 다음을 직접 확인했다.

```
owed_entry kept=1 up_entry=1 up_send=1 before_ACK=1 paid_after_ACK=1
owed_send  kept=1 up_entry=1 up_send=1 before_ACK=1 paid_after_ACK=1
noX_up_refused=1 debt_kept=1 Reset_clears=1
unknown_press_suppressed_until_up=1
rapid_entry_down_up_before_send=1
```

따라서 r2의 “최근 거부 down이 기존 Sent/owed를 가림” 지적은 닫는다. 정상적으로 entry에서 down/up이 모두 queue된 빠른 클릭도 수용된다.

## N1 — queue된 down은 남고, 거부된 up만 사라짐

이번 ledger는 아직 **허용돼 queue된 down의 후속 up이 entry에서 거부됐다는 사실**을 sender에 전달하지 않는다.

1. X 지원 상태에서 down을 **AdmitQueued(true)** 한다. down은 queue에 있고 아직 송신되지 않았다.
2. no-X capability 상태에서 같은 press의 up을 **AdmitQueued(false)** 한다. entryHeld가 있어도 `ok=hostTakesX=false`라서 up은 queue되지 않으며, entryHeld/entryRefused는 모두 false가 된다.
3. 그 down을 sender가 꺼내기 전에 X cap이 복귀한다. **AdmitSend(true, down)**은 허용하고, 송신/ACK 뒤 owed=true가 된다.
4. 대응 up은 이미 false로 반환돼 버려졌고 queue에 없다. 물리/매크로 press는 끝났는데 down만 나갈 수 있다.

직접 고정 header 결과:

```
queued_down_then_refused_up
down_entry=1 up_entry=0 later_down_send=1 debt_without_queued_up=1
```

이는 실제 Host wire/OS latch를 관측한 결과가 아니라, **현재 entry/send 함수와 controller의 false→enqueue하지 않음 경로**를 연결한 source 반례다. control thread가 다른 요청 응답을 기다리거나 송신이 지연된 동안 entry가 up을 거부하고, 이후 Pong이 queue의 down보다 먼저 처리되는 순서가 가능하다. 현재 fake tests는 down의 sender 폐기를 확인한 뒤 다음 phase로 진행하므로 이 순서를 검사한 것이 아니다.

**아직 송신되지 않은 down과 그 대응 up을 함께 정리**한다. up이 entry에서 거부됐으면 pending down을 sender 전에 취소하거나, 그에 대응하는 release를 잃지 않는 형태로 보존/처리해 down만 늦게 나가지 않게 한다. no-X wire에 X edge/bit를 보내는 예외는 만들지 않는다.

정상적인 빠른 down/up은 entryHeld가 이미 false여도 **둘 다 queue에 있으므로** 계속 정상이어야 한다. 단순히 entryHeld=false인 모든 down을 drop하면 이 정상 경로를 깨뜨린다. 실제로 이미 송신된 down의 owed release와도 구분한다. 필요하면 queued action의 seq/press/연결 세대와 disposition을 연결해, 최신 버튼 bool이 이전/다음 queued press의 결과를 잘못 소비하지 않게 한다. 특정 자료구조나 wire 변경을 강제하는 요구는 아니다.

대조는 **down entry 허용→송신 대기→no-X Pong→up entry 거부→X Pong→sender 재개**를 실제 controller/fake Host의 phase로 만든다. 결과는 **orphan down 없음·기존 owed 손실 없음·미지원 wire X 0**이어야 한다. 정상 rapid pair·기존 owed/거부 pair·Reset·X2/left도 유지한다. 실제 Host/사용자 버튼은 필요 없다.

## 유지하는 판정·한계

- cap 감소 그 자체로 즉시 종료가 아니라 **owed 포함 park/drain 실패 때 종료**라는 문구 정정을 수용한다. 그 fail-closed 방향을 바꾸지 않는다. 실제 APK UI/재연결·자동 재탈취 0은 통합 EVIDENCE 범위다.
- NDK/JNI/4 ABI·실 Host/AVD와 cd9937a의 X up 보존이 합쳐진 최종 통합 시험은 미검증이다. 소비 target 재빌드/role_park/prelogin_select PASS는 작업용 근거로 유지하며, Source 성공과 실제 app 행동을 섞지 않는다.
- PC product Source·공용 guard 지정 8개+fatal·ordinary record/legacy 및 슬롯과 독립된 runtime r9 범위는 이 잔여 때문에 다시 전량 보류하지 않는다. 기존 APK X task의 해당 전환만 보수한다.

## 직접 산출물과 사고 기록

`.claude/codex_test_injection_review/apk_xfence_7c5/ledger_r3_probe.cpp/exe`에 원문과 probe를 보존했다. exe SHA-256 **95fc6e4ca851d4516576c40932bc4120749628fddc48337c3e313e5dcdb286f2**.

기존 5개 closure case와 새 queued-down/refused-up 반례를 함께 확인했다. 반환 0은 그 **수정 확인+반례 재현**이며 전체 제품 PASS가 아니다. 실제 Host/Viewer/AVD/JNI/network/input/power/clipboard API는 0이다.

보고된 **C:/Users/shota/nul_placeholder, 6897 B, 공개 header 사본**은 작업용 사고 보고로 분리해 기록한다. Codex가 외부 파일의 내용/크기/비밀 부재를 독립 조사한 것은 아니다. 기존 deletion candidate만 유지하고 삭제·이동/복구하지 않는다. 이후 대조 파일/명령은 repository 내부의 명시 workdir/소유 run으로 한정한다. 제품 SOURCE와 이 명령 실수를 일괄 무효로 묶지 않는다.

제품 편집·전체 제품/NDK 빌드·설치·운영/삭제·commit/push는 하지 않았다. 검증용은 위 **pending pair 처리와 phase 대조**를 기존 작업용1 X task 한 정본으로 연결한다. 닫은 owed 분리의 재설계나 별도 writer는 필요 없다. 사용자 현재 조치 없음.
