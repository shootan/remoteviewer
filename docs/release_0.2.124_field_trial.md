# 0.2.124 — 기존 업데이트 채널을 이용한 비상용 실기 배포

## 목적과 허용 범위

사용자 지시(2026-09-14): 아직 상용 운영 전이며, 롤백을 전제로 기존 업데이트 경로에서 편하게 설치해 시험한다. 따라서 별도 시험 링크만 제공하는 대신 **기존 Windows 업데이트 채널에 0.2.124를 게시**한다. 이 선택은 미측정 GPU 부하를 해결했다는 뜻이 아니며, GPU/게임/WAN/다중 세션/장시간 결과를 이 배포로 확인한다.

현재 문서 상태: **패키지 준비·게시 전 독립 검증 대기**. 실제 게시 결과는 아래 게시 기록으로 갱신한다. 사용 중인 Host/Client의 설치·재시작·UAC 조작은 자동 수행하지 않는다. main merge와 git push도 하지 않는다.

## 후보

- 작업 트리: `D:/remote/remote/.claude/worktrees/remote-game-stability`, branch `fix/remote-game-stability`, build-incident Release.
- 이미 검증용 MERGE_OK를 받은 안정화 코드 45개는 해시 변화 없음. 제품 소스 변경은 `product_version.hpp`의 0.2.123→0.2.124뿐.
- 배포 스크립트는 별도 보수: NAS lock을 삭제하지 않고 기록 폴더로 이동, 부분 manifest 교체 실패를 정직하게 보고하고 임시 파일 보존, 로컬 임시 파일/테스트 정리는 repo 안으로 한정, dry-run은 publish lock 미취득.
- 두 HTML은 Git 원문의 서명 대상 LF 바이트 사용. shell `3e59eb07...`, macro `f589f5df...`로 이전 게시본과 동일.
- 패키지: `.claude/rel/0.2.124/`, 파일별 크기/해시와 검증은 `release-report.json`.
- manifest SHA256: `dfbcc4b3f996572c2fc6f2e229802c0a85c8c7025108c3ffa06f5042f4a8e435`.
- signature 파일 SHA256: `73ef2827aa78a0e4e3f4822541df765f45f9f7e126490055290fda12d40a6b35`.
- 운영 P-256 키는 이 PC의 DPAPI 보호 저장소에서 메모리로만 사용했다. NAS에는 공개 파일·manifest·서명만 전달한다. 이는 manifest 서명이며 exe Authenticode 인증을 새로 추가한 것은 아니다.
- Git MCP bridge의 worktree repo_path 동작을 확인했다. 고정 빌드 소스와 동일한 변경을 Git MCP로 커밋하며 실제 commit hash는 release-report.json 및 게시 기록에 기록한다. main merge/push는 하지 않는다.

## 게시 전 확인

- 제품 `default_verifier()`로 실제 0.2.124 서명 `Ok`, 1바이트 변조는 `SignatureInvalid`.
- 필수 payload 10/10, 설치기 RCDATA 9/9가 서명된 payload의 실제 바이트와 동일.
- manifest 회귀 85/0, update_effects 219/0, 실제 WebView2 업데이트 UI 27/0, 모두 exit 0.
- 배포 자동화 회귀는 스크립트가 지정한 0.2.109-r2 signed fixture로 PASS. 0.2.124 payload는 별도 실제 해시/서명 검증을 수행했다.
- 새 작업 트리의 줄바꿈 자동 변환으로 signed test_manifest 벡터가 달라진 최초 실패는 보존했다. Git의 정확한 LF 서명 원문으로 복원한 뒤 genuine signature 회귀를 통과했다. 제품 0.2.124 manifest는 처음부터 LF bytes로 생성했다.
- 이전 0.2.123은 인증된 HTTPS `/api/update/manifest?platform=windows`에서 HTTP 200/version123 확인, 제품 서명 검증 통과. 401만으로 확인하지 않았다. 인증값은 메모리에서만 사용하고 출력/보존하지 않았다.
- 검증용 task: `t-kw1maaaa`. 기존 채널의 **비상용 실기 게시 OK**를 패키지 해시에 고정해 요청했다.

## 롤백

1. **안내 채널 복구**: `.claude/rollback/0.2.123/`에 NAS의 이전 manifest/sig와 payload 10개를 확보하고 모두 검증했다. `restore-channel.ps1`은 동일 배포 절차로 이전 쌍을 복원하고 인증된 HTTPS에서 0.2.123을 확인한다. 아직 실행하지 않았다.
2. **이미 설치한 PC 복구**: 채널을 0.2.123으로 되돌려도 앱은 자동 다운그레이드되지 않는다. 이전 `https://rem.shotan.net/updates/0.2.123/GNLinkSetup.exe`를 실행해 **Reinstall**을 선택한다. 이전 설치기는 더 높은 버전이 설치돼 있으면 Reinstall을 제공함을 소스로 확인했다. 실제 사용자 PC에서 downgrade 실기는 미수행이다.
3. manifest만 또는 signature만 복원하지 않는다. 두 파일은 정확한 기존 짝으로 복원하고 해시/서명을 확인한다. 새·이전 버전의 아티팩트 경로는 덮어쓰거나 삭제하지 않는다.

## 게시 기록

게시 전. 공개 버전/URL/서버·외부 해시/검증용 판정/실기 잔여를 게시 완료 후 이 절에 기록한다.
