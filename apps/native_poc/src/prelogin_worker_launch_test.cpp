// prelogin-ui r6 (V1, V4): how the service starts the pre-login worker, and what uninstall withdraws.
//
//   * the worker's environment carries the product's streaming switches whatever the service's own
//     environment holds (the switch it lacked made every worker exit rc 11) -- and they win over a base
//     that says otherwise; the rest of the base is kept, names compared as Windows does (any case)
//   * GNLinkHost's child and the pre-login worker get the same switches (one definition)
//   * a worker that keeps dying is retried less and less often, one that ran a while starts over
//   * uninstall withdraws exactly as "off" does: the record goes, the floor rises past it
// (Pure; the process-level proof is viewer_prelogin_login_e2e_test, which starts the worker with these.)

#include <windows.h>

#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "prelogin_admin_pipe.hpp"
#include "prelogin_worker_launch.hpp"

namespace pl = remote60::native_poc::prelogin;
using remote60::native_poc::BuildEnvironmentBlock;
using remote60::native_poc::ProductStreamChildEnv;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
}

std::vector<wchar_t> block(const std::vector<std::wstring>& entries) {
  std::vector<wchar_t> b;
  for (const auto& e : entries) {
    b.insert(b.end(), e.begin(), e.end());
    b.push_back(L'\0');
  }
  b.push_back(L'\0');
  return b;
}

std::map<std::wstring, std::wstring> parse(const std::vector<wchar_t>& b, int* count) {
  std::map<std::wstring, std::wstring> m;
  *count = 0;
  for (const wchar_t* p = b.data(); *p; p += wcslen(p) + 1) {
    const std::wstring e = p;
    const size_t eq = e.find(L'=', 1);
    m[e.substr(0, eq)] = e.substr(eq + 1);
    ++*count;
  }
  return m;
}
}  // namespace

int main() {
  std::printf("pre-login worker launch contract (prelogin-ui r6)\n\n");
  {
    // The service's own environment before r6: nothing about streaming.
    const auto base = block({L"Path=C:\\Windows", L"SystemRoot=C:\\Windows", L"ProgramData=C:\\ProgramData"});
    int n = 0;
    const auto env = parse(pl::PreloginWorkerEnvironment(base.data()), &n);
    check("V1: the worker gets REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1 (without it: rc 11)",
          env.count(L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE") && env.at(L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE") == L"1");
    check("...the product preset (low_latency) and GPU surface encode off",
          env.count(L"REMOTE60_NATIVE_ENCODER_TUNE_MODE") && env.at(L"REMOTE60_NATIVE_ENCODER_TUNE_MODE") == L"low_latency" &&
              env.count(L"REMOTE60_NATIVE_NV12_SURFACE") && env.at(L"REMOTE60_NATIVE_NV12_SURFACE") == L"0");
    check("...and keeps the service's own environment (ProgramData is where the record is)",
          env.count(L"ProgramData") && env.at(L"ProgramData") == L"C:\\ProgramData" && n == 6, std::to_string(n));
  }
  {
    // A base that already says something else -- in another case -- does not win.
    const auto base = block({L"remote60_native_encoded_experiment_force=0", L"REMOTE60_NATIVE_NV12_SURFACE=1", L"A=b"});
    int n = 0;
    const auto env = parse(pl::PreloginWorkerEnvironment(base.data()), &n);
    check("V1: a base saying FORCE=0 (any case) is replaced, not duplicated",
          env.count(L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE") && env.at(L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE") == L"1" &&
              !env.count(L"remote60_native_encoded_experiment_force") && n == 4,
          std::to_string(n));
    check("...and so is NV12=1 (the worker runs with it off)", env.at(L"REMOTE60_NATIVE_NV12_SURFACE") == L"0");
  }
  {
    // GNLinkHost and the service state the same switches (the host's nv12 decision aside).
    const auto host = ProductStreamChildEnv(false);
    int n = 0;
    const auto worker = parse(pl::PreloginWorkerEnvironment(block({}).data()), &n);
    bool same = true;
    for (const auto& kv : host) same = same && worker.count(kv.first) && worker.at(kv.first) == kv.second;
    check("V1: GNLinkHost's child and the pre-login worker get the same switches (one definition)", same && n == 3);
    check("...and the worker's whole argument list is --prelogin", pl::PreloginWorkerArgs() == L"--prelogin");
  }
  {
    pl::WorkerRestartBackoff b;
    check("V1: the first start may go at once", b.MayLaunch(0));
    uint64_t t = 10000;
    const uint64_t w1 = b.OnExit(t, t + 500);  // died after 0.5 s
    const uint64_t w2 = b.OnExit(t + 3000, t + 3500);
    const uint64_t w3 = b.OnExit(t + 9000, t + 9100);
    check("a worker that keeps dying is retried after 2 s, 4 s, 8 s ...", w1 == 2000 && w2 == 4000 && w3 == 8000,
          std::to_string(w1) + " " + std::to_string(w2) + " " + std::to_string(w3));
    check("...not before", !b.MayLaunch(t + 9100 + 7999) && b.MayLaunch(t + 9100 + 8000));
    uint64_t w = 0;
    for (int i = 0; i < 10; ++i) w = b.OnExit(t, t + 1);
    check("...never more than 60 s apart", w == 60000, std::to_string(w));
    check("one that ran a while (>= 15 s) starts the wait over (1 s)", b.OnExit(t, t + 20000) == 1000);
    check("a start that failed outright counts as a quick death", b.OnLaunchFailed(t) == 2000);
  }
  // V4 + r9 R2 + r10 T1: uninstall = off, as one writer, and "off" proven by what the revoke DID. In-memory
  // primitives standing in for the store, the floor, the SCM and the mark; `log` records the order.
  struct Opt {
    const char* title;
    bool floorWorks = true;
    bool stopWorks = true;
    bool readFails = false;    // the record is there but cannot be read (ACL / IO / DPAPI)
    bool removeFails = false;  // ...nor removed
    bool probeError = false;   // whether a record file is there cannot be told
    bool startTypeFails = false;  // the service is already gone: its start type cannot be put back
    bool markWorks = true;
    int issuedState = 1;       // r11 U1: the issued bound: 1 kept (7, what Enable issued), 0 none kept, -1 unreadable
    bool recordThere = true;   // a record file at the start
  };
  struct Out {
    pl::UninstallRevokeResult r;
    bool recordLeft, mark, enabled;
    uint64_t floor;
    uint32_t startType;
    bool remembered;
    std::string log;
  };
  auto uninstall_case = [&](const Opt& o) {
    std::printf("\n--- uninstall: %s ---\n", o.title);
    std::map<std::wstring, pl::MachineRecord> files;
    uint64_t floor = 3;
    uint32_t startType = 2;
    bool remembered = true;
    bool mark = false;
    std::string log;
    uint64_t issued = 7;
    pl::AdminPrimitives p;
    p.recordPath = [] { return std::wstring(L"rec"); };
    p.machineId = [] { return std::string("m"); };
    p.readFloor = [&](uint64_t* out) { *out = floor; return true; };
    p.raiseFloor = [&](uint64_t f) {
      log += "floor ";
      if (!o.floorWorks) return false;
      if (f > floor) floor = f;
      return true;
    };
    p.readIssued = [&](uint64_t* out) {
      *out = o.issuedState == 1 ? issued : 0;
      return o.issuedState;
    };
    p.raiseIssued = [&](uint64_t g) { if (g > issued) issued = g; return true; };
    p.revokePending = [&] { return mark ? 1 : 0; };
    p.clearRevokePending = [&] { mark = false; return true; };
    p.markRevokePending = [&] { mark = true; return true; };
    p.readRecord = [&](const std::wstring& path, pl::MachineRecord* out) {
      if (o.readFails) return false;
      auto it = files.find(path);
      if (it == files.end()) return false;
      *out = it->second;
      return true;
    };
    p.probeRecord = [&](const std::wstring& path) -> int {
      if (o.probeError) return -1;
      return files.count(path) ? 1 : 0;
    };
    p.writeRecord = [&](const std::wstring& path, const pl::MachineRecord& r) -> uint32_t { files[path] = r; return 0; };
    p.removeRecord = [&](const std::wstring& path) -> uint32_t {
      if (o.removeFails) return ERROR_ACCESS_DENIED;
      files.erase(path);
      return 0;
    };
    p.featureEnabled = [&] { return !mark && !files.empty(); };
    p.queryStartType = [&](uint32_t* out) { *out = startType; return true; };
    p.setStartType = [&](uint32_t t) -> uint32_t {
      if (o.startTypeFails) return ERROR_SERVICE_DOES_NOT_EXIST;
      startType = t;
      return 0;
    };
    p.loadRememberedStartType = [&](uint32_t* out) { if (!remembered) return false; *out = 3; return true; };
    p.rememberStartType = [&](uint32_t) { remembered = true; return true; };
    p.forgetRememberedStartType = [&] { remembered = false; };
    pl::MachineRecord rec;
    rec.ownerGeneration = 7;
    rec.enabled = true;
    rec.machineId = "m";
    rec.hostId = 1;
    rec.credential = {1, 2, 3};
    if (o.recordThere) files[L"rec"] = rec;
    const pl::UninstallRevokeResult r = pl::RevokeForUninstall(
        p, [&] { log += "mark "; if (o.markWorks) mark = true; return o.markWorks; },
        [&] { log += "stop "; return o.stopWorks; });
    return Out{r, !files.empty(), mark, p.featureEnabled(), floor, startType, remembered, log};
  };
  // r11 U1: a copy of the generation-7 record, put back after the revoke -- what the product's own
  // ValidateMachineRecord says of it over the floor the revoke left.
  auto restored_copy_verdict = [](uint64_t floorAfter) {
    pl::MachineRecord copy;
    copy.ownerGeneration = 7;
    copy.enabled = true;
    copy.machineId = "m";
    copy.hostId = 1;
    copy.credential = {1, 2, 3};
    pl::BindingContext ctx;
    ctx.currentMachineId = "m";
    ctx.originAllowed = true;
    ctx.minOwnerGeneration = floorAfter;
    return pl::ValidateMachineRecord(copy, ctx);
  };
  {
    Opt o;
    o.title = "the normal case";
    const auto r = uninstall_case(o);
    check("V4: uninstall withdraws pre-login access: revoked, the record is gone", r.r.revoked && !r.recordLeft && !r.enabled);
    check("...the floor rose past it (an old copy restored after a reinstall stays revoked)", r.floor > 7,
          std::to_string(r.floor));
    check("...and the remembered start type is put back and forgotten", r.startType == 3 && !r.remembered);
    check("R2: in this order: the mark, the service stopped, THEN the floor (one writer)", r.log == "mark stop floor ", r.log);
    check("...and the mark is cleared once it really worked", !r.mark);
  }
  {
    Opt o;
    o.title = "the floor cannot be raised";
    o.floorWorks = false;
    const auto r = uninstall_case(o);
    check("R2: the revoke failed: NOT reported as revoked", !r.r.revoked && r.r.reply.status == pl::AdminStatus::Failed);
    check("...the record is still there, but the mark stays down: the feature is off and a reinstall's service "
          "revokes first",
          r.recordLeft && r.mark && !r.enabled);
  }
  {
    Opt o;
    o.title = "the service does not stop";
    o.stopWorks = false;
    const auto r = uninstall_case(o);
    check("R2: the service did not stop: this process writes NOTHING (no second writer)",
          !r.r.revoked && r.log == "mark stop " && r.recordLeft && r.floor == 3, r.log);
    check("...the mark is down: the live service finishes the revoke itself", r.mark && !r.enabled);
  }
  {
    Opt o;
    o.title = "T1: the record is there but cannot be read, and the floor cannot be raised";
    o.readFails = true;
    o.floorWorks = false;
    const auto r = uninstall_case(o);
    check("T1: unreadable is not absent: NOT revoked, the mark STAYS (r9 cleared it here)",
          !r.r.revoked && r.mark && r.recordLeft, "state " + std::to_string(r.r.recordState));
  }
  {
    Opt o;
    o.title = "T1: unreadable and cannot be removed";
    o.readFails = true;
    o.removeFails = true;
    const auto r = uninstall_case(o);
    check("T1: the floor rose but the record could not be removed: NOT revoked, the mark stays",
          !r.r.revoked && r.r.floorRaised && r.r.removeError == ERROR_ACCESS_DENIED && r.mark && r.recordLeft);
  }
  {
    Opt o;
    o.title = "T1: whether a record file is there cannot be told";
    o.probeError = true;
    const auto r = uninstall_case(o);
    check("T1: removed, but its absence cannot be confirmed: NOT revoked, the mark stays",
          !r.r.revoked && r.r.recordState == -1 && r.mark);
  }
  {
    Opt o;
    o.title = "T1: the service is already gone (only its start type cannot be put back)";
    o.startTypeFails = true;
    const auto r = uninstall_case(o);
    check("T1: floor raised, record removed and absent -- revoked, although the reply says Failed (start type)",
          r.r.revoked && r.r.reply.status == pl::AdminStatus::Failed && !r.mark && !r.recordLeft);
  }
  {
    Opt o;
    o.title = "T1: neither the mark nor the stop";
    o.markWorks = false;
    o.stopWorks = false;
    const auto r = uninstall_case(o);
    check("T1: nothing recorded and nothing done: NOT revoked and NOT marked (the caller must say \"unconfirmed\")",
          !r.r.revoked && !r.r.marked && !r.mark && r.recordLeft);
  }
  {
    Opt o;
    o.title = "U1: floor 3, record 7 -- the record cannot be read, its removal works";
    o.readFails = true;
    const auto r = uninstall_case(o);
    check("U1: the floor goes above the ISSUED bound (7), not above an unread record taken for 0 (r10: 4)",
          r.r.revoked && r.floor > 7, std::to_string(r.floor));
    check("...a copy of the generation-7 record restored afterwards is Revoked (product ValidateMachineRecord)",
          restored_copy_verdict(r.floor) == pl::RecordVerdict::Revoked);
  }
  {
    Opt o;
    o.title = "U1: unreadable record, and no issued bound was ever kept";
    o.readFails = true;
    o.issuedState = 0;
    const auto r = uninstall_case(o);
    check("U1: no proof the floor is above it: NOT revoked, the mark STAYS, the record is LEFT (its generation is "
          "the only evidence)",
          !r.r.revoked && !r.r.boundProven && r.mark && r.recordLeft && !r.enabled,
          "floor " + std::to_string(r.floor));
  }
  {
    Opt o;
    o.title = "U1: the issued bound cannot be read";
    o.issuedState = -1;
    const auto r = uninstall_case(o);
    check("U1: NOT revoked, the mark stays (the record could be read -- the bound still is not known)",
          !r.r.revoked && r.mark && !r.enabled);
  }
  {
    Opt o;
    o.title = "U1: no record file before the removal, the bound kept";
    o.recordThere = false;
    const auto r = uninstall_case(o);
    check("U1: revoked, the floor above the issued 7 -- a copy of that record restored later is Revoked",
          r.r.revoked && r.floor > 7 && restored_copy_verdict(r.floor) == pl::RecordVerdict::Revoked,
          std::to_string(r.floor));
  }
  {
    Opt o;
    o.title = "U1: no record file and no bound ever kept (nothing was ever issued)";
    o.recordThere = false;
    o.issuedState = 0;
    const auto r = uninstall_case(o);
    check("U1: nothing was issued, nothing is there: revoked", r.r.revoked && !r.mark);
  }
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
