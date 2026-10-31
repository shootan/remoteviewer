import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
edit(r'D:\remote\remote\apps\native_poc\src\native_video_client_shared_core_test.cpp', [
("""  if (!expect(a.PopDelivery(t4 + 2000, true, &out) && out.frame.header.seq == 9 &&
                  out.droppedPreviousIncomplete,
              "hold: AU 9 delivered before AU 10 (gap to 8 is real)")) return false;
""",
"""  if (!expect(a.PopDelivery(t4 + 2000, true, &out) && out.frame.header.seq == 9 &&
                  !out.droppedPreviousIncomplete,
              "hold: AU 9 delivered before AU 10 (contiguous with 8)")) return false;
"""),
])
edit(r'D:\remote\remote\apps\native_poc\src\viewer_udp_recovery_test.cpp', [
("""      if (!only && key && plan_.keyInterChunkDelayUs > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(plan_.keyInterChunkDelayUs));
      }
""",
"""      if (!only && key && plan_.keyInterChunkDelayUs > 0) {
        // Spin, not sleep: a Windows sleep is 1-2 ms at best, which would stretch a 72-chunk IDR
        // past the 120 ms tail grace and turn the scenario into a test of the timer instead.
        const uint64_t untilUs = qpc_now_us() + plan_.keyInterChunkDelayUs;
        while (qpc_now_us() < untilUs) {
        }
      }
"""),
("""    if (connect(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) return false;
    // The same handshake connect_media_socket runs, with the same options.
""",
"""    if (connect(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) return false;
    // The product's receive buffer (connect_media_socket): the first IDR's decoder initialisation
    // takes ~100 ms, during which the next frames must fit in the socket, not spill.
    const int recvBuf = 1024 * 1024;
    (void)setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&recvBuf), sizeof(recvBuf));
    // The same handshake connect_media_socket runs, with the same options.
"""),
("""  plan.keyInterChunkDelayUs = 400;  // ~150 chunks -> ~60 ms on the wire
""",
"""  plan.keyInterChunkDelayUs = 700;  // ~72 chunks -> ~50 ms on the wire: past the hole grace, inside the tail grace
"""),
])
