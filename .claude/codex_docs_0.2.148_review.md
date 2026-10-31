# 0.2.148 + APK0.2.25 — 최종 외부 확인 및 docs r1 검토

기준 `aeebe33ae605eb435940563042b59f835dfb1172`. 검토 blob: history `13bcc9cb`, 작업목록 `cc216e9b`, 구현계획 `d31b19b8`.

## 배포 근거 — Codex 직접 확인 PASS

- 읽기 전용 audit: `.claude/codex-final-0.2.148/audit.ps1`, 결과 `result.json`, checked_at_utc **2026-10-01T16:01:45.1000543Z**(한국 10-02).
- 라이브 NAS windows/android manifest+sig를 scp로 읽어 로컬 릴리스 및 전달받은 고정 해시와 일치 확인.
- 출하 소스의 공개키로 양쪽 서명 검증 및 변조 manifest 거부 확인. HTTPS로 Windows11/11 + Android1/1 파일을 직접 내려받아 서명된 크기/SHA256 전부 일치 확인.
- Windows manifest `19cb7a271b2b0e311e61b4e1722e05afb4acbd9388d093ce0f4295c62a03a511`, sig `1bee1ef02af01deafb8d956a3d6c71d71e282afc4c74c20a7fc553ef11f3fd14`.
- Android manifest `73dde974115ffd31d6a26f823cf9cfb1f919c4a033703cd8dd8814eeea3c4516`, sig `2e55dabc9286c2e3bbfbdab2cb4a09607d865bba550c18cbefe96c97db5f79eb`.
- 외부 APK apksigner verify PASS, certificate SHA256 `dcc806aeb30b2e3c53e4a0b96b9675f9e25af2bf071dbb37a3cb09f6b8ec2990`; aapt2 versionName0.2.25/code24, arm64-v8a/armeabi-v7a/x86/x86_64 확인. `apk-certificate.txt`, `apk-badging.txt`에 근거 보존.
- 저장소 **루트** `D:/remote/remote/dist/GNLinkSetup-0.2.148.exe` SHA256 `1a4eef1e3aa5f9a213e88039de15b36fa8a8870ba8001f39c6e326e40cae5f22`, `GNLink-0.2.25.apk` SHA256 `0be35bbc60330454867c5d3e14041aa15264d836a7500c4b3776ff904dd264e1` — 외부 파일과 일치. main worktree 안의 dist를 뜻하지 않는다.
- audit 당시 `git ls-remote origin refs/heads/main` = aeebe33. 통합 summary ALLDONE/개별 exit0, release/deploy 및 APK clean build 성공 로그를 읽었다. 설치/재시작/사용자 실기 조작은 하지 않았다.

## 문서 판정 — 아래 한 묶음 수정 후 3파일만 승인 가능

1. **C0 전체 완료 취소.** 기존 C0 목표는 '대역폭 추정→화질 상한 연동'이다. 이번 hard cap은 사용자 설정값을 지키는 독립 기능이며 추정값 연동은 미구현이다. 작업목록 C0의 취소선/전체 완료 표시를 제거하고 남은 추정 연동을 유지한다. 원래 단계 ① 관찰, ② 추정 기반 cap/pacing/ABR 적용, ③ UI를 임의로 '③ ABR'로 재정의하지 않는다. 필요한 경우 별도의 완료 항목/부분 상태로 이번 hard cap을 연결한다. 구현계획은 체크박스/짧은 진척 상태만 변경하고 경위는 history에 둔다.

2. **현재 버전/남은 실기 동기화.** 작업목록 머리의 main@df274be/Windows0.2.146/Android0.2.24를 aeebe33/0.2.148/0.2.25(code24), 10-02 기준으로 갱신한다. V11의 아직 안 한 로그인/이행 실기는 최신 APK에서 이어가되 이전 0.2.24 게시 근거는 역사 사실로 구분한다. V14는 0.2.147 양방향 파일 복사 사용자 확인을 반영하고 취소·대용량 등 명시 확인이 없는 항목은 남긴다. '0.2.145 updater가 helper를 받는지 미확인이라 Setup 권장'이라는 오래된 안내는 제거/정정한다. 여기서 폰 실기·파일 복사 취소까지 완료로 확대하지 않는다.

3. **상한의 범위와 현장 측정.** history/구현계획/작업목록 V15는 해당 **H.264 UDP 영상 스트림의 IPv4/IP층 산정량**(data+FEC+NACK+앱/UDP/IP 헤더)임을 유지한다. NIC 전체/다른 앱/파일복사/오디오/제어의 합계가 6.6M 이하라는 계약으로 쓰지 않는다. 작업 관리자는 보조 관찰이고 정량 판정은 해당 흐름을 분리해서 한다. '+10%'는 검증한 1.5M/6M 등의 1s/250ms sliding-window 관찰 범위이며 모든 저율 설정에서 동일 상대오차를 보장한다고 확대하지 않는다(최소 packet burst 양자화가 있음).

4. **history의 두 과장 정정.** 'FEC·NACK·헤더는 어떤 예산에도 없었다' → '사용자 설정을 기준으로 하는 공통 총량 예산에 합산되지 않았다'(기존 AU pacing에 FEC, NACK 별도 budget이 있었음). '클릭 한 번→화면' → '격리 fixture 단일 변화→decoded-picture marker'; 실제 OS 입력 왕복/화면 present로 확대하지 않는다. 측정된 큰 전환 지연은 수용했지만 byte 직렬화가 유일한 원인으로 입증됐다고 다시 쓰지 않는다. '매 라운드 독립 재현'은 '독립 실행과 Codex 소스 검토를 반복' 정도로 좁힌다.

5. **근거·URL·직접 확인 구분.** 게시 항목에 위 Codex 직접 확인을 검증용 실행과 별도 한 줄로 추가하고 audit 경로/UTC를 연결한다. 설치 파일 URL은 manifest대로 명시: Windows `https://gnlink.shotan.net/updates/0.2.148/GNLinkSetup.exe`, APK `https://gnlink.shotan.net/updates/0.2.25/GNLink-0.2.25.apk`. 주요 release/deploy/integ/rv_cap6/반복 검증 근거 경로도 연결한다. dist는 저장소 루트 경로임을 명시한다. main 버전 변경 직전 APK는 versionName0.2.23/code22였음을 소스로 확인했으므로, 필요하면 그 사실로 좁혀 쓰고 'main에 담긴 적이 없다'라는 전체 역사 단정은 피한다.

위 수정 외 제품 변경/재빌드/재게시를 요구하지 않는다. 검증용이 3파일 수정 diff+blob을 한 번 보내면 전문 검토 후 같은 범위의 Git MCP Conventional Commit+정상 push로 마무리한다. 사용자의 '게시=push' 승인은 유지된다.

## 최종 승인·완료 확인 (2026-10-02)

r2 전문 검토 뒤 V15의 실제 IP 흐름 계측 표현, V14 설치 경로 문구, 완전한 다운로드 URL의 확정 정정을 포함해 3파일 commit/push를 승인했다. 최종 commit **`59642d5b3f1eb29eb87fd62e7d0fb64ec96d755b`**를 다시 읽어 관련 문서 3파일만 포함됨을 확인했다.

- history blob `3e30308a8f9e12e9a319e6bc9f73a47ed319b996`
- 구현계획 blob `1ba229c9474f58832a39a498905d799c3ef0c7d4`
- 작업목록 blob `b7aa5079b7d55595abedf3b8dc5b9fe3872a6f92`
- `git diff --check HEAD^ HEAD` exit0. 추적 파일 변경 없음(미추적 `.claude/`만).
- `git ls-remote origin refs/heads/main` = **59642d5b3f1eb29eb87fd62e7d0fb64ec96d755b**. 제품/버전 commit은 aeebe33 그대로이며 docs push가 뒤따른 상태다.

**최종 판정: PC0.2.148 + APK0.2.25 배포 완료(마일스톤), 문서 검토/commit/push 완료.** Codex 직접 라이브 서명·변조 거부·외부 12/12 파일 hash 및 APK 서명/버전/ABI·루트 dist 정합 확인 완료. 남은 것은 사용자 업데이트 후 두 PC/WAN 6 Mbps 현장과 폰 실기이며, 로컬 무손실 smoke의 키 요청 host 왕복은 별도 미검증으로 유지한다. 사용자 설치본은 변경하지 않았다.
