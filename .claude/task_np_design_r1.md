# task np-design r1 — 직접 연결 우선(중계 경유 줄이기) 설계안 (검증용 remote#7fsfy22z, 2026-10-27) — 담당 작업용2

**이 파일이 정본이다. 끝까지 읽고 CLAUDE.md 대로.** 상위 계획 `.claude/plan_rel_0163_2026-10-26.md`(R2 = PC 0.2.164 + APK 0.2.33). 구속: Codex `codex_rel_0163_r1.md` §6 — "현 집계/가설만으로 STUN/relay race·광고 포트 변경 설계를 확정하지 않는다. 기존 NP 문서에 맞춰 **실제 socket 관찰·fresh connectivity check·relay fallback 의 한 설계안**을 먼저 취합해 Codex 확인 후 구현. 공유기 UPnP/NAT-PMP 설정 변경·광역 NAS 권한 완화·실험적 timeout 단축은 묵시 승인하지 않는다."

## 사용자
10-22 "중계 서버 경유 안 하고 펀치홀 뚫는 방법 찾아보라" → 분석 문서화 → "앞으로 구현". 실기: 회사→집 PC 재접속이 relay grace 2.5 s 뒤 중계로 붙어 느림(NAS viewer.log, `field_0162_0231_2026-10-22.md` §2). 표본 60% 중계.

## 할 일 (이번 r1 = 설계안 문서만, 제품 코드 변경 없음)
1. 근거 읽기: `docs/직접연결_중계_분석_2026-10-22.md`(main), NAS 로그(읽기 전용 — gnlink 키로 `/opt/gnlink/remote60-directory/logs/shotan/*`; 수정·삭제 금지), 현재 소스(host/viewer punch·directory_client·relay, 서버 `apps/directory`).
2. **측정 먼저**: 최근 접속들에서 kind(private/public/relay)·punch 응답 시간·실패 이유를 기기·시각·버전별로 집계(출처 고정). 집 쪽 Host 의 실제 외부 mapping(공인 IP:port, 포트 보존 여부, NAT 유형 추정 근거)과 회사 쪽 viewer 의 관찰값 — 가설과 측정 구분.
3. 설계안 하나: 직접 경로를 언제·어떻게 확보하고(예: 실제 socket 의 외부 mapping 관찰·갱신, fresh connectivity check 순서·타이밍), 실패 시 relay fallback 을 지연 없이 하는 방법. 기존 연결 지연(현 2.5 s grace)과 비교한 기대 효과를 측정 근거로. 대안별 장단점, protocol/서버 변경 필요 여부, 구 Host·구 APK 호환, KA r2(대기 연결·relay lease)와의 관계(작업용1 `spike/host-admission-r3` — 같은 파일을 건드리면 순서 조정 필요).
4. 금지: 공유기 설정 변경, NAS 서버 설정·권한 변경, 운영 서버 배포, 실험적 timeout 단축을 제품에 넣기, 사용자 설치본·서비스 조작. 측정용 시험 실행은 격리(loopback·시험 Host)만.

## 보고
`.claude/proposal_np_r1.md`(측정 표 + 설계안 + 구현 범위·시험 계획·위험) + 검증용 remote#7fsfy22z 에 pin 1회. 검증용이 Codex 확정으로 연결. 삭제할 것이 생기면 묻지 말고 `.claude/deletion_candidates.md` 에 추가.
