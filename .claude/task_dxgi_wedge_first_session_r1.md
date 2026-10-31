# task dxgi-wedge-first-session r1 — 회사 PC 첫 접속 몇 분 안에 화면 멈춤·뷰어 창 닫힘 (검증용 remote#4houkzd5, 2026-10-21) — 담당 작업용2

사용자 보고(10-21 15:15 이후): "회사 PC 게임하려고 접속했는데 멈추고 창이 꺼졌다. host 시작하고 첫 번째 접속 때 몇 분 안 돼서 바로 꺼진다. 매번 이렇다." 조사 먼저 — 수정은 원인 확정 뒤 검증용→Codex.

## 검증용이 NAS 로그로 확정한 사실
- 호스트 = 회사 PC DEV-SHOTAN-L (hostId df15b73b18cda3b1), NAS `/opt/gnlink/remote60-directory/logs/shotan/51bd6657-9b5b-41d0-8385-6557d42c3990/host.log`(0.2.161). 뷰어 = 68f79d01(…/68f79d01-…/viewer.log, 0.2.161).
- 15:16:39 접속(session epoch=2), DXGI 캡처, 1920x1080 8 Mbps.
- **15:16:51 `[dxgi-watchdog] dxgi-worker-wedge phase=release phaseAgeUs=5230677 … terminating (exit 44) for supervisor relaunch`** → host-app: `streaming host self-terminated (dxgi-worker watchdog) code=44 ranMs=986860 recoveries5m=1 restart_planned=true` → `repeated DXGI wedge: using WGC recovery backend` → 15:16:52 GNLinkStream 재기동(pid 35952), 15:16:52.526 directory online.
- 뷰어(pid 55212): 15:16:55 수신 정지 → 15:16:59 link-silent → 15:17:01 control peer-lost → **15:17:07 session-dead action=close cause=control-lost → 창 닫힘**. 호스트가 1 s 만에 다시 떴는데 뷰어는 재접속하지 않고 닫았다. 15:17:08 사용자가 다시 접속(pid 27888) → keyframe 재시도 3회, 15:17:13 done.
- 과거 같은 패턴: host.log.3 **10-17 13:46:50 dxgi-worker-wedge phase=release**(5.18 s). 다른 날에도 있었는지 viewer/host 로그 전 회전본 확인 필요.
- "첫 접속만" 설명 후보: wedge 뒤 host-app 이 WGC recovery backend 로 전환 → 같은 호스트 실행 중 다음 접속은 WGC 라 재발 안 함. 호스트 재시작 시 DXGI 로 되돌아감.

## 할 일
1. **wedge 원인**: DXGI ReleaseFrame(phase=release) 이 5 s 넘게 반환하지 않는 조건 — 게임(전체 화면 독점/해상도·모드 전환/HDR/VRR 등)과의 관계. 회사 PC 의 로그(호스트 로그 전후, 캡처·디스플레이 이벤트)로 근거 정리. 다른 날 발생 이력 전수(host.log ~ .N, viewer.log 회전본)와 각 시각에 무엇이 화면에 있었는지.
2. **뷰어 동작**: 호스트 스트림 재기동(exit 44 + 1 s 내 재등록)인데 뷰어가 session-dead 로 창을 닫는 경로 — 재접속(같은 호스트 자동 재연결) 설계가 있는지, 왜 안 탔는지(liveness 정책·12 s).
3. 개선안(구현 전): ⓐ wedge 회피/조기 전환(예: 게임·독점 전체 화면 감지 시 WGC, 또는 wedge 이력을 재시작 뒤에도 유지) ⓑ 뷰어가 호스트 재기동을 '잠깐 멈춤 + 자동 재연결' 로 처리. 각각 근거·위험·시험 방법.

## 규칙
사용자 PC·회사 PC 조작 금지(로그 읽기만, NAS 는 배포 계정 읽기). 제품 수정·버전·게시 금지(조사 단계). 측정 슬롯 CreateNew.

## 보고 (1회)
`D:/remote/remote/.claude/report_dxgi_wedge_first_session_r1.md` + 검증용 pin.


## 주소 갱신 (10-21 저녁 세션 clear)
이전 세션 주소(검증용 4houkzd5 등)는 무효. 보고는 a2a_agents 의 displayName 'remote_claude' 세션에 pin. 이전 세션이 끊겨 보고가 없었으므로 처음부터 진행.
