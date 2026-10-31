# task sh-strength r2 — 선명도 강 0.60 + 표시 크기 1패스 A/B (검증용 remote#wwml7h91, 2026-10-22) — 담당 작업용2 (keepalive-r2 APK 준비 보고 뒤 착수)

**이 파일이 정본이다. 끝까지 읽고 CLAUDE.md 대로.** 구속 계약: `.claude/codex_field_0162_0231.md` §4 + **§4B**(+ 기존 `codex_sh_filter_r1.md`·`codex_sh_filter_impl_r1~r3.md` 중 유지 항목). 실기·스크린샷 근거: `.claude/field_0162_0231_2026-10-22.md` §3·§4(확대 `D:\remote\remote\temp\compare_crop_2026-10-22.png`).

## 사용자 실기 (APK 0.2.31)
"약하게랑 강하게랑 차이가 없어" / "0.2 를 OSLink 랑 비교했을 때 OSLink 가 훨씬 선이 두껍고 잘 보여". 로그상 s=0.20/0.35 선택·적용은 정상. OSLink 는 s≈0.35 고정, 확대와 선명화를 같은 draw 에서 수행.

## 범위
- **A (제품 후보)**: 끄기 / 약 0.20 / **강 0.60**(기존 0.35 → 0.60). 기본 약 0.20·명시 끄기 저장값 보존, 저장된 strong 은 새 강도로(정책 기록). UI 에 실제 수치 표시. 1패스 5탭·source 1px·crop/회전·fallback 유지.
- **B (A/B 실험 후보 — 제품 기본 채택·출하 승인 아님)**: 실제 view 픽셀 크기로 출력 + 같은 ViewerViewport placement 를 GL 에서 1회(확대+선명화 동시), TextureView 추가 transform 은 identity. source/output/view 세대 분리, 원격 터치 기준은 source 유지, off 는 원래 경로 복원. taps 는 source 1px·crop·회전을 1회 적용, **ring miss → output-size fallback 금지(B 에서)**. pan 정지 redraw·resize·old callback·단일 producer·BLOCKED·keepalive binding 유지.
- **필수 A/B 대조**: 같은 강도 **A 0.35 ↔ B 0.35** 를 반드시 포함해 "위치 효과" 와 "강도 효과" 분리. 그다음 A 0.60, (B 0.60).
- **먼저 확인**: 현 0.35 가 실제로 다른 uniform·최종 픽셀을 만들었는지 same-frame off/0.20/0.35 결정적 대조(메뉴 로그만으로 적용 버그 배제 금지).
- 판정 기준(Codex): 실제 최종 same-frame 캡처·geometry + Host 좌표, double transform/잘못된 tap 부정 대조, p95·미스·발열 비용. 검정/흰 배경 글씨·저대비·한글·컬러 글씨·1px 선/체커·사진 경계에서 halo/ringing/클리핑·스크롤 shimmer. 중간 clamp·양자화·색 차이도 비교("후단 bilinear 라 무조건 열화" 단정 금지). 1패스라도 픽셀 수 증가 비용 있음, S4 AVD 미달 기록 유지. A 0.60 결과는 B 실험 실패와 분리해 판정 가능.
- 폰 자동 설치·조작 금지. 비교 캡처·선택 방법을 준비해 실기 안내에 묶는다.

## 규칙
새 worktree·브랜치(main 82b3d14 기준, 예 `C:/work/gnlink-wt/feat-sh-r2`, `feat/sh-r2`). keepalive-r2 APK 브랜치와 같은 파일(MainActivity·renderer·bridge)을 동시에 쓰지 않도록 — 겹치면 검증용에 보고 후 순서 조정. repo root 밖 삭제 금지, 버전·서명·게시·push 금지, 측정 슬롯 CreateNew, 커밋 Git MCP.

## 보고 (1회)
`.claude/report_sh_strength_r2.md` + `remote#wwml7h91` pin: A/B 별 same-frame 캡처·수치, 위치 효과 vs 강도 효과, 부정 대조, 비용, 권고(A 만 / A+B / 보류).
