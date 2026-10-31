# OSLink Android 추가 분석 — 원격 화면 렌더러의 선명화 필터 (2026-10-22, Android 2차)

`C:\work\oslink-analysis\android\README.md` 와 `..\tools.md` 를 먼저 끝까지 읽고, 같은 APK(`downloads\OSLink.apk`, SHA256 `87c12f6f…657f`)·같은 REA/JADX 환경(힙 4 GiB)을 재사용해라. 기존 제약 유지: 대상 설치·실행·후킹 금지, 발췌는 질의당 최대 10줄, 확인/추정/미확인 구분, GNLink 저장소에 쓰지 않음.

## 단서 (검증용이 DEX 를 직접 훑은 결과 — **바이트 단위 휴리스틱이라 JADX 로 확정 필요**)
- `classes2.dex` 에 GPUImage 계열 3×3 선명화 fragment shader 문자열(`centerMultiplier`/`edgeMultiplier`, left/right/top/bottom 이웃)이 있고, 그 문자열을 로드하는 클래스는 `Lbe/b;`(super `Lbe/a;`) 의 메서드 `h`.
- `Lae/f;` 는 필드 `a:Lbe/b;`, `b:Z`(켜기 여부로 보임)만 가진 작은 클래스.
- 앱의 원격 화면 렌더러 `com.link.cloud.core.control.render.engine.Renderer`(classes7) 가 필드 `a,b:Lae/c;`, `c:Lae/i;`, **`d:Lae/f;`** 를 갖고, `<init>`·`l`·`p`·`v` 에서 `ae/f` 를 참조한다. Renderer 문자열에 FBO(`Fbo createYuvTexture`, `Fbo drawYuv`), `onSurfaceChanged viewWidth/viewHeight` 가 있다.
- 즉 **YUV → FBO → (ae/c·ae/i·ae/f 필터 체인) → 화면** 구조와, 그중 하나가 선명화일 가능성이 높다(추정).

## 질문 (우선순위 순)
1. `Renderer` 가 원격 영상 프레임마다 `ae/f`(→`be/b` 선명화)를 실제로 그리는가. 켜는 조건(`ae/f.b` 를 바꾸는 호출자, 설정·기기·해상도·화질 모드·확대 상태 조건), 기본값.
2. 선명화 세기: `centerMultiplier`/`edgeMultiplier` 를 정하는 uniform 값과 그 출처(고정 상수, 설정값, 해상도 비율 계산 — GPUImageSharpenFilter 라면 sharpness → center = 1+4s, edge = s 형태인지), 이웃 오프셋(텍셀 크기 = 1/입력 폭·높이인지 1/출력인지).
3. **어느 해상도에서 선명화하는가**: 원본 비디오 크기 FBO 에서 하고 나중에 확대하는가, 화면(view) 크기로 먼저 확대한 뒤 하는가. glViewport·FBO 크기·텍스처 필터(GL_LINEAR/NEAREST) 설정.
4. `ae/c`·`ae/i` 의 역할(색 변환·확대·다른 필터인지)과 체인 순서.
5. 손가락 확대(zoom) 시 같은 체인을 다시 그리는지(행렬만 바꾸는지), 터치 좌표 변환과의 관계.
6. (여유 있으면) 화질 설정 화면의 선택지와 이 필터 켜기의 연결.

## 산출물
- `C:\work\oslink-analysis\android\q8_sharpen_renderer.md`: 질문별 결론 표(판정·클래스/메서드·Evidence ID), 렌더 체인 그림(입력 크기→FBO→필터→화면, 각 단계 해상도), 미확인 목록, **GNLink 시사점**(분석자 제안으로 표시 — GNLink 휴대폰 선명화 필터 설계 SH 에 쓸 것: 기본 켜짐 여부·세기·적용 해상도·확대 순서).
- `android\README.md` 에 요약 절 추가, `tools.md`·`evidence\` 갱신.
- 위 단서가 틀렸으면 틀렸다고 명시한다(휴리스틱 오탐 가능).
