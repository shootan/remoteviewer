# APK decoder inbox C r2 — SOURCE NEEDS_CHANGES (N2 잔여 1건)

2026-10-30 · Codex remote#tt7fcvky → 검증용 remote#7fsfy22z

## 고정과 결론

후보는 `C:/work/gnlink-wt/apk-inbox-c`, `feat/apk-inbox-c`, `d50415e94df845911a7d650c928d3bd23f554250`다. 제품 마지막8a40eff와 d50415e 사이에는 CMake/시험만 바뀐 것을 확인했다. tracked clean, `79b5c25..d50415e` diff --check0이다.

직접 확인한 SHA-256:

- packet: `9b4ffb5caf55f9bf157abac1e25d3bf01e6aa47d1c140abb236b91e9ab5831c8`
- 전체 diff: `1215edfc1ddfcaa015a5a802048a48ed08483a3526e267407c44120002b7c2b4`
- r2 diff: `503ccdd91c6679f44d7e6f00ddc0f75c5a041b094c4d688aaca6622cb1214738`
- SUMS: `c75efd66ccc56325b73ad66aae2b4e5d1cb6b8b28615c935da2835661704b99a`

**N1-a/N1-b/N3와 §8(a)의 기본 경로는 확인했다. N2에는 늦은 과거 descriptor가 최신 유효 원인을 덮어 지우는 잔여가 있어 SOURCE NEEDS_CHANGES다.** 아래 한 건을 기존 C task의 N2 보수로 잇는다. 새 설계 task나 전체 시험 반복을 추가하지 않는다. C Android120초 실행은 이 보수본 SOURCE 확인 후로 두며, 독립 빌드 환경/고정 기록 준비는 계속할 수 있다.

## 직접 재검증한 것

고정 blob으로 sink·fake NDK를 빌드해 r1의 같은 반례를 다시 실행했다.

- delta12/13/IDR14가 큐에 있을 때 MarkLoss11 후 첫 Pop은 IDR14다.
- seq100 뒤 과거 delta99를31번 넣어도 admitted0이다.
- pending delta2를 IDR3으로 대체한 뒤 실제 commit하고 정상 delta4를 주면 fake codec 입력은 **1,3,4**, tailGap0, recovery idle이다. r1의 불필요한 새 episode 경로가 닫혔다.

이어 고정 제품 session·sink·shared core/UDP control 소스와 후보의 현재 session 시험을 별도 경로에서 재빌드해 실행했다. **22 checks, 0 failed, rc0/RESULT PASS**다.

- S1의 Host 수신 간격은 첫 요청 대비 **0/1010/3015/7019ms**,4회 후 exhausted, ping gap55ms, 늦은 IDR 회복이다.
- S2/S3/S5/S7/S9의 owner 변경·accepted 취소·held 무효화·동시 publish 대조는 통과했다.
- S6에서 새 pipeline 원인은 보존돼 다음 limiter 허용 때 송신됐다.
- S8은 Host 수신8회→covered→최종 window 뒤 coveredExh이며 추가 요청8→8, 늦은 IDR로 gate 의무/covered가 닫혔다. 여기서 Host가 실제로8회를 받은 것은 이 시험 관측이고, 제품의 상한 정의는 여전히 **limiter 채택8회**다.

따라서 §8을 새4회로 이어받는 정책 변경은 요구하지 않는다. worker가 보고한 old-source/API 불일치, 단일 mutant 생존과 이중 방어, C10의 옛 sink PASS를 구분한 설명도 수용한다. 컴파일 실패를 행위 음성으로 세거나 C10이 옛 sink에서 FAIL했다고 고쳐 쓰지 않는다. N1-a의 순수 제품 queue 반례와 N1-b의 sink 반례를 각각의 근거로 남긴다.

## N2 잔여 · P1 — 늦은 과거 요청이 최신 epoch/episode를 덮음

관련 파일은 **decoder_key_request.hpp의 Merge**, session.cpp의 TakeDecoderKeyRequest→RequestKey 경계, accepted/held의 Merge 호출이다.

r2는 limiter publish와 descriptor merge를 같은 lock에서 처리하고, action take도 묶었다. 이 보수는 맞다. 하지만 descriptor를 **sink에서 취득하는 시점**과 그 lock에 **게시하는 시점**은 다르다. RX와 worker가 각각 취득하므로 먼저 취득한 과거 descriptor가 나중에 게시될 수 있다.

Merge의 “o는 나중에 요청된 것”이라는 가정은 그 호출 경로에서 보장되지 않는다. 현재는 다른 sinkEpoch이면 기존 sink-scoped causes를 지우고 **들어온 값으로 무조건 교체**하며, 같은 epoch의 episodeId도 무조건 덮는다. held version은 snapshot의 무조건 Clear를 막지만, bucket 자체가 과거 값으로 덮인 뒤에는 최신 원인을 복구하지 못한다. S6의 새 pipeline bit는 sink-scoped가 아니어서 이 반례를 덮지 않는다.

### 실제 제품 sink가 만든 descriptor로 직접 재현

제품 sink + fake NDK에서 IDR commit/손실/새 surface를 실제로 구동해 descriptor를 취득했다. 임의 숫자만 만든 가짜 descriptor가 아니다. 다음 결과를 얻었다.

1. 같은 epoch2에서 episode2 요청을 취득해 보류한다. IDR3 commit으로2가 닫힌 뒤 다음 손실이 episode3을 열고 최신 요청을 취득한다. 최신 요청에 과거 요청을 Merge하면 **episode3→2**로 역행한다. 실제 sink의 유효성 판정은 **최신 원본=Recovery(1), merged=0**이다.
2. 새 surface로 epoch가2→3이 된 뒤 최신 요청을 취득한다. 그 위에 epoch2의 보류 요청을 Merge하면 **epoch3→2**로 역행한다. 역시 실제 sink의 판정은 **최신 원본=1, merged=0**이다.

이것은 descriptor 취득 지연/게시 순서를 조절한 제품 Merge + 제품 sink 대조다. 실제 session의 두 thread를 그 순서로 정지시켜 wire 소실까지 관찰한 시험이라고 주장하지 않는다. session의 두 producer가 Take를 ledger 밖에서 하고, held/accepted 모두 이 Merge를 쓰며, merged 원인0을 송신 전에 취소하는 소스 연결로 영향이 확인된다. 예를 들어 두 요청이 limiter에서 거부돼 같은 held에 들어가는 순서라면, 최신 유효 요청도 과거 요청과 함께 취소될 수 있다. recovery는4초 reissue까지 늦어질 수 있고, 일회성 원인은 재발급을 기대해서는 안 된다.

### 같은 N2 안에서 필요한 보수·대조

- 게시 순서를 발급 순서라고 가정하지 않는다. 이 sink의 실제 발급 순서(sinkEpoch, 같은 epoch의 recovery episode)를 역행하는 요청이 최신 상태를 덮지 않게 한다. ledger의 현재 유효 원인을 보존하고, 오래된 쪽의 무효 sink-scoped 원인만 제외한다.
- 새 epoch를 무조건 유지하면서 과거 원인 bit만 새 epoch로 옮겨 붙이는 것도 안 된다. 원인과 그 유효성 식별자를 함께 취급한다. pipeline/legacy 등 독립 원인은 계속 보존한다. owedToken도 다른 유효 의무를 덮어 지우는 경로가 없도록 같은 원칙을 적용한다.
- 최소 순수 대조: 새 epoch→늦은 구 epoch, 같은 epoch의 새 episode→늦은 구 episode, 정상 발급 순서, 중복, 혼합 원인의 일부 무효화. 최신 descriptor의 유효성이 과거 Merge 때문에0이 되지 않고, 과거 원인을 최신 것으로 승격시키지도 않아야 한다.
- 제품 session 대조에서는 과거 descriptor를 취득한 producer를 멈춘 뒤 최신 요청을 먼저 게시하고 과거 것을 뒤에 게시한다. **held와 accepted/coalesce**에서 최신 유효 요청이 보존·송신/회계되는지 확인한다. 기존 S9의 publish 원자성 대조와 구분한다.
- sink query/network를 ledger lock 안에 넣거나 limiter/wire/§8 예산을 바꿔 우회하지 않는다. Source/helper API 변경에 맞는 compile과 관련 회귀를 확인하되 이미 닫은 전체14시험×5를 다시 요구하지 않는다.

이것은 새로운 기능 범위가 아니라, r1 N2의 “다른 유효 요청을 지우지 않는다”라는 기존 완료 조건의 잔여다. 보수 후 고정 delta와 이 대조를 한 패킷으로 잇는다.

## 직접 실행 근거와 한계

`.claude/codex_decoder_liveness_review/c_r2_review.py`와 `c_r2_merge_probe.py`, 산출물 `c_d504_review/`에 소스·빌드·원문·hash를 보존했다.

- probe.log: `3d8a444d7973480d937f9a6bcf25466c5d0e268ec143153e33d38054460e3b1c`
- session_r2.log: `ed303be1974d94f4545e1963ddaae20f359956ac54239f9051b6399e67236b69`
- merge_probe.log: `cf5bdb2685e2880078e20fc31d4963f31aa1af2924d6b26ad6433ae5b7729ec0`

SHA256SUMS.json에 cpp/header/exe도 고정했다. 세션 빌드는 후보의 기존 common.lib를 재사용했으므로 모든 의존성의 독립 재빌드라고 하지 않는다. merger probe rc0은 반례가 관찰돼 프로그램이 정상 종료했다는 뜻이지 제품 계약 PASS가 아니다. Source 검토용 fake NDK/loopback 확인이며 검증용의 최종 독립 EVIDENCE나 실제 Android 관측을 대신하지 않는다. 실제 GNLinkStream/Viewer·adb·AVD·GPU·OS 입력·설치·운영·push는0이다.

## 독립 APK·측정 계획

앞서 허용한 clean 제품 tree 독립 debug 빌드와 자신의 artifact hash 고정 방식은 유지한다. **N2 보수본**에 맞춰 commit/clean·toolchain/인자·BuildConfig/endpoint·signer/ABI·trace/seam/auto-prepare와 실제 설치 hash를 기록한다. 현재8a40eff/d50415e를 최종 승인 source라고 표시하거나 바로 C 실기를 시작하지 않는다.

c_measure.ps1 `fefd6c082d898c79e6c4a5f01faf823a1cf12bfaa94a20b05cbdc6ed0b85126e`의 r2 필드 추가를 확인했다. covered/coveredExh는 상태, asks는 현재 episode의 시도 값이라 시작→끝 차이를 누적 요청 수로 해석하지 않는다. recSent/Host keyframe-request 등 누적·실제 수신 근거와 함께 본다. peakPre0은 미계측, B와 C의 inboxDrops/gapDrops 정의 차이도 계속 명시한다. Source OK 뒤 SH-off120초1회, B1/B2 한정 baseline과 같은 구간의 out 차이/시각·연결/age·요청량을 보는 계획은 유지한다.

PC old-apk lane의 우선순위는 그대로다. 그쪽은 seq16132/16133의 정확한 보정 패치·독립 회귀·고정 commit/runner 조건으로 이미 연결돼 있으며, b11 원본에 대한 무조건 SOURCE OK를 기다리는 상태로 되돌리지 않는다. 제품/추적 문서 변경이 없는 전문 검토라 Codex 제품 commit은 만들지 않았다. 사용자 현재 조치 없음.
