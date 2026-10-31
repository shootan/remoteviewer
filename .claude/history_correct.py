# -*- coding: utf-8 -*-
import io

p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()

entry = u'''
### 502) 2026-09-10 🔴 정정 — **"실패 지점이 앞으로 이동했다" 는 틀렸다. 뒤로 물러난 것이다**
#499·#500 과 사용자 보고에 *"게시 복구가 유효했고 실패 지점이 이동했다"* 고 적었다. **틀렸다.** 검증용이 짚었고 코드로 확인했다.

```
update_state_machine.cpp:199  PrepareForSwap()   ← 18:56 여기서 실패 (could not ask pid ... to stop, :428)
update_state_machine.cpp:209  Quiesce()
update_state_machine.cpp:219  Swap()             ← 15:45 여기까지 갔다 (the staged release does not contain, :492)
```
- **15:45 이 더 깊이 갔고, 18:56 은 더 얕은 곳에서 죽었다.** 차이는 수정 효과가 아니라 **18:56 에 창 없는 child 가 살아 있었다**는 것뿐이다.
- ⚠️ **따라서 `updater.log` 는 Setup 복구가 통했다는 증거를 전혀 주지 않는다.** 18:56 은 그 검사가 있는 단계에 **도달조차 못했다.**
- ⚠️ **`version verified: 0.2.109` 도 required-set 통과 근거가 아니다** — 서명 검증과 `Download`/`VerifyDownload` 까지의 증거일 뿐이다.
- **게시측 required 충족의 근거는 로그가 아니라 별도다**: manifest 10줄 대 `payloadNames` 9개 **기계 대조(MISSING none)** 와 **공개 URL 10/10 해시**. 이건 실기 로그와 무관하게 유효하다.

**내가 왜 틀렸나** — 두 오류 문구가 **다른 단계**에서 나온다는 것을 확인하지 않고 **시각 순서만 보고 "앞으로 갔다" 고 읽었다.** 로그 두 줄의 시각 차이는 단계의 깊이를 말해 주지 않는다. **문자열이 어느 함수에 있는지 먼저 봤어야 했다** — 실제로 보니 한쪽은 `:428`, 다른 쪽은 `:492` 이고 호출 순서는 그 반대였다.
⚠️ 검증용도 같은 오류를 냈고 스스로 정정했다. **둘이 같은 방향으로 틀리면 서로가 근거처럼 보인다.**

**그래서 남는 상태**: 0.2.109 게시 복구는 **여전히 유효**하다(위 별도 근거로). 다만 **실기에서 그것이 통했다는 증거는 아직 없다** — 인앱 업데이트가 그 단계까지 가 본 적이 없기 때문이다.
'''

d = d.rstrip('\n') + '\n' + entry
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok')
