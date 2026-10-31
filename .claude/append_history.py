import os
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
path = os.path.join(ROOT, "docs", "history.md")
entry = [
    "",
    "### 2026-09-22 문서 재편 — 남은 작업만의 작업목록, 끝난 문서 38건은 legacy/2026-09 로",
    "",
    "- 목표: 사용자 지시 \"남은 작업만 새로 리스트업하고, 필요 없는 문서는 아카이브\". 기준 `main` a6cba13(0.2.133).",
    "- 변경: `docs/작업목록.md` 전면 재작성(실기·검증 대기 V1~V10 / 코드 C1~C14 효과 순 / 배포·운영 결정 D1~D3 / 기록 부채 R1~R3 / 백로그). `docs/README.md` 읽기 순서를 작업목록 우선으로. `docs/legacy/2026-09/` 신설 + `docs/legacy/README.md` 색인. 살아 있는 문서의 링크는 새 경로로 고쳤고, 옮긴 문서끼리의 링크는 그대로 유효하다.",
    "- 옮긴 것: HANDOFF · 09-06~09-08 뷰어 정지 분석/작업 지시/실기 4건 · 감사 2건 · 업데이트 배선 계획 · NAS 게시 요청서 2건 · 09-12 스레드·복구 전수 점검 3건 · 09-12 사건 3건 · 출시 준비 · 안정화 구현/검토/머지 3건 · 복구 구현/커밋 분리/통합 3건 · 0.2.124/0.2.127 게시·실기 4건 · 09-15 GPU/끊김/디스패처 3건 · abandon race 크래시 · 리팩터 계획 2건 · 발견 원장 2건.",
    "- 작업목록에서 뺀 것(끝났거나 대체됨): 0.0.1 업데이터 자기 갱신(0.2.110 에서 해결, #507; 오늘 설치본 Updater 해시 = 133 payload) · 0.0.11/0.0.13 Client 팝업·버튼(인앱 경로로 133 까지 실사용) · 0.2.109/0.2.111 인앱 대기 · T1 버전 로깅(0.2.126, 오늘 로그 확인) · PICK(0.2.123 에서 같은 swapchain 으로 present) · K1 중복 줄 · 1.11 TLS 미검증(https 실사용 중) · SRV 로테이션 재배포(배포본 = HEAD − 27d9e74 이므로 포함됨) · HN13 코드(안정화 라운드 encodeSource 수정, 현장 확인만 V6).",
    "- 근거로 새로 확인한 사실: 집 호스트 09-22 13:12 132→133 첫 시도 AbandonedBeforeSwap(구 132 updater), 13:14 `readiness claim accepted → Updated 0.2.133`. 회사 PC2 는 14:11 `localVersion=0.2.130`(Host 는 트레이 클릭, Client 는 로그인 때만 확인 → 트리거 없음이 유력, 로그 확인 전엔 가설). 뷰어측 ControlResume(item 8 layer 3) 미구현. 서버 배포본 = repo HEAD 에서 27d9e74 만 되돌린 것과 diff 0, journal 오류 0. 133 설치본·payload·manifest·NAS 해시 정합. Codex(remote_codex) 와 2회 왕복한 우선순위 근거는 `.claude/priority_2026-09-22.md`.",
    "- 미검증: 문서 이동뿐이라 제품 빌드·테스트 없음. 09-18~22 의 상세 기록(item 8·12, 0.2.131~133, 헬스 게이트, abandon race)은 아직 이 파일에 없다 → 작업목록 R1.",
    "- 제품/테스트/문서: 문서만. 루트 워크트리(`refactor/viewer-split`)는 손대지 않았다(작업목록 D3).",
]
with open(path, "rb") as h:
    data = h.read()
if not data.endswith(b"\r\n"):
    data += b"\r\n"
data += "\r\n".join(entry).encode("utf-8") + b"\r\n"
with open(path, "wb") as h:
    h.write(data)
print("appended", len(entry), "lines")
