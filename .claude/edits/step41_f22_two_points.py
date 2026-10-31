import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
ROOT = r'D:\remote\remote'
edit(ROOT + r'\docs\뷰어_리팩터_발견사항.md', [
("| `apps/native_poc/src/viewer_udp_recovery_test.cpp:1294` S12 `CHECK(host.corrupted_keys() == 1)` | `CorruptNextKey()` → `SendFrame(true)` 직후 단정하지만 비동기 인코더 호출에서는 강제 IDR 이 그 호출에 안 나올 수 있어 corrupt(zero) 가 뒤 `pump` 의 SendFrame 에서 일어남 → 단정만 비동기 인코더와 경쟁(시나리오 의미는 유지: SendAu 는 AU 단위 key 로 corrupt).",
 "| `apps/native_poc/src/viewer_udp_recovery_test.cpp:1294` S12 `CHECK(host.corrupted_keys() == 1)` 와 `:1302` `CHECK(goodIdr != 0)` 두 지점 | 둘 다 `SendFrame(true)` 가 강제 IDR 을 **그 호출에서 동기 송출**한다고 가정하지만, 비동기 인코더 호출에서는 IDR 이 뒤 `pump` 의 SendFrame 에서 나올 수 있음 — 1294 는 corrupt(zero) 가 늦게 일어나 0, 1302 는 반환 seq 가 0 → 단정만 비동기 인코더와 경쟁(시나리오 의미는 유지: SendAu 는 AU 단위 key 로 corrupt)."),
])
edit(ROOT + r'\docs\history.md', [
("- 이 커밋: `docs/뷰어_리팩터_발견사항.md` F-22 — S12 `viewer_udp_recovery_test.cpp:1294` 단정이 비동기 인코더와 경쟁(하네스, 제품 회귀 아님, 범위 밖·미수정, Codex 범위 판단 대기). `docs/history.md` #411.",
 "- 이 커밋: `docs/뷰어_리팩터_발견사항.md` F-22 — S12 `viewer_udp_recovery_test.cpp:1294` 단정이 비동기 인코더와 경쟁(하네스, 제품 회귀 아님, 범위 밖·미수정, Codex 범위 판단 대기). `docs/history.md` #411. 후속 docs 커밋: 검증용 지적대로 단정 지점을 `:1294`(`corrupted_keys()==1`)·`:1302`(`goodIdr != 0`) 두 곳으로 정정 — 둘 다 `SendFrame(true)` 의 동기 송출 가정."),
])
print('f22 two points ok')
