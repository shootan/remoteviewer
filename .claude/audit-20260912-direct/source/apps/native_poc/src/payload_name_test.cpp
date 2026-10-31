// Payload names from a manifest, and every shape of them that must not become a path.
//
// This is the second lock. The first is the signature: a manifest that did not verify never gets
// here. But a name is data on its way to becoming a filesystem path used with administrator
// rights, and "the signature was checked" is not a reason to skip checking it -- the signing key
// could leak, the publishing side could be tricked into signing something odd, or the
// verification could have a bug of its own. None of those should end in a write outside the
// install directory.
//
// Names are refused, never sanitised. Turning a hostile name into a safe one is a much harder
// problem than deciding whether it is already safe, and the set of names the product actually
// ships is small and boring.

#include "payload_name.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace remote60::native_poc::update;

int gFailures = 0;
int gChecks = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::cout << (ok ? "PASS  " : "FAIL  ") << name;
  if (!detail.empty()) std::cout << "  " << detail;
  std::cout << "\n";
}

std::string narrow(const std::wstring& w) { return std::string(w.begin(), w.end()); }

void expect(const std::wstring& name, PayloadNameVerdict want) {
  const PayloadNameVerdict got = check_payload_name(name);
  check("\"" + narrow(name) + "\" -> " + payload_name_verdict_name(want), got == want,
        std::string("got ") + payload_name_verdict_name(got));
}

}  // namespace

int main() {
  // ---------------------------------------------------------------- what the product ships

  expect(L"GNLinkHost.exe", PayloadNameVerdict::Ok);
  expect(L"GNLinkSetup.exe", PayloadNameVerdict::Ok);
  expect(L"ui\\shell.html", PayloadNameVerdict::Ok);
  expect(L"ui\\macro.html", PayloadNameVerdict::Ok);
  expect(L"ui/shell.html", PayloadNameVerdict::Ok);  // forward slashes are folded, then re-checked
  expect(L"a\\b\\c\\d.txt", PayloadNameVerdict::Ok);

  // ---------------------------------------------------------------- traversal

  expect(L"..", PayloadNameVerdict::Traversal);
  expect(L"..\\evil.exe", PayloadNameVerdict::Traversal);
  expect(L"ui\\..\\..\\evil.exe", PayloadNameVerdict::Traversal);
  expect(L"a\\..\\b", PayloadNameVerdict::Traversal);
  // The one that would slip past a check done on only one separator form.
  expect(L"ui/../../evil.exe", PayloadNameVerdict::Traversal);
  expect(L"ui/..\\evil.exe", PayloadNameVerdict::Traversal);
  // "..." is not a traversal component, but it ends in a dot, which Windows strips.
  expect(L"...", PayloadNameVerdict::TrailingDotOrSpace);

  // ---------------------------------------------------------------- absolute and drive-relative

  expect(L"\\evil.exe", PayloadNameVerdict::Absolute);
  expect(L"/evil.exe", PayloadNameVerdict::Absolute);
  expect(L"C:\\Windows\\System32\\evil.dll", PayloadNameVerdict::Absolute);
  expect(L"C:/Windows/System32/evil.dll", PayloadNameVerdict::Absolute);
  expect(L"\\\\server\\share\\evil.exe", PayloadNameVerdict::Absolute);  // UNC
  // "C:file" is not absolute -- it resolves against that drive's own current directory, which is
  // a different surprise but no less of one.
  expect(L"C:evil.exe", PayloadNameVerdict::DriveRelative);

  // ---------------------------------------------------------------- NTFS oddities

  // An alternate data stream writes hidden content into an existing file.
  expect(L"GNLinkHost.exe:hidden", PayloadNameVerdict::AlternateStream);
  expect(L"ui\\shell.html:evil:$DATA", PayloadNameVerdict::AlternateStream);

  // Windows strips a trailing dot or space, so "a." and "a" are the same file -- two entries
  // could collide without looking like duplicates.
  expect(L"GNLinkHost.exe.", PayloadNameVerdict::TrailingDotOrSpace);
  expect(L"GNLinkHost.exe ", PayloadNameVerdict::TrailingDotOrSpace);
  expect(L" GNLinkHost.exe", PayloadNameVerdict::TrailingDotOrSpace);
  expect(L"ui.\\shell.html", PayloadNameVerdict::TrailingDotOrSpace);

  // Opening one of these does not create a file at all: a payload named NUL would make a swap
  // appear to succeed while writing nothing.
  expect(L"NUL", PayloadNameVerdict::ReservedDeviceName);
  expect(L"nul", PayloadNameVerdict::ReservedDeviceName);
  expect(L"CON.txt", PayloadNameVerdict::ReservedDeviceName);
  expect(L"COM1", PayloadNameVerdict::ReservedDeviceName);
  expect(L"lpt9.dat", PayloadNameVerdict::ReservedDeviceName);
  expect(L"ui\\NUL", PayloadNameVerdict::ReservedDeviceName);
  // Not reserved -- the list is exact, not a prefix match.
  expect(L"NULL.txt", PayloadNameVerdict::Ok);
  expect(L"COM10", PayloadNameVerdict::Ok);
  expect(L"console.html", PayloadNameVerdict::Ok);

  // ---------------------------------------------------------------- malformed

  expect(L"", PayloadNameVerdict::Empty);
  expect(L"a\\\\b", PayloadNameVerdict::EmptyComponent);
  expect(L"a\\", PayloadNameVerdict::EmptyComponent);
  expect(L"a//b", PayloadNameVerdict::EmptyComponent);
  expect(L"*.exe", PayloadNameVerdict::Wildcard);
  expect(L"file?.txt", PayloadNameVerdict::Wildcard);
  expect(L"a<b.txt", PayloadNameVerdict::ControlCharacter);
  expect(L"a|b.txt", PayloadNameVerdict::ControlCharacter);
  expect(std::wstring(L"bad\x01name.txt"), PayloadNameVerdict::ControlCharacter);
  expect(std::wstring(200, L'a'), PayloadNameVerdict::TooLong);

  // ---------------------------------------------------------------- lists

  {
    size_t bad = 999;
    PayloadNameVerdict verdict = PayloadNameVerdict::Ok;
    const std::vector<std::wstring> good = {L"GNLinkHost.exe", L"GNLinkStream.exe",
                                            L"ui\\shell.html"};
    check("a good list passes", check_payload_names(good, &bad, &verdict),
          payload_name_verdict_name(verdict));
  }
  {
    size_t bad = 0;
    PayloadNameVerdict verdict = PayloadNameVerdict::Ok;
    const std::vector<std::wstring> withTraversal = {L"GNLinkHost.exe", L"..\\evil.exe",
                                                     L"ui\\shell.html"};
    check("a list containing a traversal is refused",
          !check_payload_names(withTraversal, &bad, &verdict));
    check("and says which entry", bad == 1, std::to_string(bad));
    check("and why", verdict == PayloadNameVerdict::Traversal,
          payload_name_verdict_name(verdict));
  }
  {
    // Duplicates would have the second move-aside overwrite the first file's backup, so a
    // rollback would restore the wrong bytes.
    size_t bad = 0;
    PayloadNameVerdict verdict = PayloadNameVerdict::Ok;
    const std::vector<std::wstring> dupes = {L"GNLinkHost.exe", L"GNLinkStream.exe",
                                             L"gnlinkhost.EXE"};
    check("a case-different duplicate is refused", !check_payload_names(dupes, &bad, &verdict));
    check("reported as a duplicate", verdict == PayloadNameVerdict::Duplicate,
          payload_name_verdict_name(verdict));
    check("at the second occurrence", bad == 2, std::to_string(bad));
  }
  {
    // Same file, different separator: still the same file.
    size_t bad = 0;
    PayloadNameVerdict verdict = PayloadNameVerdict::Ok;
    const std::vector<std::wstring> dupes = {L"ui\\shell.html", L"ui/shell.html"};
    check("a separator-different duplicate is refused",
          !check_payload_names(dupes, &bad, &verdict));
    check("reported as a duplicate", verdict == PayloadNameVerdict::Duplicate,
          payload_name_verdict_name(verdict));
  }
  {
    size_t bad = 999;
    PayloadNameVerdict verdict = PayloadNameVerdict::Ok;
    check("an empty list passes this check (emptiness is the config's own rule)",
          check_payload_names({}, &bad, &verdict));
  }

  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED")
            << "  (" << gChecks << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}
