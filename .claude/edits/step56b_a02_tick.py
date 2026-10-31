import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
S = r'D:\remote\remote\apps\native_poc\src'
NL = chr(92) + 'n'   # literal \n inside a C++ string

edit(S + r'\host_stage_time_limit.cpp', [
("  sender.PumpUdpHello(transport, encoder);\n  pump_cursor_forward(hx, nowUs);",
 "  sender.PumpUdpHello(transport, encoder);\n"
 "  // A02/A06: a provenance rebuild that could not run yet (the epoch gate's budget was spent, or\n"
 "  // initialize() failed) is retried here, on the tick -- not in the encode path, which a static\n"
 "  // desktop never reaches. The gate stays closed and every AU stays epoch 0 until it succeeds.\n"
 "  if (encoder.provenanceResyncPending && encoder.TryProvenanceResync(capture, nowUs)) {\n"
 "    std::cout << \"[native-video-host] encoder provenance resync ok (tick) resets=\" << encoder.provenanceResyncCount\n"
 "              << \" curEpoch=\" << capture.inputEpoch.load(std::memory_order_acquire)\n"
 "              << \" -> forcing a key" + NL + "\";\n"
 "    kick.Arm(nowUs, useH264);  // a still screen needs the kick to carry the forced IDR out\n"
 "  }\n"
 "  pump_cursor_forward(hx, nowUs);"),
])
print('A02 tick retry ok')
