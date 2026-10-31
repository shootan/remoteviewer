# verify lockscreen-keys r1 — 검증용(remote#wwml7h91) NAS 확인, 2026-10-21

대상: report_lockscreen_keys_r1.md §5 요청 3건. 출처: NAS gnlink 읽기, `/opt/gnlink/remote60-directory/logs/shotan/68f79d01-d6c4-4a4a-a3ae-69da5be54826/viewer.log.5`(10-21 14:35 구간)·`viewer.log.4`(15:08 구간). 뷰어 시계 기준.

1. IME: pid 49932(0.2.160, host f6e8e54a) `14:35:34.582 [ime] enter negotiation gen=1` → `14:35:34.586 align done status=1 open=2` → `14:35:34.590 [ime] activated reportedOpen=2`. **K1/K2 확인**(호스트 IME 모드 진입).
2. input-timing 14:35~14:38, pid 49932: `actionKind=12 eventKind=0` **42줄**, actionKind=10 eventKind=2/3 각 21, eventKind=6 3줄. actionKind 12 = `PhysicalKey`(native_video_client_shared_core.hpp:211-223 enum 순서, 5063cd9). **키는 PhysicalKey 로 송신됐다 — "key down 0건" 은 집계 착시 확인.** 호스트 K3 드롭은 로그가 없어 여전히 코드 근거만.
3. unlock: 같은 기기 68f79d01 의 0.2.161 뷰어 pid 22696 이 `15:08:01.668`~`15:08:17.804` 사이 `challenge rejected: rejected(policy/not-locked)` 9회, pid 52832 가 15:08:25.707 1회. **U1~U3 확인**: 매번 RejectedPolicy, 프롬프트 재표시 없이 저장 비밀번호로 재시도, 사용자에게 보이는 결과 없음. 15:08:03 호스트 쪽 교환의 출처 = 이 기기 0.2.161 뷰어.
