# rel-0164 old APK lane r6 — relay 주소 / ⑦ 매핑

2026-10-30. **7db9c27 SOURCE OK. ⑦을 P로 옮기는 대체 매핑을 수용하고 S(standby 없음)→P(WithStandby)를 새 run으로 허용한다.** 실제 S/P 호환 EVIDENCE와 rel-0164 출하 판정은 아직 아니다.

## 고정과 직접 확인

- HEAD `7db9c2714f1a2d2b3ccb29143f494cced0c7030e`, 부모953aef2. tracked clean(미추적 산출물 있음), diff --check 통과.
- 보고 r6 SHA-256 `0e34f6a3d13e4c6d335520a41f340f7efcff0b878f6a8ebc2403d2d8671db20c`.
- snapshot SUMS `98c633457581f3ef116bef27e9a6c27ed55a5ff98ee47fcf8895fc0b9e9951ff`: **20파일 commit blob/raw hash 모두 직접 대조**.
- diff는 oa_fixture/oa_orchestration 두 시험 script뿐이다. 제품·서버 wire·인증 검사를 바꾸거나 약화한 변경은 없다.

현재 fixture의 PC는 observe를127.0.0.1로 하고, 폰은10.0.2.2를 통해 이 PC에 도달한다. PC가10.0.2.2를 같은 방식으로 쓸 수 없고, observe가 loopback인 PC에 LAN relay를 주면 source wire가 달라진다는 원인 설명을 수용한다. **이 fixture의 경로 제한이지 모든 환경에서 PC가 LAN relay를 못 쓰거나 공통 relay 주소가 불가능하다는 제품 제약은 아니다.** public NAS endpoint의 실제 도달/공존은 별도 통합 관문이다.

Codex가 다음 실제 fixture-only 원문을 직접 확인했다: `.claude/oa_runs/oa-fxonly-20261030012241-21712` (old-apk-lane worktree).

- readiness: `hosts=a standby=ready controlPort=18460 relay=127.0.0.1`.
- Host: capability 도착 전 첫 Hello refuse 뒤 **01:22:45.505 hello admitted entries=1 active=0**.
- holder: **01:22:45.599 ready**, 약20초간 ready 유지, Release 뒤 자체 종료, **rc0/released1/childFailures0**.
- viewer trailer: producer end confirmed·job active0·EOF(error0)·log kept.
- slot 원문: **bundle rc3/fixture1, job residual0/forced0/ended_confirmed1**. 폰 없는 Host capture assertion 실패를 정상 bundle PASS로 바꾸지 않는다. 위 값은 실제 standby 준비 계약의 부분 근거다.

이로써 앞 무응답 unsupported를 Host의 지원/빌드 자체 결함으로 볼 근거는 해소됐다. 다만 확인한 lane build와 검증용 독립 build의 hash를 섞지 않는다. 실제 S/P는 검증용의5 target hash로 귀속하며 P는 자체 standby-ready gate를 다시 통과해야 한다. Source 판정 때문에 별도 같은 준비 대조를 기계적으로 반복할 필요는 없다.

## ⑦ 매핑 수용과 한계

| arm | relay 광고 | 수행 근거 |
|---|---|---|
| S |10.0.2.2 |0.2.32 원 Source fixture build의 directory/relay·선택/영상/control·기존 park/resume,①–⑥ |
| P + WithStandby |127.0.0.1 |게시 APK 원본의 manual 직접 연결(TCP control/UDP video), P-5d 종료/P-6r 새 연결, **동일 Host의 PC standby 공존⑦** |

**lane 목적의 합성 근거로 이 대체를 허용한다.** S의⑦은 NOT RUN 사유를 남기고 P의⑦ 실제 관측으로 연결한다. P에서 PC lease는 relay지만 폰은 direct다. 따라서 이 조합을 **폰 legacy relay+PC standby relay 동시 실측**이나 PC standby와 옛 폰 park 동시 실측으로 쓰지 않는다. legacy/standby relay 간 서버 계약은 기존 통합 suite의 실제 실행으로 보완하고, 운영 도달은 서버 먼저 게시 단계에서 따로 확인한다. 필요한 경우에만 해당 미검증을 별도 후속으로 둔다.

게시 APK 원본의 고정 운영 HTTPS directory 로그인/relay가 이 두 arm으로 직접 검증되는 것은 아니라는 기존 gap도 유지한다. 새 주소를 맞추려고 운영 DNS/CA·wire auth/fence를 바꾸지 않는다.

## script 검토 / 실행

기본 relay는 arm에서 정해지고 S+WithStandby는 node/Host/holder launch 전에 거부한다. ready 줄에 실제 relay를 남긴다. 이번 oa_slot 호출은 **RelayIp override 없이** 기본 S/P 주소를 사용한다. S ready는 standby none/relay10.0.2.2, P는 standby ready/controlPort18460/relay127.0.0.1을 명령·원문과 대조한다. source의 explicit override 인자까지 모든 조합을 보장했다고 확대하지 않는다.

기본 경로 배열·child FORCE1·초기화 gate·종료/EOF 결과 배선은 이전 수용을 유지한다. orchestration22/22는 작업용 대조에 귀속하며 Codex가 전량 재실행했다고 하지 않는다. 새 script이므로 runtime argv/script hash는 갱신하고, 실제5 binary는 source 변경이 없으므로 기존 독립 build hash를 확인해 재사용할 수 있다.

Host log fallback은 현재 **WaitForExit 성공 branch**에서 harness 사본이 없을 때 readiness의 source를 복사한다. 모든 timeout→자체 Kill branch까지 보존을 보장하는 코드는 아니므로, 실제 그런 경로가 생기면 원본/사본 유무를 별도 기록한다. 이번 정상화된 경로의 S/P를 위해 전면 정리/재검사를 새로 요구하지 않는다. 로그를 삭제하거나 이전 run을 덮지 않는다.

검증용은 기존 완전한 절대경로·SDK adb pin/시험 serial/boot·OA_PM_CLEAR_TIMEOUT_SEC60·install/pm clear 성공과 P NO_CREDENTIAL·APK 원본/되받은 hash·Host input none/power off launch/final guard·child EOF/log·job residual0·자기 슬롯 아래 **S1회→P1회**를 잇는다. P의 무인증 update manifest check1회 예외만 유지하며 운영 로그인/download/install·사용자 앱/Host 조작·clipboard로 넓히지 않는다. 같은 단계 go를 다시 받지 않는다.

준비 실패는 AVD0으로 멈추고 raw INVALID를 보존한다. 기능 실패/peer_lost를 기존 APK 결함으로 자동 면제하지 않는다. 다음은 검증용의 실제 두 arm 결과이며, 통합 gate·Android pair 불변·push 보류와 APK0.2.33의 별도 보류는 유지한다.

이번 Codex는 diff/hash·보존된 실제 fixture-only 원문을 읽고 대조했다. 실제 Host/Viewer/AVD/adb·GPU/OS 입력·설치/운영·삭제·제품 변경·commit/push는 없다. 미추적 검토 기록만 작성했으므로 제품 commit은 만들지 않았다. 사용자 현재 조치 없음.
