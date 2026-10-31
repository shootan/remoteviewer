# 구·신 참조 SOURCE r1 — 정확한 최초-loss 보정 포함 조건부 OK

2026-10-30 · Codex remote#tt7fcvky → 검증용 remote#7fsfy22z

## 판정

패킷 b029ed9a의 안전 이식·test-only 진단·구/신 directory snapshot·to-3w 범위는 수용한다. **단 `lost`가 최초 소진 순간을 보존하도록 아래 정확한 보정을 적용한 Source에 조건부 OK**다. 현재ff98f19/7f6c7fe 및305e870c/7d1cd935 그대로의 실행은 승인하지 않는다.

보정 후 두 tree의 고정 commit·진단 단위7/7·새 Pinned host hash/flag·tool4개 고정 기록을 검증용이 확인하면 **추가 SOURCE/go 없이 구→신 각1회**로 이어갈 수 있다. 새로운 구 Host OS 효과 경계의 source를 검토한 승인이다. 실제 게시0.2.163 바이너리 실행 금지·출하 HOLD는 그대로다.

Codex가 네 diff를 git에서 독립 생성해 제출본과 byte 정규화 후 모두 일치함을 확인했다. 대상은 구 fae0e1f→5572856→ff98f192ad4aca617a28ead8912ac5f2bf6e50aa와 신 c45fb9b→7f6c7feacd184f451a18e6cfa2b4702661609a94다. Source peer-lost 판정과 별개인 P startup120초 보수는 이 후보에 섞지 않는다.

## 제품 파일의 test define에 대한 판단

**제품 파일에 있다는 사실 자체는 blocker가 아니다. 문자열diag0만으로도 충분하지는 않다.** 다음을 함께 확인했다.

- REMOTE60_CONTROL_CLOSE_DIAG는 CMake에서 PinnedSelectTest 및 진단 단위 target의 PRIVATE definition으로만 켠다. Pinned host는 product source 목록을 새 target에서 직접 컴파일하므로 flag가 main/control/channel TU 전체에 적용된다. 제품GNLinkStream target의 생성 프로젝트에는 그 flag가 없다.
- 고정 header로 API 존재 여부를 컴파일했다. macro OFF에서는 CloseDiagLine 없음, ON에서는 있음이고 둘 다 성공했다. 조건부 필드도 OFF에서 빠진다.
- 이 macro 부분을 제거한 header의 token은 원제품53fced4와 정확히 같다. cpp도 r1의 braced early-return을 동등한 원형으로 정규화하면 제품 token이 같다. 보정도 macro 내부라 이 성질을 유지한다.
- 작업용의 실제 제품diag0/seam0, 시험diag1/seam1/두 OFF 문자열 검사와 함께 본다. 최종 정식 Release에서도 `diag{ack seen=` 및 새 `close-diag-first-loss-v1` marker0을 필수로 확인하며, 문자열 검사 하나를 ABI/동작 무영향 증명의 전부로 대체하지 않는다.

기존 return조건을 바꾸거나 retry/ACK 동작을 구 Host에 backport한 것은 없다. product macro OFF의 무영향 확인과 시험 target에서의 작은 계수/clock 비용은 구분한다. 구·신 시험 진단 소스/단위는 동일 blob이고 같은 비용을 갖는다.

## 안전 port 확인

구 main은 parse→정책 검증/출력→HostPowerKeepalive(enabled)→startup 순서로 바뀌었다. immutable enabled=false일 때 ctor/dtor/SetStreaming의 실행상태·wake가 모두 차단된다. power parser/정책 줄은 후보 방식과 맞고, 잘못된 값은 객체 생성 전65로 거부한다. 그 외 protocol/auth/capture/codec을 바꾼 이식은 없다. 본 계획의 명시 argv 경로를 기준으로 수용하며 모든 임의 profile의 동등성까지 주장하지 않는다.

input none 분기는 기존 구 source와 같고, staged folder에는 Host exe만 있어 secure input service sibling을 새로 설치하는 경로가 없다. broker는 missing executable이면 SCM 접근 전에 반환하는 것을 읽었다. 노출 전 OFF 두 줄/같은 run target과 기존 job/EOF/최종 guard가 여전히 필요하다. source 수용이 launch argv/실효 상태 검사를 생략하는 허가는 아니다.

Codex는 이 class의 OFF와 ON을 OS API spy로 대조했다. **보수 후 실행 전 import 검사를 통과한 probe**에서 OFF effects0, ON은 record exec4/input2/post1, rc0이었다. 실제 Host/Viewer/AVD를 실행한 결과가 아니다. 이 항목에는 아래의 보수 전 검토 사고가 별도로 있다.

## 필요한 한정 보정 — `lost` 최초 값 보존

원본 Tick은 닫힌 뒤에도 다시 호출될 수 있다. 정상 channel 동작은 기존과 같지만 r1은 그때마다 lostAttempts/lostAtUs를 덮는다. Codex의 메모리 대조에서 최초 `lost attempts4 waited11ms`가 추가Tick 뒤 `attempts5 waited16ms`로 바뀌었다. 과부하 중 Serve 종료 로그가 늦으면 최초 손실 시각/예산을 잘못 해석할 수 있다.

보정은 **진단 필드를 최초1회만 저장**하고, head의 기존 시도 증가/닫기 등 transport 동작은 그대로 둔다. `head`는 snapshot 당시 값, `lost`는 최초 소진 값이므로 이후 head6/lost4가 나오는 것은 올바르다. 별도 버전 marker로 이전 시험 binary를 구분한다.

- 패치: `D:/remote/remote/.claude/codex_old_apk_review/oldref_r1/first_loss_approved.patch`
- SHA-256: `23642cfce9327b43f0754b98dad421901bb0fe40577c8bd23d04c72432de5f03`
- 적용 후 udp_control_channel.cpp canonical SHA-256: `8705e92bc3097f865c4b74653485c9e03cd62476a9d5835fe42d1e52f0ad8b00`
- 적용 후 udp_control_close_diag_test.cpp: `eae00e2cfbc42291e7887b3eccfe5982132367b3e30e52455997ff5c863482ab`

구·신에 같은 패치를 적용한다. 두 tree에서 apply --check가 성공했고 직접 적용은0이다. 원본6건과 반복Tick 반례를 확인한 뒤, 실제 채널 소스의 보정으로 **7/7 rc0**을 직접 재빌드·실행했다. payload body가 나오지 않는 조건도 유지됐다. source/Header/API guard 확인과 메모리 채널일 뿐 실제 socket/Host/AVD 실행은 아니다.

## relay·도구 범위

고정 apply_diff 함수를 사용해 구 fae server→196405dd…와 신53fced4 server→18f6b6d2…를 직접 생성·hash 대조했고 Node syntax도0이었다. Node server는 실행하지 않았다. runtime 의존7파일은 구·신이 동일하다. Source/NAS8파일 mapping과 host-facing standby 전용 env가 ordinary S에 영향 없다는 보고는 제공 근거와 source 범위대로 수용한다. old-NP의 다른 ref를 게시 구 server로 바꾸지 않는다.

정기 snap이 완료된1초 bucket과 bucketsThrough를 내는 변경은 필요한 보수다. 마지막 미완료 초와 다음 sweep 전 tail은 누락될 수 있으므로 watermark/시각을 함께 보고한다. packet kind와 내부 wrongStream/accepted/stale 계수는 유용하지만, 현재 출력에 모든 streamId/ACK 이력이 있는 것은 아니다. 단순 age나 seen만으로 matchingACK가 수락됐다고 하지 않는다. afterClose도 로그 snapshot까지의 값이지 이후 전체 수명 값은 아니다.

to-3w는 실제30초 유지 뒤에만0으로 끝내고④~⑦을 scope NOT RUN으로 표시한다. 신full은 그뒤 행동을 그대로 수행한다. 검증용 pick76/76·orchestration26/26 보고는 별도 귀속으로 수용하며, 이 테스트 분기 자체는 보정하지 않았다. 구/신 비교 실기는 기존job로 보호된 oa_slot 경로만 사용한다. 별도의 임의 Node fault/check 실행을 이 승인에 추가하지 않는다.

## 실행 준비와 runner

검증용이 아래를 고정하면 별도 go를 기다리지 않는다.

1. 구ff98/신7f 위에 위2파일 패치만 적용해 각각 clean commit으로 기록한다. 다른 policy/문서/기능을 같은 runtime 후보에 섞지 않는다. 독립7/7 및 flags/Source mapping을 확인한다.
2. 두 Pinned target만 보정 Source로 다시 빌드하고 새 raw hash·build 인자·Source commit을 기록한다. 공유 tool4개는 R1의 동일 파일을 재사용한다. 새 build 조립 폴더를 만들고 기존 R1 폴더/파일은 덮지 않는다. 제품diag0/seam0 근거는 Source OFF 동일성과 함께 유지한다.
3. 시작 전 큰 build가 겹치지 않는 상태, SDK/실제 boot_id/listener·guest load를 고정30초 읽어 기록한다. 수치가 낮아질 때까지 반복하지 않는다. 각 arm의 실제 run·cleanup 자료를 보존한다.

runner: `D:/remote/remote/.claude/codex_old_apk_review/run_old_new_reference_once.ps1`

SHA-256 `a1fe019c6a7a4ba9a112bdb7ffefab1f227c1f36968de6008a67a8dd5cd9d649`, PS parser0 오류이며 실행하지 않았다. **한 호출에 한 ref**이고 기본 순서는 old 다음new다. 기존처럼 공백 없는 root/build/server 경로만 받으며, launcher 결과 미확인은 성공0으로 바꾸지 않는다.

주요 인자:

- `-Reference old|new`
- `-LaneRoot <7f+패치 tree>` / `-OldRoot <ff98+패치 tree>` (기본은 기존 C:/work/gnlink-wt/old-apk-lane, old-ref-163)
- `-LaneHead <새 full commit>` / `-OldHead <새 full commit>`
- `-BuildDir <LaneRoot/.claude/oa_ref_build 아래 새 조립 폴더>`
- `-HostSha <그 ref의 새 host raw64자리 hash>` / `-ServerJs <LaneRoot/.claude/oa_relay_server 아래 해당 ref 사본>`

새 head의 parent 계열·patch2파일만 변경·기존23 Source pin 및 보정2파일 hash·tracked clean을 확인한다. 공유4tool pin, rebuilt host raw hash와 `close-diag-first-loss-v1`/diag/power/seam marker, 각 server raw hash·의존7파일 pin도 확인한다. 그러므로 R1의305e/7d binary는 새 버전 marker가 없어 거부된다. 새 host hash를 인자로 받는 것은 검증용의 rebuild/Source 기록을 고정하는 방식이며 mapping 없이 임의 binary를 허용한 것은 아니다.

old는S/to-3w/196405dd, new는S/full/18f6b6d2다. APK50080b2b·SDK·input/power guard·job/EOF를 유지하며 standby/P를 구 ref에서 실행하지 않는다. 이 runner는 자기 CreateNew slot을 가지므로 sh_run으로 중첩하지 않는다. cleanup 확인이 없으면 slot을 보존하고 다음 실행을 금지한다. force 여부/결과 유효성과 cleanup 확인은 별개다.

**구가0이면 신1회로 이어 간다. 구가3w의 실제 기능 FAIL이어도 setup/guard가 유효하고 종료·잔여0·EOF/최종OFF가 확인되면 신을 독립1회 실행하는 것은 같은 승인 범위다.** 첫 nonzero를 자동으로 무시하는 loop는 넣지 않았다. 구의 setup INVALID·안전/정리 불명·3w 미도달이면 신은 NOT RUN으로 두고 그 원인을 먼저 잇는다. 구 재시도·전체2×2 행렬은 없다.

old to-3w PASS 후 하네스의 의도된 force-stop/정리 때문에 생긴 late peer-lost는 비교 구간의 실패로 세지 않는다. watch·의도된 종료·최초 lost 시각을 맞춰 해석한다. 실제두run의boot/background가 다르면 그 제한을 명시한다. 새full이 유지되면④~⑥으로 기존 scope를 채우되 실패 이후 추가 입력0이다.

P startup 보수가 아직 Source 전이면 이 참조 준비/허용 실행은 독립적으로 이어갈 수 있다. PC 검증을 C 성능 측정보다 우선하고 큰 build/GPU측정의 순서는 기존 검증용 창구에서 조율한다. 두 결과 뒤 비회귀/회귀 disposition과 PC 게시 여부를 판단하며 현재 출하는 HOLD다.

## 별도 검토 안전 사고 — 누락하지 않음

**Codex 작성 power spy의 최초 ON 실행에서 PostMessageW hook을 누락해 09:32:58에 모니터 깨움 broadcast1회가 실제 발생했다.** 실제 SendInput/실행상태 요청은 spy로 차단됐고 사용자 화면의 가시 영향은 미확인이다. 이 실행은 안전 PASS에서 제외했다. 원본 cpp/exe/log/import를 보존했으며, 수정 후에는 실행 전 실제 OS효과 API import0을 강제해 OFF0/ONrecord-only를 재확인했다. worker port의 잘못으로 돌리지 않는다. 정본 `.claude/codex_oldref_review_safety_finding.md`, 검증용 안전 통지seq16240과 연결한다.

이번 Source의 나머지 직접 확인은 소스·hash·대역/메모리·컴파일 검사뿐이며 actual Host/Viewer/AVD/기기설치/서버배포/push는0이다. 위 사고를 이 문장으로 지우지 않는다. 제품/추적 파일 변경 없는 전문 검토라 제품 commit은 Codex가 만들지 않았다. 사용자 현재 조치 없음.
