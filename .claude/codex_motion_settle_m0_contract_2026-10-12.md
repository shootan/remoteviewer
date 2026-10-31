# motion-settle r2 — M0 근거와 D/F/A/B 계약 보정

2026-10-12. 수신 task `t-0wsagafw`, 검증용 `remote#34id7hp2`. 기존 `codex_motion_settle_2026-10-12.md` r1의 **정제 입력 상한·일시 ceiling·측정 기준**을 아래와 같이 보정한다. 나머지 신원/키 의무/참조 보존·회선 계약·AMD/NVIDIA 검증은 유지한다. 설계·측정 GO이며 미커밋 제품의 SOURCE OK나 출하 승인이 아니다.

## 1. 결정 요약

| 안 | 판정 |
|---|---|
| D: 고정 plateau 기준 | **필수, 조건부 수용.** 고정 장기 reference를 기준/후보가 공유한다. ‘30초니까 plateau’는 불가. 아래 안정성·실패 집계 보수부터 한다. |
| F: settle 동안 낮은 MaxQP ceiling | **가장 먼저 구현·비교 측정하도록 승인.** AMD 해당 프로파일만, 20→16 비교, 기존 8입력/1초 안에서 먼저 평가. 2~4장으로 충분하다는 결론은 아직 없음. |
| A: 30입력/3초 | **F 부족 시 같은 task에서 비교 후보로 진행 가능.** hard bound 확대를 승인하되, sliceQp를 화질 폐루프로 쓰는 원안은 수용하지 않음. 출하 선택은 정제 효과·새 움직임 방해를 함께 측정한 뒤. |
| B: EncodeQP | **격리 대조 측정만.** 지원 표시가 한 프레임 한정 적용/복원 보장이 아니다. 기본 제품 경로 채택은 보류. |
| C: 1-IDR | 기존대로 격리 대조 유지. 이미 인코딩된 IDR/P 폐기 금지. |
| M2 단독 | 실험 행은 필요하지만 **검증된 회복 수단 없이 단독 출하하지 않는다.** F라는 특정 구현만 필수인 것은 아니며, F 또는 A 등과의 결합이 화질·응답성 조건을 만족해야 한다. |

M1의 ‘skip된 최신 화면을 재제출’은 이 결정과 독립적으로 계속 보수한다. M4도 이미 확인한 통계 결함 보수 범위에서 진행한다. M5/cap 확대·기본 CPU 인코더 전환·새 와이어는 넣지 않는다.

## 2. 이번에 직접 확인한 근거와 해석 한계

- 작업용 worktree `C:/work/gnlink-wt/fix-motion-settle`, HEAD `8b51bdf`; M1/M2/parser/측정기 일부는 미커밋 상태다. 보고서 r1, `plateau_45m30_r1`의 episodes/deliveries CSV, `base_45m30_r1`, in3/in8 CSV, 현재 측정기/parser/정제 상태 코드를 읽었다. GPU 실험을 Codex가 재실행한 것은 아니다.
- M0+M4 기준선 120 episode 중 **9개가 `never sharp within the hold`**다(minrestore4, noise1, scroll1s1, scrolllong2, typechar1). 다른 무효 사유는 이 CSV에서 없었다. 측정기는 이를 valid=false로 만든다(viewer_udp_recovery_test.cpp:2259), summary는 valid행만 p95에 넣는다(automation/motion_settle_summary.py:49 이후). 따라서 보고서의 3.3~5.4초는 **성공한 행에 한정된 분위수**이며 전체 p95라고 쓸 수 없다.
- 30초 run의 typechar0은 24.193초 46.07dB → 26.236초 47.93 → 29.239초 49.44다. 마지막 5초에도 약 3.4dB 상승한다. **이 행은 plateau를 얻은 행이 아니다.** typechar1의 끝은 53.56dB이고 scroll1s는 55dB 근처이나, 장면·최종 그림·직전 RC 상태가 다르므로 하나의 고정 55dB reference로 합칠 수 없다.
- sliceQp 1단계 안팎의 하강과 P 반복에 따른 품질 향상은 유용한 관찰이다. 그러나 이 reader는 스스로 명시한 대로 **첫 slice의 시작 QP**만 읽고 mb_qp_delta는 읽지 않는다. 이는 실제 프레임 평균 QP/글자 ROI의 QP가 아니다. ‘AMD는 반드시 1 QP/input만 회복’ 또는 ‘8입력이면 정확히 8단계’라는 일반 법칙은 입증되지 않았다.
- 그러므로 **현재 측정된 장면·경로·초기 상태에서 8입력안이 목표에 못 미침**은 수용한다. 그것을 AMD 전체/모든 제어 수단의 ‘물리적으로 불가능’으로 확대하지 않는다. F의 낮은 ceiling을 실제로 전환한 원자료도 아직 없으므로 ‘바로 ≤16/20, 2~4장으로 완료’ 역시 가설이다.

## 3. D: 먼저 측정 기준을 고정한다

1. reference는 `(최종 원본/seed/hash, 글꼴·배율·색 변환, 실제 coded/visible 크기, ROI/mask, R/fps, vendor/profile/driver)`별로 보존하고 **기준선과 후보에 같은 값을 쓴다**. 각 후보의 마지막 프레임을 각자의 기준으로 삼아 더 흐린 후보를 유리하게 만들지 않는다. source에 대한 PSNR와 long-static reference의 PSNR를 구분해 기록한다.
2. 30초는 최초 calibration 관찰 길이로 허용한다. 예를 들어 마지막 5초의 **새로 decoded된 동일 final-content 표본 3개 이상**, PSNR 변동폭 ≤0.5dB·기울기 절댓값 ≤0.1dB/s, 크기/내용 불변을 모두 확인한 구간의 대표값을 reference로 고정한다. 프레임이 안 나와 같은 buffer만 읽힌 상태를 수렴 증거로 쓰지 않는다. 미수렴이면 calibration만 60초 등 명시된 상한까지 연장하고, 끝까지 미수렴이면 reference 미확정으로 보고한다. 전체 성능 행렬을 reference 없이 반복하지 않는다.
3. **품질 미도달/마지막 그림 미도착은 제품 결과의 실패 또는 right-censored(관찰 마감보다 큼)**다. 캡처 fixture 자체 실패·원본 변동·잘못된 크기/이벤트 결합과 같은 시험 무효와 별도 필드로 분리한다. 유효 workload 수·도달 수·미도달 수·관찰 마감을 함께 보고, censored를 0이나 누락으로 바꾸지 않는다. p95를 식별할 수 없으면 `>관찰 마감`/`추정 불가`로 남긴다. 이전 표는 참고값으로만 유지한다.
4. plateau 목표와 별도로 `T_final`(최종 내용 첫 도착), 0.25/0.5/1/2/3초의 glyph 품질 및 실제 크롭을 보고한다. 마지막 내용의 신속한 도착과 미세한 잔여 정제를 한 숫자로 섞지 않는다. 사람이 이미 읽을 수 있는 그림의 49→55dB 개선과 사용자가 못 읽는 31dB를 동일한 체감 실패라고 단정하지 않는다. **기존 plateau−1dB/500ms 유지와 시간 목표를 이번에 낮추지는 않는다.** 수치와 실제 가독성이 크게 다르면, 별도의 perceptual 완료 기준은 근거를 모아 명시적으로 결정하며 현재 실패를 몰래 PASS로 바꾸지 않는다.
5. 직전 RC 상태가 결과를 지배하므로 cold, 정상 static에서 motion 진입, 강한 motion 직후 정지의 precondition을 고정한다. 각 episode의 pre-quality/instance/누적 입력을 기록한다. 긴 reference calibration로 따뜻해진 codec을 후보에만 물려주거나 scene 순서 차이로 결론 내리지 않는다.

## 4. F: 일시 저QP ceiling을 허용하는 계약

여기서 16/20은 **MaxQP ceiling의 임시 값**이다. MinQP나 고정 QP floor가 아니다. Microsoft 문서상 MaxQP는 static 속성이라, 해당 AMD MFT의 live 전환 효과는 별도 실증에 의존한다. [MaxQP 문서](https://learn.microsoft.com/en-us/windows/win32/medfound/codecapi-avencvideomaxqp)

### 4.1 측정과 적용 범위

- 먼저 AMD에서 `일반 desired(32 또는 motion 38/40) →20 → 현재 일반 desired`를, 다음으로 16을 동일 장면·같은 precondition에서 비교한다. Set/Get 결과뿐 아니라 per-AU attribution·bytes·decoded ROI 품질·wire 지연을 확인한다. 낮은 값일수록 좋다고 단정하거나 12/8까지 자동으로 내려가지 않는다.
- 기존 M1의 2~4입력 및 최대8/1초로 먼저 평가한다. F 진입은 quiet/최신 캐시 정합·실제 전송 backlog/재전송 압력·키 의무를 본다. queue.empty()만으로 sender의 현재 송신 중 AU까지 없다고 판단하면 안 된다.
- 현재 해당 AMD legacy/live 프로파일만 대상. unknown legacy와 NVIDIA는 이 override를 적용하지 않는다. NVIDIA Home5는 이전 기능의 검증이지 이번 settle 목표 충족 증거가 아니므로 공통 M1의 NVIDIA 회귀는 그대로 필요하다.

### 4.2 한 정책·한 setter, 복원은 현재 상태로

- 일반 desired(수동 설정/slow governor/fast pressure)와 임시 settle override를 구분하되, **유효 ceiling 계산과 실제 적용은 한 소유자·Reconcile 한 경로**로 한다. 일반 tick이 settle20을32로 되돌리고 settle이 다시20을 쓰는 경쟁은 금지한다.
- 임시 override는 `(episodeId, media/input epoch, generation, attachment, codec instance, contentVersion)`에 결합한다. 새 움직임/새 내용·target/fps/R 변경·선택·reconnect·잠금·키 복구 의무·마감에서 제거한다. 복원 값은 **그 순간의 일반 desired**다. 무조건32 또는 entry 때 저장했던 값을 써서 새40/수동 목표를 덮어쓰지 않는다.
- **r1 C4에 대한 명시적 예외:** settle의 임시20/16은 다음 codec 초기값이 아니다. next-instance seed는 일반 desired를 유지한다. 현재 Reconcile이 매 호출 `set_initial_max_qp`도 갱신하므로, 임시 override 때문에 재초기화까지20이 전파되지 않도록 current applied / ordinary desired / next seed를 분리해 로그·시험으로 확인한다. 재초기화 직전에는 옛 episode가 폐기된다.
- **r1의 250ms Set 간격도 제한적 보정:** 저QP 상태에서 새 실제 움직임을 만났을 때는 첫 새 encode 전에 override 제거·필요한 복원을 우선한다. 방금 dip했다는 이유로 250ms 동안 움직임을 저QP로 인코딩하거나 실제 입력을 묶지 않는다. 이 취소 복원 1회는 간격 예외로 허용하되, dip 재진입은 quiet/최소 간격/cooldown으로 제한해 반복 토글 총량을 묶는다. 재진입으로 실패 예산을 초기화하지 않는다.
- Set 성공/상충 없는 readback까지 applied와 requested를 구분한다. **복원 실패는 degraded 표시만 남기고 끝낼 수 없다**: 낮은 ceiling이 motion에 남으면 과대 AU가 지속될 수 있다. 제한된 재시도 뒤 기존 repair 경로로 일반 계획을 복구하거나 해당 instance의 F를 비활성화해 안전한 일반 상태를 확보한다. 실패시 repair만 예외이며 정상 settle마다 reinit하지 않는다. stop/종료 도중 복원을 위해 새 인코더를 시작하지 않는다.
- async MFT에서 entry/exit 사이 출력됐다고 그 AU가 해당 settle 입력으로 인코딩됐다고 가정하지 않는다. accepted input ordinal과 episode 출처를 전달하고, 늦은 A-episode 출력이 B-episode의 카운터/종료/복원을 건드리지 않도록 한다. 현재 미커밋 `NoteEmitted`는 최신 episode 카운터를 증가시키므로, F/A의 제어 입력으로 쓰기 전 이 귀속을 보완해야 한다.

### 4.3 회선 상한만으로 충분하지 않다

- strict cap/ledger/FEC/NACK/헤더 계정은 그대로 유지한다. 그러나 이것은 **과대한 한 장의 전송 시간이나 다음 motion 지연의 상한이 아니다**. quiet 직후 언제든 사용자 입력이 올 수 있다. 총량을 넘지 않았다는 이유로 F를 통과시키지 않는다.
- dip 뒤 첫 P의 크기/실제 wire 비용·남은 sender backlog·새 내용의 enqueue→display를 측정한다. 4.5/6M와 낮은 rate에서 F 직후 50/100/200ms에 motion을 다시 시작하는 반례를 넣는다. 이미 인코딩된 참조 AU는 크다고 버리지 않는다. 큰 잔여 AU가 다음 입력을 막으면 ceiling/입력 수/적용 범위를 보수적으로 조정한다.
- settle AU가 M2의 fast-pressure를 재발동해 자기20을40으로 뒤집지 않게 출처를 구분한다. 실제 wire 혼잡은 무시하지 않고 정제를 멈춘다. 키 복구/새 내용은 정제보다 우선하며, old settle 때문에 recovery 키를 저QP로 크게 만드는 경로를 확인한다.
- 신규 송신 대기/고비용 작업은 WGC FrameArrived callback에 넣지 않는다. 기존 signal-only 계약을 유지한다.

## 5. A/B/C와 M2 결합

### A — 제한 확대는 허용, sliceQp 화질 폐루프는 보류

F가 불충분하면 별도 재승인 없이 **최대30 accepted input, 최대60 submit attempt, quiet 후 episode 시작부터3초**의 비교 후보를 같은 task에서 평가해도 된다. 이 값은 소비해야 하는 quota가 아니라 상한이다. sender 여유에 맞추고 마지막 drain용 입력까지 합산하며, 실제 새 프레임/identity 변경/키 의무가 우선한다. 3초까지 허용했다고 기존 p951초 목표를3초로 바꾼 것이 아니다. 각 후보가 어느 시간 목표를 충족/실패했는지 그대로 제시한다.

다만 `sliceQp>16이면 계속`이라는 production 폐루프는 승인하지 않는다. QP16이 plateau와 일치하지도 않고, 이 값은 첫 slice 시작값일 뿐이다. 작은 정해진 묶음 + wall time/byte/backlog 예산을 우선하며 sliceQp는 진단 근거로 둔다. Unknown에서 무한 지속하거나 즉시 ‘완료’ 처리하지 않는다. 30×30KB/3초=2.4Mbps는 **payload 가정치**이며 FEC/헤더/NACK를 더한 실측 wire 비용이 아니다. 같은 제한을 모든 rate/vendor에 강제하면 안 된다.

### B/C — 효과 확인용 대조

`CODECAPI_AVEncVideoEncodeQP`는 fixed-QP 값 설정이며, 지원/Set S_OK가 ‘다음 한 장만, 나머지 RC unchanged’를 보장하지 않는다. 같은 API라도 async pending 입력과 속성의 지속 범위를 확인해야 한다. 별도 프로세스/시험 빌드에서만 RC 모드·Set/Get·입출력 attribution·복원 후 다음 motion을 대조한다. 제품 기본에 시험 스위치/새 레버를 섞지 않는다. [EncodeQP 문서](https://learn.microsoft.com/en-us/windows/win32/medfound/codecapi-avencvideoencodeqp)

C는 이미 허용한 bounded1-IDR 대조를 마무리한다. raw IDR인지, actual wire tail인지, 품질과 다음 motion 비용이 어떤지로 비교한다. 예상만으로 좋다/나쁘다 판정하지 않는다.

### M2 — F 구현 이름보다 회복 결과가 조건

M2→시작 QP 상승→정제 지연의 충돌 가능성에 동의한다. `M1만`, `M1+F`, `M1+M2`, `M1+M2+F`의 핵심 짝을 같은 precondition에서 비교한다(A 선택 시 그 조합으로 대체). M2를 빼면 onset이 나빠지고 넣으면 다시 읽기 어려운 시간이 길어지는지 각각 보여야 한다. **F/A가 검증되지 않은 상태의 M2 단독 출하에는 동의하지 않는다.** 반대로 F 없이도 다른 유계 회복 경로가 시간/가독성/모션 재개 기준을 모두 만족하면 F를 의무로 남길 필요는 없다.

## 6. 새 parser와 측정기: 전체 행렬 전에 닫을 작은 관문

`h264_slice_qp.hpp`는 아직 미커밋이며 SPS/PPS/slice에서 id를 읽고 버린다. 여러 parameter set이 나오면 현재 보관한 하나가 slice가 지정한 것인지 확인하지 못한다. slice_type도 `%5`를 먼저 하므로 불법 범위를 정상 타입으로 바꿀 수 있다. 64회 제한 루프가 종료 코드를 못 만났을 때와 unsupported bit depth/profile도 명시적 Unknown이어야 한다. first-slice-only라는 사실을 로그/보고에 유지한다. 안전한 지원 범위를 강하게 제한하거나 id 결합·유효 범위를 처리하고, 절단/교체/다중 PPS/새 instance에 대한 결정 벡터로 확인한다. ‘실AMD90개 읽음·≤32’는 독립 정답 검증이 아니며 sample QP 평균과 slice 시작값이 다를 수 있어 단순90% 일치도 일반 계약으로 두지 않는다.

큰 QP parser를 완성하라는 요청은 아니다. **제품 제어 의존을 없애 진단/격리 측정에 한정하면 범위를 줄일 수 있다.** 이때도 잘못된 판독을 ‘AMD의 물리 법칙’ 근거로 삼지 않는다. 무지원에서 Unknown으로 진행하는 기존 계약은 유지한다.

먼저 D 집계/기준선 고정 + 대표 scroll/typechar/motion-restart의 짧은 F 비교를 한다. 30초 calibration은 별도이며 모든 후보의 모든 episode마다 반복할 필요 없다. 안전 조건/효과가 드러난 후보만 r1의 반복·셀·AMD DXGI/WGC·NVIDIA 회귀 행렬로 확대한다. fixture minrestore는 그려낸 장면임을 계속 표시하고 실제 DWM 최소화 실기와 혼동하지 않는다.

검증용은 이 문서를 기존 단일 task 지시서 r2에 반영해 작업용1에 한 번 전달한다. M0 보정→F→필요 시A/B 대조는 위 범위에서 연속 진행할 수 있다. 제품 선택과 검증 자료를 고정 후보 한 묶음으로 회신하면 된다. 모든 미커밋 코드의 전문 소스 검토는 그 후보에서 한다.
