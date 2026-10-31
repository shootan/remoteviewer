import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit

ROOT = r'D:\remote\remote'
REPORT = ROOT + r'\docs\field_test_2026-09-07_0.2.100.md'
HIST = ROOT + r'\docs\history.md'

edit(REPORT, [
("""user-feedback 표본 = `pipeUs`(= `hdr.sendQpcUs − hdr.captureQpcUs`, 캡처→와이어, 인코더 보유 시간 포함; `host_stage_encode_send_h264_au.cpp:563`) 가 경고 임계 이상인 AU 만, 1초 rate-limit.""",
 """user-feedback 표본 = `pipeUs`(= `hdr.sendQpcUs − hdr.captureQpcUs`; `hdr.sendQpcUs` 는 주 스레드가 **입큐 전**에 찍는 송신 준비 시각(`host_stage_encode_send_h264_au.cpp:268`)이고 실제 와이어 시각은 sender 스레드가 `item.udpHdr.sendQpcUs` 에만 다시 쓴다(`host_encoded_sender.cpp:175`) → pipeUs = **AU capture stamp → 주 스레드 입큐 전 송신 준비 시각**, 인코더 보유 포함, sender 큐 대기·실제 UDP 송신 제외, capture stamp 는 보정된 값일 수 있음; `…h264_au.cpp:563`) 가 경고 임계 이상인 AU 만, 1초 rate-limit."""),
("""| user-feedback: pipeUs(capture→wire) med / max | 184,769 / 369,968 | 120,799 (n=1) | 174,688 / 392,405 |""",
 """| user-feedback: pipeUs(capture stamp→입큐 전 송신 준비, 큐·UDP 송신 제외) med / max | 184,769 / 369,968 | 120,799 (n=1) | 174,688 / 392,405 |"""),
("""- user-feedback 표본의 `pipeUs` 180~190ms(정적·타이핑 구간 실제 프레임)는 캡처→와이어 전체이며 인코더 보유 시간을 포함한다. 희소 입력에서 비동기 인코더가 다음 입력(150ms trailing kick)까지 프레임을 쥐고 있는 설계(`host_kick.hpp`)와 크기가 부합하므로, 타이핑 마지막 프레임의 호스트 몫 지연 후보로 본다 — 단 경고 임계 이상 표본만 기록되므로 대표값이 아니며 확정하지 않는다. UAC 뒤에는 readback 이 표본상 +6~8ms 더해진다.""",
 """- user-feedback 표본의 `pipeUs` 180~190ms(정적·타이핑 구간 실제 프레임)는 capture stamp 에서 주 스레드의 입큐 전 송신 준비 시각까지이며 인코더 보유 시간을 포함하지만 sender 큐 대기와 실제 UDP 송신은 포함하지 않는다(capture→wire 총지연이 아님). 희소 입력에서 비동기 인코더가 다음 입력(150ms trailing kick)까지 프레임을 쥐고 있는 설계(`host_kick.hpp`)와 크기가 부합하므로, 타이핑 마지막 프레임의 호스트 몫 지연 후보로 본다 — 단 경고 임계 이상 표본만 기록되므로 대표값이 아니며 확정하지 않는다. UAC 뒤에는 readback 이 표본상 +6~8ms 더해진다."""),
])

edit(HIST, [
("""전체 capture→wire 분포는 미측정; 표본상 정적 구간 실제 프레임 pipeUs 180~190ms 는 비동기 인코더 보유+150ms trailing kick 설계와 크기 부합, 확정 아님)""",
 """전체 capture→wire 분포는 미측정; 표본상 정적 구간 실제 프레임 pipeUs 180~190ms — pipeUs 는 capture stamp→주 스레드 입큐 전 송신 준비 시각(인코더 보유 포함, sender 큐·실제 UDP 송신 제외, `…h264_au.cpp:268/563`)이지 capture→wire 총지연이 아님 — 는 비동기 인코더 보유+150ms trailing kick 설계와 크기 부합, 확정 아님)"""),
])
