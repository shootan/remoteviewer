import re, sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
ROOT = r'D:\remote\remote'
RUNS = open(ROOT + r'\.claude\edits\s12_runs_placeholder.txt', encoding='utf-8').read().strip()
SWEEP = open(ROOT + r'\.claude\edits\s12_sweep_placeholder.txt', encoding='utf-8').read().strip()

# ledger F-22 -> fixed
edit(ROOT + r'\docs\뷰어_리팩터_발견사항.md', [
(" 0.2.100 부터 있던 하네스 타이밍, 제품 회귀 아님(1005ebe 는 pending 규칙만 변경, S12 는 held 프레임 없음). 릴리스 sweep 1회 FAIL `.claude/release_0.2.103_sweep_fail_remote60_viewer_udp_recovery_test.log:162` | task t-5ahw0t5x 범위 밖 — **미수정**, Codex 범위 판단 대기. 수정안: 키가 나갈 때까지 SendFrame 반복 또는 pump 뒤 단정 |",
 " 0.2.100 부터 있던 하네스 타이밍, 제품 회귀 아님(1005ebe 는 pending 규칙만 변경, S12 는 held 프레임 없음). 재현: 릴리스 sweep FAIL `.claude/release_0.2.103_sweep_fail_remote60_viewer_udp_recovery_test.log:162`(:1294) **및 단독 실행에서도 재현**(검증용 `.claude/verify_recovery_release_run3.log:166`, :1302) — sweep 부하 전용이 아님 | **수정됨** 2026-09-07 test(viewer) 커밋(history #412; Codex 가 task t-5ahw0t5x 범위에 포함): `send_key_and_wait_on_wire` = 강제 키 1회 + 일반 픽처 최대 30회·1500 ms 안에서 wire 관측(`keyframes_sent`/`last_key_seq`), 상한 초과 시 FAIL; `corrupted_keys()==1` 단정은 관측 시점으로 이동·유지 + 정상 IDR 뒤·시나리오 끝에도 ==1; 정상 IDR 은 관측한 seq 그대로 rig `published_key(seq)`(key 프레임 publish 집합)로 \"그 seq 의 key 프레임이 표시됨\" 단정(0 sentinel·뒤 P 로 통과 불가); 비동기 순서는 `S12 order` 로그로 관측만. 제품·설치본 불변 |"),
("남은 항목: F-22(범위 밖, Codex 판단 대기). F-01~F-21 전부 처리(2026-08-31, 위 \"Phase 4 처리\" 절 참조).",
 "남은 항목: 없음 — F-01~F-22 전부 처리(F-22 는 2026-09-07 test(viewer) 커밋, history #412)."),
])

# history #412
p = ROOT + r'\docs\history.md'
raw = open(p, 'rb').read(); t = raw.decode('utf-8')
assert '### 412)' not in t
nl = '\r\n'
block = (nl + '### 412) 2026-09-07 S12 하네스 수정(F-22) — 강제 IDR 을 wire 에서 관측, 정확한 IDR seq 표시 단정, 단독 ×5 + sweep ×1 (A2A task t-5ahw0t5x, Codex 범위 포함·검증용 조건 a–f + 보충 3건)' + nl
  + '- 결함: `viewer_udp_recovery_test.cpp` S12 의 두 단정(`:1294 corrupted_keys()==1`, `:1302 goodIdr != 0`)이 `SendFrame(true)` 가 강제 IDR 을 그 호출에서 동기 송출한다고 가정. 비동기 인코더(MFT hw)는 그 호출에서 이전 P 또는 AU 0개를 내고 IDR 은 다음 호출에서 나옴. **단독 실행에서도 재현**(검증용 `.claude/verify_recovery_release_run3.log:166` :1302, 단독 실패율 1/3) — sweep 부하 전용이 아님. 릴리스 sweep 재현 `.claude/release_0.2.103_sweep_fail_remote60_viewer_udp_recovery_test.log:162` :1294. 제품 회귀 아님(1005ebe 는 pending 규칙만, S12 는 held 프레임 없음). 수정 전 로그 모두 보존.' + nl
  + '- 수정(테스트 전용, 제품 소스·설치본 불변): FakeHost `last_key_seq()`(마지막 key AU 의 wire seq); 헬퍼 `send_key_and_wait_on_wire` = `SendFrame(true)` 정확히 1회 뒤 `SendFrame(false)` 를 최대 30회·1500 ms 안에서 반복하며 `keyframes_sent` 증가(=그 IDR 이 wire 에 나감) 순간 중단, 상한 초과 시 ok=false → FAIL; 두 번째 강제 키는 만들지 않음(손상 주입 정확히 1회, "다음 정상 IDR" 도 1개). 손상 IDR: `zeroed.ok` + `corrupted_keys()==1`(단정 삭제·완화 없음, 시점만 관측 뒤로). 정상 IDR: `goodIdr = good.keySeq`(관측한 실제 seq, 호출 반환값 아님) `!= 0`, 송신 뒤 `corrupted_keys()==1` 유지(무손상), rig 에 key 프레임 publish 집합(`publishedKeySeqs`/`published_key(seq)`, present 스레드 poll 이 놓친 version 수 `missedPublishes` 진단)을 두어 **그 seq 의 key 프레임이 publish 됨**을 단정(뒤 P 프레임이나 0 sentinel 로 통과 불가) + published 증가(비공백 출력). 무폭주(≤ before+2)·비정지·Normal 단정 유지, 끝에 `corrupted_keys()==1`(정확히 1개). 비동기 순서는 `S12 order (…): first call emitted P|nothing|the IDR seq=…; IDR seq=… on call k` 로그로 관측만(동기 인코더는 call 1 통과).' + nl
  + '- 검증(테스트 exe 만 재빌드 `--target remote60_viewer_udp_recovery_test`, 제품 바이너리 5종 sha256 전후 동일 `.claude/s12_product_bins_before/after.txt`, 설치본 c263758b… 미재생성): ' + RUNS + ' ' + SWEEP + nl
  + '- 상태: task working 유지, 검증용 재검사 → Codex 최종 OK 대기. 설치·push·서버 배포 없음.' + nl)
t = t.rstrip('\r\n') + nl + block
open(p, 'wb').write(t.encode('utf-8'))
print('s12 docs ok')
