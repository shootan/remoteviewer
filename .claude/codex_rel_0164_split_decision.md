# rel-0164 — PC/서버 분리와 통합 검증 범위

2026-10-29. 입력 지시서 `.claude/task_rel_0164_r1.md` SHA-256 `13e88b44ed352fc2e4e41434c35eb4f28b9362cfddb90aea1755e06088200f81` 및 구성 단위 판정들을 확인했다.

## ① 분리 허용 — 호환 확인을 전제로 한 PC 먼저 게시

**PC 0.2.164 + directory server를 APK 0.2.33보다 먼저 게시하는 분리를 허용한다.** APK decoder/control 보수와 새 active peer_lost 조사는 계속하며 해당 APK의 보류는 유지한다. 이것은 통합/호환 확인 없이 바로 게시하라는 승인이 아니다. dry-run → 고정 통합 SOURCE/검증용 독립 EVIDENCE → 최종 확인 → 승인된 배포 순서를 따른다. 같은 범위의 정상 릴리스 게시를 위해 사용자 go를 또 요구하지 않는다.

새 Host의 standby는 별도 패킷/entry/activation이며 구 APK의 ordinary Hello/control을 강제로 standby로 바꾸는 기능이 아니다. 서버도 legacy lease와 standby lease를 구분하고 NP의 relayEarly는 요청 단위 opt-in이다. 따라서 **설계상 APK 0.2.32와 공존할 수 있고 APK 0.2.33이 새 PC의 실행 필수 조건은 아니다.** 다만 합쳐진 Host/서버에서 legacy 경로가 유지되는지는 아래 증거로 확인해야 한다. 구성 단위의 SOURCE OK만으로 “게시 APK와 호환 검증 완료”라고 확정하지 않는다.

지시서/안내는 다음처럼 바로잡는다.

- 새 PC client가 구 Host에 연결했다가 목록으로 돌아가면 standby 미지원 안내 후 종료하는 ⓐ 계약은 유지한다. **APK 0.2.32는 아직 새 e 배선이 없으므로 그 APK의 기존 park/resume가 PC/서버 선게시만으로 사라진다고 안내하지 않는다.** APK 0.2.33의 해당 변경은 그 APK 게시 때 안내한다.
- `client_shell_main.cpp:1923`의 `kPrepareRecentOnSignIn`은 현재 **false**다. 최근 3대 자동 준비를 이번 acceptance에 유지하려면 최종 통합 후보에서 ON 값을 명시적으로 고정하고 실제 출하 구성으로 검증한다. OFF 시험 결과로 ON 출하를 대신하거나 dry-run 뒤 flag만 바꿔 무검증 게시하지 않는다. OFF 상태로는 “명시 연결 후 목록 복귀의 유지”만 주장할 수 있다. 자동 준비 독립 검증 전까지 기존 출하 OFF 경계는 유지한다.
- “다시 누르면 즉시 화면”은 실제 picker와 명시 선택 뒤 첫 영상의 측정으로 설명한다. 사용자 선택을 건너뛴 자동 화면/입력 재개를 약속하지 않는다. grant 정리 중 약 12초 상태/안내도 없애지 않는다.
- 지시서 끝의 “서명·배포·push”에서 **push는 보류**로 정정한다. 로컬 main 통합/버전 commit과 NAS 게시는 가능하지만 standing 사용자 지시가 바뀌기 전 git push는 하지 않는다.

## ② 통합 dry-run 뒤 검증용 독립 필수 범위

검증용은 작업용 build와 분리한 위치에서 **같은 고정 통합 commit·출하 flag/UI·runtime server 파일집합**을 빌드/검사한다. 검증 중 제품/flag/병합 해소가 바뀌면 영향받은 lane만 새 hash로 다시 확인한다. 작업용 로그 재집계는 독립 실행을 대신하지 않는다. 각 lane은 rc·유효/INVALID·실행/NOT RUN·원문과 실제 행동을 남기며 아래 수치는 과거 PASS 합계를 재현하라는 뜻이 아니다.

| Lane | 통합본에서 필수 확인 | 주된 이유 |
|---|---|---|
| 빌드/출하 고정 | Release payload 전체, 실제 0.2.164 버전, 출하 marker/고정서버 gate, source/산출물 hash·서명 전 SUMS, 최종 ON/OFF 값과 관련 UI hash | 시험 seam/record-only/임시 endpoint가 출하물에 섞이지 않음 |
| Host 병합 | 실제 HostAgent punch start/stop/restart 시험 + directory retry stage/cycle/barrier 시험; `cycleCv_` notify와 producer/worker stop·join·queue clear 모두 유지 | 같은 directory_client 파일의 KA/dirretry 해소 검증 |
| KA 공용 | table/entry/lease, admission `--product-client --real-directory`의 direct 및 실제 server.js relay 흐름; punch_reply e2e | 동시 entry 무간섭, 일반 Hello/기존 session 보존, activation→같은 socket 복귀, Release/취소, crash·release 직후 재입장 |
| PC 사용자 행동 | shell + managed e2e를 최종 flag/UI로 독립 실행; V1의 두 activation picker 렌더→선택→Presented/WGC 포함 | 계정·logout/end·예약 3대/LRU·4번째 실패·cancel·owner/input fence·동일 lease 복귀가 합쳐진 PC에서 작동 |
| 서버/NP | 전체 directory suite와 relay standby/reply fence/early의 계약 대조, NP 새/구 요청·새/구 서버 e2e | legacy direct/relay grace 보존, 새 요청 direct 우선/T0 grace, stale reply·standby 간섭 차단 |
| 유지보수 | maint deploy sandbox 전체 + gate root/cleanup 시험 | e58bb29와 안전 root 보수가 통합된 자동 게시 경로의 pair-state/unknown·실패 처리 확인 |
| 공존/운영 준비 | 아래 legacy 조합, 준비 0/1/3 자원 관측, 실제 서버 배치/UDP 도달/rollback dry-run | 새 Host·서버가 기존 사용자와 공존하며 실제 게시 환경에서도 standby relay가 도달 |

이 외 모든 순수 시험/모든 과거 mutant를 양쪽에서 기계적으로 전량 반복할 필요는 없다. **각 구성 단위가 합쳐지는 경계와 실제 해소 파일에 해당하는 기존 시험을 한 번씩** 실행하고, mutation은 새 통합 assertion/해소를 증명할 필요가 생긴 경우에 한정한다. UI/안전 guard가 바뀌지 않은 general 8·record·fatal 전량 재실행도 PC 릴리스의 자동 필수는 아니다. 출하 Host의 기본 input/power ON 및 시험 실행 인자의 input none/power off 분리를 source/순수 policy 시험에서 확인한다.

### Legacy/공존 필수 조합

1. **새 서버 + 새 Host + 게시 APK 0.2.32**: 실제 게시 APK hash를 고정한 격리 시험 AVD에서 ordinary 연결/화면 선택/첫 영상·control 진행·기존 목록 park/resume를 확인한다. 새 PC의 standby/Ping이 같은 Host에서 이를 선점하거나 영상/입력을 깨지 않는지 확인하고, 새 PC의 명시 탭 takeover와 다음 명시 폰 연결을 구분한다. real OS 주입/clipboard 시험은 하지 않는다.
2. **새 서버 + 구 PC/폰의 legacy 요청**: login/hosts/connect, direct, relay fallback/재시도·ACK wire 호환을 확인한다. **flag=false 시험을 게시 PC 0.2.163 바이너리의 실제 UI 실행이라고 부르지 않는다.** 게시본 요청/wire fixture 또는 원 Source의 격리 시험 target을 써서 확인한 범위를 정확히 적는다. 0.2.163 client 실제 실행을 포함한다면 자체 창/UDF·파일/입력 격리를 확보한 경로만 사용한다.
3. **새 PC + 구 Host/구 서버 fallback**: standby 미지원/응답 없음·서버 metadata 미지원에서 유한 종료와 명시 cold connect 가능을 확인한다. 현재 안전 규칙상 게시 0.2.163 Host 자체는 input/power gate를 지원하지 않으므로 실행하지 않는다. 승인된 순수 old-host fixture와 old-request/old-server 대조로 채우고 실제 게시 Host 실행은 NOT RUN으로 남긴다. API 미지원이 잘못된 ready/자동 일반 연결·자동 재탈취를 만들지 않아야 한다.
4. **새 PC + 새 서버 + 새 Host**: ordinary 첫 전환과 준비된 lease 재사용을 구분하고, direct와 relay에서 모두 명시 탭·목록·다른 기기 active 무간섭·계정/해제 뒤 재생성 0을 확인한다. 최근 자동 준비 ON이면 최근 3대 준비/화면 0·decoder 0, 이미 active인 상대 무간섭을 최종 구성으로 검사한다.

새 Host에서 구 APK를 실행할 수 있다는 설계 설명이 **현재 알려진 APK decoder/control 결함의 해결**을 뜻하지는 않는다. 이 legacy arm에서 예상 밖 peer_lost/첫 영상 실패가 나오면 새 Host/서버의 기여 여부를 조사하며 자동 PASS로 처리하지 않는다. 원래 구 APK/시험 환경 문제로 한정됐다는 근거가 있으면 PC와 무관한 APK 보수로 남길 수 있으나, 현재 r21/r25 사건을 미리 “PC와 무관한 기존 결함”으로 면제하지 않는다. 안전한 원 Source 시험 build 대조는 정확한 차이를 적고 게시 Host 실행과 구분한다.

### 준비 비용·시간과 실제 경로

최근 3대 ON 출하라면 통합본에서 0/1/3 standby의 process/socket/handle·CPU·memory·네트워크를 같은 구간 기준으로 기록한다. 한정 run으로 **standby3을 10분 이상** 유지해 옛 5분 park를 넘고 증가 추세/entry 누적·영상/decoder/GPU 초기화 0을 확인한다. 이어 active1+standby2에서도 상대 active 무간섭과 해제 뒤 자기 자원 회수를 본다. 이 최소 관측을 장기 soak/모든 실 NAT 검증이라고 부르지 않는다. 예산을 무작정 허용하는 결론 대신 baseline과 증가 추세를 검증용이 평가한다.

standby relay는 각 lease의 host-facing socket을 별도로 bind한다. loopback PASS만으로 NAS 방화벽/NAT/container의 해당 UDP 경로 도달을 가정하지 않는다. 배치 환경/광고 주소·동적 port 도달을 먼저 점검하고, 서버 게시 후 전용 시험 계정/fixture Host를 써 실제 양방향 도달·activation을 외부 경로로 확인한다. **사용자 Host ID/설치본을 시험 takeover하지 않는다.** broad firewall/서비스 권한 완화는 허용하지 않았으며 gnlink로 안 되는 구체 작업만 근거·범위와 함께 연결한다.

기존 input none + power off 두 줄의 launch/final gate·job 종료/EOF·새 repo test root·슬롯·격리 UDF·물리 화면 미접촉을 모든 실제 Host/UI 실행에 적용한다. old-host는 NOT RUN, real4는 REFUSED, viewer_prelogin_login_e2e는 금지 유지다. APK 0.2.32 사용도 승인된 시험 AVD에 한정하며 사용자 물리 폰 설치·사용자 Host/Client 설치/재시작은 하지 않는다. 새 하네스/실행 경계 변경은 고정 SOURCE로 연결한다.

## ③ 서버 배포 — 같은 회차, 서버 먼저

**같은 릴리스 회차에 server를 먼저 검증·게시하고, 구 client 호환/건강 확인 뒤 Windows pair를 게시한다.** 한 번의 원자적 transaction이라고 표현하지 않는다. 준비/서명 등 독립 작업은 병행하되 새 PC가 먼저 널리 배포되어 아직 없는 standby relay에 의존하게 만들지 않는다.

순서:

1. 로컬 고정 통합본의 server runtime 파일·의존 모듈·package/lock·환경 차이·data/schema 호환과 구 서버 hash를 기록한다. 현재 ka-pc와 main의 runtime diff는 server.js 중심이지만 최종 통합본 전체를 재확인한다. test 파일 전량을 운영에 올리는 것과 runtime 의존 전량 확인은 다르다.
2. gnlink 권한으로 기존 서버 파일/설정·Windows/Android pair를 백업하고 rollback을 준비한다. data/schema를 바꾸는 migration은 별도 범위이며 이전 코드 rollback과 live DB 덮어쓰기를 같은 작업으로 묶지 않는다.
3. server를 한 번 교체/재시작한다. in-memory relay·directory session이 끊기거나 재인증될 수 있으므로 게시 안내에 일시 단절/재로그인 가능성을 포함한다. 중복 재시작을 반복하지 않는다.
4. 외부 API/인증·구 요청 direct/relay·새 standby의 실제 배치 도달과 runtime hash/서비스 상태를 확인한다. 로컬 코드가 올바르다는 이유만으로 NAS의 새 경로를 정상이라고 판정하지 않는다. 실패 시 PC pair를 게시하지 않고 서버를 복원한다.
5. 고정 Windows 0.2.164 산출물을 서명·자동 배포하고 외부 HTTPS/manifest 서명·버전·payload hash·서버 상태를 확인한다. **Android 0.2.32 manifest/sig/payload는 변경하지 않고 전후 hash 불변을 직접 확인한다.** dist/로컬 main 기록과 공개 버전 귀속도 맞춘다. push는 하지 않는다.
6. PC 공개 뒤 rollback이 필요하면 새 PC와 구 서버 fallback의 검증 결과를 기준으로 server/pair를 함께 조정하고 안내한다. server만 무조건 되돌리고 새 PC의 유지가 계속된다고 주장하지 않는다. 사용자 설치본 자동 downgrade/종료는 하지 않는다.

이 순서의 서버 교체/외부 확인은 기존 릴리스 범위다. 최종 통합 SOURCE와 검증용 EVIDENCE가 나오면 Codex는 고정 commit·Windows payload·server hash를 최종 확인하며, 동일 범위의 게시 재승인 왕복을 만들지 않는다. 아직 이 파일로 미검증 후보를 게시하지 않는다.

검증용은 위 보정을 **task_rel_0164 지시서 하나**에 합쳐 작업용2 dry-run과 후속 독립 검증을 잇는다. APK 담당의 liveness/control 조사는 별도로 계속한다. 최종 사용자 보고는 PC/서버 반영과 APK 보류·미검증·push 미실행을 구분한다.

이번 Codex는 지시서·고정 source/기존 판정 읽기와 결정 기록만 했다. 제품 편집/build·Host/AVD/서버 실행·삭제·설치·운영 게시·commit/push는 없다. 검토 artifact만 작성했으므로 제품 commit은 만들지 않았다. 사용자 현재 조치 없음.

## push 이견에 대한 근거 확인

검증용의 “docs 단독 push만 보류, 릴리스 회차에는 push” 해석은 이번 세션의 직접 제공 지시와 다르다. 사용자가 이 세션에 제공한 `# AGENTS.md instructions for D:\remote\remote` 본문에는 다음이 명시돼 있다.

> ⚠️ `git push` 는 여전히 보류다. 여기서 "자동 배포" 는 NAS 게시를 뜻하며 push 허가가 아니다. skill·도구의 push 지침보다 사용자의 standing 보류가 우선한다. 배포 문서에는 push 미실행을 명시한다.

이 문구에는 docs 단독 push로 한정하는 조건이 없다. 한편 실제 main worktree의 AGENTS.md:139–140은 “보류는 해제됐다(2026-09-28)”고 쓰여 있다. **디스크 정책과 세션에 직접 제공된 정책이 서로 다르다는 사실을 확인했다.** 로컬 문서가 보류라고 잘못 인용하지 않는다. 이번 판정은 더 최근 이 세션에 직접 제공된 사용자 지시를 따른 것이다. 검증용이 전한 과거 10-01/10-26 발언을 근거로 그 명시적 보류를 자동 해제하지 않는다.

따라서 이 세션 범위에서는 **릴리스 회차에도 push 미실행**을 유지한다. 검토된 main/docs의 로컬 commit과 NAS 게시·외부 확인은 계속하며 이 이견 때문에 빌드/검증/서버·PC 게시 전체를 멈추지 않는다. 사용자가 현재 보류를 직접 철회하면 그 범위에서 적용한다. 정책 파일을 이 판단에 맞춰 임의 수정하거나 같은 이견을 재상신할 필요는 없다.
