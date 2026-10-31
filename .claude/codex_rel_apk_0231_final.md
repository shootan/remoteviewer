# APK 0.2.31 게시 최종 근거 확인

2026-10-22 / task `t-en2shyuh` / 검증용 FINAL_PACKET / Codex 읽기 전용 독립 확인.

**APK 0.2.31과 PC 0.2.162 배포 완료(마일스톤) · 실기 테스트 대기.** 실폰 성능·화질 및 OS 실기 성공 판정은 아니다. 게시물의 기능 수정은 요구하지 않는다. 아래 기록 정정만 검증용이 처리한다.

## 직접 확인

- build `2b57bb8863ebe3e752a185bbf461dc5421e9c1b8`, merge `4394d2c`는 PC `6a127eb` + SH `c494dc6`. `5063cd9` 대비 PC apps95개와 SH apps22개는 겹침이 없으며, 공개 merge `d41162e`까지 각 원본 blob이 정확히 보존됐다. Gradle 버전만 29/0.2.30→30/0.2.31로 변경된 것을 바이트 대조했다. PC 제품 버전 변경 없음.
- 원격 `git ls-remote origin refs/heads/main` = `d41162ed636e336e796487b784a3051b5d06c16c` 확인. 이후 문서만 정정되면 최종 remote hash를 별도 연결한다.
- 공개 HTTPS APK HTTP200, 13,799,096 B, SHA256 `230a65e99a2a7ee30ce0af978e51eb0fa189f457e9a07dfddb4dc69484a9d5d4`. `dist/GNLink-0.2.31.apk`와 전체 바이트 동일. 기록 `.claude/test-tmp/codex-publish-apk-0231/external_hashes.json`.
- apksigner 직접 검증: V2 `CN=Android Debug`, 전체 인증서 SHA256 `dcc806aeb30b2e3c53e4a0b96b9675f9e25af2bf071dbb37a3cb09f6b8ec2990`이 dist 0.2.30과 일치. aapt: `com.remote60.androiddirect`, vc30/0.2.31, 4ABI.
- 실제 APK gate 직접 실행 ALL PASS7. 실제 생성된 Debug compile_commands 4ABI 각8개 TU의 시험/seam/mutant define 없음 확인. 빌드·AVD 실행 자체는 작업용/검증용 귀속.
- 로컬 Android manifest JS 검증 PASS, version 한 글자 변조본은 `SignatureInvalid` 거부. Windows arm64 미지원 verifier를 Android 성공 근거로 사용하지 않았다.
- 읽기 전용 SSH 직접 확인: 서버 Android manifest `309a9c5e06e6e96e297067459f09a03568236eb26eb70ee0ae78f8a8f91c3411`, sig `cbb5b7d608464c6ffd196df99a2cfaafb8c010def897fd88dfb9959c4c0654e3`, 로컬과 일치·version0.2.31. Windows `814e4bb9…/a39b2f14…` 및 version0.2.162 보존. 두 채널 원자적 게시라는 주장은 하지 않는다.
- 독립 AVD 로그 직접 읽음: monitors13/0, compare25/0, one-screen12/1(rc1). off와0.35 PNG 직접 열람: 같은 테두리/배치, 글씨 가장자리 차이 확인. 실폰 화질·성능 판단은 아니다.

## 실패 1건 및 미검증 구분

one-screen의 `labels == ['Desktop']`는 모든 node text를 수집하여 탭+카드를 함께 센다. layout `listDevicesButton`과 카드 문자열 모두 Desktop, MainActivity 단일 모니터 분기는 카드 하나를 생성한다. multi-monitor 대조에서 첫 Desktop은 탭이라는 점도 일치한다. **시험 기대값 오류라는 설명 수용**, dump resource-id 원문은 없으므로 실행 UI구조까지 직접 증명했다고 확대하지 않으며 rc1/12PASS·1FAIL은 유지한다. 제품 재빌드/성공할 때까지 반복은 불필요하다.

S4 AVD +13~15ms 기준 미달, 실폰 전체(화질·지연·발열·Codec2 crop·회전 터치·실망 재연결)는 미검증. 수동IP 약10s 제어 정지/간헐 control loop failed 원장은 미해결이다. keepalive-multi 제외, 휴대폰 sealed 잠금 입력 미구현. 필터 포함을 이 제한의 해소로 보고하지 않는다.

## 기록 정정 — 제품 무변경, 검증용 한 묶음

공개 d41162e의 작업목록 SH행은 아직 '결정 대기/GO 뒤 설계'·'CAS계열로 확대'로 남아 실제 게시와 충돌한다. **APK0.2.31 게시·실폰대기/자체5탭 원본크기 선명화/끄기·0.20·0.35**로 정정한다. 구현계획은 해당 SH 진행 checkbox만 갱신한다(서술 이력 추가 금지). 관련 history에 게시 근거/실기 잔여와 다음 담당을 취합하고 Git MCP 문서 commit+일반push 후 실제 원격 hash를 전달한다. 사용자 게시 지시 범위의 문서 정리이며 재승인·제품 검사 반복은 필요 없다.

같은 정정 묶음에 포함: `d41162e:docs/history.md` 13438행에 merge 구분자 `=======` 한 줄이 남아 있다(다른 시작/종료 표식 없음). 양쪽 이력은 보존하고 잔존 구분자만 제거한다. APK 후보 기록의 '공유 native … DW의 화면 목록 요청'도 PC Viewer 전용 변경과 혼동되므로, APK에 공유되는 punch/hello 수정과 모니터 지원 Host 대조 성공을 구분해 적는다. APK compile_commands는 viewer_control_client.cpp를 컴파일하지 않는다.

Codex는 미추적 판정서/운영 메모만 작성했고 별도 제품 commit·빌드·서명·게시·설치·삭제는 하지 않았다.

후속 직접 확인: 문서 commit `7d8c418c68447518e0d2ef7a6c943b37742a2ecc`가 작업목록 SH행과 구현계획 SH 체크박스를 위 요청대로 갱신했고 원격 main도 같은 hash로 확인했다. 이력 구분자/PC전용 목록 요청 문구 추가 정정(seq13429)은 아직 이 commit에 반영되지 않아 검증용 후속으로 남긴다. 게시 파일·서명·배포 마일스톤에는 영향 없다. inbox13430은 inject 요청 성공했으나 전문이 현재 턴에 아직 주입되지 않았으므로 다음 턴에 읽고 처리한다.

최종 닫음: inbox13430 전문 도착·읽음. 추가 이력 정정 commit `82b3d143f89a9e0ceb1c1886ac72e22022e3f46c`의 history만 +2/−2 diff를 직접 확인했다(양쪽 본문 보존·구분자 제거·공유 punch/hello와 PC전용 요청 구분·정정 기록). merge 표식 0, diff whitespace 검사 rc0. `git ls-remote origin refs/heads/main`도 동일82b3d14로 확인. 위 기록 정정 잔여는 모두 닫혔으며 제품/게시 산출물 변화 없음. 남은 것은 실기 및 별도 ledger다.
