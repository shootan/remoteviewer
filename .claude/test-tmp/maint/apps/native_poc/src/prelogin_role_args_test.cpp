// Counter-example tests for the prelogin role transport/codec contract (host_args.hpp, r6 G1).
//
// Codex G1: the service launched the worker with `--prelogin` only; the default transport/codec resolved
// to TCP/raw, which SKIPS the UDP machine-heartbeat branch, so the box never showed online after reboot.
// These checks pin the actual launcher contract: `--prelogin` (the service's real command) must resolve
// to transport=udp + codec=h264, and an explicit TCP/other fallback must be REFUSED.

#include "host_args.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace remote60::native_poc;

static int failures = 0;
static void ok(bool c, const char* w) {
  std::printf(c ? "  ok    %s\n" : "  FAIL  %s\n", w);
  if (!c) ++failures;
}

static Args run_parse(std::vector<const char*> argv) {
  return parse_args(static_cast<int>(argv.size()), const_cast<char**>(argv.data()));
}

int main() {
  std::printf("prelogin role transport/codec contract (G1)\n");

  // --- the pure contract -------------------------------------------------------------------------
  {
    std::string t, c;  // defaults as the service passes them (nothing set)
    ok(ApplyPreloginRoleContract(t, c) == PreloginRoleArgs::Ok && t == "udp" && c == "h264",
       "defaults pin to udp + h264");
  }
  {
    std::string t, c = "raw";  // the host's own default codec
    ok(ApplyPreloginRoleContract(t, c) == PreloginRoleArgs::Ok && t == "udp" && c == "h264",
       "codec=raw default pins to h264");
  }
  {
    std::string t = "tcp", c = "h264";
    ok(ApplyPreloginRoleContract(t, c) == PreloginRoleArgs::RejectTransport,
       "NEGATIVE: explicit transport=tcp -> RejectTransport (no TCP fallback)");
  }
  {
    std::string t = "udp", c = "vp9";
    ok(ApplyPreloginRoleContract(t, c) == PreloginRoleArgs::RejectCodec,
       "NEGATIVE: explicit non-h264 codec -> RejectCodec");
  }
  {
    std::string t = "udp", c = "h264";  // already correct -> accepted unchanged
    ok(ApplyPreloginRoleContract(t, c) == PreloginRoleArgs::Ok && t == "udp" && c == "h264",
       "explicit udp + h264 is accepted");
  }

  // --- the service's ACTUAL command through the real parser --------------------------------------
  {
    const Args a = run_parse({"GNLinkStream", "--prelogin"});
    ok(a.preloginMode, "`--prelogin` sets prelogin mode");
    ok(a.transport == "udp", "`--prelogin` alone pins transport=udp (not the TCP default)");
    ok(a.codec == "h264", "`--prelogin` alone pins codec=h264 (not raw)");
    // r6 V6: the same listeners GNLinkHost gives its child (43000, then 3478).
    ok(a.bindPortCandidates.size() == 2 && a.bindPortCandidates[0] == 43000 && a.bindPortCandidates[1] == 3478,
       "r6 V6: `--prelogin` alone listens on 43000 and 3478, as the logged-in host does");
  }
  {
    // r6 V5: a profile is not taken with --prelogin (and the host refuses to start: preloginRefusedConfig).
    const Args a = run_parse({"GNLinkStream", "--prelogin", "--config", "C:\\nonexistent\\profile.json"});
    ok(a.preloginRefusedConfig, "r6 V5: `--prelogin --config ...` is marked refused (the host exits 12)");
    const Args b = run_parse({"GNLinkStream", "--config", "C:\\nonexistent\\profile.json"});
    ok(!b.preloginRefusedConfig, "...while a logged-in host still takes its --config");
  }
  {
    // An explicit TCP is left intact by the parser so resolve_transport can refuse it (negative control).
    const Args a = run_parse({"GNLinkStream", "--prelogin", "--transport", "tcp"});
    ok(a.transport == "tcp", "an explicit `--transport tcp` is preserved for resolve_transport to refuse");
  }

  if (failures != 0) { std::printf("prelogin_role_args_test: FAIL (%d)\n", failures); return 1; }
  std::printf("prelogin_role_args_test: PASS\n");
  return 0;
}
