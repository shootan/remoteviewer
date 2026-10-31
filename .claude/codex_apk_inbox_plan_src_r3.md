# APK inbox r3 — sampler 파일 경계 / A/B 실행

2026-10-30. **r12 파일 경계 SOURCE OK. trace-off A→B(SH-off,120초)를 기존 조건으로 각1회 허용한다.** 아래 검토된 배치 수정본 또는 같은 수정이 적용된 실행 경로를 사용한다. A/B의 실제 결과·C 정책/실기 EVIDENCE·APK0.2.33 출하 판정은 아직 아니다.

## 고정

- r12 snapshot SUMS `f4837b299e713e7bee09278afba4916beca157de5a98188638d72aeb189a932a`.
- r12 diff `ccdab1a254f95faea7b115386985d076bca8133bede7c8b90a9aaf5aa150c6ff`.
- plan 최신 `6d4ded8e46a675223b91cdcc0ca491de49c3feb48bfc9b324c054c443028358d`.
- design 최신 `57d68eb0776ea46fbe4883d789dc4ed8b5a246412bc1cee0e7e79e76b18ee6bc`.
- 입력 batch `cfd1027f280c146ef1022f9482c12e39a131be3b5ee5d1143d28b4f8941e6f39`.
- 계측79b5c25와 A/B APK/server 고정은 앞 r2 수용을 유지한다. 실제 install은 A8c6422fe…와 Bac587dd plain32bb788f…이며 새 계측/C APK를 B로 바꾸지 않는다.

## 파일 경계 보수 — 직접 대조10/10

`_run()`은 자기 OUT/sample_io에 pid-counter 이름을 **exclusive xb**로 만들고 stdout을 연결한다. 성공/timeout 모두 파일을 보존하며 default TEMP·자동 삭제·덮어쓰기로 fallback하지 않는다. 해당 OUT은 기존 slot의 검증된 새 run 경로를 쓴다. sample test도 tempfile 대신 자기 새 runs 폴더를 만들고 보존한다.

Codex는 snapshot13개 hash를 직접 대조하고 자기 review 복사본에서 `ke_sample_test.py`를 실행했다. **10 checks/0 failed, rc0**, stdout 파일 **16개 보존**,6초 deadline arm은 약6.2초에 반환돼 miss로 기록됐다. 실제 adb/qemu 조회는 fake module/프로세스 대역이며 기기를 사용하지 않았다.

기록: `.claude/codex_decoder_liveness_review/r12_sample/`의 고정 snapshot·`review_sample.log`·`runs/ke_sample_test-20261030014149-31672/avd/sample_io/`. 파일 경계의 기존 N1을 닫는다. 과거 r11 TEMP 사용은 작성자가 실행 환경 근거로 기록했고, 외부 탐색/정리를 하지 않는 방침을 유지한다.

## 배치 wrapper — 검토한 최소 수정본 사용

입력 cfd1027f batch는 각 arm rc를 출력하지만 **실패 뒤에도 B를 시작하고 마지막에 항상 exit0**한다. 또한 boot_id 오류 문자열이 매번 같아도 unique1로 STABLE이 될 수 있다. 이 wrapper의0을 전체 시험 PASS로 쓰거나 정리 미확인 뒤 다음 arm을 자동 시작하면 안 된다.

승인 범위를 구체화하기 위해 **원본을 변경하지 않고 검토용 실행 수정본**을 준비했다.

- [ab_shoff_batch_reviewed_r3.ps1](D:/remote/remote/.claude/codex_decoder_liveness_review/ab_shoff_batch_reviewed_r3.ps1)
- SHA-256 `6c519f116426a43fd3ed132947c60ad8cccc5ecf4ddae22a8e8c87acff2ee627`.
- 원본 cfd1027f에서만 생성됐고 바뀐 것은 (1) boot 값이 rc0+UUID 형식인지 확인, (2) 첫 nonzero/unknown arm rc에서 중단·남은 arm NOT RUN·그 rc 반환이다. env 정리도 그 분기에서 한다.
- PowerShell parse0 오류. 실제 수정본의 종료 gate를 별도 child에서 순수 실행해 **rc0→다음 arm 허용, rc3→exit3/다음 arm0, unknown→exit2/다음 arm0**을 확인했다. 파일은 `r12_batch_checks/{ok,fail,unknown}.ps1`에 있다. 이 검증에서 SDK/Host/AVD는 실행하지 않았다.

검증용은 **이 수정본으로 실행하거나 동일한 수정만 원래 wrapper에 적용하고 새 hash를 기록**하면 된다. 이 구체 수정은 이번 승인에 포함하며 이를 위해 별도 Codex go/재상신을 받지 않는다. 자동으로 검사 범위를 늘리는 수정본은 아니다.

A가 조기 실패하면 원문/cleanup·job ended/residual/adb identity를 먼저 확인한다. 실패한 A를 즉시 반복하지 않는다. 그 뒤 검증용이 cleanup과 같은 시험 환경을 확인했다면 **같은 허용 범위에서 B만 독립1회** 진행할 수 있다(사용자나 Codex에게 다시 permission을 묻지 않음). 정리 미확인이면 다음 arm은 실행하지 않는다. 그 결과는 정상120초 matched A/B와 구분하고 실제 관찰 길이를 기록한다.

## 실제 실행 조건

기존 sh_run의 자기 슬롯 아래 배치를 child PowerShell로 실행한다. live `$e`의 파일이 r12 snapshot과 같은지, APK 두 hash·SDK pin·Host/harness/job hash·79b5c25 server raw372e6eb8…를 **실행 전** 확인하고 원문에 연결한다. 원본 batch의 hash 출력만으로 값이 맞았다고 가정하지 않는다. 미사용 ApkOn은 기록만 하며 설치하지 않는다.

30초 adb 안정 관찰·같은 시험 serial/boot, E2-only a Host·SH-off/effective direct, KE_WATCH_SEC120, local relay trace, source/순서/시간/부하 정규화, 초기화/Host input none+power off launch/final guard·EOF/job·새 repo run·비노출/clipboard 조건을 유지한다. snapshot/서버가 prototype C로 바뀌면 그 상태로 기존 A/B라 부르지 않는다.

rate는 snap의 실제 시각 차와 경계 오차/해상도로, IDR은 encoded AU·원인별로, relay는 UDP payload로 보고한다. sampled wire 합계와 창별 p95 평균을 쓰지 않는다. control/영상 종료·early end·데이터 부재는 그대로 실패/미판정으로 남긴다. B+trace는 별도 진단 run이며 이 두 arm의 CPU/latency에 섞지 않는다.

design의 최종4초 대기(t0+11초)는 **C prototype의 제안값**으로 수용할 수 있다. 실제 candidate가 도착했지만 codec queue 중인 상태와 network 응답을 못 받은 상태, 이미 시작한 교환을 C source에서 분리하고 늦은 유효 결과/owner 취소를 시험한다. 이 설계 보충은 앱 trace 없는 A/B의 추가 blocker가 아니다.

## 다음 상태

old-apk-lane은7db9c27 SOURCE를 이미 seq16068로 승인했다. “diff 확인 대기”를 이유로 그 승인을 다시 받을 필요는 없고, **검증용이 실제 현재 진행/슬롯 소유를 확인해 기존 우선순위대로 조율**한다. 다른 시험을 중복 실행/중단하지 않는다. 두 arm 관측 뒤 inbox 회귀/부하 원인을 판정하고 C를 선택한다. APK0.2.33 보류와 PC0.2.164의 별도 통합 관문·push 보류는 유지한다.

이번 Codex는 고정 diff/hash와 자기 복사본의 sampler 대조10건·batch 종료 gate만 실행했고 검토용 배치 수정본을 작성했다. 실제 Host/Viewer/AVD/adb·GPU/OS 입력·설치/운영·삭제·제품 파일 수정·commit/push는 없다. 미추적 검토 산출물만 작성했으므로 제품 commit은 만들지 않았다. 사용자 현재 조치 없음.
