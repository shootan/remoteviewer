# task reconnect-hello-delay r1 — 첫 재연결 hello 지연 조사 (검증용 remote#wwml7h91, 2026-10-22) — 담당 작업용1 (LK r4 보고 뒤 착수)

**이 파일이 정본이다. 끝까지 읽고 CLAUDE.md 대로.** 조사만 — 제품 수정 금지(원인 확정 뒤 검증용→Codex).
근거: Codex `.claude/codex_dxgi_wedge_fix_r3.md` ②, 검증용 기록 `C:/work/gnlink-wt/verify-eop/.verify/verify_dxgi_wedge_r3.md`(+ `dxgi_r3/` 로그).

## 사실
- dxgi-wedge E3 사슬(4db9f17)에서 호스트는 살아 있고 뷰어만 exit 43 으로 죽은 뒤의 **첫 자동 재연결 hello 가 11.25 s**(attempts 181, badAcks 25). 호스트는 그 사이 epoch=2 세션을 열었다 닫음 → `window_list_timeout` → control-lost → exit 43, 두 번째 재연결은 정상. 작업용2 실행에서도 hello 7.4 / 9.5 / 9.2 s 관측(시간 안이라 PASS).

## 할 일 (제한 조사)
1. 기준판(main 5063cd9)과 후보(4db9f17) 각각에서 "Host 생존 + Viewer 프로세스 재생성" 조건의 재연결 hello 시간을 fixture 로 재현·측정(반복 N회, 분포).
2. 타임라인: fixture/capability/epoch — 새 뷰어의 hello 와 호스트의 이전 epoch 정리·새 epoch 개설·badAck 원인(옛 세션 잔여 패킷? capability 1회용·30 s? epoch 불일치?)을 양쪽 로그로 맞춤.
3. 원인 후보와 근거, 최소 수정안(있다면) 보고. **원인·baseline 확정 금지 수준의 추측 금지 — 근거로만.** 인증 완화·timeout 증가·재시도 확대는 수정안으로 내지 않는다.

## 규칙
측정 슬롯 CreateNew, 설치본·실서비스·세션 조작 금지, 새 worktree 또는 기존 시험 빌드 재사용(산출물 공유 금지 원칙은 검증용 쪽). 보고 1회: `.claude/report_reconnect_hello_delay_r1.md` + 검증용 pin.
