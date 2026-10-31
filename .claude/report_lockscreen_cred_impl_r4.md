# report lockscreen-cred-impl r4 — Codex 53506ab NEEDS_CHANGES R1 / R2 (작업용1 remote#rqxbj3jb, 2026-10-22)

task t-rs9nhsdv / rev r4 (정본 `.claude/task_lockscreen_cred_impl_r1.md` 'r4 보수', 근거 `codex_lockcred_impl_r3.md`) / kind=implement-report.

## 고정 후보
- worktree `C:/work/gnlink-wt/fix-lockscreen-cred`, 브랜치 `fix/lockscreen-cred`, HEAD **`e728485`**. 워킹트리 깨끗. 버전·서명·게시·push 없음.
- r4 커밋 (53506ab 위):
  - **`661a0c3`** = **제품 + 시험이 한 커밋**이다.
    - 제품: `host_unlock_relay.cpp` +4/−1, `viewer_unlock.cpp` 1줄.
    - 시험: `host_unlock_relay_ondemand_test.cpp`.
    - 원래 제품 커밋을 따로 만들려 했으나, 커밋 메시지 JSON 이 cp949 로 써져 Git MCP 가 UnicodeDecodeError 로 실패했다. 이미 stage 된 두 제품 파일이 다음 시험 커밋에 함께 들어갔다. 이력 재작성(amend) 대신 그대로 두고 여기 적는다.
  - 문서 **`e728485`**.
- 출하 구성 빌드 `fix-lockscreen-cred-build` (Release, 661a0c3 소스) sha256:
  - GNLinkInputService `B22C456153ACC2CCEF6E3FF6962FAE2024E070B59109A0E0444F189B8DE6C67C` — r3 와 같다. 서비스 소스는 바뀌지 않았다.
  - GNLinkStream `1510A2417E2AE41CAF86517DB254C634E97CCCFD3BA673F6D5F2197112129845`
  - GNLinkViewer `69276B4E951484603C3E2B6851DE7D2DC9427E5F9EC538FA9D36B45D815F0452`
  - gate `--dir` 9/9 PASS.

## R1 — 같은 typed 요청 재전송이 보유 마감을 늘림
- 수정 (`host_unlock_relay.cpp`, sealed 결과 처리): 응답이 비종결이고 살아 있는 트랜잭션일 때, **이미 그 같은 트랜잭션**(cookie·requestId·txGen)**을 typed 로 보유 중이면 `hold_for` 를 호출하지 않는다.**
  - 최초 typed 마감이 유지된다.
  - 재전송의 교환 자체도 남은 보유 시간 예산(r3 `budget`) 안에서만 돈다.
  - 처음 typed 가 될 때만 130 s 보유가 시작된다.
- 반례 (relay **41/41 ×3**, 보유 2 s 시험 seam):
  - 첫 Injected 뒤 0.5 s·1.0 s·1.3 s(서비스 응답 +0.3 s 지연)에 같은 sealed 를 재전송했다. 서비스에 3회 도달했고 3회 모두 다시 “typed” 로 답했다.
  - 최초 마감 직후(2.34 s)에 다음 Host(별도 relay)의 challenge 가 발급됐다.
  - 그 트랜잭션은 UnlockUnconfirmed(종결)로 끝났다.
  - 대조: 최종 답 뒤 재전송은 서비스로 보내지 않는다(기존 멱등 유지).
  - 서비스 단위의 “재전송은 재입력 0” 은 기존 lockcred 시험 그대로이고, 이번에는 바꾸지 않았다.
- 변이 (재전송마다 마감 재시작 = r3 동작) → “다음 Host 통과” FAIL(8.5 s 뒤에도 실패). 원복하면 41/41.
- 실제 249 s 시험을 돌렸다는 주장은 아니다.

## R2 — Busy 문구
- `viewer_unlock.cpp`: “그 요청이 끝나거나 취소된 뒤 다시 누르세요.” 로 바꿨다(“(길어야 2분)” 삭제).
- 현장 묶음 `C:\work\gnlink-wt\lockcred-scratch\field_bundle.md` A⑧-b 문구도 같이 바꿨다.
- history 의 r3 줄은 당시 문구 그대로 두고, r4 줄에 변경을 적었다.
- 제품 UI 1회: e2e `--lock-screen` **44/44**, exit 0.
  - SLOT_REQUEST CreateNew → 삭제 완료. 콘솔 세션. 설치본·실서비스 무조작.
  - Busy 창 캡처: `C:\work\gnlink-wt\lockcred-scratch\e2e_r4\cap_lock-screen\lock_screen_busy.png`
    - 1행 “다른 연결이 이 원격 PC의 Windows 로그인(잠금 해제) 요청을 처리하고 있어 보내지 않았습니다.”
    - 2행 “그 요청이 끝나거나 취소된 뒤 다시 누르세요.”
  - 일반 오류 대조 창(“오류 0”, Busy 문구 없음)은 같은 실행에서 PASS.

## 미검증·유지
- OS 실기 A~D 는 미검증이다: 실제 SYSTEM 서비스 → 콘솔 agent, LogonUI, 재부팅, UAC, APK.
- S1 role_park 간헐은 미해결이다.
- APK 0.2.30 영향은 유지한다.

다음 담당: 검증용 — `e728485` 확인 → Codex 검토.
