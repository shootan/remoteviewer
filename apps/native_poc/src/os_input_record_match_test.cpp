// The record matcher's own negative controls (r5, Codex T1): the evidence is "this session's this input", so a record
// of another session, a missing record, records out of order, a record under another input, an unexpected effect --
// each FAILS. Pure: it reads made-up host output; no host, no input API.

#include <cstdio>
#include <string>

#include "os_input_record_match.hpp"

namespace e = remote60::native_poc::e2e;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
}
std::string rec(const char* what, int seq, int epoch, int frag, const char* api) {
  return "10-28 x [os-input-record] ctx what=" + std::string(what) + " seq=" + std::to_string(seq) + " epoch=" +
         std::to_string(epoch) + " frag=" + std::to_string(frag) + " " + api + "\n";
}
std::string disp(int seq) { return "10-28 x [native-video-host][input] injected seq=" + std::to_string(seq) + " kind=5 key=9 mode=desktop\n"; }
std::string dispText(int seq) { return "10-28 x [native-video-host][input-text] injected seq=" + std::to_string(seq) + " utf16Count=2 mode=desktop\n"; }

// Tab down (seq 1), "Ab" (text seq 1): the IME preparation optional, the key and the two units required, in order
const std::vector<e::ExpectedInput> kWant = {
    {"input", {"api=SendInput n=1 key vk=9 down"}},
    {"text", {"unicode units=1 fnv=aaaaaaaa", "unicode units=1 fnv=bbbbbbbb"}},
};
std::string good(int epoch = 7) {
  return "10-28 x [os-input-record] ctx none api=SendInput n=2 mouse flags=0x1 dx=1 dy=0 data=0\n" +  // the wake: no input's
         rec("input", 1, epoch, 0, "api=AttachThreadInput from=1 to=2 attach") +
         rec("input", 1, epoch, 1, "api=ImmSetConversionStatus conv=0x0 sentence=0x0") +
         rec("input", 1, epoch, 2, "api=AttachThreadInput from=1 to=2 detach") +
         rec("input", 1, epoch, 3, "api=SendInput n=1 key vk=9 down") + disp(1) +
         rec("text", 1, epoch, 0, "api=SendInput n=2 unicode units=1 fnv=aaaaaaaa") +
         rec("text", 1, epoch, 1, "api=SendInput n=2 unicode units=1 fnv=bbbbbbbb") + dispText(1);
}
}  // namespace

int main() {
  std::string why;
  check("this session's inputs, in order (IME preparation allowed, the wake ignored): PASS",
        e::e2e_match_dispatches(good(), 0, 7, kWant, &why), why);
  why.clear();
  check("the same records under ANOTHER session's epoch: FAIL", !e::e2e_match_dispatches(good(9), 0, 7, kWant, &why), why);
  why.clear();
  {
    std::string g = good();
    const std::string drop = rec("text", 1, 7, 1, "api=SendInput n=2 unicode units=1 fnv=bbbbbbbb");
    g.erase(g.find(drop), drop.size());
    check("a missing unit (b): FAIL", !e::e2e_match_dispatches(g, 0, 7, kWant, &why), why);
  }
  why.clear();
  {
    std::string g = "10-28 x [os-input-record] ctx none api=x\n" + rec("input", 1, 7, 0, "api=SendInput n=1 key vk=9 down") +
                    disp(1) + rec("text", 1, 7, 0, "api=SendInput n=2 unicode units=1 fnv=bbbbbbbb") +
                    rec("text", 1, 7, 1, "api=SendInput n=2 unicode units=1 fnv=aaaaaaaa") + dispText(1);
    check("the units in the wrong order (b, A): FAIL", !e::e2e_match_dispatches(g, 0, 7, kWant, &why), why);
  }
  why.clear();
  {
    std::string g = rec("input", 2, 7, 0, "api=SendInput n=1 key vk=9 down") + disp(1) +
                    rec("text", 1, 7, 0, "api=SendInput n=2 unicode units=1 fnv=aaaaaaaa") +
                    rec("text", 1, 7, 1, "api=SendInput n=2 unicode units=1 fnv=bbbbbbbb") + dispText(1);
    check("the key recorded under another input (seq 2, dispatched seq 1): FAIL", !e::e2e_match_dispatches(g, 0, 7, kWant, &why), why);
  }
  why.clear();
  {
    std::string g = good();
    g += rec("input", 1, 7, 4, "api=SetCursorPos x=1 y=1");
    check("an unexpected effect in that input (SetCursorPos after the key): FAIL", !e::e2e_match_dispatches(g, 0, 7, kWant, &why), why);
  }
  why.clear();
  {
    std::string g = rec("input", 1, 7, 1, "api=SendInput n=1 key vk=9 down") + disp(1) +
                    rec("text", 1, 7, 0, "api=SendInput n=2 unicode units=1 fnv=aaaaaaaa") +
                    rec("text", 1, 7, 1, "api=SendInput n=2 unicode units=1 fnv=bbbbbbbb") + dispText(1);
    check("a fragment numbering gap (frag 1 with no 0): FAIL", !e::e2e_match_dispatches(g, 0, 7, kWant, &why), why);
  }
  why.clear();
  {
    std::string g = rec("input", 1, 7, 0, "api=SendInput n=1 key vk=9 down") +
                    rec("text", 1, 7, 0, "api=SendInput n=2 unicode units=1 fnv=aaaaaaaa") +
                    rec("text", 1, 7, 1, "api=SendInput n=2 unicode units=1 fnv=bbbbbbbb") + dispText(1);
    check("records with no dispatch line for the input: FAIL", !e::e2e_match_dispatches(g, 0, 7, kWant, &why), why);
  }
  why.clear();
  check("the wake's 'ctx none' record never stands in for an input",
        !e::e2e_match_dispatches("10-28 x [os-input-record] ctx none api=SendInput n=1 key vk=9 down\n" + disp(1), 0, 7,
                                 {{"input", {"api=SendInput n=1 key vk=9 down"}}}, &why),
        why);
  std::printf("\n%d checks, %d failures\n%s\n", gChecks, gFailures, gFailures == 0 ? "RESULT PASS" : "RESULT FAIL");
  return gFailures == 0 ? 0 : 1;
}
