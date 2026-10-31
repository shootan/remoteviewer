R = 'C:/work/gnlink-wt/feat-apk-0233/'


def edit(name, reps):
    p = R + name
    s = open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    if crlf:
        s = s.replace('\r\n', '\n')
    for a, b in reps:
        assert s.count(a) == 1, (name, a[:90], s.count(a))
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'w', encoding='utf-8', newline='').write(s)
    print('ok', name)


edit('apps/native_poc/src/host_pinned_select_seam.hpp', [
    ('''#ifdef REMOTE60_PINNED_SELECT_TEST_SEAM
#include <windows.h>
''', '''#ifdef REMOTE60_PINNED_SELECT_TEST_SEAM
#include <windows.h>

#include <cstdint>
#include <string>
'''),
    ('''// Arms drop_keys (called on a window-select) / takes one drop (the sender, per key frame): true = drop it.
void pinned_select_seam_arm_key_drops(unsigned n);
bool pinned_select_seam_take_key_drop();
''', '''// Arms drop_keys (called on a window-select) / takes one drop (the sender, per key frame): true = drop it.
void pinned_select_seam_arm_key_drops(unsigned n);
bool pinned_select_seam_take_key_drop();

// keepalive-r2 e (Codex codex_ka_e_src_r3.md decision 2): the activation failure of ONE test episode, so the APK's
// fallback (nativeConnectStandby gen 0, still its operation -> the same tap's ordinary connect) runs for real.
//   fail_activation_once=<token>  the NEXT StandbyActivate this host receives is refused as an unknown entry (the
//                                 existing kUdpStandbyStatusUnknown answer, as when an entry expired) -- once per
//                                 token: a token already used never applies again, so a later activation and every
//                                 ordinary connect (a Hello, not a StandbyActivate) are untouched. A new episode
//                                 writes a new token (ASCII, up to 32: letters, digits, '-', '_').
// True when this activation is the one to refuse; *log then names the token, the entry and the generation.
bool pinned_select_seam_take_activation_fault(uint64_t entryId, uint32_t gen, std::string* log);
'''),
])

edit('apps/native_poc/src/host_control_session.cpp', [
    ('''std::atomic<unsigned> g_pinnedKeyDrops{0};''', '''// keepalive-r2 e (decision 2): fail_activation_once=<token> -- read at the moment an activation arrives; a token
// applies to one activation only (the tokens used are kept for this process's life).
bool pinned_select_seam_take_activation_fault(uint64_t entryId, uint32_t gen, std::string* log) {
  char path[MAX_PATH] = {};
  if (GetEnvironmentVariableA("REMOTE60_PINNED_TEST_FAULT_FILE", path, sizeof(path)) == 0) return false;
  FILE* fp = nullptr;
  if (fopen_s(&fp, path, "rb") != 0 || !fp) return false;
  std::string token;
  char line[128];
  while (std::fgets(line, sizeof(line), fp)) {
    char t[40] = {};
    if (std::sscanf(line, "fail_activation_once=%32[A-Za-z0-9_-]", t) == 1) token = t;
  }
  std::fclose(fp);
  if (token.empty()) return false;
  static std::mutex mu;
  static std::set<std::string> used;
  {
    std::lock_guard<std::mutex> lk(mu);
    if (!used.insert(token).second) return false;  // this episode's activation was already refused
  }
  if (log) {
    *log = "[pinned-select-test] fault: fail_activation applied token=" + token + " entry=" + std::to_string(entryId) +
           " gen=" + std::to_string(gen) + " -- refused as unknown (once)";
  }
  return true;
}

std::atomic<unsigned> g_pinnedKeyDrops{0};'''),
])

edit('apps/native_poc/src/host_standby_wire.hpp', [
    ('''  // StandbyActivate (StandbyAck / StandbyPong arriving here are a peer's mistake: silent).
  if (kind != static_cast<uint16_t>(UdpPacketKind::StandbyActivate)) return out;
''', '''  // StandbyActivate (StandbyAck / StandbyPong arriving here are a peer's mistake: silent).
  if (kind != static_cast<uint16_t>(UdpPacketKind::StandbyActivate)) return out;
#ifdef REMOTE60_PINNED_SELECT_TEST_SEAM
  // TEST BUILD ONLY (keepalive-r2 e, decision 2): one episode's activation refused as an unknown entry -- the answer
  // the host gives an expired entry -- so the client's fallback runs for real. Nothing of the table changes.
  {
    std::string faultLog;
    if (pinned_select_seam_take_activation_fault(sb.entryId, sb.clientGen, &faultLog)) {
      out.action = StandbyAction::Reply;
      out.reply.kind = static_cast<uint16_t>(UdpPacketKind::StandbyPong);
      out.reply.entryId = sb.entryId;
      out.reply.clientGen = sb.clientGen;
      out.reply.status = kUdpStandbyStatusUnknown;
      out.log = faultLog;
      return out;
    }
  }
#endif
'''),
])

edit('automation/gnlink_check_fixed_server.py', [
    ('''    'os-input-record',            # os_input_effects.hpp, REMOTE60_OS_INPUT_RECORD_ONLY (record-only test host)
''', '''    'os-input-record',            # os_input_effects.hpp, REMOTE60_OS_INPUT_RECORD_ONLY (record-only test host)
    'pinned-select-test',         # host_control_session.cpp, REMOTE60_PINNED_SELECT_TEST_SEAM (its log tag, faults)
    'REMOTE60_PINNED_TEST_',      # the same seam's environment (FAULT_FILE, MACHINE_ID, NO_CAPTURE, CAPTURE_PROBE)
'''),
])
