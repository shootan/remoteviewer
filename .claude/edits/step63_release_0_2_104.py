"""0.2.104 release step -- PREPARED ONLY. Run when the verifying Claude relays Codex's final OK.

   python .claude/edits/step63_release_0_2_104.py bump     # product_version.hpp 0.2.103 -> 0.2.104
   (full Release build, then)
   python .claude/edits/step63_release_0_2_104.py docs      # history #420 + plan + task list

Nothing here builds, copies or installs: the build and the dist copy are run by hand so the
installer is produced exactly once, from the release commit's tree.
"""
import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
ROOT = r'D:\remote\remote'

def rw(path, fn):
    raw = open(path, 'rb').read(); t = raw.decode('utf-8')
    nl = '\r\n' if '\r\n' in t else '\n'
    out = fn(t, nl).encode('utf-8')  # build first: an exception must not truncate the file
    open(path, 'wb').write(out); print('ok', path)

def bump():
    edit(ROOT + r'\apps\native_poc\src\product_version.hpp', [
        ('constexpr wchar_t kProductVersion[] = L"0.2.103";',
         'constexpr wchar_t kProductVersion[] = L"0.2.104";'),
    ])
    print('bumped to 0.2.104')

def docs():
    ART = open(ROOT + r'\.claude\edits\release_0_2_104_artifact.txt', encoding='utf-8').read().strip()

    def history(t, nl):
        assert '### 420)' not in t
        L = [
"### 420) 2026-09-08 0.2.104 후보 — A01·V14/A04(+후속)·A02/A06/HN07 반영 릴리스 빌드·설치본 (A2A task, 검증용 remote#srb0qig4 구현 OK · Codex 최종 확인 뒤)",
"- 포함 커밋: 858f766(A01 업로더 401 busy loop, #416) · 7d0d69b(V14 공통 maintenance + A04 유계 포기, #417) · c77b01d(A02/A06/HN07 인코더 provenance, #418) · fe42957(A04 후속 tombstone, #419) 위에 이 커밋(버전만).",
"- 변경 파일: `apps/native_poc/src/product_version.hpp` 0.2.103→0.2.104(제품 소스의 다른 버전 참조 없음), `docs/history.md`, `docs/구현계획.md`, `docs/작업목록.md`.",
"- 빌드·산출물: " + ART,
"- 검증: 릴리스 트리 = fe42957 + 이 버전 bump(제품 코드 변경 없음). 직전 sweep 근거는 #418(a02_sweep1 39+1)·#419(v14b_sweep1 38+2, 환경 2건). 설치·push·서버 배포 없음.",
"- 미검증(사용자 실기 대기): 401 뒤 업로드 재개, 제어만 오는 링크에서 복구 타이머, 정적 화면 꼬리 손실 복구, UAC 복귀·인코더 재동기, 고화질 영상 정지 재현 여부. 2차 패킷 캡처(뷰어 측 포함)는 별건.",
"- 상태: 검증용 대조(payload sha256·임베드 버전·빌드 commit·크기) 전 — 완료 아님.",
        ]
        return t.rstrip('\r\n') + nl + nl + nl.join(L) + nl

    def plan(t, nl):
        a = "- [~] 0.2.103 실기 — 정적 정지 없음·UAC 클릭/복귀 부분 확인, 고화질·재접속 정지 미해결"
        assert t.count(a) == 1
        return t.replace(a, a + " — 0.2.104 후보 빌드(history #420): A01·V14/A04·A02/A06/HN07 반영, 실기 대기")

    def tasks(t, nl):
        anchor = "1.7 [x] **A04 후속(포기 identity tombstone)**"
        assert t.count(anchor) == 1
        line = "1.8 [ ] **0.2.104 실기** — A01(업로더 401)·V14/A04(수신 복구)·A02/A06/HN07(인코더 provenance) 반영 설치본. 설치 후 401 재개·정적 꼬리 손실 복구·UAC 복귀·고화질 정지 재현 확인 (history #420)"
        idx = t.index(anchor); end = t.index(nl, idx)
        return t[:end] + nl + line + t[end:]

    rw(ROOT + r'\docs\history.md', history)
    rw(ROOT + r'\docs\구현계획.md', plan)
    rw(ROOT + r'\docs\작업목록.md', tasks)
    print('release docs ok')

if __name__ == '__main__':
    what = sys.argv[1] if len(sys.argv) > 1 else ''
    if what == 'bump':
        bump()
    elif what == 'docs':
        docs()
    else:
        print(__doc__)
