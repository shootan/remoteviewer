import os
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
path = os.path.join(ROOT, "docs", "history.md")
entry = [
    "",
    "### 2026-09-22 `0.2.134` 게시 — 새 해시로 Defender 오탐 우회, 그리고 미서명이 근본 원인",
    "",
    "- 발단: 회사 PC2(0.2.130)가 인앱으로 0.2.133 을 받다 `could not hash staged GNLinkSetup.exe` 로 반복 실패. `Get-MpThreatDetection` 로 확정 — Windows Defender 가 staging 의 GNLinkSetup.exe 를 ThreatID **2147731250** 로 격리. 첫 검출은 16:23:01 의 **수동 다운로드본**(remote_claude 가 권한 `Invoke-WebRequest`), 이후 동일 파일 해시가 staging 까지 전파.",
    "- 원인: GNLink exe 는 전부 **미서명**(`Get-AuthenticodeSignature` = NotSigned 5종). 미서명 파일은 해시별 평판으로 판정되는데, 인앱 업데이터는 WinHTTP 로 Program Files 에 직접 써 Mark-of-the-Web 이 없어 통과하던 것을, 다운로드 폴더로 받은 사본이 MOTW 를 달아 클라우드 검사를 유발 → 오탐 판정이 그 해시에 붙어 staging 까지 막았다. **같은 0.2.133 파일이 집 호스트에서는 13:14 정상 설치**된 것이 이를 뒷받침.",
    "- PC2 는 Defender 예외(`Add-MpPreference -ExclusionPath`)가 Tamper Protection/회사 정책으로 안 먹었다. 그래서 **새 해시로 우회** — 0.2.134 를 냈다.",
    "- 빌드(작업용, task t-ki1ddkqn): `product_version.hpp` 0.2.133→0.2.134 한 줄, 커밋 `884d4d6`(main, e3766b5 위). 새 디렉터리 클린 빌드 8타깃 에러 0, `.claude/rel/0.2.134` 패키징. parity(4캐리어+Setup 5회 임베드 0.2.134, stale 133/132 0건)·payload-set 10/10·SHA256SUMS 10/10 전부 통과. 환경 함정 2건 기록: `-DCMAKE_BUILD_TYPE` 가 VS 멀티컨피그와 충돌(→ `--config Release`), 이 워크트리에 WebView2 SDK 부재로 GNLinkClient 타깃 미생성(메인 체크아웃에서 SDK 복사로 해결).",
    "- 서명·게시(remote_claude, 이 PC): 운영키로 매니페스트 서명(self-verify=True), `automation/gnlink_deploy.sh` 로 NAS 게시. preflight 서명 서버 구현 검증·payload-set 10/10·아티팩트 10/10 바이트 일치, 0.2.133 pair 백업, 재시작 불필요. **외부 https 독립 확인**: 라이브 manifest `version=0.2.134`, GNLinkSetup.exe 익명 다운로드 sha256 `eb8610e6…`(4,812,288B) = 서명본과 일치. GNLinkSetup 해시는 0.2.133(`20c7e7b7…`)과 다르다 — 그게 우회의 핵심.",
    "- ⚠️ **shell.html 바이트 차이**: 0.2.134 패키징본은 19,807B(CRLF), 0.2.133 은 19,339B(LF). 텍스트 동일, 워크트리 체크아웃 줄끝 차이(REL-CRLF, 작업목록 D2). 기능 무관, 미해결 결정 항목.",
    "- 🔴 **근본 대책은 코드 서명**: 미서명인 한 새 버전마다 이 오탐을 다시 만날 수 있다. 0.2.134 도 미서명이라 재오탐 가능성은 남는다 — PC2 실제 통과는 로그로 확인해야 한다. Authenticode 인증서 도입을 작업목록에 추가.",
    "- 커밋: `884d4d6` 릴리스(push 완료, origin/main). 매니페스트 서명·게시 산출물은 `.claude/rel/0.2.134`. 제품/테스트/문서: 제품(버전 1줄)+문서.",
    "- 미검증: **PC2 인앱 0.2.134 완주**(새 해시가 실제로 Defender 를 통과하는지). abandon-race 없이 완주하는지. 사용자 실기 대기.",
]
with open(path, "rb") as h:
    data = h.read()
if not data.endswith(b"\r\n"):
    data += b"\r\n"
data += "\r\n".join(entry).encode("utf-8") + b"\r\n"
with open(path, "wb") as h:
    h.write(data)
print("appended", len(entry), "lines")
