"""0.2.104 릴리스 문서 기록 — history #426 + 구현계획 + 작업목록.

검증용(remote#srb0qig4)의 필수 기재 항목을 반영한 판(step75 의 docs() 대체):
포함 커밋 9건, 릴리스 트리 = adeffe7 + 이 커밋(주석 정정 + 버전 bump, 로직 변경 0),
직전 근거(seam_sweep1 38+2 / bnd_sweep1 38+2 / recovery x5 / shared_core x3),
미검증(A06 stage 배선은 코드 대조까지), 미해소(e2e 43000 / GDI 3.6fps / holdKick / S15),
"전수 PASS" 표기 금지, 11:27 물리 단절 해결 보장 아님.

  python .claude/edits/step77_release_docs_0_2_104.py
(먼저 .claude/edits/release_0_2_104_artifact.txt 에 산출물 한 줄을 써 둘 것)
"""
import sys
ROOT = r'D:\remote\remote'


def rw(path, fn):
    raw = open(path, 'rb').read()
    bom = raw.startswith(b'\xef\xbb\xbf')
    if bom:
        raw = raw[3:]
    t = raw.decode('utf-8')
    nl = '\r\n' if '\r\n' in t else '\n'
    out = fn(t, nl).encode('utf-8')  # build first: an exception must not truncate the file
    if bom:
        out = b'\xef\xbb\xbf' + out
    open(path, 'wb').write(out)
    print('ok', path, 'crlf=' + str(nl == '\r\n'), 'bom=' + str(bom))


ART = open(ROOT + r'\.claude\edits\release_0_2_104_artifact.txt', encoding='utf-8').read().strip()

L = [
    "### 426) 2026-09-08 0.2.104 후보 — Windows 안정화 1차(A01 · V14/A04 · A02/A06/HN07) 반영 릴리스 빌드·설치본 (검증용 remote#srb0qig4 구현 OK, Codex 최종 확인 뒤 후보 생성 승인)",
    "- 포함 커밋 9건: 858f766(A01 로그 업로더 401 busy loop, #416) · 7d0d69b(V14 공통 maintenance + A04 유계 포기, #417) · c77b01d(A02/A06/HN07 인코더 provenance, #418) · fe42957(A04 tombstone, #419) · 5549d7c(배선·경계·로그 스로틀, #420) · f77adad(반려안 제거·미완 표시, #421) · ac48758(포화 episode, #422) · f13887b(A06 실행 회귀·동작 보존 추출, #423) · adeffe7(포화 경계 6건, #425).",
    "- 릴리스 트리 = **adeffe7 + 이 커밋**. 이 커밋의 제품 변경은 (a) `product_version.hpp` 0.2.103→0.2.104, (b) Codex 정정 지시 1건인 `NoteKeyAccepted` **주석** 문구뿐이다 — 로직·wire·ABI 변경 0.",
    "- 주석 정정: \"decoder's new reference\" 는 decode **전** 의 조립/게이트 수락 경계라 부정확했다 → \"수락 경계이며 decode 성공 주장이 아니다(그 뒤의 decode 실패는 기존 reason 4/5/7 복구 담당)\" 취지로(`native_video_client_shared_core.cpp:665`, `.hpp:355`). #422 의 같은 취지 문구는 이미 정확해 그대로 둔다.",
    "- 빌드·산출물: " + ART,
    "- 근거(이 커밋에서 재실행하지 않음, 직전 커밋들에서 실행된 것): `.claude/seam_sweep1.txt` **38 PASS + 2 FAIL**(#423), `.claude/bnd_sweep1.txt` **38 PASS + 2 FAIL**(#425), `viewer_udp_recovery_test` 단독 ×5 PASS, `shared_core_test` ×3 PASS. **전수 PASS 가 아니다.**",
    "- 미해소·원인 미확정(PASS 로 상쇄하지 않고 그대로 기록): `udp_control_e2e_test`(UDP 43000 을 라이브 GNLinkStream 이 점유) · `gdi_capture_process_test`(GDI_DELIVERED_FPS 3.6x) · epoch [3] `holdKick` 단정(1607404 도입) · S15 `WaitRealSenderHeld(2000)`(1287883 도입) — 뒤 둘은 **이번 변경 이전부터 있던 자산**이고 실 MFT/실 sender 타이밍에 의존하며 **재현에 실패해 원인을 확정하지 못했다**.",
    "- 미검증: A06 의 stage 호출 배선은 **코드 대조까지**(실행 회귀가 덮는 것은 `RunEncodeCall` 이하, #423 ④). 사용자 실기 전이므로 401 뒤 업로드 재개 · 제어만 오는 링크의 복구 타이머 · 정적 화면 꼬리 손실 복구와 포기 1회 · UAC 복귀 뒤 인코더 재동기 · 고화질 영상 정지 재현 여부는 전부 미확인.",
    "- 성격: 이번 후보는 **확인된 코드 결함 수정의 실기용**이며, 09-08 11:27 의 물리 단절(제어·영상 동시 정지) 해결을 **보장하지 않는다**. 설치·라이브 세션 중지·서버 배포·push 없음.",
    "- 변경 파일: `apps/native_poc/src/product_version.hpp`, `native_video_client_shared_core.cpp`, `native_video_client_shared_core.hpp`(주석), `docs/history.md`, `docs/구현계획.md`, `docs/작업목록.md`.",
    "- 상태: 검증용의 산출물 대조(payload sha256·임베드 버전·빌드 commit·크기·트리 clean) 전 — 완료 아님.",
]


def history(t, nl):
    assert '### 426)' not in t
    return t.rstrip('\r\n') + nl + nl + nl.join(L) + nl


def plan(t, nl):
    a = "- [~] 0.2.103 실기 — 정적 정지 없음·UAC 클릭/복귀 부분 확인, 고화질·재접속 정지 미해결 — 11:27 정지 호스트 캡처 1차 분석 완료(history #415), 2차 캡처 방법 Codex 결정 대기"
    assert t.count(a) == 1
    return t.replace(a, a + " → 0.2.104 후보 빌드(history #426: A01·V14/A04·A02/A06/HN07 반영), 실기 대기")


def tasks(t, nl):
    anchor = "1.7 [x] **A04 후속(포기 identity tombstone + 포화 episode)**"
    assert t.count(anchor) == 1
    line = "1.8 [ ] **0.2.104 실기** — A01(업로더 401)·V14/A04(수신 복구·포기 1회·포화 episode)·A02/A06/HN07(인코더 provenance) 반영 설치본 `dist/GNLinkSetup-0.2.104.exe`. 설치 후 401 뒤 업로드 재개·정적 꼬리 손실 복구·UAC 복귀 재동기·고화질 정지 재현 확인 (history #426). 11:27 물리 단절 해결 보장 아님"
    idx = t.index(anchor)
    end = t.index(nl, idx)
    return t[:end] + nl + line + t[end:]


rw(ROOT + r'\docs\history.md', history)
rw(ROOT + r'\docs\구현계획.md', plan)
rw(ROOT + r'\docs\작업목록.md', tasks)
print('release docs ok')
