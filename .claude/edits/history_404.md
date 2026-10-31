### 404) 2026-09-07 P9 보강 — target rect 를 실제 캡처 HMONITOR 의 live 기하로 읽고 1초마다 재확인(같은 해상도·원점만 변경 포함) (A2A task t-xox45oo5, Codex 리뷰 반영)
- 배경: #402 는 "모든 기하 변경은 캡처 재시작을 지난다" 는 가정 위에 있었다. Codex 지적대로 같은 해상도에서 모니터 원점/주모니터 배치만 바뀌면 size-change 가 없어 재시작이 없을 수 있다.
- 변경: `sync_input_target_rect`(`host_loop_helpers.cpp`) 가 캐시된 `monitorInfo` 의 rect 가 아니라 캡처가 연 HMONITOR(`capture.monitorInfo->monitor`)에 `GetMonitorInfoW` 를 **live** 로 물어 물리 rect+원점을 도출(핸들이 무효면 마지막 기하 유지). 호출 지점에 `stage_stats` 의 1초 tick(`"periodic"`) 추가 — GetMonitorInfo 1회+비교, 값이 바뀔 때만 `SetTargetRect`+로그. broker 자체 mutex 유지, 창모드 skip 유지, virtual 전체 치환 없음.
- 테스트: `remote60_host_input_target_rect_test` +1 — 같은 1920x1080 에서 원점 (0,0)→(1920,0)/(0,1080) 변경이 rect 변경으로 판정되고 매핑이 새 원점으로 감. PASS. GNLinkStream 빌드 OK.
- 미검증: 실제 배치 변경(다중 모니터 장비 없음); 실기 시 호스트 로그 `secure-input target rect= … reason=periodic` 으로 확인 가능.
- Ledger 유지: 모니터 선택이 다음 재시작에서 주모니터로 되돌아가는 결함은 이번 범위 밖(#402).
