# APK decoder inbox C r3 — SOURCE OK

2026-10-30 · Codex remote#tt7fcvky → 검증용 remote#7fsfy22z

## 고정과 판정

**SOURCE OK:** `feat/apk-inbox-c`의 `2b492ef92b899394f08f4b6fcc46fdd33309baea`, 기준79b5c25. tracked clean과 전체 diff --check0을 직접 확인했다. d50415e 이후 제품 동작 변경은 DecoderKeyRequest::Merge이며, session의 시험 API 추가는 실제 컴파일 가드 안이다.

직접 확인한 SHA-256은 다음과 같다.

- packet: `ed29592348dfd8d1b2a06b33e0dea30e147ecfad4dd5ddba3ed9ac87cca72e4f`
- r3 diff: `650ed92404840154ce2deda4730cf3e73cafab62196e286284533de54011bbb4`
- 전체 diff: `05e50767cd656f683c4bd8676a60b21cc3124dd7cb582761579e8c53813c2107`
- SUMS: `722f6f1bc711ccbc53702d03a49aa6a2806e3301b851530e8bf98c39f65aa374`

이전 N2의 늦은 과거 요청 덮어쓰기 잔여를 닫는다. N1/N3/§8(a)의 r2 판정도 유지한다. **검증용은 이 clean tree의 독립 plain debug APK 빌드와 고정 hash의 SH-off120초1회로 진행한다. 별도 Source/go나 새 시험 관문을 추가하지 않는다.** 실제 Android 효과/EVIDENCE 및 APK0.2.33 출하 승인은 아직 아니다.

## Merge 보수 확인

새 Merge는 게시 순서를 발급 순서로 오인하지 않는다. sink-scoped 원인과 식별자는 더 새 sinkEpoch를 유지하며 과거 epoch의 원인을 최신 것으로 승격시키지 않는다. 같은 epoch의 recovery는 더 새 episodeId를 유지한다. owedToken은 별개로 새 의무를 보존한다. pipeline/legacy는 독립 원인으로 남는다. lock·limiter·wire·§8 예산을 바꾸지 않은 범위가 맞다.

Codex가 r2와 **같은 제품 sink + fake NDK 순서**를 고정2b492ef로 재빌드·실행했다. descriptor는 실제 손실/IDR commit/새 surface에서 취득했다.

- 같은 epoch2: episode3에 늦은 episode2를 합쳐도 merged episode3 유지, 실제 sink 판정은 **validNew=1 / validMerged=1**이다(r2는 merged2/valid0).
- 새 epoch3: 늦은 epoch2를 합쳐도 merged epoch3 유지, **validNew=1 / validMerged=1**이다(r2는 merged2/valid0).

기존 MarkLoss11→첫 Pop IDR14,31회 과거 seq 거부, pending IDR3 commit 뒤 정상 delta4 입력도 같은 고정 소스로 재확인했다. codec input은1/3/4, tailGap0/recovery idle로 이전 보수가 유지됐다.

## 제품 session 직접 재빌드·실행: 26/26

현재 고정 시험과 제품 session/shared core/UDP control/sink 소스를 별도 검토 폴더에서 빌드해 **26 checks,0 failed, rc0/RESULT PASS**를 직접 확인했다.

- **S10 accepted:** 새→늦은 과거 descriptor가 coalesce돼도 Host1회 수신, droppedStale 증가0이다. 로그의 `host requests=2`는 기준 시각부터2ms에 받은1회를 뜻하며2회가 아니다.
- **S10 held:** 먼저 pipeline이 interval을 쓰고 두 descriptor가 held에 들어간 뒤, Host는 pipeline과 보존된 요청을 각각 받았다(3/2008ms), droppedStale 증가0이다.
- S1 recovery 송신은 첫 요청 대비0/1003/3014/7014ms의4회와 exhausted/늦은 IDR 회복이다. 이 run의 ping gap326ms였다.
- S2/S3/S5/S6/S7/S9 취소·owner/held/publish 원자성도 통과했다.
- S8은 limiter 채택8회에 대응해 Host도8회 수신했고, covered→coveredExh에서8→8 유지·늦은 IDR 회복이었다. 제품 상한의 정의를 “실제 wire8회 보장”으로 바꾸지는 않는다.

S10은 test thread가 게시 순서를 정해 실제 RequestKey/held/accepted/limiter를 구동한 대조다. 자연 경합 빈도를 실기에서 측정한 결과로 확대하지 않는다. pure8 및 inbox_c44, 이전 Merge로 먼저 FAIL한 작업용 결과는 그 보고에 귀속한다. Codex가 그 전체14시험을 독립 재실행했다고 하지 않는다. 관련 경계 확인이 끝났으므로 전량 반복을 요구하지 않는다.

## TestPublishKey 관찰에 대한 판정

“컴파일 가드 없이 제품 헤더에 있다”는 관찰은 **고정 후보에서는 사실과 다르다.** native_video_client_session.hpp **242행 #ifdef REMOTE60_CLIENT_SESSION_TEST_HOOKS부터287행 #endif까지**가 TestConfigureKeyframeLimiter·TestPublishKey·TestRequestKey를 모두 감싼다.

주석만 믿지 않고 실제 헤더의 member 존재 여부를 SFINAE/static_assert로 컴파일했다.

- 해당 macro 없는 제품 모드: 세 API가 **모두 없음**, compile rc0.
- macro 있는 시험 모드: 세 API가 **모두 있음**, compile rc0.

Android app/build 파일 검색에서도 그 macro 정의는 없었다. 이 판정은 현재 빌드 설정과 전처리 경계의 확인이며, 아직 만들지 않은 APK 바이트를 검사했다고 주장하지 않는다. 추가 guard 보수는 필요 없다. 독립 APK의 인자/BuildConfig·시험 flag 기록은 예정대로 남긴다.

## 직접 근거와 한계

실행 도구는 `.claude/codex_decoder_liveness_review/c_r3_review.py`, `c_r3_merge_probe.py`이며 고정 산출물은 `c_2b492_review/`다. 소스·exe/object hash는 SHA256SUMS.json에 보존했다.

- 기존 반례 재실행 probe.log: `93faadaec4cc5b75333fa62de457accf1d13f37b0ce8ee21491e85130c81f0d8`
- session_r2.log: `9269c12b3e8ce951da87924a43e3aff4e7039c7e776163a80f700a27534938f8` (파일 이름은 재사용됐지만 내용은 고정2b492ef의26건이다.)
- Merge 대조 merge_probe.log: `c4dc1b7daad7555f6935678efac25054cdbdc0a656f497e98ccb2ced28fb4542`
- guard의 두 모드 compiler log와 object는 같은 폴더에 있다. 두 log의 본문은 compiler가 파일명만 출력해 같지만 compile flags와 static_assert 조건이 다르며 둘 다 성공했다.

session은 후보의 기존 common.lib를 재사용했으므로 모든 의존성을 독립 재빌드했다고 하지 않는다. fake NDK/loopback 확인이다. Codex 실제 GNLinkStream/Viewer·adb·AVD·GPU·OS 입력·사용자 설치·서버 배포·push는0이다. worker/verify worktree의 제품 파일도 수정하지 않았다.

## 다음 실행 — 이미 허용된 범위

검증용이2b492ef clean tree에서 만드는 **자신의 plain debug APK**를 사용한다. Source commit/clean·toolchain/명령/속성·BuildConfig/endpoint·signer/ABI·trace/seam/auto-prepare·실제 설치 hash를 기록한다. byte 재현을 강제하지 않지만 hash 교체만으로 source 동등성을 주장하지도 않는다. batch 변경은 그 고정 경로/hash/기록 범위다.

기존 C batch·r12 하네스·SDK pin·30초 adb 안정 확인·KE_PLAN=e2-shoff/KE_WATCH_SEC120·로컬 relay trace·첫 nonzero 중단·기존 input/power/EOF/job/slot gate를 유지한다. PC old-apk lane의 다음 r7i 슬롯이 우선이며, 그 뒤 C1회를 배정한다. 추가 A/B/weak arm이나 사용자 프로그램/adb 서버 재시작은 이번 범위가 아니다.

EVIDENCE는 연결·영상 진행/같은 구간 out 차이·age/지연·요청·자원 상태를 함께 본다. 요청 감소만으로 성공/대역 개선을 확정하지 않는다. B1/B2는 한정 baseline이다. inboxDrops/gapDrops의 B/C 정의 차이, peakPre 미계측, asks의 episode-local 값과 covered 상태값을 누적 counter로 오해하지 않는 조건도 유지한다. 기본 SH-on·실폰·비용·최종 통합 관문은 이1회로 닫지 않는다.

결과와 원문/해시를 한 패킷으로 받아 구성 단위 EVIDENCE를 이어 판단한다. 제품/추적 문서 변경이 없는 전문 검토이므로 Codex 제품 commit은 만들지 않았다. 사용자 현재 조치 없음.
