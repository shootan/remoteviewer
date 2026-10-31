p_in = r'D:\remote\remote\.claude\proposal_ka_pc_r1.md'
p_out = r'D:\remote\remote\.claude\proposal_ka_pc_r2.md'
s = open(p_in, encoding='utf-8').read()

def R(a, b):
    global s
    assert s.count(a) == 1, a[:70]
    s = s.replace(a, b)

R('# proposal ka-pc-design r1 — PC 클라이언트의 최근 PC 3대 유지 (KA ③) (작업용2, 2026-10-27)',
  '# proposal ka-pc-design r2 — PC 클라이언트의 최근 PC 3대 유지 (KA ③) (작업용2, 2026-10-27)\n\n'
  '> **r2 정본.** r1(`proposal_ka_pc_r1.md`, sha256 ec828cc3…) 을 대체한다.\n'
  '> 반영한 판정: Codex `codex_ka_pc_design_r1.md` — **O1 수정 조건부 GO**. 결정 ①②③④⑥ 은 경계 조건을 붙여 수용, **⑤ 는 불허**.\n'
  '> 판정과 바뀐 곳은 본문에 `[r2]` 로 표시했다. 구현 전 조건은 §11 에 모았다.\n'
  '> 구현·SOURCE·EVIDENCE·배포 승인은 아니다.\n'
  '> **착수 조건**: 작업용1 의 ② r3 보수와 e 배선으로 공용 API 가 고정되고, NP 가 병합된 뒤.')
R('> task ka-pc-design / r1 / kind=design (t-4k5y2g3r).', '> task ka-pc-design / r2 / kind=design (t-4k5y2g3r).')

# ① generation
R('**세대 카운터 [결정 1]**', '**세대 카운터 [결정 1 → r2: ⓐ 채택, 조건부]**')
R('→ **O1 을 제안한다.**', '→ **O1 을 제안한다. [r2] Codex GO** — O2 로 다시 쓰지 않는다. 비용 판단은 절대값·steady 증가 추세·0/1/3·active 1+standby 2·cold/reuse 지연을 함께 본다. "프로세스 3개 메모리 > 셸 1개" 하나만으로 구조 변경을 자동 결정하지 않는다(아래 O2 재검토 기준 문장은 r2 에서 이 문장으로 대체한다).')
R('**비ASCII 경로**:', '''- **[r2] ⓐ 확정 조건**
  - key = 정규화 directory origin + 안정 account ID + hostId. key 별 영속 CSPRNG 기기 id 와 단조 gen 을 둔다. 자식이 재시작해도 id 는 재사용한다.
  - 모든 admission/activation gen 은 송신 전에 내구 예약한다. 손상·저장 실패·소진·재설치 정책은 ② N3 보수 계약을 따른다.
  - **단일 live writer**: 같은 key 를 두 프로세스가 열면 한쪽이 **송신 전** 배타 잠금 실패로 거부한다. 잠금은 atomic replace 되는 `.gen` 이 아니라 **별도의 stable lock 파일**에 건다. LockFileEx 가 교체된 파일로 승계된다고 가정하지 않는다.
  - 파일명은 key 의 **전체 SHA-256** 이거나, 짧은 hash 일 때는 header 의 full key 로 충돌을 검증한다. 경로에 raw host·account 문자열을 넣지 않는다.
  - "설치당 allocator 1" 설명은 PC 에서 **key 별 단일 소유 allocator** 로 바뀐다. APK 의 단일 프로세스 소유는 그대로다.
  - 이 id 를 물리 기기 신원이나 전체 단말 수 제한으로 주장하지 않는다.
  - gen 저장의 flush·실패·단일 writer 보수는 **공용 담당(작업용1)** 이 한다. PC 담당은 같은 공용 파일에 중복 writer 를 만들지 않는다.
- **[r2] recent 저장**: tab 구분 필드의 escaping·길이·파싱 규칙을 고정한다. locale·시계 동률로 순서가 뒤집히지 않게 순번 등으로 stable 순서를 보존한다.

**비ASCII 경로**:''')
R('3. Host 별 지원 여부는 첫 admission 결과(Unsupported = 6 Hello 무응답 ≈3 s)로 그 셸 수명 동안 캐시한다.',
  '3. **[r2]** 지원 캐시는 directory 지원과 Host 별 결과를 분리한다.\n'
  '   - 무응답(Unsupported 판정)은 **"확인되지 않음"** 이지 구 Host 확정이 아니다.\n'
  '   - 캐시는 host 버전·owner·셸 수명에 묶고, 유한 재probe 정책을 둔다. 일시 손실을 영구 미지원으로 고정하지 않는다.')

# ② park
R('4. 창과 디코더를 **해제**한다(기본값). 숨긴 채 유지하는 안은 재활성 시간과 비용을 측정한 뒤 고른다 **[결정 2]**.',
  '4. 창·decoder·MF/D3D 와 관련 device·cache·thread 를 **해제**한다. **[r2] 결정 2 확정** — 숨긴 자원 캐시로 미리 바꾸지 않는다. 재활성 지연과 메모리는 따로 측정한다.')
R('1. 입력 release, clipboard·선택 작업 차단, decoder unbind 같은 기존 화면 이탈 fence 를 수행한다.\n2. 세션 receive/handshake 가 끝난 것을 확인한다.\n3. `SessionOver` 를 호출한다 → 같은 소켓으로 Ping 을 재개한다.',
  '1. 입력 release, clipboard·선택 작업 차단, decoder unbind 같은 기존 화면 이탈 fence 를 수행한다.\n2. 세션 reader/handshake 가 멈춘 것을 확인한다.\n3. **정확한 activation op** 의 `SessionOver` 를 호출한다 → 같은 소켓으로 Ping 을 재개한다. 이 순서는 직렬화한다.\n'
  '- **[r2] 공용 계약 정합**: 뷰어는 `ClientSessionController` 와 별도의 startup/receiver 를 쓴다. 그래서 `udpHandshake` 를 호출하는 것만으로는 안전하지 않다.\n'
  '  - ② 보수된 lease 의 ownership·Abandon·SessionOver 계약에 맞춰 다음을 실제 뷰어 경로에 연결한다: 복제 소켓 소유, 쓰지 않은 args, 실패, 취소, callback 생존.\n'
  '  - 0a5e71d 의 raw-this API 를 그대로 복사하지 않는다.\n'
  '  - 늦게 끝나는 창·renderer 종료가 새 op 의 decoder 나 socket 을 닫지 않아야 한다.')

# activation
R('3. 동시에 창·디코더 init 을 시작한다(network 와 UI init 이 서로 기다리지 않는다).',
  '3. 동시에 창·디코더 init 을 시작한다(network 와 UI init 이 서로 기다리지 않는다).\n'
  '   - **[r2]** 둘 다 **새 opId 에 결합**한다. 실패·취소가 먼저 오면, 늦게 끝난 init 이나 HelloAck 가 창을 보이거나 입력을 켜지 못한다.\n'
  '   - `PickerReady` = 보이고 입력 가능한 실제 picker + 그 op 의 연결 준비가 모두 된 시각. `Presented` = 그 선택의 첫 정상 frame. 둘을 따로 기록한다. 초기화를 줄인 것만으로 p95 ≤1 s 를 선언하지 않는다.')
R('- 창·`init_decoder`·MF·D3D·picker·입력 채널을 만들지 않는다.',
  '- IPC·WSA·lease 만 준비한다. 창·`init_decoder`·MF·D3D·picker·입력 채널을 만들지 않는다.')

# IPC
R('**전송**: 상속 익명 pipe 한 쌍. 길이 접두 + 고정 레코드다. HWND 검색·전역 PostMessage 는 쓰지 않는다(Codex §4).',
  '**전송**: 상속 익명 pipe 한 쌍. HWND 검색·전역 PostMessage 는 쓰지 않는다(Codex §4).\n'
  '- **[r2] 상속 범위**: 부모 → 자식용과 자식 → 부모용 **pipe 두 개**의 자식 쪽 끝만 handle allowlist 에 넣는다. 부모 쪽 끝·다른 자식의 끝·Job handle 은 상속하지 않는다. 복사된 불필요 handle 은 닫아서, EOF 가 실제 owner 종료를 뜻하게 한다.\n'
  '- **[r2] 프레임**: 길이·버전·type·최대 크기를 검증하고, bounded queue 로 받는다.\n'
  '- **[r2] 막힘 방지**: 전용 I/O 스레드와 취소·deadline 을 둔다. pipe 가 막혀도 UI, lease Ping, 로그아웃이 무한정 붙잡히지 않는다.')
R('**검증**: 모든 메시지에 key + child incarnation + owner epoch + opId 를 싣고, 셸이 현재 값과 다르면 버린다(늦은 Provide·SessionOver fence, 모델 A).',
  '**검증**: 모든 메시지에 key + child incarnation + owner epoch 를 싣는다. activation·PickerReady·Presented·SessionOver 는 opId 까지 검사한다. 셸의 현재 값과 다르면 버린다(늦은 Provide·SessionOver fence, 모델 A).')
R('**token 갱신**:', '**token 갱신** — **[r2]** `TokenRenewed` 에 owner 범위의 **tokenVersion** 을 붙여, 늦은 갱신이 최신 token 을 덮지 못하게 한다.\n'
  '- token 갱신 실패·401·권한 철회는 transient network retry 와 분리한다.\n'
  '- 로그아웃 뒤의 `NeedToken` 이 come_back 이나 로그인을 되살리지 않는다.\n'
  '- token 은 사설 pipe 와 메모리 수명으로만 전달한다. 로그·argv·env 에 새로 넣지 않는다.\n'
  '- 갱신 흐름:')

# ③ logout
R('**로그아웃·계정 전환**(지금 `:1763-1785` 는 뷰어를 건드리지 않는다):\n1. 모든 managed 자식에 `Release{reason=signout}` 를 보낸다. 실행 중인 세션은 fence 뒤 종료한다.',
  '**로그아웃·계정 전환**(지금 `:1763-1785` 는 뷰어를 건드리지 않는다):\n'
  '0. **[r2] 먼저 fence 한다**:\n'
  '   - owner epoch 를 올린다.\n'
  '   - 모든 pending op·auto-retry·token 요청을 무효화한다.\n'
  '   - 새 admission/activation 을 차단한다.\n'
  '   - 그 뒤 **old owner 자식의 snapshot** 에만 종료를 지시한다.\n'
  '   - Release·close 응답은 옛 owner 의 teardown 결과로만 모으고, UI 나 새 계정 상태를 갱신하지 못하게 한다.\n'
  '   - 새 로그인 뒤 생긴 같은 key 의 자식은 옛 shutdown 대상에 편입하지 않는다.\n'
  '1. snapshot 의 자식에 `Release{reason=signout}` 를 보낸다. 실행 중인 세션은 fence 뒤 종료한다.\n'
  '   - **[r2] 결정 3**: **그 셸이 그 계정으로 생성·소유한 일반 directory 뷰어도** 종료한다. managed/일반 여부로 계정 경계를 달리하지 않는다.\n'
  '   - 외부 standalone·raw IP 뷰어, 다른 셸이나 다른 사용자의 뷰어는 대상이 아니다. PID·창 이름 검색으로 넓히지 않는다.')
R('4. owner epoch 를 올려 늦은 IPC 를 버린다.\n- 일반(비 managed) 뷰어의 기존 동작 — 로그아웃 뒤에도 유지 — 을 바꿀지는 **[결정 3]** 이다.',
  '4. (epoch 증가는 0 단계에서 이미 했다.) 유한 대기와 강제 회수는 **worker** 에서 한다. 셸 UI 를 3 s 씩 동기로 막지 않는다. 강제 회수는 launch 때 보유한 자기 process/job handle 만 쓴다.')
R('**셸 crash**: managed 자식은 셸이 만든 Job(`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`)에 넣는다 → 셸 핸들이 닫히면 OS 가 회수한다. Host entry 는 20 s 뒤 소멸한다.',
  '**셸 crash**: managed 자식은 셸이 만든 Job(`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`)에 넣는다. 셸이 가진 **마지막 Job handle** 이 닫히면 OS 가 회수한다.\n'
  '- **[r2] Job handle**: 자식에게 Job handle 을 상속·누수하지 않는다.\n'
  '- **[r2] 생성 순서**: 자식을 **suspended 로 생성 → Job 편입 성공 → resume** 한다. 편입이 실패하면 소유되지 않은 자식으로 실행하지 않는다.\n'
  '- **[r2] 자식별 소유**: process/job handle 을 자식별로 둔다. 한 PC 의 제거·실패가 나머지 2대를 끊지 않는다. 계정 전체 종료와 개별 종료를 구분한다.\n'
  '- **[r2] Host 쪽**: active grant 는 20 s standby TTL 로 곧바로 소멸한다고 보장할 수 없다. active liveness 는 세션 자체이고, **peer-lost 나 실제 session end 뒤**에 standby 만료가 적용된다.')

# ④
R('5. 처음부터 standby 를 거쳐 activation 하는 안은 미지원 Host 에서 ≈3 s 를 잃는다(Unsupported 판정). 그래서 지원 캐시가 확인된 PC 에만 적용한다 **[결정 4]**.',
  '5. **[r2] 결정 4 확정** — 명시 탭도 StandbyReady 면 그 lease 로 activation 한다.\n'
  '   - 지원 캐시만 있고 아직 준비 전이면, 유한 준비·취소 또는 명시적인 실패 표시를 거친다. ready 를 꾸미지 않는다.\n'
  '   - 구 Host·prelogin·지원 unknown 은 명시 legacy 연결 경로를 별도로 유지한다. 자동 legacy fallback 은 0 이다.')

# ⑤
R('- 활성 세션이 **네트워크** 로 끊기고 lease 가 살아 있으면 같은 lease 로 다시 activation 할지는 **[결정 5]** 다. 제안: 창이 보이는 상태에서 `SessionEnded{network}` 일 때만 1회 시도하고, takeover·host 종료면 하지 않는다.',
  '- **[r2] 결정 5 불허**: 세션이 끝난 뒤의 **자동 재activation 은 0** 이다. 이유: Host 는 더 큰 gen 을 새 사용자 activation/takeover 로 처리한다. 지금의 43/control-lost 신호로는 takeover 와 단절을 가를 수 없다. 1회 제한도 권한 문제를 풀지 못한다.\n'
  '  - managed 분기는 기존 43/44/46 자동 재시작(ordinary Hello·새 activation)을 **이어받지 않는다**. 창에 연결 종료와 "다시 연결" 동작을 보여 주고, **standby 만 회복**한다(fresh admission·Ping 복구는 허용).\n'
  '  - 새 activation 은 다시 연결 버튼이나 카드 탭(새 op/gen)에서만 한다.\n'
  '  - activation handshake 중 유실에 대한 **같은 gen 재송신**, 아직 끝나지 않은 세션의 기존 control retransmit/resume 은 원 계약대로 허용한다.\n'
  '  - 사유 문구: takeover 가 확인되지 않았으면 "다른 기기가 연결했다" 고 단정하지 않는다. 확인된 사유가 있으면 그것을, 없으면 "연결이 끊겼습니다" 를 쓴다.')
R('- 다른 기기가 takeover 해서 우리 세션이 끝나면(`SessionEnded{takeover}`) standby 로만 돌아가고 자동으로 되찾지 않는다. 창에는 "다른 기기에서 연결해 이 화면이 닫혔습니다" 를 남기고 → 목록에 표시한다.',
  '- 다른 기기가 takeover 해서 우리 세션이 끝나면 standby 로만 돌아가고 자동으로 되찾지 않는다. **[r2]** 사유 문구는 takeover 가 **확인됐을 때만** 쓴다.')

# ReturningToStandby
R('5. Host 가 아직 active 로 보는 구간은 `ReturningToStandby` 로 표시하고, 유한 시간 안에 확인한다.',
  '5. Host 가 아직 active 로 보는 구간은 `ReturningToStandby` 로 표시하고, 유한 시간 안에 확인한다.\n'
  '   - **[r2]** local SessionOver, Ping Ok, server active enum 은 Host grant 반환의 확인이 아니다.\n'
  '   - Unknown/Lost 면 non-preempt 로 재입장한다.')

# ⑥ tests
R('- ⚠️ **사용자 화면을 가리지 않는다**(no-covering-user-screen). 창 실기 시험은 별도 데스크톱(CreateDesktop) 또는 화면 밖 배치 seam 이 필요하다 — 방식 확인 **[결정 6]**.',
  '- ⚠️ **사용자 화면을 가리지 않는다**(no-covering-user-screen). **[r2] 결정 6**:\n'
  '  - 기존 **화면 밖 배치 seam** 을 우선한다. 이 seam 은 실제 제품 control 렌더와 실제 입력 → bridge → native 결과를 보존해야 한다.\n'
  '  - CreateDesktop 은 실제 렌더·입력 증거를 얻을 수 있을 때만 대안으로 쓴다.\n'
  '  - `CapturePreview` 는 셸 WebView 의 증거일 뿐 뷰어 D3D 최종 presented 증거가 아니다.\n'
  '  - 화면 밖 렌더가 throttle·미표시되면 그 검증만 invalid 로 적는다. frame callback 만으로 표시를 주장하지 않는다.')
R('9. **부정 대조**:', '''9. **[r2] 추가 행동**:
   - 자동 network 재activation 0(직접·relay). takeover 를 network 처럼 보이게 하는 반례를 포함한다.
   - 로그아웃 **직전·대기 중** 에 늦게 온 TokenRenewed·Provide·Activate, 그리고 새 계정의 같은 key.
   - 자식 생성·Job 편입 중 셸 crash. Job handle 상속 부정 대조.
   - 같은 key 의 두 프로세스 잠금, 비ASCII 저장, 재시작 뒤 gen.
   - init/handshake 역순 취소.
   - 개별 자식 종료가 다른 2대를 보존.
   - 활성 → 목록 → 같은 lease standby → 재탭 동안 Host 의 실제 session end 관측.
   - crash·종료 때 클라이언트 자식 0, Host session end, entry 만료를 각각 관측.
10. **부정 대조**:''')

# §9 decisions
R('## 9) 결정 목록', '## 9) 결정 목록 — [r2] Codex 판정 결과: ①②③④⑥ 수용(조건은 본문 [r2]), ⑤ 불허')

# NP note
R('**NP 와의 관계**: standby 경로는 NAT punch 를 하지 않는다',
  '**NP 와의 관계** (**[r2]** punch 가 없는 standby helper 를 NP 의 legacy relayEarly 로 해결됐다고 간주하지 않는다): standby 경로는 NAT punch 를 하지 않는다')

s += '''

## 11) [r2] 구현 전 조건 요약 (Codex `codex_ka_pc_design_r1.md`)

1. 로그아웃·계정 전환·종료는 **fence 먼저**(epoch·pending·차단), 종료 대기는 그 뒤 worker 에서.
2. pipe 두 개, 자식 쪽 끝만 상속. Job handle 비상속. 프레임 검증, bounded, 전용 I/O 와 deadline.
3. TokenRenewed 에 tokenVersion. 401·권한 철회 ≠ network retry. 로그아웃 뒤 NeedToken 무시.
4. suspended → Job 편입 → resume. 편입 실패면 실행하지 않는다. 자식별 handle. 마지막 Job handle 수명.
5. 세대: key 별 영속 id, 송신 전 내구 예약, 별도 stable lock 파일, full key 검증. gen 저장 보수는 공용 담당.
6. activation 의 init·handshake 는 op 결합. 늦은 완료가 창·입력을 켜지 않는다. PickerReady 와 Presented 는 분리.
7. park: fence → reader 정지 → 정확한 op 의 SessionOver → Ping. ② 보수 API 의 소유·Abandon 계약에 맞춘다(raw-this 복사 금지).
8. 자동 재activation 0. standby 복구만 자동이다.
9. 지원 캐시는 "확인되지 않음" 을 영구 미지원으로 만들지 않는다.
10. 착수: 작업용1 ② r3 보수 + e 배선으로 API 고정 → NP 의 SOURCE 통과 commit 순차 통합 → PC 구현. 자동 preconnect 출하 OFF 유지.
'''
open(p_out, 'w', encoding='utf-8').write(s)
print('ok', s.count('[r2]'))
