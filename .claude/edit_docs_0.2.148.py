import io, sys
def load(p):
    s = io.open(p, encoding="utf-8", newline="").read(); c = "\r\n" in s; return s.replace("\r\n", "\n"), c
def save(p, s, c): io.open(p, "w", encoding="utf-8", newline="").write(s.replace("\n", "\r\n") if c else s)
def rep(s, old, new):
    n = s.count(old)
    if n != 1: print("ANCHOR", n, old[:70]); sys.exit(1)
    return s.replace(old, new)

entry = '''
### 2026-10-02 0.2.148 (PC) · APK 0.2.25 게시 — 사용자가 정한 비트레이트가 회선에 나가는 영상의 상한이 된다 (실기 대기)

- 발견(10-01 사용자 질문 + 이 PC 호스트 로그): 6 Mbps 설정인데 회선 ~15 Mbps. 인코더는 `PeakConstrainedVBR` mean=설정·peak=2×, AMD MFT 는 움직임에서 평균을 지키지 않고(`host_rate_governor.hpp` 실측 3~5×), 거버너는 1 s 주기로 payload 만 봐서 2~3 s 버스트를 놓쳤다(0.2.147 17:26:27 `payloadKbps=13618 targetKbps=6000`). FEC·NACK 재전송·헤더는 어떤 예산에도 들지 않았다.
- 설계(검증용 초안 → Codex r1 계약, 검증용 오류 5건 인정: 기존 AU pacing 이 있었음, 버킷 깊이 R×0.25 s 는 250 ms 창 200% 허용, gate-off 부정 대조는 shaper 가 있으면 무의미 등): 설정값 = 그 영상 스트림의 data + FEC + NACK + 앱/UDP/IP 헤더 회선 상한. 실제 datagram 직전의 공통 wire limiter(`R=cap/8`, `B=max(Lmax, R×10 ms)`, 우회 0, 폐기 대신 대기) + 인코드 전 부하 gate + 거버너 보조(1 s 유지 승인). Codex r2 결정: 모든 UDP peer 에 같은 상한(구 수신기 예외 없음), 수신 진척 기반 NACK tail 을 PC·Android 공통 코드에 — 와이어·협상 변경 없음. 킬스위치 `REMOTE60_NATIVE_WIRE_CAP=0`.
- 구현·검수(t-m0ry3hum, 작업용1 `feat/bitrate-hard-cap`, r1~r9): 24b8bf6·8fc384a(limiter·gate) → 8f2f0e7·8d85c8f(진척 tail, AU 를 송신 시작 전에 캐시) → a766486(F1~F4: 재전송을 sender 가 같은 예산으로, key 상태, 첫 datagram fence 를 token 대기 뒤로, 예약 vs 실제 송신 회계) → 31c13e8(R1~R4: 실 sender 스레드가 재전송에 깨어나고 송신 중 끼워 보냄, 세션 소유 replay) → 921ce8a(G1~G4) → **152b685**(H1 key 상태 단일 락, H2 펜스된 replay 즉시 정리·cap OFF 에도 펜스) = 승인 제품 소스. 뒤 6f98bce·5a42c06·5b1b3c6·ef81624 는 시험만. 매 라운드 Codex NEEDS_CHANGES 를 한 묶음으로 반려했고 검증용이 독립 빌드로 재현했다.
- 측정(검증용 독립 build-verify + 독립 빌드한 a95215e 호스트, console 세션): 실 MFT+실 decoder 매트릭스 155/0 — 모든 1 s·250 ms 창 ≤ 설정 +10%(6M/60 움직임 101.0/103.7%, 1.5M/30 100.8/103.1%, 손실 5%+NACK 폭주 101.0/103.7%, 6→1.5 하향은 실제 적용 시점부터). 데스크톱 유사 콘텐츠는 상한에 거의 걸리지 않음(글자 스크롤 ~54%, 창 드래그 ~47%). event-ID paired(118/0): **클릭 한 번 → 화면 중앙값 ON≈OFF≈0.2.147 소스**(6M 46/52/46 ms), 화면 전체 전환은 ON 이 느림(6M 218 vs 69·95 ms, 1.5M 335 vs ~55 ms), 접속 직후 첫 화면 최대 0.75~0.85 s — 큰 프레임을 상한 속도로 나누는 비용(Codex 수용). 첫 IDR 크기를 latency×rate 로 환산한 448/158 KB 는 실측이 아니라 철회했다.
- 실 PC 수신 경로: 제품 `VideoReceiver`(viewer_udp_recovery_test ViewerRig) + 격리 호스트 `--real-recv-smoke` 12/12 exit 0 — 키프레임은 FrameGate 권위 상태(lastDecodedKeyCaptureUs·대기 해제)로 확인, 화면 변화 전량 도달, 키 요청 0~1·복구 포기 0. 검증용 첫 실행의 간헐 FAIL(8회 중 3회, 첫 시나리오)은 시험의 2 ms poll 이 IDR publish 를 놓친 관측 문제였다(Codex 판정, 고친 뒤 12/12, 그중 5회는 옛 실패 조건을 재현하고도 통과).
- 게시: main ef81624(통합 build-integ 새 configure, 회귀 19종·real-recv·matrix exit 0) push → 버전 commit **aeebe33**(PC 0.2.148, APK versionName 0.2.25·versionCode 24 — main 은 APK 0.2.24 를 담은 적이 없다: 그 릴리스는 별도 브랜치, versionCode 23). 첫 버전 commit 시도(6c5e297)는 gradle 줄 편집 실패로 PC 줄만 담겨 메시지와 어긋났고 push 전에 되돌려 다시 만들었다. PC: `gnlink_release.sh --sign` 게이트 전부 PASS·payload 문자열 0.2.148 → deploy exit 0, manifest `19cb7a271b2b0e311e61b4e1722e05afb4acbd9388d093ce0f4295c62a03a511` · sig `1bee1ef02af01deafb8d956a3d6c71d71e282afc4c74c20a7fc553ef11f3fd14`, 11 artifacts, 백업 pair 0.2.147(7c27f0fa…/b78c97bc…). APK: clean assembleDebug + 단위 158/0, 인증서 dcc806ae…(= 기존), aapt2 24/0.2.25·4 ABI → manifest `73dde974115ffd31d6a26f823cf9cfb1f919c4a033703cd8dd8814eeea3c4516` · sig `2e55dabc9286c2e3bbfbdab2cb4a09607d865bba550c18cbefe96c97db5f79eb`, APK 0be35bbc…, 백업 pair 0.2.24. 외부 https: 스크립트 11/11·1/1 + 검증용 독립 curl 4개 일치. `dist/GNLinkSetup-0.2.148.exe`·`dist/GNLink-0.2.25.apk`. origin/main = aeebe33.
- 환경 메모: main 작업 폴더의 `apps/shared/update_manifest/` 시험 벡터 4개가 09-14 에 꺼낸 CRLF 사본이라(그 뒤 `-text` 규칙이 생겨도 기존 사본은 안 바뀜) APK 의 서명 시험이 실패했다 → 저장소 바이트로 다시 꺼낸 뒤 통과. 새 worktree 의 gradle 은 `ANDROID_HOME` 이 없으면 "BuildFlowService" 오류로 보인다.
- 미검증·경계: 두 PC 사이·WAN 의 6 Mbps 현장 측정, 폰 실기. 무손실 초기/idle 에서 뷰어의 키 요청은 관찰만 했고 호스트까지 왕복한 복구는 검증하지 않았다.
- 제품/테스트/문서: 제품(host 송신·gate·limiter·key 상태, 공통 수신 NACK tail; 버전 aeebe33) / 테스트(wire_limiter·defer·idr·fix·sender_thread, fec e2e 매트릭스·paired, recovery real-recv) / 문서(이 항목·작업목록·구현계획).
'''
h, hc = load("docs/history.md")
assert "0.2.148 (PC) · APK 0.2.25 게시" not in h
if not h.endswith("\n"): h += "\n"
h += entry
save("docs/history.md", h, hc)

t, tc = load("docs/작업목록.md")
lines = t.split("\n")
i = [k for k, l in enumerate(lines) if l.startswith("| **C0 (핵심)** |")]
assert len(i) == 1
lines[i[0]] = ("| ~~C0 (핵심)~~ | ✅ ① 관찰 `c0fdcba`(0.2.140) · ② **회선 총량 상한 0.2.148**(aeebe33, t-m0ry3hum r1~r9, 제품 152b685) — 설정값 = 영상 data+FEC+NACK+헤더의 1 s·250 ms 창 상한(실측 ≤ +10%), "
                 "모든 peer 동일, 수신 진척 tail(PC+APK 0.2.25). 대역 **추정**으로 상한을 자동 조절(③ ABR 연동)은 미착수 | 대역 절감 | — | 경위 `history.md` 2026-10-02 0.2.148 항목. 실기 V15 |")
t = "\n".join(lines)
t = rep(t, "| V14 | **0.2.146 파일 복사 실기**",
        "| V15 | **0.2.148 비트레이트 상한 현장 측정** — 6 Mbps 설정으로 움직이는 화면에서 회선 사용량이 6 Mbps 부근에 머무는지(작업 관리자/NAS 로그), 클릭 반응·화면 전환 체감. 폰 0.2.25 업데이트 뒤 같은 확인 1회 | 사용자 | 1 s 평균 ≤ ~6.6 Mbps, 끊김·반복 멈춤 없음 |\n"
           "| V14 | **0.2.146 파일 복사 실기**")
t = rep(t, "| R4 | `history.md` 에 Windows 0.2.140~0.2.145 · APK 0.2.21~0.2.24 의 **게시 기록이 없다**",
        "| R4 | `history.md` 에 Windows 0.2.140~0.2.145 · APK 0.2.21~0.2.24 의 **게시 기록이 없다**(0.2.146~0.2.148·APK 0.2.25 는 기록됨)")
save("docs/작업목록.md", t, tc)

p, pc = load("docs/구현계획.md")
p = rep(p, "## C0 — 대역폭 관찰 ① (2026-09-23) — 관찰만, 적용 없음",
        "## C0 — 대역폭 관찰 ① (2026-09-23) — 관찰만, 적용 없음\n\n"
        "> 2026-10-02: ② 의 \"상한 적용\" 부분은 **회선 총량 상한**으로 0.2.148 에 들어갔다(설정값 = 영상 회선 바이트의 창 상한, 대역 추정과 무관). 대역 추정으로 상한을 자동으로 내리는 연동(③)은 미착수. 경위 `history.md` 2026-10-02 0.2.148 항목.")
save("docs/구현계획.md", p, pc)
print("ok")
