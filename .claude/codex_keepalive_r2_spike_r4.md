# keepalive-r2 A r4 — SOURCE 재검토

2026-10-26 / 검증용 `remote#yj8mrmrx` / 기존 A task `t-y1lnbgnd` / 입력 `report_keepalive_r2_host_spike.md` r4 전문.

**제품 SOURCE: OK — r3 N1·N2-a/b·N3 보수 수용. ② shared control/클라이언트 구현 연결 GO, NAS 변경·자동 preconnect 출하 OFF 유지. 시험 실행 경로에는 N4(저장소 밖 삭제)를 먼저 보수한다. 독립 EVIDENCE·실 NAT·운영 배포 OK는 아니다.**

대상 **f4ddc9acda48c05f60c73f2ad6cd26674f17caa1**, diff **933814f..f4ddc9a** 전체 12파일. Host `7d0a10c`, 서버 `7ff92d5`·`9bbff8a`, 시험 `f410f7d`, 최종 문서 f4ddc9a. 고정 소스·관련 호출부·새 시험과 계약 문서를 읽었다. `git diff --check` rc0. 현재 ka-r3 worktree HEAD 일치/검사 시 tracked 변경 없음.

## N1 — 최초 pin·이동 후 재증명: 닫음

- reservation에 무작위 128-bit `hostProof`를 보관하고, hostToken으로 인증된 heartbeat의 해당 예약 목적지에만 전달한다. `/api/connect`의 client token/응답, `/api/hosts` 목록에는 이 proof를 싣지 않는다.
- 서버의 unpinned 상태는 **known host IP + 해당 예약의 현재 proof를 담은 Punch**만 받는다. 같은 IP 첫 패킷/가짜 StandbyAck/틀린 proof/다른 lease proof는 pin 전에 거부하며, 인증 뒤에는 확정 tuple만 받는다. IP만 믿던 r3와 다른 경계다.
- `HostAgent::Punch`는 목적지별 packet 복사에만 proof를 채우고, 다른 punch의 authToken을 비운 상태로 둔다. proof 대상은 `authorizedPeers_` 등록에서 제외한다. client admission token을 두 번째 capability로 등록하거나 wire에 실은 변경이 아니다.
- authenticated heartbeat에서 wire tuple 이동을 확인하면 pin을 놓고 **lease별 fresh proof**를 그 heartbeat pendingPunch에 넣는다. old proof는 현재 proof와 불일치한다. 아직 pin되지 않은 예약도 새 proof를 받으며 global heartbeat tuple이 기존 pinned NAT tuple을 매번 덮지 않는다.
- r4 시험은 정상 pin 이후만 보던 r3와 달리 **stranger 먼저 → 정상 proof → cross lease → 이동/old proof 거부 → fresh proof 복원** 순서를 포함한다. mock Host 시험과 실 Host e2e는 아래처럼 구분한다.

기존 문서/주석 중 “첫 datagram이 pin”, “다음 아무 datagram이 이동 후 pin”이라는 옛 설명은 실제 현재 proof 조건으로 맞추면 된다. 실행 의미를 다시 바꿀 필요는 없다.

## N2 — replay digest·입장 순서: 닫음

- `StandbyProof`는 32바이트 SHA-256이고 전체 배열을 비교한다. 계산 실패는 `AuthorizePeer` 이전 silent 반환이므로 capability를 소비하거나 입장시키지 않는다. Windows Host에서 실제 BCrypt 경로를 사용하고 non-Windows는 성공을 꾸미지 않는다.
- full digest·같은 device/gen·입장 endpoint·유효 lease·replay 기간 조건을 충족하는 원 요청만 Replay다. 끝점 이동은 기존 secret Ping 경로로 유지된다.
- 새 admission의 같은 device가 아직 entry를 가진 경우 gen≤admitGen이면 Stale/Superseded, entryId/secret 응답0·현재 entry/secret 불변이다. live entry는 기존 Live 거부를 우선 유지한다. 새로운 gen만 교체하고, entry가 만료/해제된 뒤의 새 입장은 문서대로 별도다.
- 표의 마지막 digest 바이트 차이, digest 실패 시 auth 호출0, gen11 뒤 unused gen10·동일gen 다른proof·정상 replay/gen12 교체 반례가 해당 보수를 겨냥한다. SHA-256 함수의 두 입력 차이 시험 하나를 전체 인증 증명으로 확대하지 않는다.

②의 의무: 동일 device identity를 계속 쓸 때 admission counter는 **host/계정/origin 등 해당 소유 범위에서 재시작을 넘어 단조**로 유지한다. counter는 송신 전에 내구성 있게 예약하며 동시에 여러 operation이 같은 gen을 받지 않는다. 재설치/저장 실패/32-bit 한계는 새 인증된 incarnation 또는 안전한 거부·재입장 정책으로 명시하고, live entry의 작은 번호를 무조건 허용하는 Host 우회는 만들지 않는다. activation counter/entry incarnation과 admission counter를 혼동하지 않는다.

## N3·결정 2 — 상한 양방향·metadata: 닫음

- 신규 legacy lease도 **다른 lease를 drop하기 전에** 호스트의 standby 수를 검사한다. standby8 뒤 새 legacy는 거부하고, legacy1+standby7에서 기존 legacy 교체는 총8로 허용한다. standby reservation은 async bind 전에 계수하므로 그 사이 경쟁도 기존 상한에 포함된다.
- env 상한은 유한한 양의 정수(현재 허용 최대 1,000,000)만 받아 잘못된 입력은 기본값·경고로 간다. NaN 비교로 검사가 없어지지 않는다. “양의 정수이면 무조건 허용”보다 실제 상한 조건을 정확히 기록한다.
- 인증된 read-only `GET /api/hosts`의 `standby.supported/relay` 추가와, **성공한 standby connect** 응답의 `standbyRelay` reserved/상한/bind/not-eligible/relay-disabled 사유를 수용한다. 401/권한 거부·Host offline 같은 실패 응답까지 이 필드가 항상 있다고 확대하지 않는다.
- 클라이언트는 mutating connect 전에 이 metadata를 확인한다. directory 지원은 **개별 Host 지원·admission ready·현재 active owner**의 증거가 아니다. 미지원/무응답·old Host는 현재 수동 연결에 legacy Hello를 던지는 자동 fallback 없이 유한 실패/일반 명시 접속으로 처리한다.
- activation Pong 상관(결정1), lease별 client socket(결정3)은 기존 GO 유지다. wire311~316/104B를 다시 배정하지 않는다.

## N4 — 시험 하네스가 저장소 밖 파일을 삭제함: 실행 전에 보수

새 `apps/directory/test/relay_env_bound_test.js`는 `path.join(os.tmpdir(), 'remote60-envbound-…json')`을 데이터 경로로 만들고 종료 시 `fs.rmSync(dataPath)`한다. 이번 호출을 포함한 기존 `test/run.js`도 os.tmpdir의 `remote60-directory-test-…json`을 시작/cleanup에서 삭제한다. 현재 Windows 기본 임시 경로는 저장소 `D:/remote/remote` 밖이므로, **AGENTS 최상위 “repository root path를 벗어나는 범위는 절대 삭제커맨드를 실행하지 않는다”**에 어긋나는 실행 경로다. 시험용 파일이라는 이유로 예외로 보지 않는다.

Codex는 이 시험/전체 suite를 실행하지 않았고 새 삭제도 하지 않았다. 저장된 suite 완료 로그와 해당 코드 경로를 보면 작업용의 보고 실행에는 이 삭제 경로가 포함된 것으로 판단되며, 삭제한 정확한 대상/실제 존재 여부는 검증용이 실행 기록으로 확인해야 한다. “제품 결함 없음”이나 PASS 숫자로 이 운영 사실을 덮지 않는다. 추가 바깥 cleanup·자료 삭제로 정리하지 않는다.

보수: 이번 검증의 임시 자료를 **D:/remote/remote/.claude/test-tmp/<task별 고유 폴더>**에 두도록 runner와 이 시험의 임시 루트를 연결하고, 최종 정규화/실경로가 그 안인지 확인한 뒤 그 경계 안에서만 정리한다. 독립 실행에서 경유하는 child 시험도 같은 루트를 따르게 하거나, 필요한 KA 시험만 안전한 격리 runner에서 실행한다. 기존 repo 전체 시험을 무관하게 대수술하는 요청은 아니다. C:/work worktree나 os.tmpdir를 새 삭제 허용 루트로 간주하지 않는다.

제품 소스는 그대로 두고 시험 실행 경계만 고칠 수 있다. 이 때문에 Host를 전량 재빌드할 필요는 없으며, 제품 blob 동일성을 확인하고 고정 exe를 유지한다. 검증용은 기존 task 한 묶음에 N4를 반영하고, 이미 실행한 범위에 관해서는 사용자에게 경로·정리 대상·근거를 사실대로 보고한다. **같은 위반을 반복하지 않는 안전한 수정이 가능하므로 사용자에게 바깥 삭제 허가를 다시 요청할 필요는 없다.**

## 근거와 후속 단계

Codex는 저장된 `.claude/caplife/ka_e2e_r4/table.txt`의56/0, e2e_1·2의89/0, `ka_r4_dir_suite.txt`의 ALL PASS와 시험 소스를 직접 읽었다. 숫자·7개 mutant 실패는 **작업용 실행 귀속**이며 Codex/검증용 독립 PASS가 아니다. 9bbff8a는 실제 Host e2e 뒤의 metadata·사유 추가이며 suite는 그 뒤 실행했다는 순서도 유지한다. mock relay 시험의 fresh proof pin과 실 Host e2e를 같은 시험이라고 부르지 않는다.

② shared control/APK 배선과 클라이언트 상태기계 구현은 기존 설계대로 진행 가능하다. **APK0.2.32 picker/SH/UA2 미결이 같은 writer의 우선순위**인 점은 유지하며 이 회신으로 선점/새 병렬 task를 만들지 않는다. 검증용이 기존 A/B task에 다음 담당을 연결한다.

운영/출하 전에는 다음을 실제 고정 후보에서 독립 확인한다.

- N4를 닫은 안전한 runner로 최초 pin/cross-proof/이동·stale proof, SHA 전량·실패, 입장 순서·상한 양방향의 핵심 행동과 부정 대조. 고정 exe/서버 소스·실행 해시를 기록한다.
- ②에서 Pong/HelloAck 역순·한쪽 손실·취소·옛 entry/gen, device counter 재시작, lease별 UDP socket, 실패/무응답 시 bounded 회수. 특히 **Host 이동 후 proof heartbeat 응답이 유실되거나 proof Punch가 전부 유실된 경우**에 client Ping 송신만으로 죽은 lease를 영구 유지하지 않고 재입장/해제를 유한하게 처리한다. proof 없는 상태를 ready로 선언하지 않는다.
- 실 NAT·서버 LAN의 HOST_FACING_IP·바인드/예약 포트의 도달·접근 범위·회수/rollback, held input release 직접 반례, 비용/장시간 유지, 실제 APK·PC 동작. 현재 local proof 성공은 이 근거를 대신하지 않는다.

NAS 서버 변경·방화벽/공유기 변경·자동 preconnect 출하 승인은 아직 아니다. PC0.2.163 게시에는 영향 없다. Codex 산출물은 미추적 판정서/인계이며 제품 수정·빌드·서버 실행·삭제·commit/push 없음. history/계획·Git MCP commit은 기존 작업에서 취합한다. 지금 사용자 조치 없음.
