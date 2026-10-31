# home-to-company-connect / 서버 relay 주소 결합 / Codex 설계 검토

2026-10-11 KST. **서버의 advertised IP와 wire IP 혼용 결함에는 동의한다. 우선 서버만 수정한다. 추천안은 수신 주소를 일괄 공인 IP로 바꾸는 대신, connect 시 relay용 실제 관측 wire IP를 따로 저장해 punch와 Hello 모두 그 키를 쓰는 것이다.** LAN relay 후보 추가는 이번 보수에 포함하지 않는다.

이번 범위는 읽기 전용 진단·설계와 이 검토 기록이다. 제품/서버 파일 수정·서비스 재시작·배포·사용자 네트워크 전환은 실행하지 않았다. 다음 구현·독립 검증 연결은 검증용의 기존 단일 창구로 진행한다.

## 1. 직접 확인한 사실과 아직 추정인 것

NAS `gnlink@192.168.0.6`로 읽기만 수행했다. `claude` 계정 접속은 공개키 거절이었으며, 저장소에 지정된 gnlink 배포 키로 허용된 읽기를 수행했다. 키 내용·환경 전체·자격 저장소는 읽거나 출력하지 않았다.

- 실행 서비스: `remote60-directory.service`, PID 3142968, 실행 경로 `/opt/gnlink/remote60-directory/server.js`, 시작 2026-09-30 14:58:02 KST.
- NAS 파일 SHA256: **`8fde4171bd4c494b93ea1c40bb440e19784369e63b231e92884285badc2afb82`**. `git show 236b858:apps/directory/server.js`의 저장소 바이트 SHA256과 일치한다. 따라서 아래 줄 번호는 출하 서버와 대응한다.
- journal 10-11 09:47:08 KST: observe `192.168.0.1:20912 → advertised 175.207.45.151:20912`, connect `94bc048e`에 relay eligible 발급, host `df15b73b` wake 전송. 약 408ms 뒤 host heartbeat가 capability를 수령했다. 이후 connect `aa0b2061`, `2a45947e`, `ba865271`도 같은 모양이다.
- Home의 NAS viewer.log에서 네 connect 모두 0.2.156, candidates=4, `private,public,public-alt,public`, 약 4.03초 뒤 `punch answered=0 / no candidate answered`를 직접 확인했다. 같은 날 다른 host에 대한 private 성공 기록도 있다. viewer와 NAS 시각은 수 초 차이가 있으므로 **connectId로 결합**했다.
- 배포된 함수도 직접 확인했다. `observedAddressFor(:314)`는 local-subnet 주소를 PUBLIC_IP로 바꾸되 `wireIp/wirePort`를 보관한다. `handleConnect(:1992, :2028, :2035)`는 advertised IP를 relay token/eligibility에 저장한다. 반면 punch `:1799`는 raw `rinfo.address`로 조회하며 **Hello `relayBindSession(:1694)`도 raw IP와 token의 advertised IP를 비교**한다.

따라서 **punch만 고치면 Hello에서 다시 거절**된다. 이 두 지점과 발급 지점은 한 보수로 다뤄야 한다.

다만 현재 listener는 자격 없는 punch를 로그 없이 버린다. “relay punch accepted 로그 없음”만으로 패킷이 listener까지 도달한 사실까지 증명되지는 않는다. 소스 결함은 확정이고 현장 증상과 매우 잘 맞지만, 이 사건의 모든 UDP 실패가 그것 하나 때문이라는 최종 판단은 보수 전/후의 수신·거절/수락 관측 또는 연결 성공으로 닫는다.

직접 경로의 “hairpin 포트가 외부 매핑과 다르다”, “회사 NAT 제한이 직접 원인이다”는 아직 가설이다. 관측 대상 endpoint에 따라 NAT 매핑 특성이 달라질 수 있으므로 observe 성공만으로 모든 외부 peer 경로가 보장되지 않는다. [RFC 4787 주소·포트 매핑 구분](https://www.rfc-editor.org/rfc/rfc4787.html#section-4.1). 이번에는 직접 후보 보정 정책까지 함께 바꾸지 않는다.

## 2. 선택할 수정안: policy/advertised와 relay wire 분리

기존 fresh OBSERVE 기록이 이미 두 주소를 가진다. 이를 발급 시 목적대로 나눠 사용한다.

| 사용처 | 사용할 주소 |
|---|---|
| 기존 RELAY_ALLOW_IPS 판정, 외부 peer에게 주는 client 후보, pendingPunch | 기존 `observation.obs.ip/port` (advertised) |
| relay용 Hello capability에 결합할 source IP | **`observation.obs.wireIp`** |
| relay punch eligibility map의 key | **같은 `wireIp`** |
| punch 조회, Hello token의 source IP 검사 | 수신한 **`rinfo.address`**, 위 wire key와 정확히 비교 |
| relaySessionByClient key, 세션의 clientIp/clientPort, replied 중복 방지, 실제 reply/forward 목적지 | **relay에서 실제 받은 `rinfo.address:rinfo.port`** |
| host leg의 송신/인덱스/인증 heartbeat에 따른 이동 | 기존 `hostSendTargetFor` 및 실제 host wire tuple 경로 유지 |

구현 시 token record 필드는 `clientWireIp`, 정책용 진단은 `clientAdvertisedIp`처럼 의미를 분명히 한다. 기존 `clientIp`를 재사용한다면 저장/검사/로그 전부 같은 의미로 바뀌었는지 확인해야 한다. **Hello의 비교를 없애는 방식은 금지**다.

두 주소 모두 서버가 받은 OBSERVE의 기록에서만 가져온다. HTTP body의 IP, X-Forwarded-For, client가 보낸 localIps, Hello의 자기 주장으로 auth wire 주소를 채우지 않는다. 현 서버는 OBSERVE마다 wire 값을 기록하고 재시작 시 관측 map이 비므로, 새 레코드에 wire가 없으면 재관측하게 한다. 불명확한 경우를 `wire || advertised`와 같은 무조건 fallback으로 숨길 이유가 없다.

**관측 port와 relay source port를 동일해야 한다고 새로 제한하지 않는다.** observe와 relay는 다른 목적 endpoint이고 port 매핑이 달라질 수 있다. 기존 계약대로 유효 token과 source IP가 맞는 Hello가 도착했을 때 그 실제 port를 세션에 결합한다. 이후 media는 그 실제 endpoint 인덱스로만 수락한다.

예시 사건은 policy/advertised `175.207.45.151`, relay auth/eligibility wire `192.168.0.1`, bound client는 실제 relay 패킷의 `192.168.0.1:<relay에서 본 port>`가 된다. 응답을 advertised public tuple로 되돌리지 않는다.

## 3. 제안된 다른 안과 신뢰 경계

### observedAddressFor(rinfo).ip로 수신 키 정규화

punch와 Hello 양쪽에 일관되게 적용하면 해당 불일치를 해소할 수는 있다. 하지만 `onServerLan`은 **특정 라우터 192.168.0.1만이 아니라 서버에 붙은 모든 비-loopback IPv4 subnet**을 검사한다. 이를 auth 비교에 직접 쓰면 서로 다른 LAN source IP와 서버의 public IP가 모두 같은 주소 범주가 된다. “같은 advertised IP”가 “같은 wire source”로 승격되는 셈이다.

token·계정·host 검사까지 없어지는 것은 아니므로 즉시 무인증 접속 허용이라고 과장하지 않는다. 그래도 기존에 보관한 구체 wire IP로 해결 가능한데 그보다 넓은 동치관계를 새로 만들 필요는 없다. **추천하지 않는다.** 양쪽 IP 키를 모두 등록하거나 `raw OR normalized`로 수락하는 안도 같은 이유로 기본안에서 제외한다.

wire 방식에서도 hairpin NAT 뒤 여러 장치가 라우터 IP 하나로 보이는 한계는 있다. 인터넷의 공유 NAT와 마찬가지로 IP는 개인 신원이 아니다. **punch 응답은 경로 탐색에 불과하며, 실제 session/lease/forward는 128-bit Hello capability + TTL + 최신 host token + host/account 활성 조건을 통과한 뒤에만** 만들어야 한다. 기존 direct-grace, replied 중복 억제, TTL 청소를 유지한다. 이 보수로 allowlist를 `*`로 넓히거나 사설 IP 전체를 신뢰하지 않는다.

주의할 잔여 경로: OBSERVE는 LAN 직접 경로, relay는 public hairpin처럼 **두 서비스에 보이는 source IP 자체가 달라지면** 정확 wire IP 비교는 거절한다. 이번에 관측된 OBSERVE는 이미 hairpin source이다. 실제 relay ingress의 주소도 확인해 이 전제를 닫는다. 다른 경로 변환까지 지원하려면 별도 증명 가능한 observation/path binding 설계가 필요하며, 이번 보수를 편하게 통과시키려고 모든 LAN 주소 alias를 추가하지 않는다.

### LAN viewer에게 NAS LAN IP를 relay 후보로 제공

현재 증상의 첫 수정으로는 선택하지 않는다. 그것만으로는 token에 public IP를 저장하고 Hello에서 raw LAN IP를 비교하는 문제가 남는다. 후보 주소·기존 관측 경로도 함께 달라지고 다른 LAN/다중 NIC에서의 도달성까지 늘어난다. `192.168.0.6` 하드코딩도 피해야 한다. 필요가 별도 실측으로 확인되면 명시적 LAN relay 설정과 관측 경로 일관성을 갖춰 후속으로 다룬다.

## 4. 완료 조건: 사용자 접속 사슬을 시험한다

기존 suite는 relay 서버를 loopback IP로, observe-correction은 별도 서버로 시험한다(`test/run.js`). 이 둘을 분리한 것이 이번 조합을 놓칠 수 있는 구조다. `same_lan_test`의 LAN host 방향뿐 아니라 **LAN viewer → 다른 host** 방향을 추가한다.

필수 검증은 다음 묶음이면 된다.

1. 서버가 실제 UDP로 **wire≠advertised** 관측을 만들고, 인증 HTTP connect에서 policy allowlist를 통과한 뒤 **punch 답장 → token Hello → host HelloAck → 양방향 고유 payload**가 실제 socket에 도착한다. map 존재나 직접 함수 호출만으로 완료하지 않는다. 기존 코드에서 이 행동 assertion이 실패하는 부정 대조를 남긴다. NAT 자체를 재현 못한 시험은 주소 치환 조합의 재현으로 정확히 부른다.
2. punch만 보수한 변이는 Hello에서 실패하고, Hello만 보수한 변이는 후보 선택에서 실패함을 확인해 두 gate가 모두 필요하다는 것을 고정한다. 답장 destination과 session indexes는 raw tuple이어야 한다.
3. 비허용 계정/정책 IP, token 없음·틀림·만료·이전 host token, 유효 token을 **다른 wire IP**에서 보낸 경우는 bind/lease 교체/host 전달 0. 같은 wire IP의 다른 port에서 유효 Hello를 보내는 기존 NAT 계약은 유지하며, 잘못된 token이 현 세션을 빼앗지 못한다.
4. 두 client의 실제 wire endpoint가 같은 advertised public IP로 보이는 조건에서 index가 충돌하지 않는다. 새 connect/이전 Hello 재전송/host 이동/TTL 정리 및 기존 LTE형 public==wire, 같은-LAN host, direct 우선 회귀를 포함한다.
5. 제한된 진단으로 connectId·advertised/wire IP·실제 relay ingress·거절 분류·bound/active를 연결한다. **원 token·계정 비밀은 로그에 넣지 않고**, 실패 datagram마다 무제한 로그를 쓰지 않는다. malformed/미인증 패킷에서 세션이 생기지 않아야 한다.

패치 후 실제 Home→회사에서는 새로운 connectId의 `punch → bound → active`와 **화면 표시·입력 성공**까지 확인한다. journal active만으로 사용자 행동 완료를 선언하지 않는다. LTE 기존 경로도 유지되는지 확인한다.

## 5. 우선순위·배포 경계·임시 우회

- 현재 정상 사용자가 접속할 수 없는 결함이므로 서버 보수를 우선 처리하는 데 동의한다. 클라이언트 와이어/PC·APK 버전 변경은 이 설계에 필요 없다.
- 검수된 서버 commit에서 **전체 런타임 모듈**을 일관되게 게시한다. 현재 require에는 `update_manifest`, `wake_target`, `device_credentials`, `accounts`가 있으며 `version_compare` 등 전이 의존성·package.json도 포함한다. 낡은 문서의 4파일 목록만으로 한정하지 않는다.
- 현재 구동은 gnlink 소유 user service임을 직접 확인했다. 코드 교체 후 그 서비스 재시작은 필요하지만 **이번 검토에서 실행하지 않았다**. allowlist/env/systemd unit/계정 권한 변경은 이 수정의 필수사항이 아니다.
- 코드/모듈을 고정해 백업·정합 확인 후 재시작한다. relay/observe/capability map은 메모리이므로 기존 연결/시도가 끊길 수 있다. 재관측·새 connect가 필요한 경계를 보고한다. 롤백은 맞는 코드·의존 모듈 묶음으로 하며 사용자 DB/로그/키/설정은 덮어쓰지 않는다.
- 게시 후 HTTP health만으로 끝내지 말고 위 UDP handshake/forward와 현장 연결을 확인한다. 서버 build commit·파일 hash·재시작 시각·직접 실행 범위·남은 현장 검증을 기록한다. 시험/검수 전 즉시 운영 파일을 한 줄 고치는 방식은 제안하지 않는다.
- **Home PC의 LTE 핫스팟은 가능한 임시 우회 후보**다. NAS와 같은 LAN/hairpin 경로를 벗어나므로 이 불일치를 피할 가능성이 높다. 과거 LTE 폰 성공은 근거지만 Home PC가 hotspot에서 성공한 것은 아직 직접 확인하지 않았다. 해당 네트워크가 relay IP allowlist에 들어가는지도 필요하다. 사용자가 선택해 전환하며, 성공 보장이나 사용자 PC 자동 네트워크 변경으로 취급하지 않는다.

## 최종 판단

**원인 후보 수용 + 서버 주소 혼용 결함 확정. 수정안은 connect 시 relay wire IP 별도 보관/키 일치, punch·Hello 동시 보수. advertised/direct 정책·실제 전송 tuple·계정/token 경계는 유지한다.** 수신 주소를 public으로 일괄 정규화하거나 LAN 후보를 추가하는 방향으로 범위를 넓힐 필요는 없다.

검토 메모 외 추적 파일 변경이 없으므로 이번 턴에는 history/계획 수정이나 commit을 하지 않았다. 구현·검증·서버 게시 기록은 이 기능 task에 묶는다.
