# SH 휴대폰 화면 선명화 필터 — 설계 제안 r1 (검증용 remote#wwml7h91, 2026-10-22)

사용자 지시(10-22): **"이번에 apk 에 필터도 같이 넣어."** → APK 0.2.30 유지 지시를 이 범위에서 해제, 다음 APK 에 SH 포함. 작업목록 SH 행(10-18 질문: 넓은 단색은 같고 가는 글씨만 흐림, OSLink 는 선명화 흔적; 데이터량 그대로).
기준 소스 main 5063cd9(APK versionName 0.2.30, versionCode 29).

## 현재 경로 (소스)
- 네이티브 디코더가 `AMediaCodec_configure(codec_, format, window_)` 로 **TextureView 의 Surface 에 직접** 출력(`app/src/main/cpp/android_video_decoder.cpp:157,636`).
- 화면 배치·확대·이동은 `TextureView.setTransform(matrix)`(`MainActivity.kt:5192-5203`), 그 행렬과 터치→원격 좌표 변환은 `ViewerViewport` 한 곳이 소유(같은 행렬로 표시·입력이 어긋나지 않게).
- 버퍼 크기는 `resolveSurfaceBufferWidth/Height`, 해상도 변경·SWITCHING 중 rebind 규칙은 `syncVideoSurface`(:5106~).
- 확대는 합성기의 bilinear — 1920×1080 을 약 1.33배 키우면서 가는 글씨가 흐려진다(handoff 설명과 일치).

## 제안
1. **GL 한 단계 삽입, 기하는 그대로**: 디코더 출력 대상을 앱 소유 `SurfaceTexture`(GL_TEXTURE_EXTERNAL_OES) 로 바꾸고, 전용 GL 스레드가 `onFrameAvailable` → `updateTexImage` → **선명화 셰이더 1패스** → TextureView 의 SurfaceTexture 에 만든 EGL window surface 로 `eglSwapBuffers`.
   - **출력 버퍼 크기 = 지금과 같은 비디오 해상도**, TextureView transform·ViewerViewport·터치 변환은 **변경 없음**. 확대는 지금처럼 합성기가 한다(선명화는 확대 전 원본 해상도에서). 기하·좌표 회귀 위험을 0 에 가깝게 두는 선택이다. 출력 크기 패스(확대+선명화 동시)는 효과가 더 좋지만 ViewerViewport 와 결합되므로 후속 후보로 둔다.
   - SurfaceTexture 의 transform matrix(`getTransformMatrix`)를 셰이더에 적용(디코더 crop·회전 반영 — 1088→1080 crop 포함 확인).
2. **셰이더**: CAS(contrast-adaptive sharpening) 계열 — 3×3 이웃의 min/max 로 국소 대비에 따라 가중치를 줄여 **평탄한 단색·이미 선명한 경계는 덜**, 가는 글씨·저대비 경계는 더. 출력 clamp, 감마 공간 그대로(추가 변환 없음). 공개 알고리즘 개념을 우리 코드로 작성(외부 코드 복사 시 라이선스 확인).
3. **강도 설정**: 화질 설정에 `선명도: 끄기 / 약하게 / 강하게`(기본값은 사용자 결정 — 제안 '약하게'). 저장은 기존 `ViewerQualitySettings` 와 같은 방식. 세션 중 바꾸면 다음 프레임부터 uniform 만 변경.
4. **폴백**: EGL/GL 초기화·셰이더 컴파일 실패, 지원 안 되는 OES, GL 스레드 오류 → **현재 경로(디코더→TextureView 직접)로 자동 복귀** + 진단 로그 1줄. 끄기 선택 시에도 현재 경로를 그대로 쓴다(끄기 = 지금과 바이트 동일한 표시 경로).
5. **수명**: TextureView 재생성(회전·SWITCHING·백그라운드)·해상도 변경·세션 park/keepalive 에서 GL 스레드·EGL surface·SurfaceTexture 를 정리/재연결. 디코더 Surface 가 앱 소유가 되면 TextureView 재생성 때 디코더 재구성이 필요 없어질 수 있으나, **이번에는 기존 rebind 의미(forceRebind·deferred resize)를 바꾸지 않는 쪽**을 우선한다 — 바꾸면 그 회귀 시험을 포함.
6. **지연·자원 계측**: 디코더 출력→swap 시각을 같은 진단 로그 체계로 1 s 창 p50/p95, 프레임 drop(늦은 프레임은 최신만 그림, 큐 누적 없음), GPU 시간은 가능하면 timer query. 판정 기준(제안): 끄기 대비 표시 지연 p95 증가 ≤ 4 ms, 60 fps 유지, 10분 재생 시 발열·배터리 체감 차 없음.

## 시험 (제안)
- 논리: 셰이더 수식 단위 시험(평탄 입력 불변, 경계 overshoot clamp, 강도 0 = 항등).
- 제품 UI: AVD(작업용2 기존 AVD 환경) 에서 실제 APK 로 가는 글씨 시험 패턴 수신 → 끄기/약/강 스크린샷 비교(같은 프레임), 확대·이동·회전·크게보기 뒤 터치 좌표가 원격 좌표와 일치(기존 ViewerViewport 시험 + 실제 탭), 해상도 변경·SWITCHING·park 복귀, GL 실패 주입 → 폴백 표시 지속.
- 실기: 사용자 폰(SM-S948N) 1회 — 글씨 선명도 체감, 지연·발열.
- 출하 gate: 시험용 주입 seam 이 출하 APK 에 없음(GA 항목: 실제 게시 산출물 기준).

## Codex 판정 요청 항목
- S1 기하 불변(출력 = 비디오 해상도, transform/터치 무변경) 방식 채택 여부 vs 출력 크기 패스.
- S2 기본 강도와 끄기 = 현재 경로 동일성 요구.
- S3 디코더 Surface 소유 변경에 따른 rebind 의미 유지 범위.
- S4 지연·자원 판정 기준값.
- S5 APK 버전·동반 범위: 다음 APK 번호(0.2.31 은 keepalive-multi 후보 브랜치가 쓰던 번호), keepalive-multi(구현·Codex OK, APK 유지로 보류) 동반 여부와 A6 영향(새 호스트 잠금 화면에서 휴대폰 비밀번호 입력 불가)에 대한 APK 측 대응 여부는 **사용자 결정 사항**으로 분리 — 검증용이 사용자에게 묻는다.
