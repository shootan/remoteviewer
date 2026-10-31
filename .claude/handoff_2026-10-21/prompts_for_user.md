# 새 세션에 붙여 넣을 프롬프트 (2026-10-21)

## remote_claude (검증용) 탭
```
너는 GNLink(D:\remote\remote) 검증용 세션 remote_claude다. 이전 세션을 clear하고 이어받는다. D:\remote\remote\.claude\handoff_2026-10-21\prompt_remote_claude.md 를 먼저 끝까지 읽고 그대로 따라 진행해. 작업용1·2도 clear됐으니 진행 중이던 조사 A·B를 다시 지시하고, 보고는 존댓말, 답 끝에 "수행 작업: …".
```

## remote_작업용1 탭 (검증용이 A2A 로 지시하기 전에 먼저 시작시키고 싶을 때만)
```
너는 GNLink(D:\remote\remote) 작업용1 세션이다. D:\remote\remote\.claude\task_lockscreen_keys_r1.md 를 끝까지 읽고 CLAUDE.md 규칙대로 조사만 진행해(사용자 결정 절 포함, 수정은 Codex 확정 뒤). 보고는 D:/remote/remote/.claude/report_lockscreen_keys_r1.md 에 쓰고 remote_claude 탭에 A2A 로 1~3줄 pin.
```

## remote_작업용2 탭 (같은 조건)
```
너는 GNLink(D:\remote\remote) 작업용2 세션이다. D:\remote\remote\.claude\task_dxgi_wedge_first_session_r1.md 를 끝까지 읽고 CLAUDE.md 규칙대로 조사만 진행해(수정은 원인 확정 뒤 검증용·Codex 경유). 보고는 D:/remote/remote/.claude/report_dxgi_wedge_first_session_r1.md 에 쓰고 remote_claude 탭에 A2A 로 1~3줄 pin.
```

## REA 세션 (OSLink 분석)
```
D:\remote\remote\.claude\prompt_oslink_re_2026-10-21.md 를 끝까지 읽고 그대로 진행해. 결과는 C:\work\oslink-analysis\ 에만 쓴다.
```

각 탭 on-call 을 켜 두면 검증용이 A2A 로 바로 지시·수신합니다.
