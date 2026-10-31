

## ② 범위 제안 (작업용1, 2026-10-27) — shared native 클라이언트 + activation 경로
근거: Codex r4 "② 구현 GO"(r6 재확인), 계약 §2.2 activation Pong 상관·§2.6 구 Host·§2.7 클라이언트 계약·§3, Codex r4 "운영/출하 전" 목록. 자동 preconnect 출하 OFF·NAS 무변경·legacy Hello 자동 fallback 없음은 그대로 유지한다.

### 구현할 것 (ka-r3, 단일 writer)
a. **순수 `StandbyEntryClient` 상태기계**(소켓 없음). 상태: Idle → HelloSent → Parked(Ping) → Activating → Active / Released / Unsupported / Failed. 입력은 Ack(Ok/Busy/Superseded/Unknown), Pong(entry·gen·status·Granted), HelloAck, 시간, 사용자 취소, release.
   반례로 시험할 것:
   - Pong→HelloAck 순서, HelloAck→Pong 순서 모두 완료한다.
   - HelloAck 가 유실되면 **같은 gen** 으로 Activate 를 재전송한다(grant 가 살아 있는 동안 Host 가 HelloAck 를 다시 보낸다).
   - Pong 유실.
   - bare HelloAck 만으로는 완료하지 않는다.
   - 취소 뒤 늦은 Pong/HelloAck 는 무시한다(그 gen 은 죽은 것).
   - 다른 entry/gen 의 Pong 은 무시한다.
   - Busy 면 backoff 하고 자동 Hello 는 보내지 않는다.
   - Ack 없음이 유한 재시도 뒤에도 계속되면 Unsupported 로 간주하고 legacy Hello 는 보내지 않는다.
   - Ping 이 Unknown 이면(Host 재시작·만료) 새 capability 로 재입장한다.
   - **proof 유실**(Host 이동 뒤 relay 가 pin 하지 못함): Ping 무응답이 유한 시간 이어지면 해제·재입장하고, ready 로 남지 않는다.
b. **단조 generation 저장 인터페이스**(`GenerationStore`). 재시작을 넘어 감소하지 않는다. 플랫폼(APK SharedPreferences, PC 파일)이 구현을 제공한다. 16 B 기기 id 도 같은 저장소에 둔다.
c. **소켓 소유**: host lease 하나에 UDP 소켓 하나다.
   - relay 경로: `/api/hosts` 의 `standby.supported` 를 먼저 확인한다. 없음·timeout 은 미지원으로 본다. 그 소켓의 `standby:true` connect 를 쓴다.
   - 두 host 를 한 소켓으로 다중화하지 않는다.
d. **`ClientSessionController` activation 모드**: lease 의 소켓(prepared)으로 Hello 대신 StandbyActivate 를 보내고, Pong(Granted)과 HelloAck 를 둘 다 받아야 연결된 것으로 본다. 그 뒤는 기존 control 경로 그대로다. 새 세션 유형이 아니라 connect 모드다.
   e2e: 제품 클라이언트 둘이 실 시험 Host 에 parked → A activation → B 의 ping 은 A 무간섭 → 명시 takeover. 기존 89/89 와 같은 순서를 **제품 클라이언트로** 실행한다(`sh_run.ps1 -NativeExe`).
e. 그 뒤 APK 배선. 출하 스위치 OFF 뒤에 둔다.

### 정해야 할 것 (제 제안)
1. **브랜치**: shared core·Host 는 ka-r3 에서 진행한다. APK 배선은 작업용2 준비분(`feat/keepalive-r2-apk` 6d85ce2, base 82b3d14 — main 보다 뒤)을 `fix/apk-prelogin-select` 병합 뒤의 새 0.2.33 브랜치에서 합친다. KA 를 위해 `fix/apk-prelogin-select` 는 건드리지 않는다.
2. **activation 취소**: HelloAck 전에 사용자가 취소하면 클라이언트는 늦은 세션을 채택하지 않는다. 이미 grant 가 났을 수 있으므로 **StandbyRelease 를 보내지 않고** 그 세션이 peer-lost 로 끝나게 둔다(Release 는 Live 면 Host 가 거부한다). entry 는 parked 로 돌아오고, 다음 activation 은 더 큰 gen 으로 한다.
3. **gen·기기 id 저장**: shared 는 인터페이스만 두고 구현은 플랫폼이 한다. 손상·누락이면 새 기기 id 로 다시 시작한다(옛 entry 는 lease 만료로 사라진다).
4. **폰 범위**: 10-26 기본대로 앱 화면에 보이는 동안만 유지한다(onStop 해제). APK 배선은 이 기본을 따른다.
5. ②에서 실측하는 것: 상태기계·activation e2e·소켓/generation. §6 비용, §7 교차 기기, 실 NAT 는 ③④ 범위로 미검증 표기한다.
