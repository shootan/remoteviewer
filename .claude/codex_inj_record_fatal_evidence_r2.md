# Record/fatal r2 — EVIDENCE와 하네스 종료 범위

2026-10-29 · **두 시험의 구성 단위 EVIDENCE OK.** ccaf745의 독립 실행에서 record 12/0, fatal 26/0을 수용한다. 앞서 수용한 일반 8종과 합쳐 **합의된 안전 실행 하네스 보수·독립 검증 관문은 닫는다.** 실제 Windows 입력 성공·prelogin 재설계 완료·제품 통합/출하의 승인은 아니다.

## 고정과 직접 확인

- 보고 `.claude/verify_inj/VERIFY_RECORD_ba2eaf2.md`의 r2 절 포함 SHA-256 **162a4fd282a8201506f2593ae6c4fcf3741bce0f1ec3e5a1d9049c55c402f53e**를 확인했다.
- 독립 worktree verify-inj-b8 HEAD **ccaf7453f366e2baa4045a426f33978bb27c61a7**, tracked 변경 없음. 기존 r10 SOURCE 판정과 일치한다.
- r2/hashes.txt의 **14개 artifact 전체 hash를 제공 파일과 직접 대조**했다. r2_hashes_build.txt의 직접 build 4개도 실행 목록과 일치한다. 그 목록에 남아 있는 다른 일반 8종 exe를 r2에서 다시 실행한 것으로 세지 않는다.

| r2 대상 | SHA-256 |
|---|---|
| os_input_record_e2e | e676405faf2225213cf712a209f2004df725edc374a4e9891cde9f4ea1043948 |
| host_fatal_exception | 9f27873ba27c7bae28433cc7a1119426f9f73bf1b5de5c138f47b52114588963 |
| GNLinkStreamInputRecordTest | ffedcfd7795d4e3bd507508a579b3deeebf18155482cfdb44d658ecff9779715 |
| GNLinkStreamMigrationTest | 9087a81db35ffc0d46253a6be63818779546ea9d02ffdba8e841d5abfe03267b |

MigrationTest의 b8 동일 hash는 기존 제품 source 변경 0 판정과 부합한다. 그 artifact 하나의 동일성만으로 모든 제품 파일의 무변경을 증명하는 것은 아니다. record Host의 record marker ASCII 2개도 직접 확인했다.

- record 원문 log SHA-256 **6aa4860b00989a46bac937444909bd615c72a10c8d55dc65d93e1f20295408f6**
- fatal 원문 log SHA-256 **e6adc962bf59f39719477dd4dd0b45752963a957d1ce796f34c0fa36e4892157**
- 보고/build manifest/실행 manifest/summary/원문/environment 파일 8개의 검토 시점 hash는 `.claude/codex_test_injection_review/verify_inj_r2_audit.json`에 보존했다.

Codex는 원문·hash·현재 고정 HEAD를 읽었으며 **실제 두 시험을 재실행하지 않았다.** console/slot CreateNew·자기 해제는 검증용의 환경/실행 기록으로 유지한다.

## Record — 12/0 수용

원문을 BOM 제거 후 읽어 **PASS 12행, FAIL 0**, summary rc 0 및 RESULT ALL PASS를 직접 확인했다.

- record-only 선언 확인 뒤 loopback client가 연결되고 Connected에 도달했다.
- display wake와 monitor-power 호출은 ctx none의 **기록**으로 확인됐다.
- 이 새 Host의 단일 첫 접속 경로와 로그 조건에서, source 초기 epoch 1을 사용했다는 출처가 출력됐다. r10에서 수용한 source 불변식에 의한 판정이며 effect의 epoch를 역으로 expected에 넣은 것이 아니다.
- Tab down/up·move·left click down/up·text의 **입력 dispatch 6개와 이 session의 효과 순서/fragment 매칭**이 통과했다. text는 A/b unit 단위다.

따라서 r1의 epoch 판독/매칭 실패는 같은 제품 source의 수정된 하네스에서 재현되지 않았고, 해당 한정 검증을 통과했다. r1 Host 원문은 삭제돼 없으므로 그 run 전체를 사후 복원하거나 옛 실패를 PASS로 바꾸지는 않는다.

이것은 검수한 record-only 경계의 **수신·정책·routing·dispatch/효과 기록** 근거다. Windows가 실제 SendInput 등을 수락했거나 실제 사용자 desktop/logon UI에서 입력이 성공했다는 증거는 아니다. real OS 효과를 실행하지 않는 별도 build라는 경계와 clipboard OFF·기존 stage/job 조건을 유지한다.

## Fatal — 26/0 수용

원문 **PASS 26행, FAIL 0**, summary rc 0·약 33초 및 RESULT ALL PASS를 직접 확인했다.

- std/winrt startup fault는 exit 49와 필요한 예외 종류/step/본문을 검사했다. early startup fault에서 input 줄이 아직 없을 수 있다는 기존 예외는 power OFF·whole output·무상충과 함께 적용됐다.
- none 정상 대조는 configured/waiting client에 실제 도달하고 codec refusal/FATAL 없이 시험 deadline으로 끝났다. 출력의 4294967295는 이 시험의 deadline sentinel이며 정상 제품 exit 0이나 제품 crash code라고 부르지 않는다. 종료 확인·EOF와 OFF 두 줄 final 판정도 통과했다.
- 허용된 prelogin worker의 **첫 configure 단계 fault**는 exit 49와 자기 log/stdout을 검증했다. 이것은 금지된 viewer_prelogin_login_e2e 실행이나 자격 증명 입력 UI 시험이 아니다.
- 각 지정 client case에서 Hello 전 gate/상충 없음, probe/wrapper trap 및 IsSupported throw/false·정상 encoder 경로의 검증이 통과했다.

보존된 `.claude/test-tmp/verify-inj/fatal-25920-609237140-0`에는 자기 lad_* 9개 폴더가 남아 있음을 직접 확인했다. 비어 있지 않은 폴더를 강제 재귀 삭제하지 않고 NOTE로 남기는 것은 검수된 정리 계약이다. **이 NOTE는 시험 실패나 cleanup 미확인을 숨긴 것이 아니다.** 이를 없애려고 추가 삭제할 필요가 없다.

## 닫는 범위와 남기는 범위

**닫을 수 있는 한정 범위**는 일반 8종의 OFF gate/실행 중·최종 판정/자식 수명 하네스, ordinary record lane, fatal lane의 보수와 그 독립 구성 단위 검증이다. 일반 8 r1 EVIDENCE와 이번 r2 두 시험을 연결하면 같은 범위의 전량 재실행·같은 수정의 재상신은 필요 없다. 이미 수용한 runtime/root/T1/J1 안전 보수도 각 고정 기록으로 연결한다.

test-injection-finding 전체를 다음까지 완료한 것으로 쓰지는 않는다.

- real-input 4개는 기본 REFUSED 유지다. 그 record-sink 전환과 원래 real OS 효과 시험은 완료 근거가 아니다.
- viewer_prelogin_login_e2e는 사용자 명시 재개 전 실행 금지이며 prelogin 관측 provider/재설계는 별도 미구현이다.
- old-host NOT RUN 및 기존 PS/LK·APK prelogin 실기/안전 재확인의 미결은 이번 normal record/fatal 성공으로 복구하지 않는다.
- ka-pc의 별도 Child job-close 보수, AVD guard의 실제 E 시나리오, R2 통합/제품 build·릴리스는 각 task에서 진행한다. 서로 다른 source/시험 결과를 합쳐 단일 통합 후보를 실행했다고 주장하지 않는다.
- 과거 실제 입력·power 효과 및 기본 TEMP 경계 위반의 사고 기록/미확인은 유지한다. 새 안전 run이 과거 영향을 소급해 0으로 만들지는 않는다.

검증용은 정본 상태를 **“안전 하네스 보수·독립 검증 완료, 별도 미구현/금지/통합 항목 유지”**로 정리하고, 기존 task의 history·계획 체크·관련 기록/커밋 절차를 마무리한다. 새 제품 변경이나 추가 실험을 요구하는 것은 아니다. 과거 중간 “미실행” 상태와 r1 실패는 이력으로 보존하면서 현재 상태를 명확히 한다. 해당 기록 정리 뒤 이 한정 하네스 작업을 종료 처리할 수 있다.

이번 Codex는 제공 자료 읽기·hash audit·판정 기록만 수행했다. 제품 변경·시험 실행·운영·설치·사용자 입력/clipboard·삭제·commit/push는 없다. 사용자 현재 조치 없음.
