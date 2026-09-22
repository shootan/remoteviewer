# 0.2.127 최종 근거 확인 — 2026-09-15

**Windows 배포 완료(마일스톤) · 실기 테스트 대기.** 전체 장애 해결이나 모든 시험 통과라는 판정은 아니다.

## 고정 대상

- 기능 기준: `6b119af`; 릴리스 source commit: `939021141f61c9ba352a77825aef267ddc1d5e81`.
- branch/worktree: `audit/host-pc-recovery`, `D:/remote/remote-worktrees/host-pc-recovery`.
- source 차이: 버전 상수0.2.126→0.2.127 한 줄. 이후 기록 커밋은 제품 소스 변경 없음.
- 설치기: [GNLinkSetup.exe](https://rem.shotan.net/updates/0.2.127/GNLinkSetup.exe).
- 설치기 SHA256: `8339ab3cfc2f151c1564c71d6058099fc3ee991cc97b3161a8b3a9dc904ffbd6`.
- manifest SHA256: `aed57a2c5976c75112d446249377a219673015c2f12c699cceb506d27ce3f3bd`.
- signature SHA256: `85411ebbc089da479d65baf07535a9ac3040d778b2a5e75d2c04985e45ed3a4f`.

## Codex가 직접 대조한 근거

1. HTTPS에서10개 파일 직접 다운로드, 서명 manifest의 size/SHA256과10/10 일치. NAS live pair도 같은 고정 manifest/signature와 일치. 제품 공개키를 읽는 Node 검증기 및 문서 변조 거부 확인. `.claude/codex-release-127-check/verification.json`.
2. NAS126 rollback pair와 로컬126 백업 일치. 서버 `manifest-backups/0.2.126` 및 `.claude/rollback/0.2.126` 보존. 설치된 앱의 자동 강등을 뜻하지 않음.
3. 검증용의 독립 UI 로그·account-b 복구 화면 직접 열람. 제품 Shell/native/실 HTTP 경로를 사용한 시험이며 설치/UAC 실행은 아님.
4. 검증용의 **게시 바이너리 격리 실행 로그** 직접 열람: `.claude/verify-runtime/stream.log`와 `viewer.log`에서 Stream/Viewer가 localVersion와 peerVersion을 양방향0.2.127로 보고. `pids.txt`는 양쪽 exit0, Stream은 done. 스크립트는 payload를 loopback43900/43901 및 전용 LOCALAPPDATA로 실행한다. 실제 실행은 검증용이 수행했고 Codex는 로그·명령·파일을 대조했다.
5. 실행 대상 Stream/Viewer의 현재 SHA256을 직접 계산해 공개 파일/manifest와 일치 확인:
   - Stream: `8dad83f34e8df74da49332a9856f4a34c4df6641f4313cabd42454c83805ba02`.
   - Viewer: `fcd3f73525f247958a58fed8a79879f4df661bee1c25e628735e7a28952e3013`.

검증용이 독립 수행한 나머지 근거: fresh configure/build, Native13종, 서버 전체 회귀, 설치기 RCDATA9/9 대조, 제품 verifier와 body/sig 변조 거부. 정본 `.claude/reviewer-findings.md`. Codex의 실제 수행과 검증용 결과 인용을 구분한다.

## 남은 실패·범위

- HW MFT `host_encode_epoch_test`의 기대 불충족1건은 미해결. `holdUs=800000`은 입력 timestamp 상수로 후속 호출을 구분한 값이지 벽시계800ms 지연이 아니다. 실제 경과시간·내부 drain·현장 지연과의 연관은 미확정이다.
- 검증용이 보고한 main2/16·병합1/16 집계는 각 회차 원시 로그/실행 순서가 없어 Codex가 재집계할 수 없다. main측 빌드 산출물도 소실됐다. 성능 개선·무악화·병합 귀책 완전 배제의 근거로 사용하지 않는다. 남은 원본과 복원된 소스 checkout을 보존한다.
- 기능 스위트의 독립 실행은6b119af 빌드 기준이다. 버전 상수만 변경한9390211의 릴리스 제품은 clean build·패키지·버전 교환을 검사했으며 전체 기능 스위트를 같은 릴리스 빌드에서 다시 실행한 것은 아니다.
- 격리 version-exchange 실행은 캡처 유휴·전송0프레임. 인코딩/전송 성능이나 사용자의 원격 세션 복구를 입증하지 않는다. 실제 GPU·다중 세션·WAN·게임·soak·UAC/설치 실기는 남음.
- **NAS 서버 저장/인증 수정(B06/B07, 27d9e74)은 미배포.** 서버 `server.js` SHA256은 `e32510c3c6a7d76a6b5cb36f59a06737c6f0e7b1a5290287a595a48d8b1f7ca5`로 main437c87c와 같고, 이 브랜치의 `d02a4854faa7c20a617f8222c5ca23472e0942c5b7ee4040f898a4371a0a612c`와 다름. 이번은 Windows 패키지 게시이며 NAS 서버 코드는 교체하지 않았다.
- 인증된 update HTTP endpoint 실호출은 미수행. 401만으로 계약 정상이라 주장하지 않는다. NAS live pair/공개 아티팩트/서명 확인과 구분한다.

Stream 실행 버전은 보완 검증으로 확인됐지만, 이전 REL-V01의 정확한 발생 원인까지 확정한 것은 아니다. 첫 게시 때 이 실행 검증이 누락됐고 이후 보완했다는 순서도 유지한다.

main 역머지·push·사용자 앱 설치/종료/재시작 없음. 지금 사용자가 할 수 있는 다음 단계는 기존 업데이트 경로나 위 Setup으로 업데이트한 뒤 실제 연결을 확인하는 것이다. 이 문서는 자동 설치 지시가 아니다.
