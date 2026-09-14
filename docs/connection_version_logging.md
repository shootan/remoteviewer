# 연결별 실행 버전 교환 로그 — 0.2.126

## 목적
회사 호스트의 11:30 로컬 health 배너는 0.2.124였으며, 11:35 정지 시각의 실행 파일 버전은 그 발췌만으로 단정하지 않는다. 새 연결에서 양쪽 실행 바이너리가 자기 버전을 보내고 상대가 받은 버전을 기록한다.

## 기록
- Viewer: `[connection-version] localProcess=GNLinkViewer localVersion=0.2.126 peerProcess=GNLinkStream peerVersion=0.2.126 peerVersionSource=peer-report`
- Stream: `[connection-version] localProcess=GNLinkStream localVersion=0.2.126 localPid=... peerProcess=GNLinkViewer peerVersion=0.2.126 peerVersionSource=peer-report seq=...`
- 연결 시작에는 자기 버전과 상대 미확인 상태를 먼저 기록한다. 첫 Pong 후 지원 capability가 있으면 연결당 한 번 교환한다. 새 Viewer→구 Host는 `unknown-unsupported`, 구 Viewer→새 Host는 `unknown-awaiting-exchange`로 남아 구버전 숫자를 추정하지 않는다.
- 위 버전은 각각 실제 Viewer/Stream 실행 바이너리의 컴파일 버전이다. Client GUI/Host 감독 앱이나 설치 디렉터리 전체의 일치까지 보장하는 값이 아니다. 혼합 설치는 별도 해시 대조가 필요하다.
- 상대 버전은 상대가 보고한 값이며 원격 바이너리 무결성 증명이 아니다. 최대31 ASCII 문자(영숫자/점/하이픈/더하기)만 기록하고, 개행·공백·미종료 문자열은 `unknown-invalid`로 대체한다.
- Stream 버전 로그는 기존 stdout/NAS 업로드와 로컬 고정 진단 bank에도 남는다. Viewer 로그는 기존 수집 경로를 사용한다.

## 호환성
기존 Hello/Ping/Pong 크기 49/20/184 bytes 불변. Pong의 미사용 capability 0x40을 알리는 새 호스트에게만 ControlVersionRequest(49)/Response(50)를 보낸다. 구버전 호스트에 미지원 opcode를 보내 응답 대기하지 않는다. 요청/응답은 기존 TCP 또는 UDP ControlLink의 메시지 경계를 사용한다.

## 검증
`remote60_peer_version_test`: 구버전 0바이트 전송, 양방향 버전 값, 응답 seq/size 검증, 로그 주입 방지, 실제 loopback TCP + 제품 ControlLink로 버전 교환 PASS. 제품 Viewer/Stream 연결 경로에 연결당 한 번의 로그와 기능협상 추가. 검증용 독립 코드/패키지 검증 통과, 기존채널 게시 및 Codex 외부 확인 완료. 실제 신·구 조합 현장 로그는 사용자 업데이트 후 확인.

## 범위
0.2.126 진단 기능이며 게임 정지/지연 해결 선언이 아니다. 현재 사용 중인 앱 설치·재시작, main merge, git push는 수행하지 않는다. 양쪽126 업데이트 후 정확한 상호 버전을 볼 수 있고, 구버전 상대는 unknown으로 명시한다.

## 게시 완료 — 2026-09-14
- 버전0.2.126, source commit `e444db6c7ceda2a71e620908663e7dbf1d7994aa`. 검증용 FINAL_PACKAGE_VERDICT: 코드+패키지 기존채널 비상용시험 PACKAGE OK.
- manifest SHA256 `8758094d9771b3902d043165b0fd46940573b891e234a73459444414df471ec5`, signature파일 SHA256 `5b25ac3259780c2b216bebf92b3cd8f0c1c9fb6aa2e1590482882a393b7af431`.
- gnlink 배포rc0, NAS·공개HTTPS10개hash일치. 인증API HTTP200/version126, 수신pair 고정hash일치 및 제품verifierOk126/1byte변조SignatureInvalid 확인.
- 설치기 https://rem.shotan.net/updates/0.2.126/GNLinkSetup.exe SHA256 `7830ad59d702d55a2f499d9fa65d2a9065d8e9dbb4ee5ce556a1d9a900b6c5c5`. worktree dist/GNLinkSetup-0.2.126.exe 동일.
- 0.2.125 복구쌍: NAS `/opt/gnlink/manifest-backups/0.2.125/`, 로컬 `.claude/rollback/0.2.125/` 전체10파일/restore스크립트. 채널복원은 이미설치한PC 자동다운그레이드가 아님.
- 실행근거: build-incident/release-0.2.126-publish.log 및 .claude/rel/0.2.126/published-https/. 여기 외부확인은 Codex직접실행, 검증용 추가독립확인은 seq2299로요청.
- 배포완료(마일스톤)·사용자양쪽업데이트/실기대기. 설치·프로세스재시작·main merge·git push 미실행.
