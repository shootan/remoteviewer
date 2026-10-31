import io

p = 'docs/업데이트_기능_설계.md'
doc = io.open(p, encoding='utf-8').read()

old = (
    '- 비상승이라는 제약(1.2)이 여기서 걸린다 — 확인·다운로드·검증까지는 비상승으로 가능하고, '
    '`Swap` 단계에서만 상승이 필요하다. 상승 수단은 3.2 (D1)/(D2) 및 4.4 의 결정에 따른다.\n'
    '- 로그 폴더 열기(`:631`)처럼 이미 있는 셸 액션 옆에 "업데이트 확인" 을 두는 것이 가장 적은 변경이다.\n\n'
    '**Android:** → 4.2 Codex 결정 대기.'
)
new = (
    '- 비상승이라는 제약(1.2)이 여기서 걸린다 — 확인·다운로드·검증까지는 비상승으로 가능하고, '
    '`Swap` 단계에서만 상승이 필요하다.\n'
    '- **Codex 확정**: `%ProgramFiles%` 를 교체할 때 **UAC 프롬프트를 1회** 띄운다. 사용자가 **취소하면 업데이트를 하지 않고 '
    '그대로 계속 사용할 수 있어야 한다**(취소가 실행을 막지 않는다). 교체 후에는 **원래의 비승격 사용자 문맥으로 재실행**한다 — '
    '상승된 토큰으로 클라이언트를 띄우면 그 뒤 세션 전체가 관리자 권한으로 돌게 되므로 안 된다.\n'
    '- 로그 폴더 열기(`:631`)처럼 이미 있는 셸 액션 옆에 "업데이트 확인" 을 두는 것이 가장 적은 변경이다.\n'
    '- **네트워크 실패가 실행·로그인을 막아서는 안 된다** — 시작 시 확인은 비동기이고, 실패하면 조용히 다음 기회로 미룬다'
    '(3.1 의 `CheckRequested` 실패 처리).\n\n'
    '**호스트 쪽 상승에 대하여 [Codex 확정]**: `GNLinkHost.exe` 는 이미 `requireAdministrator` 이므로(1.2) '
    '그 자식으로 뜬 업데이터는 **토큰을 상속받아 2차 UAC 프롬프트가 필요하지 않다.** '
    '이것을 완료조건으로 명시한다 — 호스트발 업데이트 중 UAC 프롬프트가 **한 번도 뜨지 않아야** 한다(3.6).\n\n'
    '**Android:** 진입점은 앱 안의 명시적 "업데이트 확인" 과 시작 시 비동기 확인 두 가지이며, '
    '설치는 `REQUEST_INSTALL_PACKAGES` 승인 경로를 거친다(4.2.4). 설치 방식 (A) 파일 Intent / (B) `PackageInstaller` 세션의 '
    '선택은 구현 단계 결정이고, 그 선택이 `FileProvider` 필요 여부를 정한다.'
)
assert old in doc, 'PC client / Android anchor not found'
doc = doc.replace(old, new, 1)

# 3.6 completion conditions: add the UAC conditions
old6 = ('| `Relaunch` | 교체 전에 돌고 있던 것과 같은 구성이 다시 떠 있음: 호스트가 떠 있었으면 `GNLinkHost.exe` 1개 + '
        '(로그인 상태였으면) `GNLinkStream.exe` 1개 |')
new6 = ('| `Relaunch` | 교체 전에 돌고 있던 것과 같은 구성이 다시 떠 있음: 호스트가 떠 있었으면 `GNLinkHost.exe` 1개 + '
        '(로그인 상태였으면) `GNLinkStream.exe` 1개. **PC 클라이언트는 원래의 비승격 사용자 문맥으로 떠 있어야 한다**(3.5) |\n'
        '| `Swap` 권한 (호스트발) | **UAC 프롬프트가 한 번도 뜨지 않는다** — 호스트가 이미 상승돼 있어 자식 업데이터가 토큰을 '
        '상속받는다(3.5) |\n'
        '| `Swap` 권한 (클라이언트발) | **UAC 프롬프트가 정확히 1회.** 사용자가 취소하면 교체하지 않고 클라이언트는 계속 사용 가능한 '
        '상태로 남는다(3.5) |\n'
        '| 업데이터 job | 업데이터가 **어떤 job object 에도 속하지 않는다**(3.2 ②) |')
assert old6 in doc, '3.6 Relaunch row not found'
doc = doc.replace(old6, new6, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(doc)
print('3.5 / 3.6 updated')
