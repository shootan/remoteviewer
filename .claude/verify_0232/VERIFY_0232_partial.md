# 검증용 독립 배치 — APK 0.2.32 후보 1d4a05f4 (ed7f4ad, SH r5 기준) — 부분 근거 (remote#7fsfy22z, 2026-10-27)

이 후보는 SH r6(d29a034) 이전이라 **최종 출하 후보가 아니다**. Codex(`codex_sh_strength_r6.md`) 지시대로 결과는 보존하되, SH 출력 token/first-output/latch 관련 행동과 A 실 APK UI·UA-2 는 r6 새 APK 로 다시 확인한다(자동 승계 금지).

- 실행: `run_verify.ps1` 11:09:38 → 11:41:32, 시험 exe·APK 해시 6개 실행 전후 일치, 후보 사본 해시 1D4A05F4… 확인, 슬롯은 실행기만(자기 생성분 종료 삭제).
- 결과(로그 `caplife\sh_r2\vv_*.log`, PASS/FAIL 줄 직접 계수):
| 시나리오 | 결과 |
|---|---|
| A 실 APK UI × 로그인 전 역할 시험 Host (prelogin_ui) | 15/15 |
| tripwire 대조 | 3/3 |
| **부정 대조(일반 Host)** | 기대대로 FAIL 3/5 — 로컬 선택 없음·그림 없음·window-list/select 12줄 도달 |
| UA-2 바탕화면 | 9/9 |
| 설정 UI A↔B↔끄기 | 16/16 |
| UA-2 창 대상 / 구 Host | 13/13 · 7/7 |
| B layout(letterbox·회전·pan·wm size·fence·fallback) | 47/47 |
| strength / compare / touch / latency | 35 · 26 · 36 · 6 |
| picker 실 로그인 전 Host --android-select | 37/37 |
| picker 단위 | 60/60 |
| gate --apk 후보 / --self-test | 7 · 54 ALL PASS |
