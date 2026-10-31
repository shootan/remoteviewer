# Windows 안정화 1차 + 0.2.104 후보 — 종료 메모 (작업용 remote#d2453q5i, 2026-09-08 16:2x)

Codex 최종 확인 완료. 검증용(remote#srb0qig4) 지시로 **버스 task 는 갱신하지 않는다**(재시작으로 t-uhqkw7h9·t-thjz1nw2 의
당사자 pin 이 모두 끊겨 양쪽 다 `a2a_task_update` 거부 — `.claude/codex_restart_checkpoint_20260908.md` §0 에 예고된 상황).
완료 근거는 `docs/history.md` #416~#426 + 구현계획 + 작업목록 1.4~1.8, 그리고 검증용의 검사 보고다. 이 파일은 미추적 메모.

## 커밋 10건
858f766 A01 log_upload 401 busy loop (#416) · 7d0d69b V14 공통 maintenance + A04 유계 포기 (#417) ·
c77b01d A02/A06/HN07 인코더 provenance (#418) · fe42957 A04 tombstone (#419) · 5549d7c 배선·경계·로그 스로틀 (#420) ·
f77adad 반려안 제거·미완 표시 (#421) · ac48758 포화 episode (#422) · f13887b A06 실행 회귀·동작 보존 추출 (#423) ·
adeffe7 포화 경계 6건 (#425) · **8d9ec74** chore(release) 0.2.104 버전 bump + NoteKeyAccepted 주석 정정 (#426).

## 산출물
`dist/GNLinkSetup-0.2.104.exe` — 3,475,968 bytes,
sha256 `039b3a47d36850292c2049400719ddeb0b17fd39f50ce73dec445e6d5d4dff2f`.
제품 소스 = 8d9ec74(릴리스 트리 = adeffe7 + 버전 상수 1줄 + 주석 2곳, 로직·wire·ABI 변경 0).
임베드 "0.2.104" 3건 / "0.2.103"·"0.2.102" 0건. payload 6종 해시 = 그 Release 빌드 산출물과 일치.
전체 Release 빌드 1회 exit 0 — 로그 `.claude/release_build_0.2.104.log`. **미설치**, push·라이브 중지·서버 배포 없음.

## 근거 구분
- 검증용 직접: A01 log_upload / uploader spin, shared_core, viewer_udp_recovery, host_encode_epoch(seam [5] 실제 overflow),
  host_sender_epoch, 8d9ec74 diff·설치본 크기/sha256/임베드/payload 대조.
- 작업용 보고(검증용 미재실행): 단계별 수정 전 FAIL → 수정 후 PASS, `.claude/seam_sweep1.txt` 38 PASS + 2 FAIL,
  `.claude/bnd_sweep1.txt` 38 PASS + 2 FAIL, recovery 단독 ×5, shared_core ×3.
- 8d9ec74 기준 sweep 재실행 없음(제품 로직 무변경, Codex 지시).

## 미완 — "전수 PASS"·"해결됨" 아님
1. 사용자 실기 미실시: 401 뒤 업로드 재개 · 제어만 오는 링크의 복구 타이머 · 정적 화면 꼬리 손실 복구와 포기 1회 ·
   UAC 복귀 뒤 인코더 재동기 · 고화질 영상 정지 재현 여부.
2. 미해소 4종(원인 미확정): `udp_control_e2e_test`(UDP 43000 라이브 점유) · `gdi_capture_process_test`(3.6fps) ·
   epoch [3] `holdKick` 단정(1607404 도입) · S15 `WaitRealSenderHeld(2000)`(1287883 도입). 뒤 둘은 이번 변경 이전 자산.
3. 09-08 11:27 물리 단절(제어·영상 동시 정지) 원인 미확정·미해결. 이번 후보가 해결을 보장하지 않는다. 2차 캡처는 별건·보류.
4. A06 stage 호출 배선은 **코드 대조까지**(실행 회귀는 `RunEncodeCall` 이하, #423 ④).
5. 범위 밖: A03·A05·A07~A17, HN01·HN04, N2.

## 다음
설치·라이브 세션 중지·push·서버 배포·2차 캡처·후속 구현 전부 **미시작**. 다음 지시는 검증용 remote#srb0qig4 → 작업용 remote#d2453q5i.
