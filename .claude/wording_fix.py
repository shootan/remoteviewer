import io

p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()
old = '''- ⚠️ **이 후보는 지금 업데이트를 확인하지도 설치하지도 못한다. 그리고 그것이 정상 동작이다.**
  - `trusted_public_key_hex()` 가 **빈 문자열**이고(`update_manifest.cpp:300`, "Deliberately empty"), `default_verifier()` 는 **키 길이가 안 맞으면 거부**한다(`key.size() != kP256PublicKeyBytes` → `false`). 따라서 **어떤 manifest 도 서명 검증을 통과하지 못한다.**
  - Android 도 같다: `UPDATE_MANIFEST_URL`·`UPDATE_PUBLIC_KEY_HEX` 가 **빈 기본값**이고, `UpdateFlow` 는 "이 빌드에는 업데이트 엔드포인트도 신뢰키도 없다" 를 보고하고 **아무것도 하지 않는다.**
  - **fail-closed 를 유지한 결과지 결함이 아니다.** 운영 서명키는 승인이 필요한 결정이고 이 코드가 내릴 결정이 아니다.'''
new = '''- ⚠️ **막히는 것은 앱 안의 자동 업데이트 경로뿐이다. 설치본 자체는 정상 실행된다.** 앞선 문구가 "서명 거부 = 설치본 실행 불가" 로 읽힐 수 있어 정정한다 — **둘은 다른 이야기다.**
  - **자동 업데이트는 진행되지 않는다.** `trusted_public_key_hex()` 가 **빈 문자열**이고(`update_manifest.cpp:300`, "Deliberately empty"), `default_verifier()` 는 **키 길이가 안 맞으면 거부**한다(`key.size() != kP256PublicKeyBytes` → `false`). 따라서 **어떤 manifest 도 서명 검증을 통과하지 못하고**, 확인 단계에서 멈춘다. Android 도 같다: `UPDATE_MANIFEST_URL`·`UPDATE_PUBLIC_KEY_HEX` 가 **빈 기본값**이라 `UpdateFlow` 가 "이 빌드에는 업데이트 엔드포인트도 신뢰키도 없다" 를 보고하고 **아무것도 하지 않는다.**
  - **수동 설치는 지금도 된다.** `GNLinkSetup-0.2.105.exe` 는 **손으로 실행해 설치하는 데 아무 문제가 없다** — 신뢰키는 업데이트 manifest 를 검증하는 데만 쓰이고 설치기 자체와는 무관하다. APK 도 **배포본과 같은 signer 이고 `versionCode` 12 > 11** 이라 **기존 설치 위에 덮어쓰기가 된다.**
  - **fail-closed 를 유지한 결과지 결함이 아니다.** 운영 서명키는 승인이 필요한 결정이고 이 코드가 내릴 결정이 아니다.'''
assert old in d, 'history wording'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

p = 'docs/구현계획.md'
d = io.open(p, encoding='utf-8').read()
old = '''⚠️ **이 후보는 업데이트를 확인하지도 설치하지도 못한다 — 그리고 그것이 정상이다.** 신뢰키
(`trusted_public_key_hex()`)가 **비어 있고**, `default_verifier()` 는 길이가 맞지 않는 키를 **거부**하므로
**어떤 manifest 도 통과하지 못한다.** Android 도 URL·키가 빈 기본값이라 `UpdateFlow` 가 아무것도 하지
않는다. **fail-closed 를 유지한 결과이고, 운영 서명키는 이 코드가 내릴 결정이 아니다.**'''
new = '''⚠️ **앱 안의 자동 업데이트 경로만 막혀 있다 — 설치본 자체는 정상 실행된다.** 신뢰키
(`trusted_public_key_hex()`)가 **비어 있고** `default_verifier()` 가 길이 불일치를 **거부**하므로 **어떤
manifest 도 통과하지 못하고**, 확인 단계에서 멈춘다. Android 도 URL·키가 빈 기본값이라 `UpdateFlow` 가
아무것도 하지 않는다. **fail-closed 를 유지한 결과이고, 운영 서명키는 이 코드가 내릴 결정이 아니다.**

**수동 설치는 지금도 가능하다.** 신뢰키는 업데이트 manifest 검증에만 쓰이므로 설치기 실행과는 무관하고,
APK 는 **같은 signer · `versionCode` 12 > 11** 이라 기존 설치 위에 덮어쓸 수 있다. **실기 확인을 하려면
후보를 손으로 설치하면 된다.**'''
assert old in d, 'plan wording'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('the two are now separated')
