# -*- coding: utf-8 -*-
import io

p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()

entry = u'''
### 520) 2026-09-11 UI 검사 보강 3건 — **부정 대조가 실제로 실패해야 검사다**
검증용·Codex 교차검토로 `client_update_ui_test` 의 실제 범위를 확정한 뒤의 보강이다. 그때까지
증명된 것은 *"배포될 HTML 이 그 메시지를 낼 수 있다"* 까지였다.

**1) `disabled` 가 아니라 "누를 수 있는가"**
`disabled === false` 는 부족하다 — 버튼은 **활성인 채로** 화면 밖·투명·다른 것에 덮여 있을 수 있다.
새 판정은 `offsetParent` · viewport 교차 · `getComputedStyle` 의 `visibility`/`opacity` ·
**`document.elementFromPoint(중심)` 이 그 버튼을 돌려주는지**(가림 검출) · **키보드 포커스 도달**을 본다.
**부정 대조 3건이 실제로 실패한다**: `display:none` → `not laid out` · 투명 오버레이 →
`covered by blocker` · 치운 뒤 → `pressable`. 셋을 **한 스크립트 안에서 원자적으로** 돌린다 —
왕복을 나누면 호출 사이에 페이지가 변한 채로 답이 섞인다.
**4번째 부정 대조**: **버튼이 없는 낡은 판**(0.2.115 의 페이지 모양)을 물리면 `no element` 로 실패하고
메시지도 못 보낸다. **이 파일이 0.2.115 를 잡았어야 했다는 것을 스스로 증명한다.**

**2) 검사 대상을 서명된 manifest 의 해시로 고정**
전에는 *"저장소 사본"* 을 읽을 뿐, **게시 산출물과 같다는 단정이 0건**이었다. 이제 **제품 `load_manifest()`
+ 컴파일된 운영 공개키**로 manifest 를 검증하고, 거기 적힌 `ui\\shell.html` sha256 과 **로드할 파일의
해시가 다르면 FAIL** 한다. 검증되지 않은 manifest 는 무엇이 옳은지 말할 자격이 없다 — 그렇지 않으면
텍스트 파일을 고쳐 고정점을 옮길 수 있다.
⚠️ **같은 커밋·같은 툴체인이 같은 바이너리를 보장하지 않는다**(검증용 이전 주장 철회). 해시가 유일한 고정점이다.

**3) 증거 등급 3단 분리** — ⑴ 페이지가 WebMessage 를 보냈다 ⑵ 네이티브가 받아 행동했다 ⑶ 실제 업데이터가
떴다. **이 테스트는 ⑴까지**이고, ⑴을 ⑵⑶의 증거로 쓰지 않는다. 파일 머리에 못 박아 두었다.

**🔴 하네스 자체의 결함 2건 — 제품이 아니라 제 테스트 코드**
- **완료 콜백이 지역 변수를 참조 캡처했다.** `eval()` 과 `write_screenshot()` 둘 다. 호출이 제한 시간을
  넘기면 함수는 반환하고, **늦게 도착한 콜백이 사라진 프레임에 쓴다.** 증상은 실행마다 다른 지점에서 나는
  segfault 였고, **모든 충돌이 스크린샷 이후에 났다**는 것이 단서였다. `shared_ptr` 로 고쳤다.
- **한 표현식 안에서 `eval` 을 두 번 부르면 깨진다.** `ok(wait_for(...), "...", eval(...))` 형태가
  메시지 펌프를 한 인자 목록의 양쪽에서 재진입시킨다. `pump_until` 술어 안에서 `eval` 을 부르던 5곳도
  같은 이유로 걷어내고 `wait_for()` 로 평탄화했다.
- 결과: **4회 연속 23 PASS**(전엔 3회 중 2회 충돌). ⚠️ 한 번 통과한 것을 안정으로 읽지 않는다 —
  간헐 결함은 반복 실행으로만 보인다.

**회귀**: `client_shell_bridge_test` 29 PASS · `client_update_flow_test` 23 PASS.
**게시하지 않았다** — 사용자 설치가 먼저이고, `0.2.116` 게시분은 불변이다.
'''

d = d.rstrip('\n') + '\n' + entry
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('history ok')

p2 = 'docs/작업목록.md'
t = io.open(p2, encoding='utf-8').read()
old = u'0.0.14 [x] **`0.2.116` 게시'
new = (u'0.0.15 [x] **UI 검사 보강 3건** (history #520) — 누를 수 있는가 판정(가림·포커스·viewport) + '
       u'**부정 대조 4건이 실제로 FAIL** + **서명 manifest 해시로 검사 대상 고정** + 증거 등급 3단 분리. '
       u'하네스 자체 결함 2건(콜백 참조 캡처·한 표현식 내 중복 eval) 수정, **4회 연속 23 PASS**. '
       u'⚠️ 게시 안 함 — 사용자 설치가 먼저\n'
       u'0.0.14 [x] **`0.2.116` 게시')
assert t.count(old) == 1, 'tasklist anchor'
io.open(p2, 'w', encoding='utf-8', newline='').write(t.replace(old, new, 1))
print('tasklist ok')
