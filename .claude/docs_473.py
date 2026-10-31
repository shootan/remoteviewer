import io

# ---------------------------------------------------------------- narrow the holder claim first
p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()
old = ('추측은 세 번 연속 틀렸다(실행 중 이미지 / 잔여 image section / 열린 프로세스 handle — 전부 검증하고 기각). '
       '**묻는 편이 셋 다보다 쌌다.**')
new = ('추측 셋을 각각 **한 가지 통제 조건에서** 시험했다: 실행 중 이미지 → **1초 재시도로 안 풀림**, 잔여 image section → '
       '**같은 재시도로 안 풀림**, 열린 프로세스 handle → **`~Shared()` 로 먼저 닫아도 안 풀림**. '
       '**"모든 원인을 기각했다" 는 뜻이 아니다** — 각 실험은 그 조건에서 그 설명이 성립하지 않음을 보였을 뿐이고, '
       '남은 조합이나 다른 홀더는 배제하지 못했다. **묻는 편이 세 번 추측하는 것보다 쌌다.**')
if old in d:
    d = d.replace(old, new, 1)
    print('history #471 holder claim narrowed')

entry = '''
### 473) 2026-09-09 재실행을 두 단계로 — **Required → health → commit → Optional**
- **왜 순서인가**: 클라이언트는 로그인 사용자 컨텍스트로 띄운다. handle 을 돌려주는 경로가 실패하면 셸이 대신 띄우고 **아무것도 돌려주지 않는다** — 어느 프로세스인지 확정할 수 없으니 **멈출 수도 없고**, 롤백은 그 프로세스가 잡고 있는 파일을 옮겨야 한다. **띄운 뒤에 소유 불가를 알아내는 방식은 구조적으로 늦다**(그때는 이미 프로세스가 존재한다). 그래서 **선택 이미지는 되돌릴 것이 없어진 뒤에** 띄운다.
- **인터페이스 분리**: `UpdateEffects::Relaunch()` → **`RelaunchRequired()` / `RelaunchOptional()`**. `RelaunchEffects`·`UpdateEffectsConfig`·조합 배선·`unwired()` 전부 두 seam 으로. 계획 필터는 `required_kind()` 하나를 `RelaunchOutcome::required()` 옆에 두어 **둘이 어긋날 수 없게** 했다.
- **본문은 하나, 진입점만 둘**(`run_phase(bool)`): 생존 검사·소유 규칙·로그 mark 가 두 벌이 되면 갈라지고, **갈라지는 쪽은 commit 뒤에 도는 쪽**이라 아무도 안 본다.
- **outcome 은 시도 단위로 누적**한다. 단계마다 비웠더니 **required 기록이 optional 단계와 함께 사라져** 그 뒤에 읽는 쪽이 **빈 벡터를 인덱싱**했다(실측 segfault). 로그는 새로 늘어난 것만 찍는다.
- **롤백 경로도 같은 순서**: 복원 → **구 required** → health → **구 optional**. 선택 이미지 실패가 **필수 health 판정을 가리거나 파일 복구를 막지 않는다.**
- **종착 계약 정정**: `RolledBackNotRelaunched` 는 이제 **복원 뒤 필수가 안 돌아온 경우**만이다. 창 하나가 안 열린 것과 **기계에 닿을 수 없는 것**을 같은 이름으로 부르고 있었다. commit 뒤 optional 실패는 `UpdatedButNotRelaunched` + 안내(**`Updated` 로 숨기지 않는다**).
- **회귀 7종**(요청받은 그대로, `update_state_machine_test` 115 checks):
  1. 토큰 실패 + 셸 가능 + 새 host 불건강 → **롤백 성공**, **롤백 전 optional 기동 0**
  2. RequiredMissing 이 OptionalMissing 을 이긴다, 그리고 optional 단계에 도달하지 않는다
  3. 정상 경로: **commit 전 optional 0 / 후 1**, required 는 health 앞
  4. 롤백 뒤 **구 optional 복귀**(required·health 뒤에)
  5. client-only: 필수 계획 없음 → 정상 → commit → optional, **health 1회**
  6. commit 뒤 optional 실패 → `UpdatedButNotRelaunched`, 백업은 이미 없고 롤백 없음
  7. 부분 quiesce → 디스크 무변경 abandon, **두 단계 각 1회**
- **실제 제품 경로 회귀**(`updater_scenarios_test` 115 checks): **시나리오 7 을 뒤집었다.** 예전엔 "소유 불가 클라이언트 때문에 롤백이 거부된다" 를 고정했는데, 이제는 **"그 상황 자체가 생기지 않는다"** 를 고정한다 — 롤백이 정상 수행되고 구 바이트가 돌아온다. **역대조**: 같은 소유 불가 클라이언트라도 commit 하는 경로에서는 실제로 셸로 뜬다. **시나리오 9 신설**: client-only 실제 경로.
- ⚠️ **반대 증거(순서를 되돌려 측정)**: state machine **3건 실패**(호출 흔적이 `... RelaunchRequired, RelaunchOptional, HealthCheck, Commit ...`), 시나리오 **4건 실패** — 시나리오 7 이 **`RollbackFailed`** 로 바뀌고 로그가 `started by the shell ... | could not stop ScnClient.exe ... | rollback failed: not rolling back` 을 그대로 찍는다. **이 순서가 없으면 실기에서 롤백이 막힌다는 것이 제품 경로에서 재현된다.**
- **하네스 결함 1건**: `oldHealthOk` 노브가 **한 번도 발화하지 않았다** — `rolledBack` 을 아무도 true 로 만들지 않아 복원된 빌드도 `newHealthOk` 로 판정됐다. `RestoredButUnhealthy` 를 기대하던 케이스들이 **테스트하지 않는 이유로** 통과하고 있었고, 깨끗한 `RolledBack` 은 만들 수조차 없었다. 두 번째 required 단계를 롤백 신호로 삼아 연결했다.
- **`update_relaunch_test` 를 unbuffered 로**: 이 스위트는 실제 프로세스를 띄우므로, 죽는 순간의 보고가 버퍼에 남아 사라지면 **정확히 필요할 때** 없다. 위 segfault 를 이것 없이는 못 짚었다.
- **검증**(콘솔, 활성 RDP 없음): update 12종 전부 PASS — check 28 / effects 199 / handoff 33 / http 36 / job_guard 19 / manifest 82 / relaunch 107 / release 80 / **state_machine 115** / assembly 40 / options 48 / **scenarios 115**. 나머지 C++ 바이너리 전부 exit 0. 잔존 프로세스 0 · `.claude/scenario-runs/` 0.
- 변경 파일: `update_state_machine.{hpp,cpp}` · `update_relaunch.{hpp,cpp}` · `update_effects.{hpp,cpp}` · `updater_effects.cpp` · 테스트 6종 · `docs/history.md` · `docs/업데이트_기능_설계.md` · `docs/full_code_audit_2026-09-08.md`.
- 라이브·설치·배포·버전 인상·릴리스 보류 그대로.
'''
assert '### 473)' not in d
d = d.rstrip('\n') + '\n' + entry
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ---------------------------------------------------------------- design doc: the order
p = 'docs/업데이트_기능_설계.md'
d = io.open(p, encoding='utf-8').read()
old = '''#### 3.11.1 비승격 실행 경로가 둘인 이유 — handle (2026-09-09 구현)'''
new = '''#### 3.11.0 재실행은 두 단계다 — Required → health → commit → Optional (2026-09-09 구현)

**필수(호스트·서비스)를 먼저 띄우고, health 를 묻고, commit 한 다음에야 선택(클라이언트)을 띄운다.**

이유는 3.11.1 의 handle 문제와 같은 것이다. 롤백이 가능한 구간에서 이 시도가 띄우는 프로세스는
**전부 이 시도가 멈출 수 있는 것**이어야 한다 — 롤백은 그것들이 잡고 있는 파일을 옮겨야 하기
때문이다. 클라이언트는 그 보장을 줄 수 없다(셸로 떨어지면 handle 이 없다). 그러니 **되돌릴 것이
없어진 뒤에** 띄운다.

> **"띄우고 나서 소유 불가를 알아내 재분류한다" 는 안 된다.** 알아내는 시점에 프로세스는 이미
> 존재하고, 되돌아갈 길은 제품을 다시 세우는 행위 자체로 닫혀 있다. 늦는 것은 판단이 아니라 구조다.

- **롤백 경로도 같은 순서**다: 복원 → 구 필수 → health → 구 optional. 선택 이미지가 안 돌아온 것이
  **필수 health 판정을 가리거나 파일 복구를 막아서는 안 된다.**
- **commit 뒤 optional 실패는 `UpdatedButNotRelaunched`** 이고 사용자에게 알린다. `Updated` 로 숨기지
  않는다 — 설치는 성공했고 창은 안 떴으며, 그 둘은 다른 사실이다.
- **`RolledBackNotRelaunched` 는 복원 뒤 필수가 안 돌아온 경우만**이다. 창 하나가 안 열린 것을 기계에
  닿을 수 없는 것과 같은 이름으로 부르면, 로그를 읽는 사람이 잘못된 긴급도로 움직인다.
- **호스트가 처음부터 안 돌던 client-only 업데이트**: 필수 계획이 비어 있으므로 정상 분기로 진행하고,
  **health 는 기다릴 대상이 없다는 것을 알고 즉시 답한다**(3.12). 없던 프로세스를 기다리다 시한 초과로
  멀쩡한 업데이트를 롤백하지 않는다.

#### 3.11.1 비승격 실행 경로가 둘인 이유 — handle (2026-09-09 구현)'''
assert old in d, '3.11.1 anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ---------------------------------------------------------------- ledger
p = 'docs/full_code_audit_2026-09-08.md'
d = io.open(p, encoding='utf-8').read()
anchor = '| I19 |'
start = d.find(anchor)
assert start != -1, 'I19'
end = d.find('\n', start) + 1
rows = (
 '| I20 | P1, 업데이트 해소됨 (2026-09-09, history #473) | 재실행이 한 단계라, **롤백이 가능한 구간에 소유 불가 프로세스(셸로 뜬 클라이언트)가 존재**할 수 있었다. 그러면 그 시도의 롤백이 **영구히 막힌다**(실측: 순서를 되돌리면 시나리오 7 이 `RollbackFailed`) | **Required → health → commit → Optional** 로 분리. 되돌릴 것이 없어진 뒤에만 소유 불가를 허용. 회귀 7종 + 제품 경로 시나리오 7·9 |\n'
 '| I21 | P2, 업데이트 해소됨 (2026-09-09, history #473) | 단계 분리 직후 `lastOutcomes` 를 **단계마다 비워서** required 기록이 optional 단계와 함께 사라졌고, 그 뒤 읽는 쪽이 **빈 벡터를 인덱싱**했다(실측 segfault) | 시도 단위 누적으로 변경. 로그는 새로 늘어난 것만 |\n'
 '| I22 | P2, 테스트 하네스 해소됨 (2026-09-09, history #473) | 시나리오 노브 `oldHealthOk` 가 **한 번도 발화하지 않았다** — `rolledBack` 을 아무도 true 로 만들지 않아 복원된 빌드도 `newHealthOk` 로 판정. `RestoredButUnhealthy` 기대 케이스들이 **테스트하지 않는 이유로** 통과 | 두 번째 required 단계를 롤백 신호로 연결. 깨끗한 `RolledBack` 이 이제 만들어진다 |\n'
)
d = d[:end] + rows + d[end:]
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('history 473, design 3.11.0, ledger I20-I22')
