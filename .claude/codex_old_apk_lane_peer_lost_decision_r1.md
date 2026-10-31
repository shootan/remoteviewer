# old APK lane — SH-off S 종료 판정과 다음 확인

2026-10-30 · Codex remote#tt7fcvky → 검증용 remote#7fsfy22z

## ① 관측 분류

**유효한 기능 FAIL이며 S 호환/출하 blocker를 유지한다.** 이번3w는 IME/SH 설정을 잘못 판정한 하네스 INVALID가 아니다. 로그인·IME 숨김·relay ordinary connect·SH requested off·pinned 선택/ack/영상이 실제로 진행된 뒤 Host control channel이 peer-lost로 끝났다. ④~⑦과 P는 NOT RUN이다. 이를 “goldfish라 어쩔 수 없음”으로 바꿔 출하 관문을 닫지 않는다.

고정은 c45fb9b768accaf3ad54561a7766e2d81cf56c86 / runner9deb8057 / S APK50080b2b…다. 원문은 `D:/remote/remote/.claude/worktrees/verify-0164/.claude/oa_runs/oa-s-20261030075939-35052`다. Codex가 result.json·slot/fixture 결과·Host/relay·실패 UI 문구를 직접 읽었다.8개 원문 hash는 `.claude/codex_old_apk_review/peer_lost_c45/hashes.json`에 보존했다.

직접 확인한 점:

- 1b에서 세 가시성 flag false / mIsInputViewShown internal-only, dialog0의 PASS가 기록됐다.
- SH off 공개 설정과 direct/requested off·start0이 기록됐다.
- selected71175314/streamGen2가 applied됐고 first frame/out 진행이 있다.
- Host는08:01:57.806에 `udp control session ended epoch=1 resumed=0 reason=peer-lost`를 남겼다. 이번 reason은 이전 weak run의none과 구분한다.
- relay의50.813Z에는 c2h age1966ms,55.814Z에는 c2h/h2c age18ms·sendErr0이다. 이것은 **패킷 흐름 재개**이며 해당 control head의 올바른 ACK 처리 증거가 아니다.
- slot의 종료 결과는 fixture0/AVD1, residual0/ended_confirmed1이다. capture가 전혀 없어서 끝난 앞1b 실패 run과 섞지 않는다.

3w의 held0.1초는 first frame 직후0.1초 만에 죽었다는 뜻이 아니다. pick/자료 확인 뒤 시작한 watch의 초기 동일 연결 검사에서 이미 종료를 발견한 것이다. out36→36도 그 짧은 검사에서의 값이며, 이를 영상이 전 구간 정지했다는 근거로 쓰지 않는다.

## 원인 판정의 범위

SH-off에서도 구 APK의 RX 동기 codec 생성2초와 ACK 처리 지연 가설은 남는다. 그러나 **이번에는 Host가 peer-lost를 선언했다.** 정상 channel 경로에서 이는 Host가 보낸 미확인 head의 재전송 예산 소진을 뜻한다. 소진된 메시지/streamId/seq와 클라이언트 ACK가 어디까지 갔는지는 기록이 없다. 클라이언트 head가 소진됐다고 단정하거나 앞 weak/A1/A2와 원인이 완전히 같다고 확정하지 않는다.

정상 ping도 모두 Host 로그로 남는 것은 아니므로 seq1은 마지막으로 로그에 식별되는 요청이다. 앱의 정확한 native lastError는 아직 확인되지 않았다. 실패 UI는 이미 HOSTS의 연결중 문구였으며, 이를 정확한 native 실패 이유로 대체하지 않는다.

Codex가 게시0.2.163의 정본 source와 후보를 비교했다. `fae0e1fc787456e852af37af35de7b19f87f01ac`는 verify_0163/final/VERIFY_0163_FINAL.md와 릴리스 계획의 실제0.2.163 고정 ref다. 이 ref와 후보53fced4의 udp_control_channel.cpp/hpp는 **blob이 동일**하다(각 SHA-25671be7daf…/cf7c173c…). poc_protocol의 해당 diff는 Standby kind/구조체 추가이며 기존 control layout 변경은 보이지 않는다. 반면 Host receiver/standby 배선과 directory server는 바뀌었다. 따라서 transport 구현 동일성은 중요한 비회귀 근거지만 **Host/server 전체 비회귀를 증명하지는 않는다.**

## ② 선택과 출하 blocker 종료 경로

선택은 **(b)를 먼저 실행하고, (a)를 출하 blocker를 가를 참조 대조로 준비한다. (c)로 단순 이관해 닫지는 않는다.**

### 지금: P 독립1회

정리와 안전 전제가 확인됐으므로 기존 허용 범위의 P1회를 진행한다. 게시 APK1a3eee63 그대로, SH-off·30초·P-5d/P-6r·standby 공존 매핑은 유지한다. S를 반복하지 않는다.

중요한 한계: **P의 휴대폰 경로는 manual TCP control + direct UDP video다.** S의 directory/relay/UDP control과 다르다. P 성공은 게시 byte와 새 Host의 이 경로 호환을 보충하지만, relay만 제거한 동일 UDP 실험이 아니며 S blocker를 단독으로 닫지 못한다. P의 PC standby는 여전히 fixture directory/relay를 쓴다.

P 전용 검토 runner:

`D:/remote/remote/.claude/codex_old_apk_review/run_old_apk_lane_p_direct_once.ps1`

SHA-256 `1f6c866a7c6c8b686036d0d9d26c6181602b91874e0cf517509ccacbbb72cae8`, parser0 오류다. 인자는 기존 `-CandidateHead c45fb9b768accaf3ad54561a7766e2d81cf56c86 -TraceServerJs <기존 v-traced 절대경로>`다. c45로 제한하고 S loop를 제거한 것 외 Source/3파일hash/5 exe/APK/SDK·plain/trace pin·first-nonzero·slot/finally는 유지한다. P는 env trace0/plain server다. Codex는 실행하지 않았다. **추가 Source/go 없이 검증용이 이1회를 실행할 수 있다.** 실패 시 반복0이다.

### 준비: 안전한 구 stack 참조와 새 stack의 최소 대조

작업용1의 기존 old-apk-lane 지시서 하나에 합쳐 **읽기 전용 비교와 시험 빌드 준비**를 맡긴다. 현재 c45 verify tree를 바꾸지 말고 별도 tree/시험 산출물로 고정한다. 새 구 Host 실행 경계가 생기므로 **안전한 고정 SOURCE 패킷 확인 전 actual old Host/AVD 실행은0**이다.

“구 Host×새 server도 실패”만으로 이번 릴리스가 비회귀라고 할 수 없다. 이번 릴리스는 **Host와 directory server 둘 다** 바꾼다. 새 server 문제가 구 Host에서도 발생할 수 있다. 최소 참조는 다음 두 조합이다.

- **구 조합:**0.2.163 source Host의 안전 시험 build + 게시 전 directory source snapshot.
- **새 조합:**0.2.164 후보 source Host의 동일한 안전/진단 조건 + 후보 directory snapshot.

Host 구 source 기준은 위fae0e1f다. 그 apps/directory와 main a0f991a의 apps/directory는 직접 diff상 동일했고, 구 server.js LF SHA-256은 `fdc9cbe8cdfc0015005a2074f90893099751bae45997429c4542c672dea4af7a`다. 새 server의 원본은2742143d…이고 현 trace 사본은52918c04…다. 운영 snapshot/기록의 hash와 source mapping을 읽기 전용으로 대조하고, source와 실제 게시 상태가 다르면 그 차이를 먼저 명시한다. old-NP용 다른 branch를 “게시 구 server”로 대신 쓰지 않는다. 서버는 의존 모듈도 해당 ref로 고정한다.

두 조합의 휴대폰 APK는 **같은 S fixture build50080b2b**, 같은 SDK/AVD boot·SH-off·target/해상도/fps/bitrate·배경 조건이다. 구 조합은 최소 login→ordinary/relay→선택/first frame→30초 유지까지로 범위를 잡아, 불필요한 구 input-trace/seam backport를 피한다. 새 조합은 그 구간을 맞추고 성공하면 기존④~⑥까지 이어 갈 수 있게 계획한다. 구·신 각1회의 고정 비교 실행 계획을 Source 패킷에 합친다. 지금 전체2×2 교차 행렬이나 자동 반복을 맡기지 않는다.

안전 시험 구 Host의 허용 차이는 **로컬 input/power OFF 효과 경계·동일한 시험 target 격리·header-only 진단**뿐이다. normal UDP/TCP/Hello·Pong/ACK·retry/timing·directory auth·capture/codec 동작은 old source 그대로여야 한다. KA/NP 기능이나 decoder 보수를 구 Host에 합쳐 “구 source”라고 하지 않는다. 실제 게시0.2.163 바이너리의 실행 금지는 유지하며, 이 시험 build를 게시 binary 실행 EVIDENCE라고 부르지 않는다.

구 Host는 생성자/초기화 이전부터 OS input/wake/HostPower 효과를 차단하고 실제 효과값의 두 줄 guard로 확인해야 한다. 단순 로그 문자열 추가나 unknown 옵션 무시를 OFF로 인정하지 않는다. 기존 job/EOF·startup/final guard·소유 run/cache/LOCALAPPDATA/TEMP 경계·사용자 화면 밖 target을 유지하고, 사용자 서비스 설치/입력/전경/clipboard 변경을 일으키지 않는다. real4/prelogin 시험 금지는 그대로다. 배포/사용자 제품 설치/재시작은 없다.

진단은 구·신에 같은 **test-only** 경계를 둔다. 최소 필요 정보는 Host가 닫을 때의 outstanding control head(방향·streamId/message seq·attempts/시간)와 ACK 수신/수락 여부다. relay는 control data/ACK/NACK의 방향과 header 식별을 보존한다. payload·password·credential/token/proof는 기록하지 않는다. 클라이언트 APK를 고쳐 이 비교를 통과시키지 않는다.

현재 relay trace는 bucket을 메모리에 모으지만 정기 snap에는 age만 내고, bucket은 flow close에서 낸다. fixture Node가 먼저 끝나면 필요한 bucket이 남지 않는다. **고정된 local trace 사본에서 종료 전/정기 snapshot으로 bounded header 계수가 보존되게** 하고, 새로운 hash·진단-only diff와 양성/거부 대조를 Source 패킷에 포함한다. socket send 성공과 ACK 수락을 구분한다. 무인증 운영 diagnostic endpoint나 NAS 변경으로 확대하지 않는다.

### 결과에 따른 다음 판정

- 구 조합은 유지되는데 새 조합만 같은 조건에서 실패하면 **이번 Host/server 묶음의 회귀 근거**다. packet/source 근거로 필요한 교차1개를 고르거나 수정으로 잇고, 먼저 게시하지 않는다.
- 구·신 모두 **같은 head/ACK 실패 경계**로 끝나고 old source/안전 차이·부하 근거가 맞으면, 구 APK/AVD의 기존 실패라는 **비회귀 disposition 근거**가 된다. 양쪽FAIL만 같다고 원인이 같다고 하지 않는다. S 원문은 FAIL로 보존하며, P/새 PC 기능·서버 계약·나머지 관문과 함께 최종 PC 게시 가능 여부를 Codex가 판단한다. 이것이 S를 PASS로 소급 변경하거나 자동 게시하는 규칙은 아니다.
- 새 조합이 유지되면 실제④~⑥과 필요한 공존 행동으로 lane의 빠진 항목을 채운다. 한 번 유지됐다는 이유로 앞 실패를 지우지 않는다.
- 두 결과의 부하/진단/안전 차이로 비교가 성립하지 않으면 그대로 미확정이다. 반복 횟수로 덮지 않는다.

실기기에서 실제 old-APK ordinary/선택/영상/control/park가 검증되면 직접 근거가 될 수 있지만, **아직 하지 않은 실기기로 넘긴다는 약속은 이번 blocker의 종료 근거가 아니다.** 현재 물리 폰 설치·조작을 새로 요청하거나 자동 수행하지 않는다. 먼저 승인된 P와 참조 준비를 끝낸다.

## 실행 순서와 보고

P를 먼저 시행하며, 구 참조의 source/안전 준비는 같은 창구에서 잇는다. 이미 승인한 C의 B→C 측정은 이 PC 확인보다 우선하지 않는다. 큰 build와 GPU/AVD 측정을 겹쳐 비교 부하를 흐리지 않도록 검증용이 순서를 조율한다. 구 참조 Source가 아직 준비되지 않았을 때 기존에 승인된 다른 독립 작업을 불필요하게 중단하지 않는다.

다음 패킷은 P 결과와 별도로 고정된 구/신 참조 source·안전 diff·header 진단·각1회 계획을 한 묶음으로 낸다. PC0.2.164/서버 게시는 현재 보류이고, C 성능 hold와는 별개 사유다. Codex는 로그/hash/source 판독과 P runner 작성만 했으며 actual Host/Viewer/adb/AVD·GPU/OS 입력·설치·배포·push는0이다. 미추적 전문 검토라 제품 commit은 만들지 않았다. 사용자 현재 조치 없음.
