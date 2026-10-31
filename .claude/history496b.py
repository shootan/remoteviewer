# -*- coding: utf-8 -*-
import io

p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()

old = u'''**아직 안 된 것**: **서명 미실행** — 서명 스크립트가 권한 게이트에서 차단됐다. 사용자 승인 후 `windows.sig` 생성·자체 검증·3중 검증을 하고, 그 다음이 NAS 인계다.
**하지 않은 것**: 게시·설치·라이브 실행·프로세스 중지·캐시 삭제·push·NAS 변경 없음. APK 재생성 없음.'''

new = u'''**서명**(사용자 승인 후 실행, 기존 운영키. **새 키 생성 없음**)
`windows.sig` 128자 `85fdc3c76fce72cf…`(파일 해시), 서명값 `0de7ca480a6de20b…`. 개인키는 DPAPI blob 에서 메모리로만 풀고 종료 전 소거, 출력은 서명값뿐.

**3중 검증 — 서로 다른 구현이 같은 바이트에 동의하는가**
서명자는 .NET CNG 다. **자기 자신과의 일치는 증거가 아니다** — 인코딩을 틀린 서명도 자체 검증은 똑같이 통과한다.
| 구현 | 대상 | 결과 |
|---|---|---|
| Node `apps/directory/update_manifest.js` | **실제 `windows.manifest`** | Ok · `version=0.2.109` · `releaseId=r-0.2.109` · artifact 9개 · **1바이트 변조 거부** · **다른 키로 거부** |
| Python `cryptography`(OpenSSL) | **실제 `windows.manifest`** | Ok · raw r‖s 64B · 키 128자 · **1바이트 변조 거부** |
| C++ `default_verifier()`(**컴파일된 키**) | 같은 키로 서명한 **fixture 문서** | Ok · **1바이트 변조 거부** · 다른 키 서명 거부 (86 checks) |
⚠️ **C++ 은 `windows.manifest` 자체가 아니라 fixture 를 검증한 것**이다. *"제품에 박힌 키가 이 키의 서명을 받아들인다"* 는 증명되고, *"제품이 이 문서를 읽었다"* 는 아니다. 그 구분을 지운 채 "3중 검증 완료" 라고 쓰지 않는다.

⚠️ 검증 스크립트를 처음 돌렸을 때 **수락 검사만 FAIL, 거부 검사 2개는 PASS** 였다. 원인은 `Status.Ok`(존재하지 않음, `undefined`) — **거부 검사는 `!== undefined` 라서 무엇이든 통과**하고 있었다. `Status.OK` 로 고쳤다. **틀린 상수 하나가 음성 대조 두 개를 동시에 무력화**했고, 수락 검사가 없었으면 셋 다 초록으로 보였다.

**아직 안 된 것**: **NAS 게시 미실행**(작업용은 NAS 를 만지지 않는다). 검증용 검사 → 기존 승인 경로로 인계.
**하지 않은 것**: 게시·설치·라이브 실행·프로세스 중지·캐시 삭제·push·NAS 변경 없음. APK 재생성 없음. 새 서명키 생성 없음.'''

assert d.count(old) == 1, 'anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok')
