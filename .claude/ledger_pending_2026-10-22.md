# 원장 등재 대기 (2026-10-22, 검증용) — 다음 docs 커밋(0.2.162 기록)에 작업목록/원장으로 옮긴다

원인 미확정 · 서로 같은 원인으로 단정하지 않는다(Codex `codex_sh_filter_impl_r3.md` 최종).

1. **수동 IP(TCP 제어) 접속 ~10 s 뒤 화면 굳음** — Host `udp control session ended epoch=1 resumed=0 reason=none` 4회, 필터 off 에서도. 가설: `host_startup_control.cpp` `UdpControlLink(…, 10000)` 읽기 timeout. 기준판 대조 미완료. 근거 `.claude/report_sh_filter_impl_r1.md`.
2. **control loop failed (Android 뷰어)** — 검증용 SH touch_host 1차: window-select applied 10:28:00.632 직후 `tcp client disconnected` 10:28:00.633 → 앱 "control loop failed", 첫 탭 전. 원인 native 줄 없음. 근거 `C:/work/gnlink-wt/verify-sh/.verify/verify_sh_r3.md`, 로그 `D:/remote/remote/.claude/test-tmp/verify-sh-touch-r3/`.
3. **clipboard sync failed 로 세션 종료(Android, 크게보기 6 s 뒤)** — 작업용2 r3final touch_host large_weak. 근거 `.claude/report_sh_filter_impl_r3.md`.
4. **연결 hello 지연(첫 연결·재연결)** — 원인 근거 확보·수정 진행 중(hello-delay-fix t-q4eq9aiz). 1~3 과 같은 원인으로 단정 금지.
5. 관찰: SH touch_host 성공/실패 양쪽에서 연결 ~10 s 뒤 `udp control session ended reason=none` — 그것만으로 TCP 실패 원인 확정 금지.
6. 별건(NP 문서 §6): PC 뷰어 directory 경로에서 relay kind 가 Public 으로 바뀌는 코드 경로.
7. 운영: 시험 exe 용 Windows 방화벽 Allow Inbound 'Query User' 규칙 10개(C:/work/gnlink-wt/... 시험 빌드) — 삭제는 관리자 + 사용자 판단.
8. **viewer_udp_recovery 3 FAIL**(:1081/:1098/:1146 stream flowed, max seq 52–73) — hello 후보 6e89962 와 기준판 5063cd9 가 같은 3줄에서 FAIL. 작업용의 "qemu 부하" 귀속은 검증용 측정(qemu 0.15 core/s, 전체 CPU 3–4%)으로 뒷받침 안 됨 → 원인 미확정. directory_retry 와 원인 합치지 않음(Codex codex_hello_delay_fix_r2.md).
9. **directory_retry 간헐 FAIL** — 조사 완료(`.claude/report_directory_retry_flake_r1.md`): 시험 설계 결함(고정 대기 뒤 단일 샘플이 상태 전환 경계) + 제품 상태 문구 계약 불일치(directory_client.cpp:1602 가 광고 없음 문구를 timed out 으로 덮음), hello 무관, 기준판 5/10·후보 6/10, 9월 이전 기원. 수정안 A(제품 문구 계약)/B(시험 관찰 단계) Codex 판단 대기.
