# CLAUDE.md

## 응답 규칙 (필수)

- **모든 응답의 마지막에는 "수행된 작업" 요약을 반드시 명시한다.**
  - 무엇이 변경/실행되었는지: 파일, 커밋 해시, 빌드/테스트 결과, 산출물(dist 등).
  - 분석·조사만 한 턴이면 "수행 작업: 분석만, 파일 변경 없음"처럼 명시한다.
  - 진행 중(빌드 대기 등)이면 "진행 중 작업"과 "완료된 작업"을 구분해 적는다.

## 테스트 규칙 (필수)

- **테스트를 돌리기 전에 RDP 접속 상태인지 먼저 확인한다.**

  ```
  qwinsta
  ```

  `rdp-tcp#N` 세션이 `Active` 이면 RDP 접속 중이다. (`$env:SESSIONNAME` 은 내 셸이 속한
  세션만 보여주므로 부족하다 — 콘솔에서 돌고 있는데 다른 곳에서 RDP로 붙은 경우를 놓친다.)

- **RDP 접속 중이면 테스트를 진행하지 말고, 사용자에게 RDP 종료를 요청한다.**
  사용자가 "종료했다"고 알려준 뒤에 테스트를 진행한다.

- **이유** (실측):
  - RDP 접속 중에는 DXGI 캡처가 `0x80070005`로 막혀 WGC 저품질 폴백으로 떨어진다.
    DXGI/readback 경로(NV12 lease, frozen-ring, 캡처 cadence)는 아예 판정이 안 된다.
  - `Microsoft Remote Display Adapter`의 갱신률이 낮다(2026-08-29 실측 **32Hz**).
    GDI 캡처는 화면 갱신률에 묶이므로 `remote60_gdi_capture_process_test`가
    `fps >= 50` 요구를 **물리적으로** 만족할 수 없어 항상 FAIL 한다.
    이걸 코드 회귀로 오진하기 쉬우니, 실패 시 먼저 `qwinsta`부터 볼 것.

- 즉 RDP 상태에서 나온 테스트 결과는 **PASS든 FAIL이든 판정 근거로 쓰지 않는다.**

## 3역할 절차에서 두 Claude 세션의 행동·보고 기준 (2026-09-07)

`AGENTS.md` "Agent Role Separation" 의 흐름(Codex 계획 → 검증용 Claude 검토·확정 → 작업용 Claude 구현 → 검증용 검사·OK → Codex 최종 확인)을 두 Claude 세션이 어떻게 수행하는지 정한다. 어느 역할인지는 GMux 버스 탭 displayName(검증용claude / 작업용claude)과 세션 시작 시 `a2a_whoami` 로 확인한다. 세션 ID·resume UUID 는 여기 적지 않는다.

- **검증용 Claude**
  - Codex 계획을 코드·로그·diff 로 독립 검토한다. 이견·위험·누락은 근거(파일:줄, 로그 경로)와 함께 Codex 에 수정 요청하고, OK 면 Codex 재승인 없이 작업용에 A2A task 로 직접 위임한다(범위·순서·완료 기준·보고 형식 명시). 계획 변경이 필요하다고 판단한 부분은 Codex 의 수정·확정 전에는 작업용에게 위임하지 않는다. 변경 없이 OK 한 독립 범위는 바로 위임할 수 있다.
  - 구현 결과는 diff·테스트·산출물을 독립 검사한다. 검증 목적의 빌드/테스트 실행과 검토 기록 작성은 가능하나, 기록은 `.claude/` 등 저장소 미추적 위치에 둔다. 저장소 추적 파일(제품·문서)은 수정·커밋하지 않고, 자기 수정물을 자기 승인하지 않는다(파일 변경이 없으면 사유를 밝히고 커밋을 생략한다).
  - "구현 OK" 는 고정 형식으로 낸다: 대상 commit 해시, 검토 범위, 검증용이 직접 실행한 테스트/빌드와 작업용 보고의 구분, 산출물 식별(파일명·크기·sha256·빌드 commit), 미검증 한계. OK 뒤에 제품/설정/테스트/산출물이 바뀌면 그 commit 은 다시 검사한다. 결함이 남으면 NEEDS_CHANGES, 환경 탓에 검사하지 못한 부분은 "미검증" 으로 분리한다.
- **작업용 Claude**
  - 검증용이 OK 한 계획만 구현한다: 파일 변경, 빌드·테스트, 버전/설치본, `docs/history.md`·`docs/구현계획.md` 기록, Git MCP 커밋. 계획 밖 발견은 고치지 않고 기록/보고에 적는다.
  - 완료 검사는 먼저 검증용에 요청한다(commit 해시별 변경 파일, 실행한 테스트와 로그 경로, 수정 전 FAIL/수정 후 PASS 구분, sweep 결과와 환경 실패 분리, 실행·재시도 횟수, 산출물 식별, 미검증 항목). 검증용의 수정 요구는 같은 범위 안에서 반영해 재검사를 받고, 범위 변경은 검증용을 통해 Codex 와 조율한다.
  - 검증용의 명시적 OK 뒤에만 Codex 에 최종 확인을 요청한다(진행 보고는 언제든 가능). 전체 task 는 그 전까지 working 으로 두고, subtask 완료와 전체 완료를 구분한다. 단독 실행 PASS 로 재현된 회귀 FAIL 을 상쇄하지 않는다.
- **공통**: 답장·위임은 identity+sessionId 로 pin 한다. 이 문서의 응답 규칙(수행된 작업 요약)·테스트 규칙(`qwinsta`)·로그 위치 규칙은 두 역할 모두에 그대로 적용된다.

## 릴리스 배포 계정 (NAS)

- **배포는 `gnlink` 계정으로 이 PC에서 직접 한다.** 키는 `~/.ssh/remote60_deploy`(등록된 공개키
  `remote60_deploy.pub`), 접속은 `ssh -i ~/.ssh/remote60_deploy gnlink@192.168.0.6`.
  키 전용·LAN 한정(`sshd AllowUsers gnlink@192.168.0.*`)이고, 홈은 `/opt/gnlink`(OS 디스크).
- 이 계정이 **소유**하므로 sudo 없이 되는 것:
  - `/opt/gnlink/updates/<버전>/` 생성과 아티팩트 게시
  - `/opt/gnlink/update-manifests/{windows,android}.{manifest,sig}` 교체
- **sudo가 없어 못 하는 것**: nginx 설정, systemd 유닛·dropin(=env 변경), ACL, 패키지 설치.
  이들은 최초 구축(0.2.108) 때 끝났으므로 **평시 릴리스에는 필요 없다.** 필요해지면 그때만 사람이 붙는다.
- **서비스 재시작은 평시 불필요하다.** `server.js`의 manifest 핸들러가 요청마다 디스크를 읽고 캐시하지
  않으므로, `.manifest`/`.sig` 두 파일을 짝으로 교체하면 다음 요청부터 반영된다.
  재시작이 필요한 경우는 **env를 바꿀 때뿐**이고, 그건 위의 "못 하는 것"에 해당한다.
- 게시 순서는 고정이다: **아티팩트 전량 업로드 → 크기·SHA256 전량 검증 → `.manifest`+`.sig`를 짝으로
  마지막에 원자적 교체**(`.tmp` → `mv`). 반대로 하면 manifest가 아직 없는 파일을 가리키는 창이 생긴다.
- 아티팩트는 **버전별 불변 경로**에 둔다. 기존 버전 디렉터리를 덮어쓰거나 지우지 않는다
  (manifest가 가리키는 URL의 바이트가 나중에 달라지면 서명이 무의미해진다).
- 롤백은 **`.manifest`+`.sig` 두 파일을 정확한 짝으로 원복**하는 것이 전부다. 아티팩트는 그대로 둔다.
- 게시 전후로 **문서와 디스크의 해시가 같은지** 확인한다. 서명 검증은 "문서가 서명 이후 바뀌지 않았다"만
  말하지 **"문서가 실제로 존재하는 파일을 가리킨다"는 말하지 않는다.** 둘은 따로 확인해야 한다.

### 배포는 손이 아니라 스크립트로 한다 (2026-09-10)

- **`automation/gnlink_deploy.sh` 가 위 절차의 구현이다.** 손으로 하는 게시는 하지 않는다 —
  위 순서는 전부 *"빼먹기 쉽고 나중에 보이지 않는"* 단계이고, 0.2.108 의 낡은 해시와 403 이 정확히 그 자리였다.
  ```
  automation/gnlink_deploy.sh --release-dir .claude/rel/<버전> --verify-only   # 남의 서버 상태를 처음 볼 때
  automation/gnlink_deploy.sh --release-dir .claude/rel/<버전> --dry-run
  automation/gnlink_deploy.sh --release-dir .claude/rel/<버전>
  ```
- **처음 쓰는 서버에는 `--verify-only` 부터.** 불필요한 재게시·재시작을 하지 않고, **테스트 목적으로 버전을 올리지 않는다.**
- 스크립트가 지키는 것: **재실행 멱등** · **동시 배포 배타(lock)** · **같은 버전 다른 bytes 거부**(게시된 URL 의 바이트가 달라지면 그 URL 에 대한 서명이 전부 거짓이 된다) ·
  **모드 명시 지정**(umask 에 기대지 않는다) · **실패 시 이전 릴리스 유지** · **비밀 없는 진단**.
- **`gnlink` 로 불가능한 작업은 `ESCALATE` 로 구체 조작을 출력하고 exit 3.** 그 출력을 그대로 NAS 세션에 넘긴다 —
  *"NAS 에 넘겨라"* 만 적으면 추측이 옮겨갈 뿐이다.
- **`sudo` 를 우회하지 않는다.** 권한 승격·ACL 완화·다른 서비스 변경은 이 스크립트의 일이 아니다.
- **서버앱(디렉터리 서버)을 갱신할 때 산출물 정의에 반드시 포함할 것**:
  - **의존 모듈 전량** — `server.js` · `update_manifest.js` · `version_compare.js` · `package.json`.
    ⚠️ 지난번 *"server.js 단독"* 으로 올려 첫 재시작이 `MODULE_NOT_FOUND` 였다. 외부 npm 의존은 없다(전부 node 내장).
  - **env 차분**, **재시작 필요 여부**.
- **평시 릴리스는 재시작하지 않는다** — manifest 핸들러가 요청마다 디스크를 읽는다(`apps/directory/server.js`).
  재시작이 필요한 경우는 **env 를 바꿀 때뿐**이고, 그건 `gnlink` 권한 밖이라 NAS 경로다.
- manifest 엔드포인트 확인에 자격이 필요하면 **승인된 전용 credential 을 안전하게 넣고 출력에 노출하지 않는다.**
  ⚠️ **무인증 401 만으로 업데이트 성공을 검증하지 않는다** — 잠금장치를 확인한 것이지 방 안을 본 것이 아니다.
- 회귀는 `automation/gnlink_deploy_test.sh` (격리 target · dry-run · 멱등 · 실패 전 공개 불변 · 서명/해시 실패 거부 · lock).
  ⚠️ **ssh 와 curl 은 이 회귀가 대신하지 못한다.** 그 둘은 실서버에서만 드러난다.

## 기타

- 워크플로우·커밋 규칙은 `AGENTS.md`를 따른다.

## 로그 위치 (필수 인지)

- **사용자가 증상/현상을 보고하면, 추측하지 말고 항상 먼저 NAS 로그를 열어 근거로 판단한다.**
  (viewer/host/apk(폰) 3종 모두. 로그로 확인 전에는 원인·수정 방향을 단정하지 않는다.)
- **host 로그도 무조건 NAS 것을 본다.** 이 PC 가 호스트여도 로컬 `%LOCALAPPDATA%\GNLink\host_app.log`
  사본을 보지 말 것 — NAS 의 host.log 를 봐야 viewer/apk 와 같은 시각·같은 출처로 대조된다.
  ("NAS 부하 줄이려고 로컬 본다" 같은 예외 금지. NAS 는 idle 이고, 일관성이 우선이다.)
- **host / client / viewer 로그는 전부 NAS(디렉터리 서버)로 업로드된다.** 뷰어가 회사 등 원격에
  있으면 그 `viewer.log` 는 이 PC 에 없다 — **NAS(192.168.0.6, LAN)** 에서 봐야 한다.
  - 접속: `plink`/`ssh claude@192.168.0.6` (읽기전용 계정, pw 는 메모리 참조).
  - 저장 경로: 디렉터리 서버(`apps/directory/server.js`) `<LOG_DIR>/<account>/<device>/<stream>.log`
    (stream = host.log / client.log / viewer.log). 계정: software30@chronostudio.net.
  - 이 PC `%LOCALAPPDATA%\GNLink\` 의 `client.log`·`viewer.log` 는 로컬 뷰어 것뿐이라 원격 테스트 땐 낡음.
  - 퍼블릭 175.209.236.194:29180 은 호스트 업로드용 NAT 주소일 뿐, 접속은 LAN 192.168.0.6 으로.
