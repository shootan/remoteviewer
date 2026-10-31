# report rel-0162 r1 — PC 0.2.162 릴리스 후보 dry-run (작업용1 remote#rqxbj3jb, 2026-10-22)

task t-nmq8ujkx / rev r1 / kind=release-prep-report. 정본 `.claude/task_rel_0162_r1.md`, 근거 `codex_integ_0162_r1.md`.

## 브랜치·commit
- worktree `C:/work/gnlink-wt/rel-0162`, 브랜치 **`rel/0.2.162`**:
  - 9f54912(integ/0.2.162)
  - \+ 버전 **`b4dfaed`** ← 빌드·산출물의 기준
  - \+ 문서 **`6a127eb`**(docs/history.md·docs/작업목록.md 만. `git diff b4dfaed 6a127eb -- apps automation` = 0)
  - tracked clean. `.claude/` 는 미추적이고 rel 디렉터리를 담는다.
- 버전 commit:
  - 이전 6be579b 와 같은 파일 집합: `product_version.hpp` 한 줄, `L"0.2.161"` → `L"0.2.162"`.
  - **BOM `efbbbf` 유지, CRLF 17/17 유지, 크기 1020 B 동일**(바이트 대조).
  - 다른 버전 원천은 없다(소스 전체 grep `0.2.161` 은 이 파일 하나).
- webview2:
  - 처음부터 `robocopy /E` 로 실복사했다(junction 아님): 90 파일 47,602,794 B.
  - 원본 D: 는 전후 90 파일·같은 바이트로 불변.

## dry-run
- `bash automation/gnlink_release.sh --worktree C:/work/gnlink-wt/rel-0162 --version 0.2.162 --dry-run`(Bash, POSIX PATH) → **rc 0**. 로그 `C:/work/gnlink-wt/rel-0162-dryrun.log`.
- 요약 출력:
  - commit b4dfaed, run id 20261022-141151-106, signed no, deployed no.
  - gates: parity PASS · payload-set PASS · checksums PASS · installer payload 10/10.
  - tracked files·SDK inputs·commit 이 preflight 이후 불변.
- rel 디렉터리 `C:/work/gnlink-wt/rel-0162/.claude/rel/0.2.162/`:
  - `windows.manifest`(**미서명**) sha256 `814e4bb9748c94b2ef6f63cac137779a0e20983a27583887b5555442d66bdd54`
  - `SHA256SUMS.txt` sha256 `2a91650d8bdd5165100d475c1bd64c092abb0ea147862da7835769bb0b6012c5` — 11 항목 `sha256sum -c` 모두 OK.
  - sdk set `703a6a41253e5aa4267b8ce8bdc1173bd017c1a4dd1f5bffc6188e2b4a3f0873`(16 파일).
- payload sha256:

  | 파일 | sha256 |
  |---|---|
  | GNLinkSetup.exe | `ed480e252b6a6a3b4910da115dc1b9f94be94e32a754949291568e07d52ae175` |
  | GNLinkViewer.exe | `188f7479c5ed91433429604a56f595d869156f686a2afe2786f4e3a4f8425461` |
  | GNLinkStream.exe | `c6c3c313b62813d1cd5fa1faf52b99903f22da43182e77e3485617137a457d55` |
  | GNLinkHost.exe | `67a4d7658039380f14be2956be7a950f9ae9de37b880e3a6248ce518fb970120` |
  | GNLinkClient.exe | `031117ff186f3931dc317de21404463e341e11b7895da5a74f7435ea73f0dc83` |
  | GNLinkInputService.exe | `ae849c5b3e34e4572fdf49854bf64808fa3b6c401b364606299df3dfcc739191` |
  | GNLinkUpdater.exe | `4a00c365ddd4823b741698dd69004a8eef7f69b2285b7613759adde0cc97282a` |
  | GNLinkCapture.exe | `5091b3748b43d57d9f07f67f6718888eca73040faa695fe19ce857074c8a5b5f` |
  | GNLinkClipHelper.exe | `8d7aade62e02ec8d0b499a82aabcc8732981b4956d4e3d9adaf963c1d2e669c2` |
  | ui/shell.html | `8fac49cd810e1445d36305bf5e62e9e539982fb0b671be86cb9008ee5ad62811` |
  | ui/macro.html | `380c232691d05e74506ce7269dd048d97b343735b1d3c86bd088b88227c1a268` |

## 산출물 검사 (payload 기준)
- **버전 문자열**(UTF-16):
  - Viewer·Stream·Host·Client 각 `0.2.162`=1, `0.2.161`=0.
  - Setup `0.2.162`=5 / `0.2.161`=0. carrier 수는 0.2.161 릴리스(5)와 같다.
  - InputService·Updater·Capture·ClipHelper 는 원래 literal 이 없다(0/0). ASCII 형태도 모두 0.
- **gate** `gnlink_check_fixed_server.py --dir payload` ALL PASS 9, `--self-test` ALL PASS 23.
- **compile define**: 생성 vcxproj 기준 출하 9 타깃 — host_app · host_poc · gdi_capture_worker · secure_input_service · client_shell · client_poc · file_copy_helper · installer · updater.
  - 시험·변이·seam define 0.
  - `REMOTE60_ENCODER_POLL_DIAG=0`(host_poc, 출하값 off — 0.2.161 의 "encoder_poll ship" 과 같은 상태)만 있다.
- **payload 시험 exe 0**: payload 는 제품 9 exe + ui 2 뿐이다.
- **seam·test 문자열 0**: 9 exe 를 ASCII·UTF-16 으로 검사했다. 검사 문자열: `TEST_SEAM`·`_TEST_`·`MUTANT`/`mutant`·`ForTest`·`hello-diag`·`GNLINK_SIS_TEST`·`GNLINK_STREAM_TEST`·`GNLINK_HOST_TEST`·`GNLINK_VIEWER_TEST`·`REMOTE60_HOST_TEST`·`GNLINK_CHAIN`·`TEST BUILD`.
- **shell.html**: 소스 checkout · payload · build `Release/ui` 셋이 `8fac49cd…`(Codex 통합 기록과 같음). 참고로 `git show` blob 해시는 `af2594c3…` 로 다르다. 체크아웃 줄바꿈 변환 때문이다.
- **smoke**(콘솔 세션 qwinsta Active, SLOT CreateNew→삭제, rel build dir 의 시험 실행 파일):

  | smoke | 결과 |
  |---|---|
  | `viewer_udp_recovery_test --real-recv-smoke <payload\GNLinkStream.exe>` | rc 0 PASS |
  | host_punch_reply_e2e | rc 0, 102/0 |
  | hello_backlog | rc 0, 28/0 |
  | punch_any | rc 0 PASS |

## 하지 않은 것·상태
- 서명·NAS 업로드·manifest/sig 교체·push·사용자 PC 설치/재시작/서비스 조작은 하지 않았다. repo root 밖 삭제 0.
- 잔여 시험 프로세스 0.
- **상태: 로컬 실기 후보(게시 전).** OS 실기는 미검증이다: LK A~D, DW 회사 PC, hello 실 LAN·Android 기기.
- ledger 유지: directory_retry 간헐, viewer_udp_recovery 처리량 FAIL(원인 미확정), APK S5.

다음 담당: 검증용 — 고정 후보 b4dfaed/payload 해시 독립 확인 → 서명 판단(검증용) → Codex FINAL_PACKET.
