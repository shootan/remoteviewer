# -*- coding: utf-8 -*-
import io

p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()

entry = u'''
### 517) 2026-09-11 `0.2.115` 게시 — **고아 수정이 실린 첫 릴리스**
`0.2.114` 는 Client 확인 수정만 실려 있었다. 고아 수정(`ecf340c`)이 들어간 릴리스는 이것이 처음이다.
⚠️ **그래서 손이 닿지 않는 기계에 깔 것은 `0.2.114` 가 아니라 이 릴리스다.** `0.2.114` 만 깔면 업데이트가
**보이게** 되고, 누르는 순간 고아 경합을 탄다 — 검증용 지적, 수용.

**선행 sweep** (`ecf340c` 검증분, build 14:04): **66 스위트 1865 PASS**, 실패 1건
`gdi_capture_process`(rc=1) — **알려진 문턱 스위트**(0.0.4: 문턱 20.0ms 가 관측 분산 16.8~23.3ms 안).
증분 **+4** 는 `update_stop_process_test` 에 넣은 `(static)` 검사 수와 일치한다.
⚠️ 총계는 **같은 빌드끼리만** 비교한다 — 오늘 두 번 다른 빌드를 맞대어 잘못 읽었다.

**산출물 대조** (0.2.114 → 0.2.115): 바뀐 것 **4개**뿐이다.
`GNLinkHost.exe` 660480→**664064** (고아 수정+버전) · `GNLinkClient.exe` · `GNLinkSetup.exe` ·
**`GNLinkUpdater.exe`** `17ecfa1e…`→`fd948c7f…`. 나머지 6개는 바이트 동일 — 안 바뀐 것이 재링크되지
않은 정상 결과다. 업데이터가 바뀐 것은 의미가 있다: **"업데이터가 자기를 교체했는가" 가 실기 관측점**인데,
바이트가 같으면 그 관측이 불가능했다.

**게시 전 게이트** (둘 다 통과)
- payload-set 게이트 — `product_payload_names()` 10개와 manifest 10개가 **이름까지 일치**
- `remote60_verify_release` — **제품에 컴파일된 키**로 실문서 `Ok`, **1바이트 변조 `SignatureInvalid`**.
  받아들이는 것만 보면 검증기가 아니다.

**게시** (`automation/gnlink_deploy.sh`, 순서 고정: preflight → 업로드 → **서버에서 검증** → 백업 →
manifest+sig **마지막에** 원자 교체 → 외부 https)
- manifest `9b4653e1…` 1687B · sig `7937fffe…`
- 서버 10/10 일치 · **공개 URL 10/10** 을 익명으로 내려받아 크기·sha256 재확인
- **서버가 내주는 문서를 다시 내려받아 제품 검증기로 재검증** — 서명된 바이트와 **완전 동일**
- 0.2.114 pair 백업됨(`be00ba15…`/`4203d4c4…` = 0.2.114 게시 시 값과 일치)
- **0.2.111~0.2.114 아티팩트 불변** 확인. `0.2.112` 디렉터리가 **비어 있는 것은 이전부터**이고 이번에
  건드린 것이 아니다

**아직 아닌 것**: 게시는 *"실기 통과"* 가 아니다. `still open  the field test: a real host taking
this update end to end` 는 스크립트가 스스로 찍는 줄이고, 그대로 유효하다. 실기 절차와 **복구 절차**는
`docs/update_field_retest_runbook.md` 에 먼저 적어 두었다 — 09-11 에 기기가 두 번 불능이 됐을 때
**무엇을 보고 무엇을 되돌리는지 적힌 곳이 없어서** 복구가 사람 손에 맡겨졌기 때문이다.
'''

d = d.rstrip('\n') + '\n' + entry
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('history ok')

p2 = 'docs/작업목록.md'
t = io.open(p2, encoding='utf-8').read()
old = u'0.0.9 [ ] **진입점 포함 인앱 재시험**'
new = (u'0.0.12 [x] **`0.2.115` 게시 — 고아 수정이 실린 첫 릴리스** (history #517) — manifest `9b4653e1'
       u'…`, 공개 URL 10/10, 서버 문서 제품 검증기 재검증 통과. ⚠️ **회사 PC 에 깔 것은 `0.2.114` 가 '
       u'아니라 이것이다** (`0.2.114` 에는 고아 수정 없음). ⚠️ `/S` 는 Host 를 다시 띄우지 않는다 — '
       u'대화형 설치 권장\n'
       u'0.0.9 [ ] **진입점 포함 인앱 재시험**')
assert t.count(old) == 1, 'tasklist anchor'
t = t.replace(old, new, 1)
t = t.replace(
    u'⚠️ **안전한 bootstrap 방식 확정 전에는 실행하지 않는다** — 실패 시 기기가 불능이 될 수 있다 (history #516)',
    u'⚠️ 절차와 **복구 절차**는 `docs/update_field_retest_runbook.md` 에 적어 두었다. '
    u'bootstrap 은 `0.2.115` **설치본 수동 설치**(업데이트 경로를 타지 않음) (history #516·#517)', 1)
io.open(p2, 'w', encoding='utf-8', newline='').write(t)
print('tasklist ok')
