# PC 0.2.158 — Codex 최종 게시 대조

2026-10-14 KST. **M4 배포 완료(마일스톤) · 현장 실기 대기.** 사용자 창구는 검증용 `remote#34id7hp2`이며, 이번 기록은 그 게시 보고에 대한 Codex의 독립 외부 대조다.

## 범위·고정점

- M4 제품 `7bcfd40` → 검수 후보 `2b5b64af6a125a1aa05f03d36f587455687b8b78` → docs 정정 `98786c5` → 버전 `dbf42cedaf8f60c3d49cc2188cf75039abe14914` → 게시 기록 `118420f9a09fdb9a5d75187bab5459ed88a718cd`.
- 후보에서 최종 HEAD까지의 diff는 PC 버전 한 줄과 history/구현계획/작업목록뿐이다. 제품 M4 범위 유지, tracked 변경 없음. `git ls-remote origin refs/heads/main`도 `118420f`와 일치했다.
- 변경은 긴 무프레임 뒤 PC Viewer의 통계 창이 가짜1fps를 연속 보고하던 경로의 보수다. 적용점은 **접속하는 PC의 뷰어**. Host 정제/MinQP/floor/M2/글자 선명도 보수는 이번 출하에 포함되지 않는다.

## 직접 실행한 확인

원시 결과는 `result.json`, 재현 코드는 `verify.py`. 확인 시작 UTC `2026-10-14T00:54:44.749219+00:00`.

- SSH로 NAS의 실제 `/opt/gnlink/update-manifests/windows.manifest`와 `windows.sig`를 읽어 보존했다. 둘 다 로컬 release와 바이트 일치. version `0.2.158`, releaseId `r-0.2.158`.
- 공개키 환경 override를 제거한 프로세스에서 저장소 운영 공개키의 `gnlink_verify_manifest.js` 실행: 원본 서명 수락·변조 문서 거부, exit0.
- manifest의 공개 HTTPS URL **11개 직접 다운로드**: HTTP200·길이·SHA-256 모두 서명값 및 로컬 payload와 일치.
- dist의 `GNLinkSetup-0.2.158.exe`가 공개 Setup과 같은 해시.
- 버전 wide 문자열을 가진 payload는0.2.158, 모든 exe에서0.2.157 wide 문자열 없음. 버전 문자열 자체가 없는 exe는 문자열 검사만으로 버전을 단정하지 않는다.
- 출하 Viewer에 M4의 `statWindowUs=`·`statPublished=` 표시가 있고, 새 chain 하네스의 식별 문자열4종과 RSDS는 없다. 소스 범위/릴리스 gate를 보강하는 확인이며 문자열 검사만으로 모든 시험 기능 부재를 증명했다고 주장하지 않는다.

| 항목 | SHA-256 |
|---|---|
| live manifest | `78df5305b70cc429e6779d484a4a534bf13875d81e05e2fc6e5f8da263eb9306` |
| live sig | `6dce1ca98d483da0e2a7dafa70077d3f25d340724baef415fb2b301c99a0f77f` |
| Viewer | `48fa938cfc34cace6ac619bf1780be7fa9fdfe844ba1be41e41c24466518a511` |
| Setup | `002ea1011029a4b39bb697fde78f706d0ca6af94d77a1f9aab4de49733976d67` |

공개 Setup: https://gnlink.shotan.net/updates/0.2.158/GNLinkSetup.exe

## 로그·실행 보고와의 구분

- main worktree `.claude/rel/release_0.2.158.log`: build commit dbf42ce, parity/payload-set/checksums/installer10/10, 서명 검증, RELEASE_EXIT=0 기록을 읽었다. Codex가 출하 빌드를 재실행한 것은 아니다.
- `.claude/rel/deploy-0.2.158.log`: server11/11,0.2.157 pair 백업(05167241…/cb5eb786…), pair 게시·외부 확인·재시작 불필요 기록을 읽었다. 이번 SSH/HTTPS 검사는 그 로그와 별개로 실행했다.
- 루트 `.claude/rel/integ_0.2.158/summary_integ158.txt`: stats window28/0·real-recv-smoke·RC transitions72 통과, 기존 epoch/defer 실패 기록을 확인했다. 모든 통합 시험이 최초 실행에서 통과했다고 표시하지 않는다.
- `integ158c_vstats_chain_gap_amp.log` 원문: 보고25/25초·가짜0·ABR high·FrameBuffer28.36fps/host28.76fps,17 checks/0 failed/0 not judged, RESULT PASS. 앞선 독립 old/fixed 비교는 SOURCE 검토 기록 `C:/work/gnlink-wt/fix-viewer-stats/.claude/codex_viewer_stats_2b5b64a.md`에 구분돼 있다.
- Start-Process 리다이렉트 멈춤은 별도 시험 결함이다. `8b312a9`의 diff가 **하네스 cpp 한 파일뿐**이며, product thread 실행 중 rdbuf 교체 제거와 watchdog 추가임을 읽었다. 이 commit은 출하 dbf42ce의 조상이 아니다. 하네스 보수의 반복 실행 수치는 작업용/검증용 보고이며 Codex가 재현하지 않았다. 이 보수를0.2.158 제품 수정으로 합산하지 않는다.
- `98786c5`와 `118420f`의 docs 정정에서 기존 flaky 쌍, M4 적용/실기 범위, 구현계획의0.2.158 배포·현장 대기 상태를 확인했다.

## 남은 사용자 행동·한계

접속하는 PC의 뷰어를 업데이트한 뒤 평소 사용하면 검증용이 NAS 로그에서 가짜보고/ABR 강등을 확인한다. 실제 인앱 업데이트, 현장 keyframe/selection 대기 복귀·WAN/relay, GNLinkViewer.exe UI·공유 D3D decode/present는 이번 Codex 외부 대조로 검증한 것이 아니다. 실제 체인 시험은 제품 receiver/control/host ABR와 FrameBuffer 도착을 검증한 범위로 유지한다.

motion-settle의 글자 복구·새 움직임 응답성은 별도 진행 중이다. 사용자 설치본·프로세스·NAS 서비스/게시 파일을 Codex가 변경하지 않았으며, 미추적 확인 기록만 작성했다. 공식 릴리스 기록·commit/push는 검증용이 수행했다.
