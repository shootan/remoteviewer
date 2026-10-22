# SH 선명화 필터 — 휴대폰(SM-S948N) 실기 묶음 절차 (sh-filter r1, r2 갱신)

**상태: 준비만 했다.** 설치는 검증용을 거쳐 사용자 승인을 받은 뒤에 한다. 버전 번호는 올리지 않았다
(versionName 0.2.30 / versionCode 29 그대로). S5(버전·keepalive-multi 동반·A6 대응)는 사용자가 아직
정하지 않았다. 이 절차의 수치가 나오기 전에는 "4 ms 이하·발열 차이 없음" 을 통과로 주장하지 않는다
(Codex S4).

## 무엇을 설치하는가
- 시험 후보 APK: `feat/sh-filter` 의 assembleDebug 산출물(noseam). **seam 이 없는 debug 시험물**이지 서명된 출하·게시 승인품이 아니다. 해시는 보고서를 본다. 설치 요청 전에 appId·서명자, 기존 설치·저장 정보(로그인·설정)에 미치는 영향을 확정한다(S5 미결과 함께).
- 시험 seam APK(`-Pgnlink.shTestSeam=true`)는 휴대폰에 설치하지 않는다. AVD 실패 주입 전용이다. APK gate(`gnlink_check_fixed_server.py --apk`)가 seam APK 를 거부하고, Gradle 은 seam 을 켠 release 빌드를 거부한다.

## 같은 조건으로 고정할 것
- 같은 PC 호스트, 같은 원격 화면. `automation/sh_pattern_window.ps1` 패턴 창이나 평소 쓰는 가는 글씨 화면.
- 화질·fps(30·45·60 중 하나), 화면 밝기, 충전 여부, 실내 온도.
- 회차 사이에 앱을 다시 켜서 열 상태를 맞춘다.

## 순서 (약 40분)
1. 설정 탭에서 선명도를 **끄기 / 약하게 0.20 / 강하게 0.35** 로 바꿔 가며 같은 화면을 스크린샷으로 남긴다.
   - 가는 한글·영문, 저대비 글씨, 1 px 선, 단색 면, 검정·흰색 경계를 본다.
2. 확대·이동·크게보기·화면 회전 뒤에 원격의 작은 대상(체크박스·작은 글자)을 눌러서, 누른 곳과 원격이
   반응한 곳이 같은지 본다(기하 불변).
3. 수준마다 **10분 연속 재생**한다. 순서는 끄기 → 0.20 → 0.35 → 끄기다.
   - 앱 진단 로그를 그대로 보존한다(r2 정의, AVD 와 같다).
     - `sh_latency_segment`: 수준(실효)·경로·renderer 세대·surface 가 바뀔 때마다 닫히는 **구간 전체**의 분포. 이 줄의 p95 가 판정값이다.
     - 표본은 decoder handoff → TextureView latch 의 **새 frame** 만이다(같은 timestamp 의 redraw 는 제외, `redraws` 로 따로 센다).
     - 구간 시작 1 s(`warmup`)와 이전 producer 의 frame(`stale`), ring 밖(`unmatched`)은 표본이 아니다. 36 000 개를 넘으면 `overflow` 로 세고 분위에 넣지 않는다.
     - 분위는 nearest-rank: 정렬한 표본의 1-기준 순위 ceil(q·n) 값.
     - `sh_latency`(5 s): 진행 줄이다. 같은 구간 번호(`seg=`)와 그 시점까지의 구간 p95 를 함께 적는다. 5 s 창 p95 의 중앙값을 판정값으로 쓰지 않는다.
     - `drawWallUs` 는 updateTexImage→swap 의 벽시계 시간이지 GPU 시간이 아니다. `src= out=` 는 frame 크기와 출력 크기이고 같아야 한다.
   - 같은 시각의 배터리 온도와 thermal status 를 `adb shell dumpsys battery` / `dumpsys thermalservice` 로 1 분마다 남긴다. 사용자가 허용하면 USB 디버깅을 쓰고, 아니면 앱 로그만 쓴다.
4. 판정(Codex S4):
   - 같은 fps 에서 handoff→latch p95 의 증가가 끄기 대비 4 ms 이하일 것.
   - 표시 fps 가 2 % 넘게 떨어지거나 drop 비율이 1 %p 넘게 늘면 조사한다.
   - 온도·thermal status 를 끄기와 비교한다.
   - 샘플 수와 unmatched 를 함께 적는다.
5. 실패하면 그 수치를 그대로 보고한다. 기준은 바꾸지 않는다.
   - 그 기기에서만 direct 로 되돌리는 것은 허용된다. 다만 "기본 0.20 이 그 기기에서 승인됐다" 고 쓰지 않는다.

## 남길 것
- 스크린샷 3장(같은 원격 화면), 각 수준의 `sh_latency_segment`·`sh_latency` 원문, 온도 기록.
- 화면 회전·크게보기 뒤 탭 위치 결과.
- 앱 로그의 `sharpen_path` / `sharpen_fallback`. fallback 이 났으면 기기에서 GL 이 안 되는 것이고, 사용자 화면은 원본으로 나와야 한다.
