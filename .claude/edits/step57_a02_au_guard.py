import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
S = r'D:\remote\remote\apps\native_poc\src'
NL = chr(92) + 'n'

edit(S + r'\host_stage_encode_send_h264_au.cpp', [
# HN07: an unprovenanced output closed the verified chain -> force the next key, like DropAwaitingKey
("      case EpochVerdict::DropUnknownEpoch:\n"
 "        // No FIFO provenance (0) or an epoch this host never issued: fail closed, never on the wire.\n"
 "        std::cout << \"[native-video-host] epoch-gate dropped-unknown auEpoch=\" << au.inputEpoch << \" curEpoch=\" << inputEpochNow\n"
 "                  << \" auCaptureUs=\" << gateStampUs << \" key=\" << (au.keyFrame ? 1 : 0)\n"
 "                  << \" total=\" << encoder.epochGate.droppedUnknownEpoch << \"" + NL + "\";\n"
 "        return AuFlow::Continue;",
 "      case EpochVerdict::DropUnknownEpoch:\n"
 "        // No FIFO provenance (0) or an epoch this host never issued: fail closed, never on the wire.\n"
 "        // HN07: if this arrived while the gate was open, the gate has just closed the verified\n"
 "        // chain again (an output nobody can vouch for may reference pictures the viewer has not\n"
 "        // got), so the key is re-forced -- the same answer as DropAwaitingKey. A DropOldEpoch\n"
 "        // above does not do this: its provenance is certain and the chain is still good.\n"
 "        encoder.forceKeyNext = true;\n"
 "        std::cout << \"[native-video-host] epoch-gate dropped-unknown auEpoch=\" << au.inputEpoch << \" curEpoch=\" << inputEpochNow\n"
 "                  << \" auCaptureUs=\" << gateStampUs << \" key=\" << (au.keyFrame ? 1 : 0)\n"
 "                  << \" provenanceInvalid=\" << (encoder.epochGate.provenanceInvalid ? 1 : 0)\n"
 "                  << \" reclosed=\" << encoder.epochGate.reclosedByUnknown\n"
 "                  << \" total=\" << encoder.epochGate.droppedUnknownEpoch << \"" + NL + "\";\n"
 "        return AuFlow::Continue;"),
# A06 belt and braces at the single enqueue point
("  if (!sender.udpPeerReady) {\n    ++sender.udpTxNoPeer;\n    sentOk = false;\n  } else {\n    EncodedSendItem item;",
 "  if (encoder.epochGate.provenanceInvalid) {\n"
 "    // A06 belt and braces at the only enqueue point: the gate already refuses these (their epoch\n"
 "    // is 0 -> DropUnknownEpoch), so reaching here would mean an AU from an encoder whose output\n"
 "    // order nothing describes was about to hit the wire. Never send it; the rebuild is pending.\n"
 "    ++sender.udpTxNoPeer;\n"
 "    sentOk = false;\n"
 "  } else if (!sender.udpPeerReady) {\n    ++sender.udpTxNoPeer;\n    sentOk = false;\n  } else {\n    EncodedSendItem item;"),
])
print('A02 au guard ok')
