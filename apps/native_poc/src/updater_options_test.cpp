// What the updater will and will not accept as instructions.
//
// This is the command line of a process that runs as administrator and replaces files in
// %ProgramFiles%. Most of these cases are about refusing things, and the refusals are the point:
// an updater that guessed at a missing install directory, or carried on past an argument it did
// not understand, would do something the caller did not ask for with privileges the caller has.

#include "updater_options.hpp"

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

std::string narrow(const std::wstring& s) { return std::string(s.begin(), s.end()); }

/** A complete, valid command line. Cases below remove or replace one thing at a time. */
std::vector<std::wstring> complete() {
  return {
      L"--install-dir",       L"C:\\Program Files\\GNLink",
      L"--staging-dir",       L"C:\\ProgramData\\GNLink\\staging",
      // The real location: a sibling of the install directory under %ProgramFiles%, where
      // the default ACL is already administrators-only. %TEMP% and %ProgramData% are
      // user-writable and were excluded by design (history #427).
      L"--work-dir",          L"C:\\Program Files\\GNLink.update",
      L"--manifest-url",      L"https://updates.example/api/update/manifest",
      L"--platform",          L"windows",
      L"--installed-version", L"0.2.104",
      L"--health-log",        L"C:\\Users\\u\\AppData\\Local\\GNLink\\host_app.log",
      L"--log",               L"C:\\ProgramData\\GNLink\\updater.log",
      L"--service-name",      L"GNLinkSecureInput",
      L"--registry-root",     L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\GNLink",
  };
}

/** The same, with one flag's value replaced. */
std::vector<std::wstring> with(const std::wstring& flag, const std::wstring& value) {
  std::vector<std::wstring> args = complete();
  for (size_t i = 0; i + 1 < args.size(); ++i) {
    if (args[i] == flag) {
      args[i + 1] = value;
      return args;
    }
  }
  args.push_back(flag);
  args.push_back(value);
  return args;
}

/** The same, with one flag and its value removed. */
std::vector<std::wstring> without(const std::wstring& flag) {
  std::vector<std::wstring> args = complete();
  for (size_t i = 0; i + 1 < args.size(); ++i) {
    if (args[i] == flag) {
      args.erase(args.begin() + static_cast<long>(i), args.begin() + static_cast<long>(i) + 2);
      return args;
    }
  }
  return args;
}

bool accepts(const std::vector<std::wstring>& args, std::string* detail) {
  const ParseResult parsed = parse_updater_options(args);
  if (!parsed.ok()) {
    if (detail) *detail = std::string(parse_status_name(parsed.status)) + ": " + parsed.detail;
    return false;
  }
  return parsed.options.validate(detail);
}

}  // namespace

int main() {
  // ---------------------------------------------------------------- the happy line

  {
    std::string why;
    check("a complete command line is accepted", accepts(complete(), &why), why);

    const ParseResult parsed = parse_updater_options(complete());
    check("and every value arrives where it belongs",
          parsed.options.installDir == L"C:\\Program Files\\GNLink" &&
              parsed.options.stagingDir == L"C:\\ProgramData\\GNLink\\staging" &&
              parsed.options.workDir == L"C:\\Program Files\\GNLink.update" &&
              parsed.options.manifestUrl == "https://updates.example/api/update/manifest" &&
              parsed.options.platform == "windows" &&
              parsed.options.installedVersion == "0.2.104" &&
              parsed.options.serviceName == L"GNLinkSecureInput");
    check("the optional handoff is absent by default",
          parsed.options.parentPid == 0 && parsed.options.readyEventName.empty());
    check("and it does not think it is the copy", !parsed.options.runningFromCopy);
  }

  // --------------------------------------------------- the credential arrives by name, not value
  //
  // What travels in the argument list is the NAME of a pipe. The credential itself never appears
  // in an argument, an environment block, a file or a log -- which is the whole reason there is a
  // pipe rather than one more flag.
  {
    std::vector<std::wstring> args = complete();
    args.push_back(L"--credential-pipe");
    args.push_back(L"\\\\.\\pipe\\GNLinkUpdateCred-123-456");
    args.push_back(L"--manifest-envelope");
    const ParseResult parsed = parse_updater_options(args);
    check("a credential pipe name is accepted", parsed.ok(), parsed.detail);
    check("...and kept as given",
          parsed.options.credentialPipeName == L"\\\\.\\pipe\\GNLinkUpdateCred-123-456");
    check("...alongside the wire shape", parsed.options.derivedEndpoint);

    const ParseResult without = parse_updater_options(complete());
    check("without it there is no channel", without.options.credentialPipeName.empty());
    check("...and the shape is the detached one", !without.options.derivedEndpoint);
  }

  // ------------------------------------------------------- the updater still refuses cleartext
  //
  // The directory client learned to speak https in the same change that gave both of them one
  // WinHTTP transport. Sharing a transport is not sharing a policy: the directory accepts http
  // because existing deployments are reached that way, and the updater must not have picked that
  // up. What it fetches runs with administrator rights, so a cleartext hop is a place to swap it,
  // and the signature check behind this is the second lock, not the only one.
  {
    std::string why;
    std::vector<std::wstring> args = complete();
    for (auto& arg : args) {
      if (arg.rfind(L"https://", 0) == 0) arg = L"http://updates.example/api/update/manifest";
    }
    check("an http manifest url is refused at the entry point", !accepts(args, &why), why);
    check("...and the reason says why", why.find("https") != std::string::npos, why);
  }

  // ---------------------------------------------------------------- nothing is defaulted

  {
    // The core rule. Every one of these has an obvious "sensible" value, and filling any of them
    // in is how an updater ends up replacing something nobody named.
    const std::wstring required[] = {
        L"--install-dir",  L"--staging-dir",       L"--work-dir",    L"--manifest-url",
        L"--platform",     L"--installed-version", L"--health-log",  L"--log",
        L"--service-name", L"--registry-root",
    };
    for (const std::wstring& flag : required) {
      std::string why;
      check("without " + narrow(flag) + " it refuses to run", !accepts(without(flag), &why), why);
      check("and the reason names " + narrow(flag), why.find(narrow(flag)) != std::string::npos,
            why);
    }
  }

  {
    std::string why;
    check("an empty command line refuses", !accepts({}, &why), why);
  }

  // ---------------------------------------------------------------- arguments it does not know

  {
    std::vector<std::wstring> args = complete();
    args.push_back(L"--force");
    const ParseResult parsed = parse_updater_options(args);
    // Not ignored. The caller asked for something this build does not understand, and doing most
    // of what they asked with administrator rights is worse than doing none of it.
    check("an unknown argument is an error, not something skipped",
          parsed.status == ParseStatus::UnknownArgument, parse_status_name(parsed.status));
    check("and it is named", parsed.detail.find("--force") != std::string::npos, parsed.detail);
  }

  {
    // A misspelling of a real flag is the realistic version of the same thing.
    std::vector<std::wstring> args = complete();
    args.push_back(L"--instal-dir");
    args.push_back(L"C:\\somewhere");
    const ParseResult parsed = parse_updater_options(args);
    check("a misspelled flag is refused rather than dropped",
          parsed.status == ParseStatus::UnknownArgument, parse_status_name(parsed.status));
  }

  {
    const ParseResult parsed = parse_updater_options({L"--install-dir"});
    check("a flag with no value is an error", parsed.status == ParseStatus::MissingValue,
          parse_status_name(parsed.status));
    check("and it says which flag", parsed.detail.find("--install-dir") != std::string::npos,
          parsed.detail);
  }

  // ---------------------------------------------------------------- values that look right

  {
    std::string why;
    // A relative path handed to a process that replaces files in Program Files resolves against
    // whatever directory it happened to be started in.
    check("a relative install directory is refused",
          !accepts(with(L"--install-dir", L"GNLink"), &why), why);
    check("and so is a relative staging directory",
          !accepts(with(L"--staging-dir", L"..\\staging"), &why), why);
    check("and a relative work directory",
          !accepts(with(L"--work-dir", L"work"), &why), why);
    check("a UNC path is absolute enough",
          accepts(with(L"--install-dir", L"\\\\server\\share\\GNLink"), &why), why);
  }

  {
    std::string why;
    // Enforced here as well as where the manifest is read, so a caller is told at the entry point
    // rather than having it refused three layers down.
    check("an http manifest url is refused",
          !accepts(with(L"--manifest-url", L"http://updates.example/m"), &why), why);
    check("and the reason says https", why.find("https") != std::string::npos, why);
    check("a file url is refused too",
          !accepts(with(L"--manifest-url", L"file:///C:/m.txt"), &why), why);
  }

  {
    std::string why;
    // The same rules UpdateEffectsConfig enforces, checked before anything is fetched rather
    // than after.
    check("staging inside the install directory is refused",
          !accepts(with(L"--staging-dir", L"C:\\Program Files\\GNLink\\staging"), &why), why);
    check("the working copy inside the install directory is refused",
          !accepts(with(L"--work-dir", L"C:\\Program Files\\GNLink\\work"), &why), why);
    // A directory that merely starts with the same characters is not inside it.
    check("a sibling with a similar name is not 'inside'",
          accepts(with(L"--staging-dir", L"C:\\Program Files\\GNLinkStaging"), &why), why);
    // The real one. %ProgramFiles%\\GNLink.update is where the working copy goes: a sibling of the
    // install directory, admin-only by the default ACL, and NOT inside the directory being
    // replaced -- the character after "GNLink" is a dot, not a separator. If this were rejected
    // the updater would have nowhere safe to run from.
    check("the intended working directory is accepted",
          accepts(with(L"--work-dir", L"C:\\Program Files\\GNLink.update"), &why), why);
  }

  {
    const ParseResult parsed = parse_updater_options(with(L"--parent-pid", L"12x4"));
    check("a parent pid that is not a number is refused",
          parsed.status == ParseStatus::BadValue, parse_status_name(parsed.status));
    const ParseResult big = parse_updater_options(with(L"--parent-pid", L"99999999999"));
    check("and one that does not fit is refused", big.status == ParseStatus::BadValue,
          parse_status_name(big.status));
    const ParseResult good = parse_updater_options(with(L"--parent-pid", L"5156"));
    check("a real one is read", good.ok() && good.options.parentPid == 5156,
          std::to_string(good.options.parentPid));
  }

  {
    std::vector<std::wstring> args = complete();
    args.push_back(L"--running-from-copy");
    const ParseResult parsed = parse_updater_options(args);
    check("the re-executed copy says so", parsed.ok() && parsed.options.runningFromCopy);
  }

  // ---------------------------------------------------------------- where the copy goes

  {
    check("the copy keeps its own name",
          updater_copy_path(L"C:\\ProgramData\\GNLink\\work",
                            L"C:\\Program Files\\GNLink\\GNLinkUpdater.exe") ==
              L"C:\\ProgramData\\GNLink\\work\\GNLinkUpdater.exe");
    check("a trailing separator does not double up",
          updater_copy_path(L"C:\\work\\", L"C:\\x\\GNLinkUpdater.exe") ==
              L"C:\\work\\GNLinkUpdater.exe");
    // Deterministic, so a later run can find and remove the previous copy instead of leaving one
    // directory per attempt in a place only administrators can clean.
    check("the same inputs give the same path",
          updater_copy_path(L"C:\\work", L"C:\\a\\U.exe") ==
              updater_copy_path(L"C:\\work", L"C:\\b\\U.exe"));
  }

  std::cout << (gFailures == 0 ? "RESULT: ALL PASS  (" : "RESULT: FAILED  (") << gChecks
            << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}
