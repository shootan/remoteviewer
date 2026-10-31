

## 4) E1 — 통제 스크롤의 최종 출력 (Codex codex_apk0232_evidence_final.md NEEDS_EVIDENCE) — 2026-10-27 추가
하네스만 바꿨다: `fix/apk-prelogin-select` **5f53879**(`automation/sh_filter_avd.py`, `automation/sh_pattern_window.ps1`). 제품 0이며, 후보 APK 1a3eee63 를 **그대로** 쓴다(seam 불필요).
```
Copy-Item D:\remote\remote\.claude\rel-apk-0232\GNLink-0.2.32.apk D:\remote\remote\.claude\caplife\sh_r2\app-noseam.apk -Force
& D:\remote\remote\.claude\caplife\sh_run.ps1 -Scenario scroll_shimmer -Tag v_shimmer -Seam false -NoBuild
```
- 약 3분.
- 구성:
  - 화면 밖 layered 패턴(1440x810, 주 모니터 무조작). 플래그 파일이 있는 동안 내용 전체(얇은 글씨·1px 선·체커·색 블록)가 **33 ms 마다 4 px** 위로 스크롤한다(순환).
  - 매 원본 그림 오른쪽 아래에 **프레임 번호**(6자리 + 16칸 줄무늬, 흰=1/회색=0, 왼쪽이 최하위 비트)를 넣는다.
  - `pattern.log` 에 `scroll frame=<n> offset=<px> ms=<t>` 를 기록한다(원본 약 28 fps).
- 한 연결에서 구간 4개: `off`(끄기) / `A_weak_s20`(약하게 0.20) / `A_strong_s60`(강하게 0.60, 기본 A) / `B_display_s60`(설정 "화면 크기 (실험)" + 0.60).
- 구간마다 남는 것:
  - `shimmer_<구간>.mp4`: 폰 화면 `adb screenrecord` 4 s, 스크롤 중. 녹화 간격은 screenrecord 가변 프레임이며, 누락·중복은 영상 안의 프레임 번호로 대조한다.
  - `shimmer_<구간>_still1.png`·`_still2.png`: 녹화 직후 아직 스크롤 중일 때 0.3 s 간격으로 찍은 연속 정지 2장.
  - `shimmer_<구간>_after.png`: 정지 후.
- `shimmer_summary.json`: 구간별 원본 프레임 범위, 녹화 초, 앱의 그리기 경로 로그, 두 정지의 평균 차.
  - 강하게는 약하게에서 같은 renderer 의 강도만 바뀌므로 경로 로그가 없을 수 있다.
- 판정은 "스크롤 중에 녹화됨"(mp4, 원본 프레임 범위, 두 정지가 다름)뿐이다. **반짝임·halo·세부 소실은 사람이 연다**(점수 없음).
- 작업용 실행(후보 1a3eee63, Host 881b3e05, 콘솔 세션): 6/6.
  - 원본 프레임 1–184 / 185–374 / 375–545 / 546–742.
  - 결과 폴더 `caplife\sh_r2\shimmer_2\`.
