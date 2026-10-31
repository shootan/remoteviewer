# -*- coding: utf-8 -*-
import io

p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()

old = u'''**전수 합계가 서로 다른 것도 기록해 둔다**
검증용 **1780**, 내 실행 **1762**, 차이 **18**. 다투지 않고 항목으로 쪼갠다:
- `udp_control_e2e` 가 그쪽 실행에서 **1P → 16P**(+15) — 차이의 대부분이 여기다.
- 남은 **+3** 은 **아직 설명되지 않았다.** 빨간 스위트가 죽기 전에 내는 PASS 줄 수가 실행마다 다른 것이 후보지만 **확인하지 않았다.**
- ⚠️ **합계가 다를 때 누가 맞느냐를 겨루지 않는다.** 숫자는 **차이를 항목으로 분해할 때만** 증거가 된다.'''

new = u'''**전수 합계 — 항목으로 쪼개니 잔여 0 (검증용이 마무리, 커밋 하나는 내가 정정)**
검증용의 **같은 기준 비교**(run5 `76897cf` **1730** → run6 `53d4f66` **1780**, **+50**):
| 스위트 | 증분 | 이유 |
|---|---|---|
| `directory_retry_test` | **+23** (44→67) | 캐시 토큰 fix 회귀 + HTTP 보존 회귀 |
| `log_upload_shutdown_test` | **+9** (0→9) | 신규 |
| `udp_control_e2e_test` | **+15** (1→16) | 세션 상태 쪽 |
| `update_manifest_test` | **+3** (82→85) | 공개키 내장 |
| | **+50** | **잔여 0** |

- ⚠️ **내가 "미설명" 으로 남겼던 `+3` 의 정체**는 `update_manifest_test` 였다. 내 가설(*"빨간 스위트가 죽기 전 내는 PASS 줄"*) 은 **틀렸다** — 그 넷은 양쪽 실행에서 **`^PASS` 0** 이라(출력 형식이 다르다) 차이에 기여하지 않는다.
- ⚠️ **커밋 귀속은 정정한다.** 검증용은 `422196b` 라고 했지만 **그 커밋은 `update_manifest*` 를 건드리지 않았다**(`directory_client.{hpp,cpp}` · `directory_session_client.cpp` · `directory_retry_test.cpp` · `docs/history.md` 5파일뿐). `76897cf..53d4f66` 구간에서 그 파일을 바꾼 커밋은 **`47a63b9`**(*"embed the operational public key, and stop asserting there is none"*) **하나뿐**이다. 실질(공개키 내장)은 맞고 **번호만 틀렸다.**
- ⚠️ **내가 비교한 1762 vs 1780(=18) 은 애초에 동일 기준이 아니었다.** 서로 다른 빌드의 두 숫자를 뺀 것이라 **그 18 자체가 의미 있는 양이 아니었다.** 같은 기준으로 쪼개니 전부 설명된다.
- ⚠️ **합계가 다를 때 누가 맞느냐를 겨루지 않는다.** 숫자는 **차이를 항목으로 분해할 때만** 증거가 되고, 그전에 **두 숫자가 같은 것을 재고 있는지**부터 확인해야 한다.
- 실측 참고: `update_manifest_test` 는 **벡터 디렉터리에 따라 개수가 다르다** — 기본 벡터 **85**, `.claude/keyfixture`(운영키 서명 fixture) **86**. 집계를 비교할 때 **어느 쪽으로 돌렸는지**가 숫자의 일부다.'''

assert d.count(old) == 1, 'anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok')
