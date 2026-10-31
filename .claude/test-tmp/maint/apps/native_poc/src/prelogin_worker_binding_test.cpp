// Counter-example tests for the --prelogin worker adoption decision (prelogin_worker_binding.hpp, G6).
//
// Codex G6: the worker adopted the record on `enabled && Unpack` alone, ignoring the durable owner floor
// and the machine binding, so a server-revoke/floor-raise or a copied blob still brought the box online.
// The worker now adopts ONLY a record Usable under the service's binding, and stays offline if the floor
// is unreadable.

#include "prelogin_worker_binding.hpp"

#include <cstdio>

using namespace remote60::native_poc::prelogin;

static int failures = 0;
static void ok(bool c, const char* w) { std::printf(c ? "  ok    %s\n" : "  FAIL  %s\n", w); if (!c) ++failures; }

static MachineRecord sample() {
  MachineRecord r;
  r.accountId = 0xA0C0DE01ull;
  r.hostId = 0x1122334455667788ull;
  r.ownerGeneration = 5;
  r.enabled = true;
  r.directoryOrigin = "https://dir.example:443";
  r.machineId = "MACHINE-GUID-TEST";
  r.credential = {0x10, 0x20, 0x30};
  return r;
}

int main() {
  std::printf("prelogin worker adoption binding (G6)\n");
  const MachineRecord r = sample();

  ok(PreloginWorkerAdopts(r, "MACHINE-GUID-TEST", /*floorKnown=*/true, /*floor=*/5),
     "usable record, floor == generation -> adopt");
  ok(PreloginWorkerAdopts(r, "MACHINE-GUID-TEST", true, 0),
     "usable record, floor 0 (never revoked) -> adopt");

  // Counter-example (the G6 defect): a raised floor (server revoke) must stop worker adoption.
  ok(!PreloginWorkerAdopts(r, "MACHINE-GUID-TEST", true, 6),
     "NEGATIVE: floor raised above generation (revoked) -> NOT adopted (stays offline)");
  // A copied blob on a different machine must not come online.
  ok(!PreloginWorkerAdopts(r, "SOME-OTHER-MACHINE", true, 5),
     "NEGATIVE: machine id mismatch (copied blob) -> NOT adopted");
  // The floor could not be read -> fail safe, never fail open to floor 0.
  ok(!PreloginWorkerAdopts(r, "MACHINE-GUID-TEST", /*floorKnown=*/false, 0),
     "NEGATIVE: owner floor unreadable -> NOT adopted (fail safe)");
  // A disabled record is not adopted either.
  {
    MachineRecord d = r;
    d.enabled = false;
    ok(!PreloginWorkerAdopts(d, "MACHINE-GUID-TEST", true, 5),
       "NEGATIVE: disabled record -> NOT adopted");
  }

  if (failures != 0) { std::printf("prelogin_worker_binding_test: FAIL (%d)\n", failures); return 1; }
  std::printf("prelogin_worker_binding_test: PASS\n");
  return 0;
}
