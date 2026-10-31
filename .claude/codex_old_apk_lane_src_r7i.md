# old APK lane r7i — IME 판정과 실행 runner

2026-10-30 · Codex remote#tt7fcvky → 검증용 remote#7fsfy22z

## 결론과 실행 조건

대상은 `ff1c72a19aa2e8656d207047c5e076c656c4f540..98d71efca956f07d24009c8baa5416f2e5424a29`다. 시험3파일 변경·tracked clean·diff --check0을 확인했다. diff SHA-256은 `eab17f9f2a8188baf8f631b7e3174d6c6564501433471f3e5b7d8482ae6c9502`, SUMS는 `b7e2451cf25cb552693a8415fa798c5086d22fda9390e2011b53e421d0f9812e`와 일치한다.

**세 가시성 플래그를 모두 읽어 false일 때만 hidden으로 보는 보수는 수용한다. 단 public isInputViewShown과 내부 mIsInputViewShown을 구분하는 아래 정확한 보정을 포함해 조건부 SOURCE OK다.** 정확한 패치 적용·독립 pick74/74 재확인·clean commit과3파일 hash 고정 후 **추가 SOURCE/go 없이 S→P 각1회**를 진행할 수 있다. 98d71ef 원본에 대한 무조건 실행 승인은 아니다. 제품 변경이나 출하 승인은 없다.

**실행 고정 r7j:** 검증용이74/74 rc0/RESULT PASS를 보고하고, 승인 패치를 `c45fb9b768accaf3ad54561a7766e2d81cf56c86`에 고정했다(부모98d71ef, oa_avd.py 한 파일 +4/-3). Codex가 commit 구성·tracked clean과 세 blob의 승인 canonical hash를 직접 확인했다. 조건부 SOURCE gate는 충족됐으며, runner9deb8057로 시작한 S/P의 실제 결과와 rel-0164 출하 판정은 아직 대기다.

## IME 의미 확인과 좁은 보정

검증용이 화면에서 키보드 부재/목록 유지/dialog0을 본 앞 run은 하네스의 과도한 shown 판정으로 기록한다. Codex도 같은 run의 logcat에서 HIDE_SOFT_INPUT_BY_BACK_KEY→onFinishInputView→onHidden 순서를 읽었다. 이 관측은 당시1b 해석을 보충하며 ②~⑦/제품 호환 동작이 실행됐다는 뜻은 아니다.

[AOSP Android13 InputMethodService 소스](https://android.googlesource.com/platform/frameworks/base/+/refs/heads/android13-release/core/java/android/inputmethodservice/InputMethodService.java)에서 hideWindow는 mWindowVisible과 mDecorViewVisible을 내린다. mIsInputViewShown 내부 값과 달리 public isInputViewShown()은 decor 가시성을 반환한다. 따라서 이 두 이름을 같은 “내부 상태일 뿐”으로 취급하면 안 된다. 이것은 AOSP 의미 확인이며 해당 AVD의 정확한 이미지 build 전체를 대조했다는 주장은 아니다.

98d71ef에서는 `mInputShown=false mWindowVisible=false mDecorViewVisible=false isInputViewShown=true`를 주면 **hidden**으로 판정한다. 모순된 public 가시성 보고를 안전한 무표시로 받아들이는 경로다. 보정은 public true를 보수적으로 **shown/입력 금지**로 보고, internal-only 예외는 **mIsInputViewShown**에만 적용한다. 실제 앞 run의 mIsInputViewShown-only 패턴에 대한 hidden은 그대로 유지된다. 가시성true 근거가 없고 필수3플래그 중 하나가 없으면 unknown이며, true/false 충돌은 true가 있으면 shown이다.

이 보정은 실행 하네스2줄과 설명만 바꾸며 시험의 기대값·stub·제품은 바꾸지 않는다.

## 승인 패치·직접 대조

- 패치: `D:/remote/remote/.claude/codex_old_apk_review/r7i_98/ime_public_flag_approved.patch`
- SHA-256: `c92b40e86664276d6829cfdb8074e350af745fc9df552b5b7cb062f80b9fba36`
- 보정 사본: 같은 폴더 `oa_avd_fixed.py`
- 보정 oa_avd.py canonical UTF-8/LF SHA-256: `39a0d515a03341e4e6f8b88f38dd3a23b5d558fb053d30b060bc810b71a39cdc`
- oa_pick_test.py는98 그대로: `0decb28628764745a8a4a16af42c55a8bf0b38f4f2c58e829008caed2b1b1ced`
- stubs/fake_ui_adb.py도98 그대로: `3dd6a073290274e7e65564ce551213d7f157e1f4d1025630a8eb28dd7def7465`

verify-0164에서 git apply --check 성공을 확인했으며 직접 적용하지 않았다. 실제 ime_state 함수를 고정 blob에서 추출해 대역 dumpsys 입력으로 **12건**을 확인했다. 보정본은 모두 통과했다: 모두false, internal-only, publictrue 모순, 각3플래그의true, 각 필수플래그 결측, 빈 출력, 중복true/false 충돌 등이다. 원본은 publictrue 모순을 hidden으로 봤고 보정은 shown으로 거부한다. 기록은 `r7i_review.py`, `r7i_98/checks.json`이다. 실제 adb/프로세스/Host/AVD는0이며 파일은 검토용 소유 폴더에만 썼다.

검증용이 보고한98의 pick74/pmclear12/redact7/orchestration26 모두rc0은 별도 귀속으로 수용한다. 이 정확한 보정 후 pick74를 독립 확인하고, 이미 영향 없는 나머지 결과는 재사용한다. Source를 다시 받기 위한 패킷을 만들 필요는 없다. **정확한 보정에서 벗어나는 변경 또는 회귀 실패가 있으면 실기는0으로 두고 해당 차이만 잇는다.**

## 새 runner

`D:/remote/remote/.claude/codex_old_apk_review/run_old_apk_lane_r7i_reviewed.ps1`

SHA-256: `9deb80575df4066ea46c6cde5479bda42324a2efc71544ed4bcb533767c9b4ed`. PS parser0 오류를 확인했으며 실행하지 않았다. 이전5405f48b의 두 파일 gate를 우회하거나 수동 제거하지 않는다.

인자는 기존과 같다.

- `-CandidateHead`: 위 보정 후 새 full commit. runner는98의 후손이고 HEAD 일치/clean인지 확인한다. ff1c72a 대비 **위3파일만** 바뀌고 각각의 canonical source hash가 위 값인지 고정한다. 따라서98 원본·다른 추가 변경·미커밋 상태로는 실행되지 않는다.
- `-TraceServerJs`: verify-0164의 `.claude/oa_relay_server/` 아래 실제 v-traced server.js 절대경로. raw52918c04 pin, plain 서버 LF2742143d pin과 raw/LF 기록은 그대로다.

S는 trace env1/사본/ServerSha256, P는 trace env0/기존 plain 서버, SDK/APK/5 exe gate·fake adb 제거·OA_OBSERVE_SEC30·첫 nonzero 중단·finally 자기 slot 해제·읽기 오류시 Stop을 모두 유지한다. C 실기가 끝나 slot이 비었다는 통지에 맞춰, Source/회귀/commit 조건이 충족되면 자기 CreateNew slot 아래 S→P1회로 이어 간다.

SH-off의 공개 UI 확인, 같은 viewer/연결과 IME hidden·video bounds 전제,30초 무입력 관측, 실패 직후 읽기 전용 진단, 기존 Host input/power/EOF/job guard도 유지한다. 다시 안전/입력 전제가 실패하면 후속 시나리오0·원문 보존·정리이며, 임의 BACK 반복/서비스 변경/다른 기기/운영 로그인으로 우회하지 않는다.

제품 호환 및 기본 weak 종료는 아직 미확정이며, 이번 파서 Source 확인으로 PC0.2.164 출하를 승인하지 않는다. Codex는 제품/추적 파일 변경·사용자 앱/기기 조작·설치·서버 배포·push를 하지 않았다. 미추적 검토/patch/runner만 작성해 제품 commit은 만들지 않았다. 사용자 현재 조치 없음.
