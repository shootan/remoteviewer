import io, sys
def load(p):
    s = io.open(p, encoding="utf-8", newline="").read(); c = "\r\n" in s; return s.replace("\r\n", "\n"), c
def save(p, s, c): io.open(p, "w", encoding="utf-8", newline="").write(s.replace("\n", "\r\n") if c else s)
def rep(s, old, new):
    n = s.count(old)
    if n != 1: print("ANCHOR", n, old[:80]); sys.exit(1)
    return s.replace(old, new)
def rep_line(s, prefix, new):
    lines = s.split("\n"); idx = [i for i, l in enumerate(lines) if l.startswith(prefix)]
    if len(idx) != 1: print("LINE", len(idx), prefix); sys.exit(1)
    lines[idx[0]] = new; return "\n".join(lines)

# ---- 작업목록
t, tc = load("docs/작업목록.md")
t = rep(t, "> 2026-09-22 재작성, 2026-10-01 갱신. 제품/배포 기준 `main@df274be`(0.2.146)",
        "> 2026-09-22 재작성, 2026-10-02 갱신. 제품/배포 기준 `main@aeebe33`(0.2.148 · APK 0.2.25)")
t = rep(t, "> 현재 배포(2026-10-01 NAS manifest 직접 확인): `windows=0.2.146`(df274be, 10-01 04:2x) · `android=0.2.24`(versionCode 23,\n"
           "> 09-30 17:1x — 고정 서버·자동 로그인·계정 상태 포함).",
        "> 현재 배포(2026-10-02 NAS manifest 직접 확인): `windows=0.2.148`(aeebe33) · `android=0.2.25`(versionCode 24, 같은 commit —\n"
        "> 고정 서버·자동 로그인·계정 상태(0.2.24 부터) + 수신 진척 tail).")
t = rep_line(t, "| V11 |",
        "| V11 | **폰 실기(최신 APK 0.2.25)** — 0.2.24 부터의 고정 서버(입력란 없음, 옛 자격 이행)·자동 로그인(Keystore)·계정 상태 문장 + 0.2.25 의 수신 진척 tail. 게시: 0.2.24 09-30(manifest ede573eb…), **0.2.25 10-02**(manifest 73dde974…, APK 0be35bbc…, 인증서 dcc806ae… 동일) | 사용자 폰 1회 | 업데이트 알림→설치 · 기존 유효 세션 이행 또는 최초 명시 로그인 1회 뒤 재시작 자동 로그인 · 로그아웃 뒤 재로그인 · 승인 대기 계정이면 그 문장 · 영상 끊김·반복 멈춤 없음 |")
t = rep_line(t, "| V14 |",
        "| V14 | **파일 복사 실기 잔여** — ✅ 양방향 복사·붙여넣기는 사용자 확인(10-01, 0.2.147). **남은 확인**: 전송 중 취소, 대용량, 전송 바. 0.2.147 부터는 인앱 업데이트로 도우미가 깔린다(0.2.145 출신 PC 는 0.2.146 → 0.2.147 업데이트에서). P2 한계: 원격 도우미가 세션 중 재기동되면 source 에서 재복사 필요 | 사용자 PC 2대 | 취소 시 전송 중단·결과 문구·예약 해제·이후 새 paste 가능(부분 파일 잔존 허용) · 대용량 완주 |")
t = rep_line(t, "| V15 |",
        "| V15 | **0.2.148 비트레이트 상한 현장 측정** — 6 Mbps 설정, 움직이는 화면. 판정 대상은 **GNLink 의 H.264 영상 UDP 흐름(IP 층)**이다 — NIC 전체(다른 프로그램·파일 복사·제어)는 이 상한의 계약이 아니다. 근거는 호스트 로그의 `[wire]` 실제 송신 바이트 또는 패킷 캡처로 그 흐름만 분리; 작업 관리자 NIC 그래프는 보조. 클릭 반응·화면 전환 체감. 폰 0.2.25 에서 1회 | 사용자 + 검증용 로그 판독 | 그 흐름의 1 s 창이 설정 부근(자동 검증의 관찰 범위: 1 s·250 ms 창 ≤ +10%), 끊김·반복 멈춤 없음 |")
t = rep_line(t, "| ~~C0 (핵심)~~ |",
        "| **C0 (핵심)** | ① 관찰 `c0fdcba`(0.2.140) ✅ · **독립 hard cap ✅ 0.2.148**(aeebe33, t-m0ry3hum, 제품 152b685 — 설정값 = 영상 data+FEC+NACK+헤더의 회선 상한, 대역 추정과 무관) · ② **추정 → 상한/pacing 적용(ABR 연동) 미착수** · ③ UI 미착수 | 대역 절감·FHD60 무정체 | 큼 | 경위 `history.md` 2026-10-02 0.2.148 항목. 실기 V15 |")
save("docs/작업목록.md", t, tc)

# ---- history
h, hc = load("docs/history.md")
h = rep(h, "FEC·NACK 재전송·헤더는 어떤 예산에도 들지 않았다.",
        "FEC·NACK 재전송·헤더는 영상 payload 와 **한 총량으로 합산되지 않았다**(NACK 는 별도 15% bucket, AU pacing 은 평균의 500%·최소 40 Mbps).")
h = rep(h, "event-ID paired(118/0): **클릭 한 번 → 화면 중앙값 ON≈OFF≈0.2.147 소스**(6M 46/52/46 ms)",
        "event-ID paired(118/0, fixture 의 그린 변화 → decoder 출력에서 그 marker 를 찾은 시각): **단일 변화의 중앙값 ON≈OFF≈0.2.147 소스**(6M 46/52/46 ms)")
h = rep(h, "접속 직후 첫 화면 최대 0.75~0.85 s — 큰 프레임을 상한 속도로 나누는 비용(Codex 수용).",
        "접속 직후 첫 화면 최대 0.75~0.85 s. Codex 는 이 범위를 수용 가능한 비용으로 판단했다(원인을 바이트 직렬화로 단정하지는 않는다 — ON/OFF 차이에는 gate/staging 도 섞여 있다).")
h = rep(h, "매 라운드 Codex NEEDS_CHANGES 를 한 묶음으로 반려했고 검증용이 독립 빌드로 재현했다.",
        "라운드마다 Codex NEEDS_CHANGES 를 한 묶음으로 반려했다. 검증용 독립 빌드·실행은 r2~r6(37572b1·a766486·31c13e8·921ce8a·152b685)과 r8·r9 시험 commit 에서 했다(`verify-c2-ef533f2/.claude/rv_cap*/`).")
h = rep(h, "외부 https: 스크립트 11/11·1/1 + 검증용 독립 curl 4개 일치. `dist/GNLinkSetup-0.2.148.exe`·`dist/GNLink-0.2.25.apk`. origin/main = aeebe33.",
        "외부 https(`https://gnlink.shotan.net/updates/0.2.148/`, `…/updates/0.2.25/GNLink-0.2.25.apk`): 스크립트 11/11·1/1 + 검증용 독립 curl 4개 일치. 저장소 루트 `dist/GNLinkSetup-0.2.148.exe`·`dist/GNLink-0.2.25.apk`. origin/main = aeebe33.\n"
        "- Codex(9yg69hjw) 직접 외부 확인(읽기 전용, result.json UTC 2026-10-01T16:01:45Z): 라이브 windows·android pair 해시 = 로컬, 출하 공개키로 서명 검증·변조 거부, HTTPS 12/12 크기·SHA256 일치, APK 인증서 dcc806ae…·versionCode 24·4 ABI, 루트 dist 2개 일치, origin/main = aeebe33. 결과 `main-stability-integration/.claude/codex-final-0.2.148/result.json`. 검증용 확인과 별개.")
save("docs/history.md", h, hc)

# ---- 구현계획 note: keep stage wording
p, pc = load("docs/구현계획.md")
p = rep(p, "> 2026-10-02: ② 의 \"상한 적용\" 부분은 **회선 총량 상한**으로 0.2.148 에 들어갔다(설정값 = 영상 회선 바이트의 창 상한, 대역 추정과 무관). 대역 추정으로 상한을 자동으로 내리는 연동(③)은 미착수.",
        "> 2026-10-02: 이 절의 단계(① 관찰 → ② 추정을 상한/pacing 에 적용 → ③ UI)와 별개로, **독립 hard cap**(사용자 설정값 = 영상 회선 바이트의 창 상한, 추정과 무관)이 0.2.148 에 들어갔다. ①만 완료, ②·③ 은 미착수.")
save("docs/구현계획.md", p, pc)
print("ok")
