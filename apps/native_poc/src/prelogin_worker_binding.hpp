#pragma once

// Worker-side machine-record adoption decision (prelogin-service r6 G6).
//
// Codex G6: the --prelogin worker loaded the record and checked only `enabled && Unpack`, ignoring the
// external owner floor, the current machine id, and the account/origin binding. So a server-revoke /
// floor-raise / wrong-machine blob was still adopted by the worker even though the service gate rejected
// it -- the box would register and come online after a revoke. The worker must adopt ONLY a record that
// is Usable under the SAME binding the service gate uses, and must stay offline if the floor is unknown.

#include <cstdint>
#include <string>

#include "prelogin_machine_record.hpp"

namespace remote60::native_poc::prelogin {

// True iff the worker may adopt this record. `floorKnown` is the result of reading the durable owner
// floor: when it could not be read, adoption is refused (fail safe -> stay offline). Mirrors the service
// gate (expectedAccountId=0 accepts any enrolled account; originAllowed verified at register time).
inline bool PreloginWorkerAdopts(const MachineRecord& r, const std::string& currentMachineId,
                                 bool floorKnown, uint64_t floor) {
  if (!floorKnown) return false;  // cannot confirm the revoke floor -> do not come online
  BindingContext ctx;
  ctx.currentMachineId = currentMachineId;
  ctx.expectedAccountId = 0;        // any account enrolled on this machine
  ctx.originAllowed = true;         // origin binding verified at register time; not re-litigated here
  ctx.minOwnerGeneration = floor;   // a record below the floor is Revoked
  ctx.featureGateEnabled = true;    // the worker only runs when the service already decided enabled
  return ValidateMachineRecord(r, ctx) == RecordVerdict::Usable;
}

}  // namespace remote60::native_poc::prelogin
