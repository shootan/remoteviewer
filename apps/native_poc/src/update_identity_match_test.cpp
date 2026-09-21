#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

// Same, Different, or Unknown -- and never two of those collapsed into one.
// Plus the hand-written move that carries a TargetWatch between them.
// (updater-abandon-race r3 item B, r4 items B-test and E-test)
//
// The bool this replaced returned false for "the handle belongs to somebody else" and for "the
// question could not be answered" alike, and every caller read false as "our target has already
// exited". On a machine where the query fails, that turns an unanswered question into permission
// to swap files under a process that is still running.
//
// r3's version of this file described those cases but did not produce them: every check ran
// against this process's own handle with a readable, equal path, and the two interesting states --
// a handle whose queries FAIL, and a handle to a process that has EXITED -- were argued about
// rather than created. Both are made here now, from real child processes started by this test.
//
// ---------------------------------------------------------------------------------------------
// What "identity" means here, stated because it is easy to over-read.
//
// A creation time is NOT a globally unique process id. It is a FILETIME, and two processes on two
// different pids may share one. The contract is narrower, and is enough:
//
//     for ONE pid, captured once and then re-confirmed through a handle that has been held
//     continuously since the capture, an unchanged creation time means it is the same process.
//
// The held handle is what carries that: Windows will not recycle a pid while a handle to it is
// open, so "this pid" cannot come to mean something else between the capture and the question.
// Identity also says NOTHING about whether the process is alive -- an exited process answers Same,
// deliberately, and whether it exited is a separate question answered by waiting on the handle.
// ---------------------------------------------------------------------------------------------

#include <windows.h>

#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "update_effects.hpp"

using namespace remote60::native_poc::update;

namespace {

int gChecks = 0;
int gFailures = 0;
int gSkips = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::cout << (ok ? "PASS  " : "FAIL  ") << name;
  if (!detail.empty()) std::cout << "  " << detail;
  std::cout << "\n";
}

// Not a pass and not a failure: a precondition this environment did not provide. Named so it
// cannot be mistaken for either in the totals.
void skip(const std::string& name, const std::string& why) {
  ++gSkips;
  std::cout << "SKIP  " << name << "  " << why << "\n";
}

const char* nm(IdentityMatch m) { return identity_match_name(m); }

std::wstring own_path() {
  wchar_t buffer[MAX_PATH * 2]{};
  GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
  return buffer;
}

/**
 * A child of this test that lives until it is told to stop.
 *
 * It exists so that the two states that matter can be produced rather than described: a handle
 * whose queries fail (opened with too little access), and a handle to a process that has ended.
 * Nothing is terminated -- the child is asked through an event and returns from main on its own.
 */
struct Child {
  PROCESS_INFORMATION pi{};
  HANDLE release = nullptr;
  std::wstring eventName;

  bool start(const std::wstring& tag) {
    eventName = L"Local\\gnlink-idmatch-" + tag + L"-" + std::to_wstring(GetCurrentProcessId());
    release = CreateEventW(nullptr, TRUE, FALSE, eventName.c_str());
    if (!release) return false;
    std::wstring cmd = L"\"" + own_path() + L"\" --fixture-wait " + eventName;
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    return CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                          nullptr, nullptr, &si, &pi) != FALSE;
  }

  // Ask it to leave, and wait until it has. Not a kill: the fixture returns from main.
  bool let_go() {
    if (!release) return false;
    SetEvent(release);
    return WaitForSingleObject(pi.hProcess, 10000) == WAIT_OBJECT_0;
  }

  ~Child() {
    if (release) {
      SetEvent(release);
      WaitForSingleObject(pi.hProcess, 5000);
      CloseHandle(release);
    }
    if (pi.hThread) CloseHandle(pi.hThread);
    if (pi.hProcess) CloseHandle(pi.hProcess);
  }

  uint32_t pid() const { return static_cast<uint32_t>(pi.dwProcessId); }
};

/** Waits to be released, then returns. The child side of Child. */
int run_fixture_wait(const std::wstring& eventName) {
  HANDLE e = OpenEventW(SYNCHRONIZE, FALSE, eventName.c_str());
  if (!e) return 91;
  WaitForSingleObject(e, 60000);
  CloseHandle(e);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 3 && std::string(argv[1]) == "--fixture-wait") {
    const std::string narrow(argv[2]);
    return run_fixture_wait(std::wstring(narrow.begin(), narrow.end()));
  }

  std::cout << "update_identity_match_test\n";

  // ------------------------------------------------------------------ synthetic, on this process
  ProcessTarget self;
  check("this process identifies", capture_process_identity(GetCurrentProcessId(), &self));

  HANDLE h =
      OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
  check("a handle to it was opened", h != nullptr);

  check("the same process is Same", process_identity_check(h, self) == IdentityMatch::Same,
        nm(process_identity_check(h, self)));

  {
    ProcessTarget other = self;
    other.creationTime = self.creationTime + 1;
    check("a different creation time is Different",
          process_identity_check(h, other) == IdentityMatch::Different,
          nm(process_identity_check(h, other)));
  }

  {
    ProcessTarget other = self;
    other.imagePath = L"C:\\Windows\\System32\\nowhere-at-all.exe";
    check("a readable path that disagrees is Different",
          process_identity_check(h, other) == IdentityMatch::Different,
          nm(process_identity_check(h, other)));
  }

  {
    ProcessTarget blank = self;
    blank.creationTime = 0;
    check("no recorded creation time is Unknown, not Different",
          process_identity_check(h, blank) == IdentityMatch::Unknown,
          nm(process_identity_check(h, blank)));
    check("...and the bool form refuses it", !process_identity_matches(h, blank));
  }

  check("a null handle is Unknown", process_identity_check(nullptr, self) == IdentityMatch::Unknown,
        nm(process_identity_check(nullptr, self)));

  if (h) CloseHandle(h);

  check("the three answers have three names",
        std::string(nm(IdentityMatch::Same)) != nm(IdentityMatch::Different) &&
            std::string(nm(IdentityMatch::Different)) != nm(IdentityMatch::Unknown) &&
            std::string(nm(IdentityMatch::Same)) != nm(IdentityMatch::Unknown));

  // ---------------------------------------------------------------- a handle that cannot answer
  //
  // Produced, not simulated: a handle opened with SYNCHRONIZE only. It is a perfectly valid handle
  // to a running process -- you can wait on it -- and GetProcessTimes on it fails, because that
  // needs PROCESS_QUERY_LIMITED_INFORMATION. This is the shape of the field case (a process the
  // updater may see but not interrogate), reproduced without needing another account.
  {
    Child c;
    check("a child for the query-failure case started", c.start(L"deny"));
    ProcessTarget t;
    check("its identity is captured while it runs", capture_process_identity(c.pid(), &t));

    HANDLE syncOnly = OpenProcess(SYNCHRONIZE, FALSE, c.pid());
    check("a SYNCHRONIZE-only handle opened", syncOnly != nullptr);
    if (syncOnly) {
      FILETIME a{}, b{}, k{}, u{};
      SetLastError(0);
      const bool timesOk = GetProcessTimes(syncOnly, &a, &b, &k, &u) != FALSE;
      const DWORD err = GetLastError();
      // The premise of the case, asserted rather than assumed. If this ever started succeeding,
      // the check below would pass for the wrong reason and nobody would know.
      check("...and the query really does fail on it", !timesOk,
            "GetProcessTimes err=" + std::to_string(err));
      check("...so it is a valid handle, just not an answering one",
            WaitForSingleObject(syncOnly, 0) == WAIT_TIMEOUT);
      check("a handle whose query fails is Unknown -- not Different, not Same",
            process_identity_check(syncOnly, t) == IdentityMatch::Unknown,
            nm(process_identity_check(syncOnly, t)));
      check("...and the bool form refuses it, so no caller can read it as an exit",
            !process_identity_matches(syncOnly, t));
      CloseHandle(syncOnly);
    }
  }

  // ----------------------------------------------------------- a handle to a process that ended
  //
  // The direction r3 got wrong on its first attempt and had to correct: an EXITED process cannot
  // report its image path, and calling that Unknown would have blocked every update whose target
  // had properly gone -- the opposite failure. That correction was argued from the API contract;
  // here it is produced.
  {
    Child c;
    check("a child for the exited case started", c.start(L"exit"));
    ProcessTarget t;
    check("its identity is captured while it runs", capture_process_identity(c.pid(), &t));
    check("...including a readable image path", !t.imagePath.empty());
    check("it is Same while it runs",
          process_identity_check(c.pi.hProcess, t) == IdentityMatch::Same,
          nm(process_identity_check(c.pi.hProcess, t)));

    check("the child left when it was asked to", c.let_go());
    check("...and the handle says so", WaitForSingleObject(c.pi.hProcess, 0) == WAIT_OBJECT_0);

    // The premise: the path really is unreadable now.
    wchar_t buffer[MAX_PATH * 2]{};
    DWORD size = static_cast<DWORD>(std::size(buffer));
    SetLastError(0);
    const bool pathOk = QueryFullProcessImageNameW(c.pi.hProcess, 0, buffer, &size) != FALSE;
    const DWORD pathErr = GetLastError();
    check("an exited process cannot report its image path", !pathOk,
          "QueryFullProcessImageName err=" + std::to_string(pathErr));

    // And the creation time still can, which is what makes the answer decidable at all.
    FILETIME created{}, exited{}, k{}, u{};
    check("...but it can still report its creation time",
          GetProcessTimes(c.pi.hProcess, &created, &exited, &k, &u) != FALSE);

    check("a handle to a process that EXITED is Same, not Unknown",
          process_identity_check(c.pi.hProcess, t) == IdentityMatch::Same,
          nm(process_identity_check(c.pi.hProcess, t)));
    check("...which is the r3 regression guard, now against a genuinely unreadable path",
          process_identity_matches(c.pi.hProcess, t));

    // Identity is not aliveness, and the two must not be confused in either direction.
    check("identity answers Same for a process that is gone -- exit is a separate question",
          process_identity_check(c.pi.hProcess, t) == IdentityMatch::Same &&
              WaitForSingleObject(c.pi.hProcess, 0) == WAIT_OBJECT_0);

    // The counter-direction on the same exited handle: evidence against still works.
    ProcessTarget wrong = t;
    wrong.creationTime = t.creationTime + 1;
    check("...and a creation time that disagrees is still Different on it",
          process_identity_check(c.pi.hProcess, wrong) == IdentityMatch::Different,
          nm(process_identity_check(c.pi.hProcess, wrong)));

    ProcessTarget blank = t;
    blank.creationTime = 0;
    check("...and nothing recorded is still Unknown on it",
          process_identity_check(c.pi.hProcess, blank) == IdentityMatch::Unknown,
          nm(process_identity_check(c.pi.hProcess, blank)));
  }

  // --------------------------------------------------------- two real processes are told apart
  //
  // Not a uniqueness proof -- see the note at the top of this file; creation times are not
  // globally unique -- but the case the production code actually relies on: two live processes,
  // each captured for its own pid, do not answer for each other.
  {
    Child a, b;
    check("two children started", a.start(L"pairA") && b.start(L"pairB"));
    ProcessTarget ta, tb;
    check("both identities captured",
          capture_process_identity(a.pid(), &ta) && capture_process_identity(b.pid(), &tb));
    if (ta.creationTime == tb.creationTime) {
      // Possible in principle. If it ever happens, the case below would be checking nothing, so it
      // is reported rather than silently passed.
      skip("one child's handle does not answer for the other",
           "both children reported the same creation time, so this run cannot distinguish them");
    } else {
      check("one child's handle does not answer for the other",
            process_identity_check(a.pi.hProcess, tb) == IdentityMatch::Different &&
                process_identity_check(b.pi.hProcess, ta) == IdentityMatch::Different);
    }
    check("...and each answers for itself",
          process_identity_check(a.pi.hProcess, ta) == IdentityMatch::Same &&
              process_identity_check(b.pi.hProcess, tb) == IdentityMatch::Same);
  }

  // ---------------------------------------------------------- the move that carries a watch (E)
  //
  // TargetWatch owns a handle and four facts about a target. Its move assignment is hand-written,
  // and it has already been wrong once: it copied `gone` and `requestDelivered` and dropped
  // `requestFailed`. That flag decides whether the settle waits for a target at all, so dropping
  // it means a target that was asked and refused arrives looking as though it was never asked --
  // and the settle then waits for nobody.
  //
  // Nothing caught it, because in the production order no watch is ever moved AFTER that flag is
  // set: it was latent, not live. A latent hazard in a hand-written move is exactly the thing to
  // pin with a test rather than with an argument about call order, which is why TargetWatch now
  // sits at namespace scope.
  {
    Child c;
    check("a child for the watch cases started", c.start(L"watch"));
    ProcessTarget t;
    check("its identity is captured", capture_process_identity(c.pid(), &t));

    const auto open_one = [&]() {
      return OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, c.pid());
    };

    {
      TargetWatch from;
      from.target = t;
      from.handle = open_one();
      from.gone = true;
      from.requestDelivered = true;
      from.requestFailed = true;
      const void* moved = from.handle;
      check("a watch to move was set up", from.handle != nullptr);

      TargetWatch to;
      to = std::move(from);
      check("move assignment carries the handle", to.handle == moved);
      check("move assignment carries the target",
            to.target.pid == t.pid && to.target.creationTime == t.creationTime);
      check("move assignment carries `gone`", to.gone);
      check("move assignment carries `requestDelivered`", to.requestDelivered);
      // The counter-example for the flag that was dropped.
      check("move assignment carries `requestFailed` -- the one it used to drop", to.requestFailed);
      check("...and the source is left holding nothing, so the handle is closed once",
            from.handle == nullptr);
      check("...and the moved-to handle is still usable",
            WaitForSingleObject(static_cast<HANDLE>(to.handle), 0) == WAIT_TIMEOUT);
    }

    {
      // The move that actually happens in production: a vector growing. Deliberately no reserve,
      // so every push_back past the capacity moves the elements already in it.
      std::vector<TargetWatch> watches;
      for (int i = 0; i < 8; ++i) {
        TargetWatch w;
        w.target = t;
        w.handle = open_one();
        w.requestFailed = true;
        w.requestDelivered = (i % 2) == 0;
        w.gone = (i % 3) == 0;
        watches.push_back(std::move(w));
      }
      bool flagsSurvived = true;
      bool handlesUsable = true;
      for (size_t i = 0; i < watches.size(); ++i) {
        if (!watches[i].requestFailed) flagsSurvived = false;
        if (watches[i].requestDelivered != ((i % 2) == 0)) flagsSurvived = false;
        if (watches[i].gone != ((i % 3) == 0)) flagsSurvived = false;
        if (WaitForSingleObject(static_cast<HANDLE>(watches[i].handle), 0) != WAIT_TIMEOUT) {
          handlesUsable = false;
        }
      }
      check("eight watches through a growing vector keep every flag", flagsSurvived,
            "capacity " + std::to_string(watches.capacity()));
      check("...and every handle survives the reallocations intact", handlesUsable);
    }

    {
      // Self-move must not close the handle it is about to keep.
      TargetWatch w;
      w.target = t;
      w.handle = open_one();
      w.requestFailed = true;
      TargetWatch& alias = w;
      w = std::move(alias);
      check("a self-move keeps its handle open",
            w.handle != nullptr &&
                WaitForSingleObject(static_cast<HANDLE>(w.handle), 0) == WAIT_TIMEOUT);
      check("...and keeps its flags", w.requestFailed);
    }

    {
      // The destructor is what closes them. A hundred watches, opened and dropped.
      DWORD before = 0, after = 0;
      GetProcessHandleCount(GetCurrentProcess(), &before);
      for (int i = 0; i < 100; ++i) {
        TargetWatch w;
        w.target = t;
        w.handle = open_one();
      }
      GetProcessHandleCount(GetCurrentProcess(), &after);
      check("a hundred watches opened and dropped do not accumulate handles", after <= before + 5,
            std::to_string(before) + " -> " + std::to_string(after));
    }

    {
      // And a move must not leave TWO owners of one handle: the source is emptied, so exactly one
      // destructor closes it.
      DWORD before = 0, after = 0;
      GetProcessHandleCount(GetCurrentProcess(), &before);
      for (int i = 0; i < 100; ++i) {
        TargetWatch a;
        a.target = t;
        a.handle = open_one();
        TargetWatch b;
        b = std::move(a);
      }
      GetProcessHandleCount(GetCurrentProcess(), &after);
      check("a hundred moved watches leave exactly one owner each", after <= before + 5,
            std::to_string(before) + " -> " + std::to_string(after));
    }
  }

  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED") << "  (" << gChecks
            << " checks, " << gFailures << " failed, " << gSkips << " skipped)\n";
  return gFailures == 0 ? 0 : 1;
}
