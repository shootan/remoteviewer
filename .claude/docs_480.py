import io

p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()
entry = '''
### 480) 2026-09-09 운영 서명키 최초 생성 + 세 검증기 확인 — **키는 저장소 밖, 공개값만 기록**
- **최초 생성**(회전 아님): 기존 키 없음을 먼저 확인했다(`%LOCALAPPDATA%\\GNLink\\ReleaseSigning` 미존재, 저장소 내 `.pem/.key/.pfx/.jks` 0건).
- **key-id `p256-a0e184579c5d4c2d`** · 위치 `%LOCALAPPDATA%\\GNLink\\ReleaseSigning\\<key-id>\\`
  - **임베드용 공개키(raw X‖Y, 128 hex)** — `8709ea70daac6464af4ed0fff1ed7489ec9e9a4a908d48babe5b62753242d9a872a4556df0c9ffa2e98dc70e6c553a624a8a235e8c4303488743f37ba240193e`
  - **식별용 SPKI SHA-256**(임베드용 아님) — `f6121bcaa6679b638b34c90ed6ad7fac74017feaf998dc150a0c37ad36c95b49`
  - ⚠️ **둘을 바꿔 쓰면 조용히 죽는다.** 세 검증기 전부 raw X‖Y 64바이트만 받는다(C++ `update_signature.hpp:25` · Kotlin `UpdateManifest.kt:129,132` · Node `update_manifest.js:67`). SPKI hex 를 박으면 `decode_hex` 는 통과하고 `key.size() != 64` 에서 거부되어 **에러 없이 모든 업데이트가 영원히 무시**된다.
- **보관 방식**: 개인키는 **평문으로 디스크에 쓰이지 않는다.** OS 가 만든 P-256 키를 `BCRYPT_ECCKEY_BLOB` 으로 메모리에 꺼내 **DPAPI(CurrentUser)** 로 보호한 것만 기록(`private.ecc.dpapi`, 326 B). 키는 **이름 없는 ephemeral** 이라 키 컨테이너에 남지 않고, 평문 배열은 종료 전에 zero 화한다. 비밀값은 stdout·로그·A2A·Git 어디에도 나가지 않았다.
- **저장 전 왕복 자체검사**: `Unprotect → Import → SignData → 기록할 public_xy.hex 로 만든 공개키로 Verify`, 그리고 **부정 케이스**(probe 1바이트 뒤집으면 거부). 하나라도 실패하면 **아무것도 쓰지 않는다.** "감싸서 썼다" 는 "쓸 수 있는 키다" 가 아니다.
- **ACL**: Owner `shotan\\shotan`. `Users`·`Everyone`·`Authenticated Users` **없음**. 미해결 SID 1건(`S-1-5-21-…-2881406636`, Write+ReadAndExecute)이 있는데 **`%LOCALAPPDATA%` 전체에서 상속된 기존 조건**이고 계정이 해석되지 않는다(삭제된 계정). **이 디렉터리가 만든 것이 아니라 손대지 않았다** — 저장소 밖 ACL 을 요청 없이 바꾸지 않는다. 개인키는 DPAPI CurrentUser 라 읽기 권한만으로는 풀 수 없다.
- **세 검증기 확인**(배포용 아닌 fixture 1건, `.claude/keyfixture/` 미추적). 문서는 공유 TEST 벡터를 **바이트 그대로** 복사하고 **서명만 새 키로** 만들었다 — 그 문서는 첫 줄이 TEST DATA 이고 URL 이 `updates.example`, 아티팩트가 24·18·48 B 라 유출돼도 실제 업데이트를 구동할 수 없다. **공유 벡터는 건드리지 않았다**(세 런타임 합의의 기준이라 새 키로 바꾸면 안 된다).
  - **C++** `remote60_update_manifest_test .claude/keyfixture` → **82 checks / 0 failed**. `verify_ecdsa_p256_sha256` 에 **키를 주입**하는 경로다(‼️ `default_verifier()` 는 공백키라 무조건 false 이므로 쓰지 않았다). 요구 3종: 정상 통과 · 문서 변조 거부 · 다른 키 거부.
  - **Node** `update_manifest_test.js .claude/keyfixture` → **54 checks / 0 failed**. 같은 3종 + 서명 변조 거부.
  - **Kotlin** `testDebugUnitTest --tests *UpdateManifestTest*` → **11 tests / 0 failures**. 3종은 `verifiesTheSignatureWindowsAlsoVerifies` · `rejectsTampering` · `rejectsAValidSignatureFromTheWrongKey`.
- ⚠️ **Kotlin 에 vectors override 를 추가했다** — 다른 둘은 이미 있었다(C++ `argv[1]`, Node `argv[2]`). 없으면 **공유 벡터를 새 키로 덮어야만** 검사할 수 있는데, 그 벡터는 정확히 그런 일을 막으려고 있는 것이다.
- ⚠️ **그리고 그 override 로 한 첫 실행은 거짓 양성이었다.** Gradle 테스트 워커는 **데몬에서 fork** 되고, 앞서 뜬 데몬은 그때의 환경을 유지한다 — 환경변수를 걸고 그냥 돌리면 **공유 벡터(옛 키)로 통과**한다. **없는 경로를 가리켜 실패하는지 확인**하고서야 알았다. `--no-daemon` 이 필요하고, 그 함정을 코드 주석에 적었다. **통과했다는 사실만으로는 무엇을 검사했는지 알 수 없다.**
- **백업 — 하지 않았고, "완료" 로 적지 않는다.** DPAPI CurrentUser 는 **이 PC · 이 Windows 사용자 프로필 종속**이다. OS 재설치·프로필 손실·계정 삭제면 **복구 불가**이고, **같은 디스크로 파일을 복사하는 것은 독립 백업이 아니다**(프로필이 사라지면 사본도 못 푼다). 독립 복구를 원한다면 남은 사용자 동작은 둘 중 하나다: ⓐ 외부 매체에 **별도 암호로 다시 감싼** 사본을 두거나, ⓑ 이 키를 잃으면 **새 키로 다시 서명·재배포**한다고 정하는 것. **아직 아무 결정도 내려지지 않았다.**
- **하지 않은 것**: `trusted_public_key_hex()` 수정 · 재빌드 · 설치 · 서버 게시 · 실제 릴리스 manifest 발행 · APK keystore 변경 · TLS 인증서 · endpoint 선정. 라이브 PID 3종 불변.
- 변경 파일: `apps/android_direct_client/app/src/test/java/com/remote60/androiddirect/UpdateManifestTest.kt`(vectors override + 데몬 함정 주석) · `docs/history.md`. **개인키는 저장소 밖이고, 저장소 안에 키 재료 0건**을 확인했다.
'''
assert '### 480)' not in d
d = d.rstrip('\n') + '\n' + entry
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('history 480')
