# 검증용 독립 검사 — APK 0.2.32 최종 후보 (remote#7fsfy22z, 2026-10-27)

## 고정 대상
- `fix/apk-prelogin-select` **b554e76**(제품 SOURCE OK: picker 9afcec7 · UA-2 482a901 · SH d29a034; evidence A seam ed7f4ad). 작업용1 보고 `report_rel_apk_0232_r1.md` r6.
- 후보 APK `D:\remote\remote\.claude\rel-apk-0232\GNLink-0.2.32.apk` **1a3eee63a992371f7ad455deedfabbc0e0b627c1c7432fa9f9251b8183d815ab**(0.2.32/vc31, 4 ABI, signer dcc806ae…, 작업용 단위 277/0). shseam `b105a845…`, 시험 Host `881b3e05…`, 출하 Host 대조 `1c0bc4f0…`, e2e `ed69ea1f…`, picker 단위 `453be9cf…` — **6개 실행 전후 해시 일치**(`r6/summary.txt`).
- 실행 13:20:58~13:49:29, 콘솔 세션, 슬롯은 실행기 자기 생성분만, AVD(작업용 재부팅 뒤) 재사용.

## 직접 실행 결과 (로그 `caplife\sh_r2\v6_*.log` PASS/FAIL 줄 계수)
| 시나리오 | 결과 |
|---|---|
| A 실 후보 APK UI × 로그인 전 역할 시험 Host | **15/15** — 목록→PC→바탕화면 local 선택→표시, 프레임 hold 중 취소·늦은 프레임 무영향, 같은 연결 재선택, SH 끄기/약/강/화면 크기, window list/select/thumb 0·FORBIDDEN 0 |
| tripwire 대조 | 3/3 |
| 부정 대조(일반 Host) | **기대대로 FAIL 3/5** — 그림 없음·local 선택 없음·window-list/select 13줄 도달 |
| UA-2 바탕화면 | 9/9 |
| 설정 UI A↔B↔끄기·재시작 | 16/16 |
| UA-2 창 대상 | 13/13 |
| B layout(letterbox·모서리·회전·pan·wm size·입력 fence·fallback) | 47/47 |
| strength / compare / touch / latency | 35 · 26 · 36 · 6 |
| picker 실 로그인 전 Host --android-select | 37/37 |
| picker 단위 | 60/60 |
| gate --apk 후보 / --self-test | 7 · 54 ALL PASS |
- 스크린샷 직접 열람: `v6_A/prelogin_1_shown.png`(실 APK 뷰어에 원격 패턴 표시), `v6_ua2d/ua2d_2_gdi.png`(설정: "요청: GDI 접수 · 아직 이 방식으로 캡처하지 않음 · 지금 바탕화면 캡처: DXGI · 이 연결에만 적용", 선명도 0.20/0.60, 선명화 방식 기본/화면 크기(실험)).
- 이전 후보 1d4a05f4 독립 결과(`VERIFY_0232_partial.md`)는 같은 값 — 승계가 아니라 이번 해시로 재실행한 결과다.

## Addendum E1 — 통제 스크롤 연속 최종 출력 정성 관찰 (Codex `codex_apk0232_evidence_final.md` 요구, 2026-10-27)
- 같은 후보 **1a3eee63…815ab**(복사본 해시 확인), 하네스 fix/apk-prelogin-select 5f53879(제품 0, seam 없음). 검증용 실행 `sh_run.ps1 -Scenario scroll_shimmer -Tag vv_shimmer -Seam false -NoBuild` → ALL PASS 6/6(녹화 성립 판정만): 한 연결에서 off / A 0.20 / A 0.60 / B 화면 크기 0.60 순, 화면 밖 패턴(얇은 한글·영문 글씨·1px 세로/가로 선·체커)을 33 ms 마다 4 px 스크롤, 원본 프레임 번호 구간 off [1,188]·A약 [189,378]·A강 [379,571]·B [572,760]. 자료 `caplife\sh_r2\vv_shimmer\`(mp4 4개·연속 정지 2장+정지 후 1장·pattern.log·shimmer_summary.json).
- 관찰 방법: mp4 를 ffmpeg(imageio-ffmpeg, `.claude\.venv_ffmpeg` 격리 venv)로 디코드 — 처음 6장은 같은 원본 번호가 반복(녹화가 변화 없는 출력을 중복 기록)이라 `mpdecimate` 로 서로 다른 연속 출력 4장을 뽑음(A강 원본 418·?·420·440, B 615·?·623·642 — 출력 갱신이 원본 33 ms 보다 성김). 시트 `verify_0232\shimmer\sheet_*.png`(번호·글씨·1px 선·체커 크롭), 2배 nearest 확대 `lines_2x_off_Astrong_B.png` 를 직접 열람.
- **관찰한 그대로**: 네 방식 모두 스크롤 중 연속 출력에서 글씨 획이 프레임 사이에 끊기거나 깜빡이는 모습, 글자 주변 흰/검 테두리(halo·ringing), 1px 세로·가로 선의 굵기 요동·모아레 띠, 체커 경계의 번쩍임, 깨진/비정상 출력은 **보이지 않았다**. A 0.60·B 는 off 보다 획이 더 진하고 또렷하며, B 는 같은 강도에서 A 보다 획 대비가 높다(위치 효과 관찰과 일치). 
- **한계(판정 범위 밖)**: AVD SwiftShader + screenrecord H.264 재압축을 거친 자료라 미세 반짝임이 압축으로 가려질 수 있다. 출력 갱신이 원본보다 성겨 서브픽셀 위상 변화의 모든 단계를 보지 못했다. 열람은 축소·2배 확대 화면이며 수치 판정은 하지 않았다. 실폰 GPU·디스플레이에서의 체감 shimmer 는 실기 항목으로 남는다.

## 판정하지 않은 것 / 실기 대기
- 스크롤 shimmer: 위 Addendum E1 에서 AVD 정성 관찰 수행(실폰 체감은 실기).
- 진짜 worker 의 디렉터리 인증·SYSTEM/Winlogon·DXGI 로그인 화면(A 는 시험 Host 역할 광고 + WGC 패턴; 실 worker+공유 세션 층은 --android-select 37).
- 실폰 화질·지연·발열(B 실험 옵션), 실 NAT, AVD SwiftShader 비용 ≠ 실폰.
