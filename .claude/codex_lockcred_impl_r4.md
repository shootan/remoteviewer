# lockscreen-cred r4 — Codex SOURCE/EVIDENCE 판정

2026-10-22 / task **t-rs9nhsdv** / 고정 후보 **e728485047a7c23402c45578e64b621e552c6bcb** (제품+시험661a0c3, 문서e728485).

**SOURCE / EVIDENCE OK — r3 잔여 R1/R2 닫음.** 요청된 소스 보수와 격리 자동 검증 범위의 판정이다. 상태는 **LK 테스트 후보 · OS 실기 대기**이며, 실제 SYSTEM/LogonUI 성공·전체 제품 구현 완료·통합/일반 릴리스·게시 승인을 뜻하지 않는다. 새 지적이나 이 범위의 추가 반복 보수 요청은 없다.

## 보수 확인

| 항목 | 판정·근거 |
|---|---|
| R1 최초 typed 마감 | **OK.** relay 비종결 응답 처리에서 이미 같은 cookie/requestId/txGen을 typed로 보유하면 hold_for 생략. 최초 typed 진입만130초를 시작하고 기존 r3의 절대 마감·남은 I/O budget·종결/세션 종료 반환은 유지 |
| R2 Busy 문구 | **OK.** 보장할 수 없는 “길어야2분” 삭제. 실제 결과 창의 “그 요청이 끝나거나 취소된 뒤 다시 누르세요.” 확인. Busy와 일반 오류 분기는 유지 |

53506ab→e728485의 실제 제품 diff는 relay 조건과 문구 두 파일뿐이다. 제품/시험이 한 커밋661a0c3에 포함된 점은 보고에 정확히 공개됐고 기능 범위가 같으므로 이력 재작성이나 분리 커밋을 요구하지 않는다. tracked clean과 CR-at-EOL 허용 diff-check를 확인했다.

## 직접 대조한 근거

- 작업용 `.claude/report_lockscreen_cred_impl_r4.md`, 검증용 `C:/work/gnlink-wt/verify-lockcred/.verify/verify_lockcred_r4.md`, 제품/시험 diff와 `.verify/r4/` 원본 로그를 읽었다. 양쪽 worktree의 고정 HEAD를 확인했다.
- 검증용 build-vs hash 직접 계산/일치:
  - Stream **e0952a8f1a268368274257ac07e2cfdb109ed6b75af5d9edc981a14aa99f4c3c**
  - Viewer **b4ea836339345a5f2154e06197d920c6accb99160486d05b33407a5a09e62704**
  - InputService **dcd98c971ea89782c278df85b361199f214d5357301654155a011bf5cd59decc** (r3 독립 산출물과 같음, 서비스 소스 불변).
- 독립 relay **41/41×3** 원문 확인: 같은 sealed 재전송3회 도달, 최초 typed 뒤 **2361/2347/2350ms**에 다음 Host ChallengeIssued, 원래 transaction은 UnlockUnconfirmed15로 종결, terminal 뒤 재전송0. 시험 hold2초에서 마지막 재전송이 약1.6초에 응답하므로 마감이 다시 시작했다면 약3.6초 뒤여야 한다. 관측 시각은 최초 마감 보존을 구분한다. 실제130/249초를 실행했다는 뜻은 아니다.
- 마감 재설정 변이 실패는 **작업용 실행 인용**이다. 검증용/Codex가 그 변이를 독립 재실행했다고 쓰지 않는다. 이번 독립 process 시험의 판별 수치와 소스 조건으로 R1을 닫는다.
- gate9/self19 및 seam 출하5exe0/시험6·4 원문 확인. 실행 rc0/build rc0·환경은 검증용 기록 귀속이다.
- 독립 제품 UI **--lock-screen44/44** 원문 및 Busy PNG를 직접 열었다. 실제 시험 파이프 Host A pending→Busy16→칸 무입력/A 무간섭→A 종료→수동 retry typed8·바이트 교체→unlocked9 순서, 새 안내 문구가 확인된다. 일반 오류 창의 PostMessage 대조는 문구 분기 검증이며 relay 실패 생성 근거와 구분한다. 서비스는 비-SYSTEM/가짜 잠금/owned 창의 격리 시험 빌드다.

Codex는 제품 빌드·테스트/UI·설치 서비스를 재실행/조작하지 않았고, 소스·원본 로그·해시·캡처를 독립 대조했다. r1~r3에서 닫은 목적/세션 격리·원래 expiry·최종 입력 정책·필드 교체·취소/늦은 cache·gate 판정은 이번 좁은 diff에 영향이 없어 유지한다.

## 상태와 다음 담당

- 검증용은 **LK의 현재 자동 보수 검토를 닫고 e728485를 테스트/통합 검토 입력 후보로 고정**한다. 같은 근거의 추가 재검사·재승인 왕복은 필요 없다. DW/SH의 미결 보수·검증은 기존 task에서 독립적으로 이어간다.
- 통합 때 실제 선택한 build commit·공통 protocol/gate 합집합·영향 검증을 새 고정 통합 후보에 귀속한다. 단독 LK hash를 통합 산출물 hash로 재사용하지 않는다.
- 실제 OS A~D(Session0 SYSTEM 서비스→콘솔 SYSTEM child, 일반 agent 최종 정책, 실제 LogonUI 교체/Enter/해제, 재부팅 로그인 전·UAC·APK), 두 실제 Host PC Busy·sealed Busy 화면은 미검증이다. S1 간헐도 미해결로 유지한다. 현장 묶음에서 이 한계를 분리하고 실제 설치/세션 조작은 사용자의 해당 범위 지시에 따른다.
- APK0.2.30 잠금 화면 암호 입력 차단 영향·S5 미결, 버전 인상/서명/게시/사용자 설치/push 보류 유지. 이 판정으로 그 범위를 변경하지 않는다.

사용자가 지금 할 일: 없음. Codex 변경은 미추적 검토서/인계뿐으로 별도 제품 변경·commit·push 없음. 제품 관련 기존 커밋은661a0c3/e728485이며 공개 기록은 해당 기능 task에 이미 포함돼 있다.
