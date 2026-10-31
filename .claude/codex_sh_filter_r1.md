# SH 선명화 필터 r1 — Codex 설계 판정

> **현재 상태: 사용자 명시 재개(2026-10-22).** ‘sh 결과 반영해서 작업시켜. 일단 0.2 0.35 선택되게 둘 다 봐 보자’로 앞선 보류가 해제됐다. `.claude/task_sh_filter_impl_r1.md` / 작업용2 / **t-b7ss11ov**에 구현 연결됨(직접 조회 시 submitted). 최신 델타는 아래 계약2의 **자체 작성5탭 십자 커널, 끄기/0.20/0.35·기본0.20**이다. S1~S4 나머지 계약은 유지한다. S5 버전·동반기능·A6 APK 대응은 미결이며 versionName/versionCode 인상·서명·게시·폰 설치를 허용하지 않는다.

2026-10-22 · task=sh-filter / rev=r1 / kind=design-review-result
reply_to 검증용 `remote#wwml7h91`

**아래 보완 계약으로 S1~S4 구현 진행 OK.** 원본 크기에서 처리하는 일회 GL 패스, 기본 약하게, 끄기는 기존 direct 경로를 채택한다. Surface 전환을 ‘uniform만 변경’으로 설명하는 부분과 codec→swap만으로 표시 지연을 판정하는 부분은 수정한다. S5(버전/동반 기능/A6 APK 대응)는 사용자 결정 미결로 남긴다. 필터 포함 요청이 그 기능들의 동반 승인이나 현재 APK 설치 허가는 아니다.

## 소스와 검토 범위

`proposal_sh_filter_r1.md` 전체와 main5063cd9 계열의 Android 출력/Viewport 코드를 읽었다(main은 문서 commit c04eb47까지, 제품 기준 동일). `AndroidVideoDecoderSink::SetSurface`는 **codec reset→기존 window release→새 window 구성→key gate Rearm/transition grace**를 수행한다. 동적 setOutputSurface로 가볍게 교체하는 구현이 아니다. `ReleaseHeldOutputLocked`의 ready-selection은 codec 출력 Surface로 넘기는 단계이며 TextureView latch는 별도로 `onSurfaceTextureUpdated`가 계수한다. 앱 소유 OES를 삽입하면 두 단계 사이에 새 pipeline이 생긴다. `MainActivity.syncVideoSurface`의 SWITCHING resize defer, buffer-size 결정과 ViewerViewport 단일 배치/좌표 규칙도 확인했다.

실기/AVD/빌드/필터 효과는 이번 설계 검토에서 실행하지 않았다. 선명도 향상·4ms 충족은 아직 가설/검증 목표다. 새로운 플랫폼 근거는 [SurfaceTexture API](https://developer.android.com/reference/android/graphics/SurfaceTexture), [TextureView 수명 API](https://developer.android.com/reference/android/view/TextureView.SurfaceTextureListener), [AOSP TextureView 설명](https://source.android.com/docs/core/graphics/arch-tv)을 대조했다.

| 항목 | 판정 | 확정 |
|---|---|---|
| S1 기하 | **OK** | visible video 크기의 출력 buffer 유지, 기존 TextureView transform/ViewerViewport/터치 좌표 유지. 출력 화면 크기로 확대+선명화하는 안은 이번에 하지 않는다. |
| S2 설정/끄기 | **OK, 전환 계약 보완** | 끄기/약하게/강하게, 미설정 기본 **약하게**, 명시적 끄기 보존. 끄기는 기존 codec→TextureView direct 경로이고 GL 항등 패스가 아니다. |
| S3 rebind/lifecycle | **원안 보완 필요 → 아래 계약으로 확정** | 기존 codec reset·IDR gate·SWITCHING defer·park 의미 유지. GL 자원 소유/producer 전환·최종 표시 확인·fallback을 명시한다. |
| S4 판정 기준 | **원안 측정 보완 필요 → 아래 계약으로 확정** | steady-state 최종 표시 지연 p95 증가≤4ms를 목표/통과 기준으로 채택. swap 완료만으로 통과하지 않는다. 고정60fps/10분 체감 무차이 대신 동일 조건·선택fps·열 상태를 측정한다. |

## 구현 계약

1. **기하·샘플링:** 출력은 coded padding이 아니라 지금 resolveSurfaceBufferWidth/Height가 선택하는 visible 크기다. 1920×1088 coded/1920×1080 visible, nonzero crop, 회전/상하반전, resize의 소스 좌표→OES transform→출력 visible pixel을 정확히 한 번 적용한다. 매 `updateTexImage` 후 그 frame의 transform을 취득한다. 3×3 이웃은 **보이는 소스의 1pixel 간격**이며 crop/회전을 반영해 모든 tap이 유효 영역 안에 있게 한다. OES uv에 무조건1/codedWidth를 더하거나 padding을 이웃으로 읽지 않는다. 외곽 tap은 유효 crop에 clamp하고 border/텍스트/비정사각 패턴으로 중복 crop/flip·왜곡을 검사한다. GL이 TextureView 배치 transform을 다시 수행하지 않는다.
2. **셰이더/색(사용자 재개 델타 반영):** 자체 작성5탭 십자 선명화 `out=(1+4s)C-s(L+R+T+B)`를 RGB에 적용하고 alpha1, 출력0..1 clamp로 한다. **끄기 / 약0.20 / 강0.35**, 기본0.20이며 UI에 값을 표시한다. 이 고정 커널은 contrast-adaptive CAS가 아니므로 CAS/OSLink와 동등한 품질이라고 부르지 않는다. 참고 q8의 s≈0.35/소스1pixel 간격은 정적 관찰이고 OSLink는 최종 화면 viewport에 선명화·표시변환을 함께 하는 경로다. **GNLink는 S1의 원본크기 pass 후 기존 compositor 확대를 유지**하므로 같은 계수라도 최종 화면이 같다는 보장은 없다. q8의 모든기기/모드·GPU실행성공도 확인됐다고 확대하지 않는다. 수식 개념을 자체 구현하고 외부 소스 사용 시 실제 license·notice를 포함한다. 평탄색 보존, 검정/흰색/채도 경계의 ringing·clipping/색 번짐, 한글·가는 글씨와 영상/노이즈를 비교한다. 단순0..1 clamp만으로 halo가 해결됐다고 보지 않는다. 현재 SDR 표시의 color range/transfer/dataspace를 유지하고 임의 감마 변환이나 이중 sRGB 변환을 넣지 않는다. 지원하지 않는 색/format은 안전하게 direct로 간다. HDR/광색역 확장은 이번 범위가 아니다.
3. **설정과 경로 전환:** sharpness enum/pref는 기존 bitrate/fps schema와 독립시켜 업그레이드 때 화질·fps를 바꾸지 않는다. 약↔강은 uniform 변경이며 **정적 화면에서도** 현재 동일 generation frame을 1회 redraw해 즉시 반영한다. 끄기↔약/강과 GL→direct fallback은 output producer가 바뀌므로 **기존 SetSurface/reset/IDR 재요청 절차를 거치는 직렬화된 전환**이다. 이때 ‘다음 frame부터 uniform만’이라고 쓰지 않는다. codec와 EGL이 같은 TextureView BufferQueue의 producer로 동시에 붙지 않게 이전 producer 분리를 확인한 뒤 새 경로를 연결한다. 직접 경로 복귀가 실제 codec→TextureView임을 실행 로그/실패 주입으로 확인한다. ‘바이트 동일’은 같은 제품 처리 경로 사용이라는 뜻이며 하드웨어/compositor 스크린샷의 비트 단위 동일성을 보장하지 않는다.
4. **GL 소유와 수명:** 전용 render thread/Handler가 EGL context·input SurfaceTexture/OES texture·output EGL surface를 단독 소유한다. UI thread에서 updateTexImage/swap/긴 join을 실행하지 않는다. callback은 generation이 맞는 render thread에 작업을 알리며 **최대 한 개 pending draw**로 합친다. latest-wins의 건너뛴 frame을 계수하되 busy loop/성장하는 frame queue를 만들지 않는다. decoder 입력 Surface와 TextureView 출력 Surface의 참조/해제 주체를 분리하고, owner session/selection/surface generation을 모든 callback·실패·resize 명령에 붙인다. 오래된 callback이 새 renderer/nativeSetSurface를 덮지 못하게 한다. `onSurfaceTextureDestroyed=true`로 Android가 surface를 회수할 때 GL이 계속 접근하지 않게 하고, false를 사용한다면 앱의 단일 release 책임을 명시한다. 이 변경에서 Activity/TextureView 수명을 유지하는 별도 최적화는 하지 않는다.
5. **기존 rebind/park 의미:** 비디오 크기 변경, SWITCHING deferred resize, view destroy/recreate, foreground/background, park/resume, 다른 host/target 선택에 기존 native reset·keyframe owed·delta-before-first-IDR 거부·transition grace 규칙을 유지한다. 앱 소유 OES가 오래 산다는 이유로 decoder reset을 생략하거나 parked session을 계속 렌더하지 않는다. 멀티 keepalive 포함 여부는 S5 결정 전 확정하지 않는다. 포함되면 선택된 active host의 현재 Surface만 연결하고 다른 parked host의 callback/캐시가 넘어오지 않는 대조를 추가한다.
6. **표시 단계/통계:** codec가 OES로 release한 것은 **디코더 출력**, onFrameAvailable/updateTexImage는 **GL 입력 취득**, eglSwapBuffers는 **출력 제출**, TextureView updated/latch는 **앱 표시 관측**이다. 이들을 구분한다. 필터 경로에서 codec ready만으로 SWITCHING을 풀거나 첫 화면/복귀 성공을 보고하지 않는다. 기존 decode/선택 판정은 보존하되 UI 공개에는 **현재 selection/surface generation의 GL 출력 및 최종 latch 확인**을 추가한다. frame PTS/id와 generation을 bounded metadata로 연결해 오래된 이미지가 새 선택으로 인정되지 않게 한다. 원래 mobile ABR의 decodedTotal/grace와 presentation 통계의 의미를 swap 횟수로 바꾸지 않는다. 필터 uniform 변경으로 같은 영상을 다시 그린 것은 새 decoded video frame으로 계수하지 않는다.
7. **자동 fallback:** OES/EGL/context/shader 생성 실패와 복구 가능한 GL/swap 오류를 UI control 쪽에 알리고 위 직렬화 전환으로 direct를 재구성한다. renderer 오류/상태는 `requested=weak/strong`, `effective=off`, fallback 사유를 구분하며 UI에 짧게 ‘선명도 적용 불가, 원본 표시’가 보이게 한다. 사용자 선호를 몰래 끄기로 덮어쓰지 않는다. 같은 Surface/세션 episode에서 무한 GL 재시도·producer 교대 루프는 금지한다. 실패한 GL resource를 재사용해 decoder도 정지시키지 않는다. UI/decoder lock을 잡고 GL teardown 완료를 무한 대기하지 않는다. OS/GPU driver 자체 hang까지 자동 fallback이 보장된다고 주장하지 않는다. GL thread 강제 종료로 current context를 다른 스레드에서 안전하게 탈취할 수 있다는 가정도 금지다. 복구 불가능한 경우의 남은 한계를 별도로 보고한다.
8. **데이터량 유지의 의미:** 필터는 decode 뒤 로컬 pixel 처리다. bitrate/fps/해상도 요청·encoder·AU/네트워크 payload는 필터 설정으로 변경하지 않는다. 같은 고정 encoded 입력을 off/weak/strong에 재생해 원격 요청값/수신 frame·byte 조건이 같은지 확인한다. 실제 무선 세션의 재전송·ABR 변화까지 포함해 ‘총 데이터량이 언제나 정확히 같다’고 약속하지 않는다. 색상/저대비 경계의 시각적 개선이지 손실된 원본 글자 정보 복원이나 초해상도 보장이 아니다.

## S4 측정·완료 기준

- **화질/기하:** 동일 원본 패턴과 crop을 재현해 off/weak/strong의 실제 APK 화면을 비교한다(선명화만 다른 같은 영상 frame). 얇은 한글/영문·저대비 글씨·1pixel 선·평탄색·고대비 경계·움직이는 영상이 포함돼야 한다. 확대/이동·전체/크게보기·회전·1088 crop·양끝 경계에서 원격 탭 위치가 맞는 실행 증거를 남긴다. 수식 단위 시험은 GPU 셰이더/제품 UI 효과의 대체 증거가 아니다.
- **지연:** 같은 기기·decoder·전송 입력·해상도·선택fps·주사율·밝기에서 off/weak/strong 교차 A/B. native codec handoff, frame-available/latch, GL submit, TextureView latch를 frame PTS/id+generation으로 매칭한다. **codec handoff→최종 TextureView latch**의 p50/p95와 off 대비 p95 증가≤4ms를 보고한다. swap p95/GPU timer는 세부 원인 지표이고 최종 표시 지연의 대체가 아니다. SurfaceTexture timestamp는 producer별 의미/원점이 다를 수 있으므로 임의로 wall/monotonic 시간에서 빼지 않는다. 기존 codec releaseAtTime/PTS와의 관계를 검증하고 같은 monotonic 이벤트시각을 수집한다. 최종 latch는 앱에서 관측한 단계이며 광학적 패널 발광 실측이라고 부르지 않는다.
- **프레임률:** source/선택fps가30/45이면 그 fps와 비교한다. 자동으로60을 강제하지 않는다. 지원 입력60fps도 별도 대조하고 warmed steady-state 표시fps가 off보다2% 넘게 악화되거나 drop 비율이1%p 넘게 증가하면 조사한다. 샘플 수·입력frame수·스킵/drop·동일frame redraw를 함께 기록한다. 1초창 p95 몇 개만 평균내지 말고 동일 길이 A/B 구간의 분포와 샘플 수를 보존한다. startup/끄기 전환/해상도 변경/park 복귀 소요는 steady-state4ms와 구분하며 새 첫 프레임까지 측정한다.
- **수명/자원:** surface 재생성·off↔필터 전환·강도 변경·회전·target 전환·park/resume를 각각 반복해 black/stale frame, native/GL 오류, orphan render thread/미회수 Surface·EGL 객체의 누적을 확인한다. context/shader/swap 실패를 주입해 **현재 direct 경로에서 새 frame이 다시 표시되는 것**까지 본다(설정 문자열off만으로 PASS 금지). 최소10분 연속 재생에서 입력frame/해상도/밝기 동일 조건으로 CPU/GPU 가용 지표·thermal status/배터리 온도·메모리·frame pacing을 비교한다. 배터리 잔량1% 차이나 ‘체감 없음’만으로 전력 동등을 입증하지 않는다. API28에서 없는 계측은 unavailable로 적는다.
- **검증 층:** AVD의 실제 APK UI·GL lifecycle·부정 대조는 자동화로 수행한다. 실제 SM-S948N의 crop/성능/색/열/선명도까지 AVD 결과로 대체하지 않는다. 자동화와 후보 준비 뒤 사용자 폰 확인을 한 묶음으로 제시하며 무단 설치하지 않는다. 기기 근거 없이4ms/발열 통과를 주장하지 않고 **시험 후보·폰 실기 대기**로 구분한다. 수치 미달을 숨기고 기준을 바꾸지 않는다. 특정 장치에서 안전한 direct fallback은 허용하지만 기본 필터를 그 기기에서 승인했다고 보고하지 않는다.
- **출하 분리:** shader 실패/Surface churn/인증 우회·fixture 강제값 등 시험 seam과 시험키가 실제 게시 APK에 없음을 산출물에서 검사한다. 정상 diagnostic 계수/효과 설정은 제품 기능이다. build·APK metadata·UI/source hash/서명된 최종 파일을 고정한다. local fixture screenshot의 source/frame이 무엇인지도 남긴다.

## 다음 담당과 범위

검증용이 이 계약을 반영한 `.claude/task_sh_filter_impl_r1.md`를 작업용2의 **t-b7ss11ov**로 이미 연결했다. 새 중복task를 만들지 않는다. DW는 구현보고 후 검증용 독립 검사 중이며 보수가 오면 우선 처리한다. S1~S4의 보완과 위5탭/계수 델타는 이 범위에서 구현 가능하며, 매 단계 추가 GO를 요구하지 않는다.

S5는 사용자 답을 검증용이 수집해 버전 충돌/keepalive-multi 동반/A6 APK sealed 대응을 통합 결정한다. SH만 허용됐다는 이유로 APK용 sealed credential·새 protocol·멀티 keepalive까지 채택하지 않는다. 반대로 S5 답을 기다린다는 이유로 SH 독립 구현/격리 검증 준비를 멈추지 않는다. APK0.2.30 유지의 예외는 **차기 SH 포함 APK 작업 범위**로 기록하고 다른 기존 운영 경계를 지킨다. 이 검토서만 작성했으므로 공개 문서 변경/commit/push는 없다.
