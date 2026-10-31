# APK 0.2.30 최종 근거 확인

2026-10-16 · 판정: **배포 완료(마일스톤) · 실폰 테스트 대기**.

직접 실행 결과는 result.json(all_checks_pass=true, verify.py exit 0), APK badging/certificate 및 debug-hook-audit.json에 보존했다.

- source 5b0fb6f → main b61e0f5: 트리 diff 0. release b4fd204: source 대비 gradle 버전만 변경. docs 0c5d394 이후 확인 중 origin/main이 b03594f로 전진했다. b03594f는 history만 14줄 추가한 merge이며 제품 변경 없음을 직접 대조했다. 최종 ls-remote b03594f1f2144b78e9c30afecbad2e210d9966d7.
- 익명 HTTPS 실제 다운로드 200, 13,492,132 bytes. APK SHA256 e6a6eecbf129f92998d5c4d4e2844057485315489c001023b24f5966a93608cf. signed manifest·release payload·dist 모두 동일.
- aapt2 versionName 0.2.30 / versionCode 29, 4 ABI. apksigner 검증 성공, 인증서 dcc806aeb30b2e3c53e4a0b96b9675f9e25af2bf071dbb37a3cb09f6b8ec2990(기존 동일).
- NAS android.manifest b64c1b49045ed0f939b5f733fdb51e04a6964458192f0f2a77cf40f34d0d593e, sig 210ab909bba41e8690531ada7057400c3ebf19c0e9a1d0873d22b8b6df0b20c3: 로컬 동일. 제품 신뢰 공개키(override 없음) 검증 및 validator 내 변조 거부 확인.
- Windows manifest 78df5305b70cc429e6779d484a4a534bf13875d81e05e2fc6e5f8da263eb9306, sig 6dce1ca98d483da0e2a7dafa70077d3f25d340724baef415fb2b301c99a0f77f: PC 0.2.158 게시 쌍 그대로.

## Debug 게시본 정정

게시 APK는 assembleDebug 파이프라인이다. 실제 다운로드한 4 ABI에서 close_explicit_cancelled 등 hook 이름 리터럴 존재, testHook 리터럴 0, abr-trace/input-trace/10.0.2.2 0을 직접 확인했다. ConnectOpGate::Hook의 실제 기계어를 각 ABI에서 확인했다(ARM32는 Thumb 모드로 재해석): 인수/스택 처리 후 즉시 반환하며 callback 호출이 없는 빈 함수다. 소스의 매크로 비활성 경로와 일치한다. 이 이름의 존재를 실행 가능한 시험 callback과 혼동하지 않으며, 이전 release-unsigned의 ‘hook 문자열 0’을 게시 APK에도 적용하지 않는다. 모든 시험 기능 부재를 이 단일 함수만으로 일반화하지 않는다.

## 검증 등급과 잔여

직접 확인은 위 공개 산출물·서명·인증서·버전·4 ABI·소스 계보·함수 기계어다. 통합 AVD 9/9·6M UI 20/20·업그레이드3/3·sharpness 입력 반례·Android211/0은 기존 검증용 독립 실행 보고를 인용한다. main의 단위 XML은 현재 없어 여기서 직접 재집계하거나 재실행하지 않았다. 최초 검증 스크립트는 이전 버전의 문서 diff 기대값/단위 XML 기대값을 사용해 실패했고 이를 이번 artifact 확인 범위로 정정했다. 결과를 제품 실패나 테스트 재실행 성공으로 기록하지 않는다.

실폰 crop/1088 표시·체감·확대 사용감, 1080p 6M 회복, DesktopText·실 태블릿 등은 미확인. park 중 설정은 UI 도달 불가로 종결한 범위다. 이 배포로 모든 흐림 해결을 주장하지 않는다.

사용자: 폰을 0.2.30으로 업데이트하고 같은 PC에 1~2분 연결하여 목록 재진입, 크게 보기, 최고화질을 각 1회 확인. 검증용이 NAS coded/cropSrc/output/park_reuse 로그와 연결한다. 아직 task의 사용자 행동 성공은 미확인이다. Codex는 설치·앱 재시작·게시·push를 수행하지 않았다.
