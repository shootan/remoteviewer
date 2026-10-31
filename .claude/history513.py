# -*- coding: utf-8 -*-
import io

p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()

old = u'''**미검증 / 막힌 것**
- **`GNLinkStream` 이 부모 급사 시 스스로 나가는가** — 코드에 **부모 감시도 job object 도 없다**(job 은 `gdi_capture_process.cpp` 만). **(b) 라면 기한 대기만으로는 실기가 여전히 막힌다.** 라이브 호스트 종료가 **권한 분류기에 차단**돼 관측하지 못했다. **추정하지 않는다.**'''

new = u'''**미검증 / 막힌 것**
- **`GNLinkStream` 이 부모 급사 시 스스로 나가는가** — 코드에 **부모 감시도 job object 도 없다**(job 은 `gdi_capture_process.cpp` 만). 라이브 호스트 종료가 **권한 분류기에 차단**돼 관측하지 못했다.
  → 🔴 **별도 실험은 하지 않는다**(검증용 판단, 수용). 이유가 둘이다: (1) **관측 시점이 이미 계획 안에 있다** — 4단계 인앱 시험에서 Host 는 업데이트 과정으로 내려가고, 그때 Stream 이 남는지가 **새 사유 3종 분리로 이름을 갖고** 드러난다. 같은 관측을 위해 라이브 프로세스를 따로 죽일 이유가 없다. (2) **(b) 라 해도 주 경로를 막지 않는다** — `host_app_main.cpp:470` 이 Host 종료 시 Stream 을 **명시적으로 `TerminateChild`** 하므로 정상 경로에서는 고아가 생기지 않는다. 10:11 의 고아는 **경합**이었고 10:12 재시도는 Prepare 를 통과했다.
  → 따라서 (b) 의 실체는 **"드문 경합에서 정직하게 abandon 된다"** 이고 그것이 현 구현이 하는 일이다. `TerminateProcess` 승격은 설계 변경이라 범위 밖. **알려진 좁은 한계로 기록**하고 넘어간다(작업목록 0.0.8). **권한 요청은 철회했다.**'''
assert d.count(old) == 1, 'limitation'
d = d.replace(old, new, 1)

old2 = u'''**하지 않은 것**: `update_relaunch.cpp` 무수정 · 등록 비치명화 우회 없음 · 상태기계 개편 없음 · 운영 CommonPrograms·서비스·방화벽 무접촉 · 게시 없음 · `git push` 없음.'''
new2 = u'''**검증 경계**(검증용 명시 요청): 검증용은 **총합을 재현하지 않았고**, `update_stop_process` · `update_registration_com` · `update_effects`(210) · `update_release`(80) · `updater_assembly`(49) **5개 스위트만 직접 확인**했다. `parent_has_exited`(`update_effects.cpp:465-477`)도 코드로 확인했다.
**하지 않은 것**: `update_relaunch.cpp` 무수정 · 등록 비치명화 우회 없음 · 상태기계 개편 없음 · 운영 CommonPrograms·서비스·방화벽 무접촉 · 게시 없음 · `git push` 없음 · **라이브 프로세스 종료 실험 없음**(철회).'''
assert d.count(old2) == 1, 'boundary'
d = d.replace(old2, new2, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok history')

p2 = 'docs/작업목록.md'
t = io.open(p2, encoding='utf-8').read()
old3 = u'''0.0.5 [ ] **`udp_control_e2e`'''
new3 = u'''0.0.8 [ ] **고아가 끝내 안 나가면 그 업데이트 시도는 실패한다** (알려진 좁은 한계, 수정 안 함) — 창 없는 자식의 부모가 이미 종료됐으면 요청할 상대가 없어 **기한 대기 → 미종료 시 abandon** 이다. 사유는 `orphaned windowless pid N did not exit … nobody to ask` 로 구분된다. **정상 경로에서는 생기지 않는다** — `host_app_main.cpp:470` 이 Host 종료 시 Stream 을 명시적으로 `TerminateChild` 한다. 실체는 **드문 경합**. `TerminateProcess` 승격은 설계 변경이라 범위 밖 (history #512)
0.0.5 [ ] **`udp_control_e2e`'''
assert t.count(old3) == 1, 'tasklist'
t = t.replace(old3, new3, 1)
io.open(p2, 'w', encoding='utf-8', newline='').write(t)
print('ok tasklist')
