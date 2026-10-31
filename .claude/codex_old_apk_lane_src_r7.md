# old APK lane r7b/r7c SOURCE — pinned window 선택/입력

2026-10-30. **선택 chain r7b와 같은 범위의 후속 r7c를 함께 검토해 SOURCE OK. 실제 실행 기준은 현재 검증 worktree와 일치하는 `548d19911d85c503f4b650d8bfd5ac1e0afada19`로 고정한다.** S→P 각1회를 허용하며 실제 호환 EVIDENCE/출하 판정은 아직 아니다. 별도 r7c 재상신은 필요 없다.

## 후보 정합

요청은69ff7e3/665a47d와 r7b diff8c3d94ec…/SUMS9fb1ed62…였다. 검토 중 author의 미커밋 수정이 commit548d199로 정리되고 검증용 HEAD도548d199로 진행된 것을 확인했다. 그 증분은3개 시험 Python 파일의 입력 pair/Windows tab 보강이며 제품·서버·C++ 변경은 없다. **665의 blob과 작성 중 working copy를 혼합해 승인하지 않았다.**

- r7b 고정 diff 및665 blob을 읽고,665→548 증분 diff와 diff --check도 확인했다.
- 현재 보고 SHA-256 `8f6da199682efcf4b8d0a72a09ec8c50b560599ccfebfa1f4b92f91a78a8fc9c`(r7c 보충 포함)을 읽었다.
- r7c SUMS `b42777a72fb1d482807976328403e8fa098a67a17d375dec23fa275b7a3173d6`, 증분 diff `888b1be5bb7b56cdc24648d14049b40d9e0a65c4e3ee57196c8a2710888c533b`.
- **실행에 쓰는10개 script의 검증 worktree 내용이548 commit과 같은지 직접 대조**했다. 일부 SUMS는 raw CRLF, 일부는 canonical LF라 그 형태를 구분했다. 새 실행에서665의 SUMS와548의 내용을 혼용하지 않는다.
- 정확한548 blob SHA와 실행 copy의 raw SHA는 `.claude/codex_old_apk_review/r7_548/review_hashes.json`에 함께 고정했다(SHA `c19914e0be728ca922161b03a8d61a55d6c24603ef0305bd9e06d573fbb81e9d`). 검증 worktree는 tracked clean이었다.

## ③/6b/6r 선택 판정 수용

현재 run의 capture HWND와 Host가 list한 단일 nonzero selectedId를 맞추고, 실제 Windows tab/card를 눌러 선택한다. desktop0/누락된 카드/Host 불일치는 거부한다. APK b554e76 source의 실제 select_request/ack/ready 필드 형식과 대조했다.

app의 pinned request+WINDOWS+gen, Host requested pinned/applied1/streamGen·거부0, 같은 app ack gen/streamGen, 같은 target/gen ready, 이후 video_debug out>0을 연결하는 판정을 수용한다. card title이 잘려도 현재 Host list와 유일한 visible/enabled card·실제 요청ID로 마무리하므로 prefix만으로 다른 target의 성공을 세지 않는다. HWND를 이전 run 값으로 고정하지 않는다.

이 source는 실제0.2.32 UI dump/렌더를 아직 증명하지 않는다. 실제 run의 targets_ui와 screenshot을 보존·직접 열람해 pinned window 내용이 보이는지 확인한다. **screenshot hash·out counter만으로 흰 화면/다른 화면을 정상 영상이라 승인하지 않는다.** 모니터/desktop/prelogin 경로를 검증한 것으로 확대하지 않는 앞 scope 결정도 유지한다.

## ④ 입력 수신 판정과 한계

r7b의 tap 직전 Host offset 뒤 down(kind2)/더 큰 seq의 up(kind3)/창 안 검사에서, r7c는 **첫 down/첫 후속 up·동일 위치±2px**로 보강했다. 첫 pair가 잘못됐는데 나중 pair만 골라 통과시키는 경로도 막았다. 연결 때의 modifier release·move만으로 PASS하지 않는다.

kind 값은 source의 button down/up과 맞는다.0.2.32 touch handler는 view point를 video content 좌표로 변환해 down/up을 보낸다. **같은 창 안에서 실제 pair를 수신한 근거로 한정 수용**하며, tap1200,540의 정확한 기대 pixel·영상 layout/letterbox 변환 정확도까지 검증했다고 하지 않는다. 기록용 aim/off도 판정이 아님을 유지한다. Host input none의 수신/기록이며 실제 OS 주입 성공이 아니다.

## 독립 대조

후속 정본 갱신 통지로 **검증용 verify-0164@548d199의 pick19/pmclear12/redact7/orchestration22, 모두 rc0/RESULT PASS**도 보고됐다. 아래 Codex 직접12건과 별도 실행 귀속으로 보충한다.548d199는 이미 본 SOURCE OK의 대상이므로 같은 후보를 다시 승인받을 필요는 없다.

Codex가548의 판정 함수들을 고정 blob에서 읽어 실제 함수로 순수 입력 대조를 실행했다. **12/12 통과**:

- 선택 chain 양성, id0·다른 ack stream·다른 ready gen·ready 뒤 out0 음성.
- 동일 위치의 down/up 양성, modifier releases/up만/down만/이동 pair/첫 invalid pair 뒤 정상 pair/size 없음 음성.

기록 `.claude/codex_old_apk_review/r7_fixed_checks.py`, `r7_548/checks.json`, `r7_fixed_checks.log`(SHA `65b9a02639d71bfc9a3a95d97f640232694194006bbac3ceca0a67249f0666f8`). 이는 함수 대조이며 실제 adb·uiautomator·AVD 입력은0이다.

검증용의665 독립 oa_pick15/pmclear12/redact7/orchestration22와 작업용의548 pick19는 각각의 보고에 귀속한다. Codex가 그 전체를 다시 실행했다고 하지 않는다. 첫 orchestration SETUP 실패와 사본 배치 후 성공은 구분해 보존한다. 이 보수로 binary5개를 새로 빌드할 필요는 없으며 stub용build 폴더와 실제build-verify를 혼동하지 않는다.

## 실행 경로와 승인 범위

현재 `run_old_apk_lane.ps1`은548 HEAD/같은5 exe hash·S standby없음/P WithStandby·APK hash/timeout60을 이미 가리킨다. 다만 arm 결과가 nonzero여도 바로 다음 arm으로 가므로, **정리 미확인 뒤 연속 실행하지 않는 검토용 수정본**을 만들었다(원본 변경 없음).

- [run_old_apk_lane_r7_reviewed.ps1](D:/remote/remote/.claude/codex_old_apk_review/run_old_apk_lane_r7_reviewed.ps1)
- SHA-256 `5533a518f9da78a2f747945a2e7913e7dc5ec5e1c7a19c390eef3fd6842437ff`.
- 원본 ace239bf… 기준으로 HEAD를full548로 정확히 확인·tracked dirty면 거부·첫 nonzero/unknown arm 뒤 남은 arm NOT RUN/해당rc 반환을 추가했다. 기존 try/finally의 자기 슬롯 해제는 유지했다. PS parse0 오류.

검증용은 이 수정본 또는 동일한 안전 보정이 적용된 runner로 실행하고 hash를 기록하면 된다. **이 수정과548 gate 정합은 이번 승인에 포함하며 별도 go/재검토 왕복은 필요 없다.** S가 실패하면 원문/cleanup·ended/residual/adb 상태를 먼저 확인한다. 안전 확인 후 P만 독립1회는 같은 허용 범위이며, 실패한 S의 즉시 반복은 아니다. cleanup 미확인이면 다음 arm0이다.

실제 조건은 기존과 같다: SDK pin·단일 시험AVD/package·install/pm clear 성공 전 start0/P NO_CREDENTIAL, APK S50080b2b…·P1a3eee63… 및 되받은 hash, holder65b937cd…/harnessbc78fea0…/Host980ab2cc…/Viewer691889f9…/job a0fd86c0…의 전체 hash, 완전한 절대경로·새 run·Host input none/power off launch/final guard·child EOF/log/job·자기 슬롯을 유지한다.

S는window③④⑤park/⑥reuse, P는window③④/P5d 종료/P6r 새 연결이며⑤⑥ NOT RUN,⑦은P의PC standby 공존이다. P의 무인증 update check1회만 허용하며 운영 로그인/download/install·사용자 앱/Host·clipboard로 확대하지 않는다. 실제 UI/입력 형태가 다르면 원문/dump/이미지를 남겨 해당 단계로 판정하고 handler 직접 호출이나 seam 완화로 통과시키지 않는다.

다음은 검증용의 실제 S/P 결과다. 정상 단계 go를 반복하지 않는다. rel-0164 통합/출하·Android pair 불변·push 보류와 APK0.2.33 별도 보류는 유지한다.

이번 Codex는 고정 source/hash·순수 판정 함수12건·검토용 runner 작성/parse만 수행했다. 실제 Host/Viewer/AVD/adb·GPU/OS 입력·설치·운영·삭제·제품 파일 변경·commit/push는 없다. 미추적 검토 산출물만 작성했으므로 제품 commit은 만들지 않았다. 사용자 현재 조치 없음.
