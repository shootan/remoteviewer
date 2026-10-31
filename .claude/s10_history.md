
### 444) 2026-09-08 업데이트 step4-7 — payload 이름 검증을 manifest 경로에 연결 (검증용 지적: "좋은 자물쇠가 아직 문에 달리지 않았다")
- 배경: #442 가 payload 이름 검증을 만들었지만 **manifest 에서 이름이 오는 경로가 없어** 실제로는 아무 문에도 달려 있지 않았다. 검증용의 지적이 정확했다 — 검증이 존재하는 것과 검증이 **실제 입력에 적용되는 것**은 다른 주장이다.
- **manifest 가 payload 이름을 나른다**: 반복 가능한 `payload=` 줄을 `ManifestFields::payloadNames` 로 파싱한다. 스키마 규칙상 **모르는 키는 무시**하므로 이 추가는 구 클라이언트와 호환된다. **순서를 보존**한다 — 교체가 그 순서로 파일을 옆으로 옮기므로, 순서가 바뀌면 롤백이 쓰는 백업이 달라진다.
- **자물쇠를 문에 달았다**: `load_manifest()` 가 서명 검증·필드 검증을 통과한 뒤 **`check_payload_names_utf8()` 로 이름을 검사**하고, 하나라도 걸리면 `Malformed` 로 끝난다. 따라서 **불안전한 이름을 담은 `VerifiedManifest` 는 존재할 수 없다** — 검증된 manifest 로 설치 설정을 만드는 호출자는 그런 이름을 받을 경로 자체가 없다. 서명은 "이 바이트가 우리 것" 을 말할 뿐 **"이 바이트 안의 이름이 안전" 을 말하지 않는다**.
- **UTF-8 경로 추가**: manifest 는 이름을 UTF-8 로 나르므로 `check_payload_name_utf8()`·`check_payload_names_utf8()` 를 더했다. **ASCII 밖의 바이트는 디코드하지 않고 거부**(`NonAscii`)한다 — 제품이 배포하는 이름은 전부 ASCII 이고, 그 이상을 받아들이면 **경로가 되기 직전인 문자열에 대해 정규화 형식과 동형 문자를 따져야** 한다. 풀려는 문제보다 훨씬 큰 문제다.
- 회귀 24종 추가(`update_manifest_test.cpp`, 41 → **65 checks**): **전부 accepting verifier 로 서명이 통과한 상태**라 거부하는 것은 오직 이름 검사다 — traversal · 혼합 구분자 traversal · 절대경로 · UNC · 대체 스트림 · 예약 장치명 · 후행 점 · 중복(대소문자 차이) · 비-ASCII 각각에 대해 `Malformed` + **`VerifiedManifest` 부재**를 단정한다. 통과해야 하는 것(payload 줄 없음, 평범한 이름 3개)도 함께 둬 과잉 차단이 아님을 보인다.
- **순서가 새 검사에도 유지된다**: 불안전한 이름 + **나쁜 서명**이면 여전히 `SignatureInvalid` 다(`Malformed` 아님). 새 검사를 서명 앞으로 끌어오지 않았다는 증거다.
- **이름이 살아서 나온다**: 통과한 경우 세 이름이 **쓰인 순서 그대로** `fields().payloadNames` 에 있는지 단정한다.
- 검증 — **`qwinsta`: `console` 만 Active, `rdp-tcp` 는 `Listen` → RDP 미접속.**
  - `remote60_update_manifest_test` **65 checks / 0 failed**(41 → 65), exit 0.
  - 업데이트 **8종 합계 550 checks / 0 failed**, 전부 exit 0: version_compare 139 · manifest 65 · state_machine 57 · effects 126 · http 36 · registration 45 · check 28 · payload_name 54.
  - `remote60_host_app` 재빌드 exit 0.
- 미검증·미착수(그대로): **다중 파일 패키지 형식 없음** — manifest 가 이제 이름을 나르지만 **그 이름들에 해당하는 파일을 담은 패키지 포맷과 추출은 미구현**이라, `Swap()` 은 여전히 단일 아티팩트를 각 이름으로 복사한다. 업데이터 `RegisterInstall` 배선 · `Relaunch`/`HealthCheck` production · 클라 셸 시작 시 비동기 확인 · 실 HTTPS 왕복 0회 · 실제 서비스·방화벽·시작메뉴 0회 · 관리자 권한 실증 없음.
- 변경 파일: `apps/native_poc/src/payload_name.hpp`·`payload_name.cpp`(UTF-8 경로) · `update_manifest.hpp`·`update_manifest.cpp`(payload 필드 + 검사) · `update_manifest_test.cpp` · `apps/native_poc/CMakeLists.txt` · `docs/history.md` · `docs/구현계획.md`.
- 버전 인상·설치본 생성·설치·라이브 조작·서버 배포·push 없음.
- 상태: 검증용 검사 대기. **전체 완료 아님.**
