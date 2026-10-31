// Tests for the remote-reboot wire codec (reboot_wire.hpp). Pins the fixed layout, the request/reply
// round trip, and the validity checks (echoed id, enum range, win32 only on Failed). (prelogin-service
// r2; message types 84/85 assigned by the verifier.)

#include "reboot_wire.hpp"

#include <cstdio>

using namespace remote60::native_poc;

namespace {
int gFailures = 0;
void check(bool cond, const char* what) {
  std::printf(cond ? "  ok   %s\n" : "  FAIL %s\n", what);
  if (!cond) ++gFailures;
}
}  // namespace

int main() {
  std::printf("reboot wire codec\n");

  // Request round trip.
  auto reqBytes = build_reboot_request(0xABCDEF01u);
  check(reqBytes.size() == sizeof(ControlRebootRequestMessage), "request is the fixed size");
  ControlRebootRequestMessage req{};
  std::memcpy(&req, reqBytes.data(), sizeof(req));
  check(req.header.magic == kMagic && req.header.type == static_cast<uint16_t>(MessageType::ControlRebootRequest) &&
            req.header.size == sizeof(req),
        "request header: magic/type(84)/size");
  check(req.requestId == 0xABCDEF01u, "request id round-trips");
  check(reboot_request_valid(req), "a non-zero request id is valid");

  ControlRebootRequestMessage zero{};
  zero.header.magic = kMagic;
  zero.header.type = static_cast<uint16_t>(MessageType::ControlRebootRequest);
  zero.header.size = sizeof(zero);
  zero.requestId = 0;
  check(!reboot_request_valid(zero), "NEGATIVE: a zero request id is rejected");

  ControlRebootRequestMessage wrongType = req;
  wrongType.header.type = static_cast<uint16_t>(MessageType::ControlPing);
  check(!reboot_request_valid(wrongType), "NEGATIVE: wrong type is rejected");

  // Reply: each result, win32 discipline, echo check.
  auto accepted = make_reboot_reply(0xABCDEF01u, RebootResult::Accepted, 0);
  check(accepted.header.type == static_cast<uint16_t>(MessageType::ControlRebootReply) &&
            accepted.header.size == sizeof(ControlRebootReplyMessage),
        "reply header: type(85)/size");
  check(reboot_reply_valid(accepted, 0xABCDEF01u), "Accepted reply with the echoed id is valid");
  check(!reboot_reply_valid(accepted, 0x00000002u), "NEGATIVE: a mismatched echo id is rejected");

  auto failed = make_reboot_reply(7, RebootResult::Failed, 1115 /*ERROR_SHUTDOWN_IN_PROGRESS*/);
  check(failed.result == static_cast<uint8_t>(RebootResult::Failed) && failed.win32 == 1115u,
        "Failed carries the win32 code");

  auto refused = make_reboot_reply(7, RebootResult::Refused, 1115);
  check(refused.win32 == 0u, "win32 is zeroed unless result == Failed");

  auto notAuth = make_reboot_reply(7, RebootResult::NotAuthorized, 0);
  check(reboot_reply_valid(notAuth, 7) && notAuth.result == static_cast<uint8_t>(RebootResult::NotAuthorized),
        "NotAuthorized reply is valid and in range");

  ControlRebootReplyMessage badResult = accepted;
  badResult.result = kRebootResultMax + 1;
  check(!reboot_reply_valid(badResult, 0xABCDEF01u), "NEGATIVE: an out-of-range result is rejected");

  if (gFailures != 0) {
    std::printf("reboot_wire_test: FAIL (%d)\n", gFailures);
    return 1;
  }
  std::printf("reboot_wire_test: PASS\n");
  return 0;
}
