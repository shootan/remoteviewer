# motion-settle r3 — onset MinQP 대조 허용 · M2 제외

2026-10-13, task `t-0wsagafw`, 검증용 `remote#34id7hp2`. r1/r2 중 아래 항목만 보정한다. 제품 범위/설계 판단이며 후보 SOURCE OK·출하 승인이 아니다.

## 1. 결정

**(c) AMD의 짧은 onset MinQP 상향을 같은 task의 구현·비교 후보로 허용한다. M2 fast-pressure(MaxQP 상향)는 이번 출하 후보에서 제외한다.** (a) 정제 간격 66/100ms, 후반 입력 축소, F ceiling 완화 대조는 기존 범위대로 계속할 수 있다. MinQP가 ‘유일한 레버’라고 확정하거나 새 기본값으로 바로 채택하지 않는다. 더 단순한 (a)/(b)/(d)가 화질과 응답성 조건을 함께 만족하면 그것을 선택해도 된다.

MinQP는 variable-QP 인코딩의 **하한**이므로 낮게 남은 QP를 올리는 실험의 방향은 맞다. 다만 MaxQP live 동작의 실증이 MinQP live 동작까지 증명하지는 않는다. 지원/Set/Get, 실제 해당 입력의 출력, 해제 후 동작을 별도로 확인한다. [Microsoft MinQP 정의](https://learn.microsoft.com/en-us/windows/win32/medfound/codecapi-avencvideominqp), [H.264 encoder의 MinQP 설명](https://learn.microsoft.com/en-us/windows/win32/medfound/h-264-video-encoder).

## 2. 근거와 수치 정정

- 고정 소스 `0701e50`(4b136d9/62aae8c 후속), 작업용 보고 r1 §5, 검증용 의견, `m1f16a_45m30_r1`·`m1m2f16a_45m30_r1` episodes.csv 및 summary의 fixedRef/분위수 계산을 읽었다. GPU 실험 재실행은 하지 않았다.
- CSV의 **sharpFixedMs**로 다시 계산하면 M1+F16+A minrestore 6/6 도달·p95=1248ms, scroll1s 6/6·716ms가 재현된다. **1248ms는 p95≤1초를 충족하지 않는다.** 보고 §5.4-1과 검증용의 ‘minrestore에서 화질 목표 처음 충족’ 문구를 정정한다. ‘모든 표본에서 reference 도달’과 ‘시간 목표 통과’를 구별한다.
- 같은 CSV에서 minrestore gap p95/max=315.5/371ms, +M2=381.5/458ms. M1+F16+A restart200은 p95=236ms여도 max=270ms다. 기존 첫1초 최대 gap 조건을 p95만으로 완화하지 않는다. 각각 6 episode·1회인 비교 자료이며 최종 합격 근거는 아니다.
- ‘ceiling 복원 뒤 slice 시작 QP가 낮게 남았다’는 해석은 타당한 후보다. 그러나 첫 P 5→8KB 또는17→21KB 증가만으로 300ms 이상 gap 전부를 설명할 수는 없다. 이미 보낸 정제 AU의 tail, sender/MFT pending, 캐시 공급·decoder 지연을 **같은 onset event/입력 ordinal**로 분해한다. (c)는 새 입력의 인코딩만 바꿀 수 있고 이미 큐에 쌓인 참조 AU를 작게 만들지는 못한다.

## 3. MinQP 임시 구간 계약

### 적용 자격과 값

- 해당 AMD legacy만. NVIDIA/unknown legacy는 MinQP **Set 0회**, 기존 계획 유지. MinQP의 IsSupported/GetParameterRange/GetValue와 HRESULT·타입을 확인하고, 이 instance의 정상 MinQP를 보존한다. 복원할 값이 불명확하면 production 적용을 건너뛴다. 문서 기본값0만 믿고 vendor의 상태를 추측하지 않는다.
- 시험 시작값은 **26**, 효과가 부족하면 28/30을 비교한다. 이 값은 실제 QP의 목표가 아니라 하한이다. 실제 MinQP 지원 범위와 **확인된 ordinary MaxQP** 안에 있어야 한다. MinQP를 넣으려고 사용자/기존 정책의 MaxQP를 추가로 높이거나 수동 설정을 덮지 않는다. 기존 min이 이미 요청 하한 이상이면 불필요한 Set 없이 처리한다.
- 동작을 먼저 시험 빌드에서 입증한 뒤 제품 후보 선택을 고정한다. S_OK/readback만으로 효과 PASS가 아니며 첫 실제 motion 입력의 AU 크기·slice/샘플 관찰·수신 품질을 함께 확인한다. 전역 RC 모드/mean/peak/VBV/EncodeQP는 건드리지 않는다.

### 트리거와 수명

- 목표는 정제/정지 뒤 **quiet→실제 새 내용**의 첫 입력 구간이다. 마우스/키 이벤트, 합성 입력, 동일 화면 callback만으로 발동하지 않는다. 같은 motion episode 안에서 매 contentVersion 증가마다 deadline/입력 수를 다시 시작하지 않는다.
- 먼저 F를 취소하고 ordinary ceiling으로 복원한 뒤, 자격이 맞으면 새 real input을 encoder에 주기 전에 floor를 적용한다. 적용이 늦어 첫 AU가 이미 인코딩됐으면 그 첫 장에 효과가 있었다고 주장하지 않는다. 매 loop에서 ‘floor 설정을 기다리느라’ 사용자 프레임을 보류하거나 pending MFT가 0이 될 때까지 입력을 막지 않는다.
- 첫 후보는 **최대4 accepted real input 또는 최초 적용 시도 후250ms 중 먼저 도달한 때** 종료한다. 필요하면 같은 비교 범위에서 **최대8 accepted/500ms**까지 허용한다. 이 상한은 성공 Set/출력 시점이 늦어졌다고 뒤로 밀리지 않는다. synthetic/drain 입력은 motion count를 채우는 근거가 아니며, 이들을 만들기 위해 별도 지속 loop를 두지 않는다.
- quiet로 돌아감, F 재진입 필요, recovery/selection key, target/R/fps/수동 설정 변경, 새 media/input epoch·attachment·codec instance, stop에서는 먼저 해제한다. **정지 뒤 다음 frame이 없어도 wall-clock 종료·해제가 실행돼야 한다.** 가짜 프레임을 계속 넣어야만 floor가 풀리는 설계는 불가하다.
- `sliceQp >= floor`는 floor가 작용했다는 보조 관측이지 RC 내부 상태가 정상화됐다는 증거가 아니다. 이것만으로 수명/성공을 결정하지 않는다. floor 해제 뒤 다음1/3/8개 motion AU가 다시 낮은 QP·큰 크기로 튀는지도 측정한다. 기존 r2처럼 sliceQp는 프레임 전체 품질을 대표하지 않는다.
- floor episode는 session/media/input/attachment/codec instance와 onset id에 결합한다. 뒤따르는 같은 motion의 내용 변경은 유효하되 episode를 연장하지 않는다. 옛 episode의 늦은 출력이 다음 episode의 카운터·복원·재arm을 건드리면 안 된다. 단일 글자 편집/캐럿 변화도 별도 반례로 넣어 불필요한 전체 화면 품질 저하가 지속되지 않는지 본다.

## 4. 하나의 QP 범위 소유자, 순서와 실패 처리

MinQP와 MaxQP는 **두 개의 독립 controller가 아니라 한 QP-range 상태**로 적용한다. ordinary desired·temporary override·confirmed applied·next-instance 계획을 구분한다. 실제 setter는 각각 한 필드만 쓰고 같은 값은0 Set이다.

**항상 `confirmed MinQP ≤ confirmed MaxQP`. 전환 중에도 지킨다.**

1. F(max16) → motion(min26/28/30): 먼저 max를 현재 ordinary 값(예32)으로 복원·확인한다. 그 뒤에만 min을 올린다. max 복원 실패/미확인 상태에서 min28을 쓰지 않는다.
2. motion floor → F(max16): 먼저 min을 정상 값으로 해제·확인한다. 그 뒤에만 max를 낮춘다. min28이 남았는데 max16을 쓰면 안 된다.
3. 일반 range 자체가 변경될 때도 현재·목표 쌍이 모순되지 않는 순서로 적용한다. 실패한 반쪽을 applied로 표시하거나 전체 성공으로 로그하지 않는다. 값 간격을 맞추려고 목표를 몰래 바꾸지 않는다.

- F와 floor는 동시에 desired-active가 될 수 없다. **F 진입 전 floor가 실제 해제됐다는 확인**까지 포함한다. wall-clock/새 내용/키 복구에 의한 해제는 평소250ms Set 제한보다 우선하며, 재진입은 quiet/쿨다운과 세션 단위 토글 예산으로 제한한다. 매 real frame 두 속성을 재쓰기 금지.
- 임시 min/max는 다음 instance seed/일반 RcPlan으로 전파하지 않는다. 기존 F의 seed 분리와 같은 원칙이다. 새 instance/복원 시 기본 MinQP가 실제 정상값인지 다시 확보한다.
- 거절·stuck·Unknown을 구별하고 작은 실패 예산(최대3회)을 같은 instance/세션에 귀속한다. episode/value 교대로 무한 초기화하지 않는다. floor가 실제로 설정됐을 가능성이 있는데 해제 확인에 실패하면 **이후 F 금지 + transient 정책 disable**, 필요 시 기존 복구 경로에서 정상 계획으로 한 번 복원한다. 정상 상태를 확보하기 전 새 dip을 반복하지 않는다. 실패하는 새 instance마다 재초기화 loop를 만들지 않도록 세션 단위 제한도 둔다.
- MinQP가 남으면 정지 텍스트가 계속 흐릴 수 있으므로 ‘degraded 로그만’으로 성공 종료하지 않는다. 반대로 unsupported/미설정이면 일반 경로로 계속한다. 사용자 stop/프로세스 종료 뒤 복구를 위해 새 encoder를 만들지 않는다. 정상 성공 경로에서 reinit·IDR·epoch 증가로 MinQP 효과를 대신하지 않는다.
- async MFT는 Set 호출과 같은 call에서 이전 입력의 AU를 낼 수 있다. accepted input ordinal·속성 적용 시점·출력 attribution을 유지한다. entry/exit 자체가 pending 입력들의 실제 QP에 어떤 영향을 주는지는 실측으로 남기고 ‘첫 한 장에만 적용’이라고 이름 붙이지 않는다. 기존 참조 AU는 폐기하지 않는다.

## 5. M2 제외의 의미

**이번 기능의 새 fast-pressure로 MaxQP를 올리는 M2 정책은 제외 확정.** 기존의 1초 slow governor까지 제거하라는 뜻은 아니다. MinQP onset 구간은 M2의 이름만 바꾼 승인도 아니며 위 제한 아래 따로 검증한다.

현재 `0701e50`의 `FastPressureEligible()`는 `REMOTE60_NATIVE_FAST_PRESSURE=0`일 때만 꺼져 **기본 ON**이다. 보고의 ‘코드는 기본 off’는 아직 미래 작업이다. 후보에서 이를 수정하고, 출하 기본 환경에서 fast boost/그 정책의 QP 쓰기0을 확인한다. M2 대조 코드를 남겨 ‘출하 구성 제외’라고 하려면 테스트 target에서만 켜거나 제품 호출 경로를 제거한다. 일반 바이너리에 활성화 가능한 옵션을 남겼다면 ‘기본 비활성 기능’이라고 정확히 구분하고, 이번 승인 없이 릴리스 환경에서 켜지지 않게 한다. F/(c)가 필요한 공통 QP setter·간격/실패 예산은 유지할 수 있다.

## 6. 작은 비교를 먼저, 한 후보만 전체 검증

- 고정 reference·같은 precondition의 `M1+F16+A(M2 off)`를 기준으로, **간격66/100ms만**, **MinQP만**, 필요할 때만 결합을 비교한다. 한 번에 여러 조건을 바꾸어 MinQP 효과라고 부르지 않는다. (c) unsupported이면 (a)/(b)/(d) 경로로 진행하며 다른 API로 임의 대체하지 않는다.
- 50/100/200ms restart에 더해 floor 만료 전후, 단일 글자 후 즉시 정지, 길게 지속되는 motion, 짧은 stop/start 반복, 새 epoch/selection/키 요청, 정상 MinQP nonzero와 `ordinaryMax < requestedFloor`, 각 Set/Get 실패·stuck을 시험한다. 키 의무·최신 내용 제출·F 해제·floor 해제가 서로 막히지 않아야 한다.
- 실제 전송/decoder 결과로 **first-content/첫1초 최대gap/plateau 도달/0.5·1·3초 glyph 품질**을 같이 판정한다. 기존 최대gap 및 p95 품질시간 기준, censored 포함, 원본 크기/PSNR/crop과 글자 가독성 확인을 유지한다. MinQP로 작은 AU를 얻었지만 텍스트가 오래 흐리거나 floor를 풀자 다시 큰 AU가 터지면 미통과다.
- 실제 R/wire/FEC/NACK/2초 ledger 계약은 불변이다. 새 floor가 기존 sender의 잔여 tail을 지웠다고 주장하지 않는다. MinQP/MaxQP·ordinary seed와 state를 기록하되 callback에서 새 잠금/Set을 하지 않는다. 진단 로그는 유계로 유지한다.
- 짧은 AMD 비교에서 하나의 후보를 선택한 뒤만 기존 반복행렬·DXGI/WGC·NVIDIA 무회귀로 확대한다. MinQP/정제 목표가 계속 충돌하면 실패값과 대안을 보고하고 합격선을 몰래 바꾸지 않는다. 개별 시험 설정마다 Codex GO를 재요청할 필요는 없다.

검증용은 본 보정을 단일 지시서에 반영해 기존 작업용1 task를 이어가면 된다. 다음 요청은 고정 후보와 기능별 독립 근거 한 묶음으로 한다. 이 단계에서 게시 버전은 정하지 않는다.
