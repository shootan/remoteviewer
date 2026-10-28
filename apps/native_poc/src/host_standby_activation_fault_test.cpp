// keepalive-r2 e (Codex codex_ka_e_src_r3.md decision 2): the pinned test host's activation fault, through the REAL
// Standby* handling (host_standby_wire.hpp, standby_handle) compiled as the pinned test host compiles it
// (REMOTE60_PINNED_SELECT_TEST_SEAM). No host process, no socket traffic: the packets are handed to the handler.
//
//   fault armed (fail_activation_once=<token>)  the next StandbyActivate is refused as an unknown entry (the
//                                               existing kUdpStandbyStatusUnknown answer), its log names the token,
//                                               the entry and the generation; the table is untouched
//   the same token again                        the next activation of that entry is ACCEPTED (once per token)
//   a new token                                 one more refusal, then accepted again
//   no fault file / an empty file / no token    accepted (the positive control: the same packets, no fault)
//   a Hello                                     never touched (an ordinary connect is not a StandbyActivate)
// The fault file lives in this run's own scratch folder inside the repository (test_scratch_dir), never %TEMP%.

#include <winsock2.h>
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "host_standby_wire.hpp"
#include "test_scratch_dir.hpp"

#ifndef REMOTE60_PINNED_SELECT_TEST_SEAM
#error "this test is the pinned test host's seam: build it with REMOTE60_PINNED_SELECT_TEST_SEAM"
#endif

using namespace remote60::native_poc;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
}
sockaddr_in Addr(uint16_t port) {
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = 0x0100007F;
  a.sin_port = htons(port);
  return a;
}
UdpStandbyPacket Hello(uint8_t dev, uint32_t gen, const char* token) {
  UdpStandbyPacket p{};
  p.kind = static_cast<uint16_t>(UdpPacketKind::StandbyHello);
  p.size = sizeof(p);
  for (uint8_t& x : p.deviceId) x = dev;
  p.clientGen = gen;
  std::strncpy(p.authToken, token, sizeof(p.authToken) - 1);
  return p;
}
UdpStandbyPacket Op(UdpPacketKind kind, uint64_t entryId, const uint8_t secret[16], uint32_t gen) {
  UdpStandbyPacket p{};
  p.kind = static_cast<uint16_t>(kind);
  p.size = sizeof(p);
  p.entryId = entryId;
  std::memcpy(p.entrySecret, secret, 16);
  p.clientGen = gen;
  return p;
}
bool write_file(const std::wstring& path, const std::string& text) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  DWORD put = 0;
  const bool ok = text.empty() || (WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &put, nullptr) && put == text.size());
  CloseHandle(h);
  return ok;
}
}  // namespace

int main() {
  std::printf("host_standby_activation_fault_test (keepalive-r2 e, decision 2)\n\n");
  const std::wstring dir = test_support::make_scratch_dir(L"actfault");
  if (dir.empty()) {
    std::printf("FAIL  no scratch folder inside the repository (%s): refused -- nothing created\nRESULT: INVALID\n",
                test_support::scratch_root_problem().c_str());
    return 2;
  }
  const std::wstring fault = dir + L"\\fault.txt";
  const std::string faultA(fault.begin(), fault.end());
  SetEnvironmentVariableW(L"REMOTE60_PINNED_TEST_FAULT_FILE", fault.c_str());

  StandbyTable w;
  int tokens = 0;
  const auto auth = [&](const std::string&, const sockaddr_in&) { return ++tokens > 0; };
  uint64_t now = 1000000;
  UdpStandbyPacket h = Hello(5, 1, "cap-1");
  auto o = standby_handle(w, reinterpret_cast<const uint8_t*>(&h), sizeof(h), Addr(2000), false, auth, now);
  const UdpStandbyPacket ack = o.reply;
  check("setup: a standby entry admitted", o.action == StandbyAction::Reply && ack.status == kUdpStandbyStatusOk && ack.entryId != 0);
  uint32_t gen = 1;
  auto activate = [&] {
    UdpStandbyPacket act = Op(UdpPacketKind::StandbyActivate, ack.entryId, ack.entrySecret, ++gen);
    now += 100;
    return standby_handle(w, reinterpret_cast<const uint8_t*>(&act), sizeof(act), Addr(2000), false, auth, now);
  };
  auto release_grant = [&](const StandbyOutcome& r) {  // the session it opened ends: the entry may be activated again
    if (r.action != StandbyAction::Activate) return;
    w.BindActiveEpoch(r.entry.grantId, static_cast<uint32_t>(gen));
    UdpStandbyPacket ping = Op(UdpPacketKind::StandbyPing, ack.entryId, ack.entrySecret, 0);
    now += 10;
    (void)standby_handle(w, reinterpret_cast<const uint8_t*>(&ping), sizeof(ping), Addr(2000), false, auth, now,
                         /*endedEpoch=*/gen);
  };

  // the positive controls: no fault file / empty / no token -- accepted
  DeleteFileW(fault.c_str());
  o = activate();
  check("no fault file: the activation is ACCEPTED (positive control)", o.action == StandbyAction::Activate && o.fresh,
        o.log);
  release_grant(o);
  check("an empty fault file can be written in the run's own folder", write_file(fault, ""));
  o = activate();
  check("an empty fault file: accepted", o.action == StandbyAction::Activate, o.log);
  release_grant(o);
  check("a fault file of the other faults only (pong_delay_ms): accepted", write_file(fault, "pong_delay_ms=2000\n"));
  o = activate();
  check("...accepted (only fail_activation_once refuses)", o.action == StandbyAction::Activate, o.log);
  release_grant(o);

  // armed: one refusal per token
  check("arm: fail_activation_once=ep1", write_file(fault, "pong_delay_ms=0\nfail_activation_once=ep1\n"));
  const size_t entries = w.size();
  o = activate();
  const uint32_t refusedGen = gen;
  check("armed: the next activation is REFUSED as an unknown entry (the existing StandbyPong refusal), naming its generation",
        o.action == StandbyAction::Reply && o.reply.kind == static_cast<uint16_t>(UdpPacketKind::StandbyPong) &&
            o.reply.status == kUdpStandbyStatusUnknown && o.reply.entryId == ack.entryId && o.reply.clientGen == refusedGen,
        o.log);
  check("...its log names the token, the entry and the generation",
        o.log.find("[pinned-select-test] fault: fail_activation applied token=ep1 entry=" + std::to_string(ack.entryId) +
                   " gen=" + std::to_string(refusedGen)) != std::string::npos,
        o.log);
  check("...the table is untouched (the entry is still there, nothing activated)", w.size() == entries &&
        w.Find(ack.entryId) != nullptr && w.activeEntryId() == 0);
  o = activate();
  check("the same token again: the next activation is ACCEPTED (once per token)", o.action == StandbyAction::Activate &&
        o.log.find("fault") == std::string::npos, o.log);
  release_grant(o);
  UdpStandbyPacket h2 = Hello(6, 1, "cap-2");
  o = standby_handle(w, reinterpret_cast<const uint8_t*>(&h2), sizeof(h2), Addr(2001), false, auth, now += 10);
  check("a Hello while armed is untouched (an ordinary admission, no fault)", o.action == StandbyAction::Reply &&
        o.reply.status == kUdpStandbyStatusOk && o.log.find("fault") == std::string::npos, o.log);
  check("arm a new episode: fail_activation_once=ep2", write_file(fault, "fail_activation_once=ep2\n"));
  o = activate();
  check("a new token: one more refusal", o.action == StandbyAction::Reply && o.reply.status == kUdpStandbyStatusUnknown &&
        o.log.find("token=ep2") != std::string::npos, o.log);
  o = activate();
  check("...then accepted again", o.action == StandbyAction::Activate, o.log);
  release_grant(o);
  check("a malformed token (not [A-Za-z0-9_-]) is no fault", write_file(fault, "fail_activation_once=../x\n"));
  o = activate();
  check("...accepted", o.action == StandbyAction::Activate, o.log);

  // this run's own file and folder only
  DeleteFileW(fault.c_str());
  RemoveDirectoryW(dir.c_str());
  SetEnvironmentVariableW(L"REMOTE60_PINNED_TEST_FAULT_FILE", nullptr);
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
