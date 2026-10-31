
### 2026-10-26 capture-lifecycle r1 — PS(로그인 전 정지 화면 영상 0)·UA(같은 연결의 UAC 진입·복귀) (fix/capture-lifecycle, 미게시 · R1 0.2.163 통합 대기)
- 근거: 0.2.162 실기 10-22(크래시 재부팅 뒤 진짜 로그인 전 화면에서 PC·폰 모두 영상 0장, 워커 rc 15) · 10-23(UAC 예/아니오가 원격에 안 보임 → 재접속, 승인 뒤 검은 화면 → 두 번째 재접속). 판독 `field_prelogin_reboot_2026-10-22.md`·`codex_uac_prompt_check_2026-10-23.md`, 계약 `codex_rel_0163_r1.md` §1~§4, 정본 `task_capture_lifecycle_r1.md`. 기반 fix/lk-retry 9991b36 위(LK e2e 파일 공유).
- PS-A(정지 화면 원본): 캐시 프레임에 입력 데스크톱 이름·세대(provenance)를 붙이고, 보여도 되는 데스크톱(로그인 전 = 워커가 서비스하는 logon 데스크톱, 로그인 뒤 = Default)일 때만 다시 보낸다(예전: Default 만 → 로그인 전 캐시는 한 번도 못 나감). 데스크톱 변화(두 번 같은 값으로 확인)·detach·보지 않는 동안의 변화는 캐시를 지운다. 캐시가 없으면 캡처에 '전체 프레임'을 요청(DXGI: 다음 acquire 를 내용으로, 없으면 duplication 재생성 최대 3회 / WGC: 세션 재시작), 에피소드당 3회·1 s 간격·재부착 직후 0.5 s 유예. 정지 화면 갱신 1000 → 500 ms, 첫 AU 없이도 갱신 대상.
- PS-B: 로그인 전 워커의 재부착은 보안 데스크톱이어도 DXGI(서비스 데스크톱 attach) 유지 — 예전 "보안 데스크톱이면 WGC" 분기가 picker→idle detach→선택에서 rc 15 를 냈다. 로컬 mode 로만 판단.
- UA-1: 입력 데스크톱 변화를 main loop 한 곳에서 추적(100 ms, 두 번 일치, 판정 불가는 변화 아님·잠금 아님). WGC 데스크톱 캡처면 확인된 변화마다 WGC 재구성 + IDR(변화당 1회, 실패 재시도 3회·backoff). DXGI 는 기존 강등/climb-back 그대로.
- UA-2: backend 요청은 그 논리 세션(epoch) 소유 — 새 세션은 호스트 시작 기본(DXGI, 격리 규칙 유지)으로 리셋, 같은 세션의 resume 은 유지. 세션마다 `session epoch=N backend requested=… active=…` 한 줄.
