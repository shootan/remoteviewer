# APK0.2.31 — 게시 지시 반영·범위 확인

2026-10-22 / 검증용 user-directive+delegation / 정본 `.claude/task_rel_apk_0231_r1.md`.

**사용자 지시를 반영해 SH를 APK0.2.31에 포함하고 PC0.2.162와 게시하는 진행을 수용한다.** 이전의 APK 버전/S5 미결·게시 보류는 **이번 SH 포함 APK0.2.31/versionCode30 범위에서 해소**됐다. 동일 범위의 재승인을 사용자에게 반복 요청하지 않는다. 이는 배포 후 실폰 검증 경로이며, 실기·성능 성공 판정이나 자동 폰 설치 허가가 아니다.

## 포함·제외

- 입력은 PC rel/0.2.162의 고정 버전 commit **b4dfaedfa08e2023a495154fc59e7bda66272cbb**(통합9f54912 위 버전만 변경) + 승인된 SH **c494dc656b9aff372fd4d03997c8a1e44e514eea**다. 생성된 rel/apk-0.2.31의 실제 parents·공유 파일·gate 합집합을 고정한다. 이미 작업이 시작됐으면 같은 task에서 이어가고 중복 구현 지시를 만들지 않는다.
- **keepalive-multi 제외는 현재 범위로 수용**한다. 기존 `codex_keepalive_multi_r1.md`와 branch 기록에도 최대3대/park5분은 제안값·사용자 답 대기이고 최종 출하 판정이 남아 있다. 오래된 base/재통합 수고만으로 제외하는 것이 아니라 **정책 미확정·현재 통합 미검증 후보**이므로 이번 완료된 SH 게시와 분리하는 것이다. 미게시 과제/다음 담당을 유지하고 “시킨 모든 기능이 완료/게시됐다”고 보고하지 않는다.
- **휴대폰 sealed 잠금 입력은 이번에 구현하지 않는다.** APK0.2.31도 새 PC Host의 잠금 화면 암호 입력 지원이 추가된 것은 아니다. SH와 이 제한을 혼동하지 않게 최종 안내에 적는다.
- 기존 기본0.20·0.20/0.35/끄기·off 직결·실패 fallback·기하/입력 계약은 유지한다. 새로운 사용자 설정/세션 정책을 임의 확정하지 않는다.

## 게시 전 확인 범위

현재 위임 정본의 빌드·AVD smoke 계획을 수용한다. 아래는 그 산출물 검사의 구체 범위다.

1. **병합/버전:** rel PC의 LK/DW/hello가 사라지지 않고 SH decoder metadata-before-buffer·수명/출력 gate·shader 보수가 보존됐는지 공유 hunk를 대조한다. product conflict는 취합해 보고하고 임의 해석하지 않는다. APK versionName0.2.31/code30, PC 버전은0.2.162 보존. 기능 변경 없이 버전/허용 기록만 추가한다.
2. **실제 게시 APK:** 기존 appId와 지난 APK와 같은 signer를 비교한다. 제안된 debug 키 사용은 기존 게시 방식으로 수용하되 **apksigner의 전체 인증서 SHA256이 이전 고정 APK와 일치**해야 한다(prefix dcc806ae만 보지 않는다). 새 키 생성/키 교체/사용자 기기 데이터 초기화는 하지 않는다. APK 자체 서명과 Android update manifest의 운영 P256 서명은 별개로 검증한다.
3. **시험 코드 분리:** 게시용 debug APK는 noseam으로 빌드하고 dex/.so gate 정상·시험 APK 양성 대조,4ABI·version/appId·고정 주소를 확인한다. PC와 SH gate의 표식/부정 대조를 합집합으로 유지한다. 전처리 매크로는 바이트 문자열 검사만으로 부재를 증명하지 말고 Android native 실제 compile 정의/빌드 설정에서도 SH/hello 시험·변이 정의가 없는지 대조한다.
4. **공유 native 실행:** 고정된 실제 APK로 AVD 접속→영상→필터0.20/0.35/off→재연결1회·모니터 목록 지원 Host 접속을 확인하고 compare1회를 한다. 접속/재연결 로그에서 새 native hello가 실제 성공했는지 구분한다. 시험 Host/가상 monitor·실APK·AVD와 실폰의 층도 명시한다. 기존 시험 공급 경계는 허용하되 APK gate를 통과하는 게시 파일과 다른 시험 빌드는 구분한다. 관측 실패를 성공할 때까지 반복해 지우지 않는다.
5. **독립 검사/게시:** 작업용2 고정 보고 후 검증용이 APK hash·서명/appId/version·gate/공유 통합·핵심 smoke를 독립 확인한다. 기능 차이가 생기면 그 차이만 Codex에, 동일 승인 범위가 통과하면 Android manifest 운영 서명/검증→배포 dry-run→`gnlink_deploy.sh --platform android` 실게시→외부 HTTPS/서명/버전/APK hash 검증으로 이어간다. 개인키는 PC 보호 저장소 밖으로 내보내지 않는다. root 밖 기존 worktree/증거 삭제도 하지 않는다.

## PC와 두 채널 게시의 관계

새 사용자 지시는 APK도 함께 게시하는 것이다. 따라서 **최종 Android pair 불변**이라는 예전 PC-only 체크를 전체 릴리스 완료 조건으로 유지하면 안 된다.

- Windows 게시 단계에서는 그 단계가 Android pair를 건드리지 않았는지 확인할 수 있다.
- 이후 승인된 Android 단계에서 pair가0.2.31로 바뀌는 것은 의도한 변경이다. 반대로 이 단계가 Windows0.2.162 pair를 훼손하지 않았는지 확인한다.
- 최종 두 채널 각각의 manifest/sig·artifact size/hash·HTTPS 도달·버전을 검증하고 게시 시각·rollback pair를 구분한다. 두 채널 또는 manifest/sig pair를 원자적으로 교체했다고 주장하지 않는다. 기존 배포 스크립트의 부분 swap 실패 제한도 유지한다.

게시/외부 확인이 끝나면 최종 상태는 **배포 완료(마일스톤)·사용자 실기 대기**다. AVD의 S4+13.1~15.1ms 미달은 유효한 결과로 유지하며 실폰 성능/화질/발열·실GPU/Codec2 crop·회전터치는 게시 뒤 사용자 실기 항목이다. 기존 SH control/clipboard 관찰·PC OS/회사 PC 한계도 숨기지 않는다. 그 수치를 성능 PASS나 전체 task 완료로 바꾸지 않는다.

실제 사용자 폰/PC 설치·재시작·잠금·재부팅·SCM 변경은 자동으로 하지 않는다. 최종 묶음에 설치 대상·기존 데이터/서명 영향·반영 버전·알려진 제한·실기할 행동을 한 번에 안내한다. git push 범위는 **별도 대기 중인 기존 사용자 원문 확인**에 따른다. 이번 APK 게시 문구만으로 기존 명시 보류를 임의 해석하지 않는다.

검증용은 APK 준비의 실제 task ID·담당 작업용2와 단일 FINAL_PACKET을 연결한다. directory-retry 후속보다 이 게시 준비가 우선인 지시를 수용한다. 사용자에게 지금 별도 확인을 요구할 일은 없다. Codex는 정본/기존 keepalive 기록 확인·이 범위 문서 작성만 했고 제품 수정/빌드/서명/기기 설치/게시·commit·push를 직접 수행하지 않았다.
