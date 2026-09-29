// Build gate (CLAUDE.md, "테스트 빌드와 출하 빌드는 달라도 된다"): the clipboard-image test seam --
// received images written to REMOTE60_CLIP_IMAGE_TEST_SINK_DIR instead of the clipboard -- exists in
// the test build GNLinkStreamClipSink.exe and NOT in the shipped GNLinkStream.exe.
//
// Read from the binaries themselves, in both encodings the name could take (the seam reads it
// with GetEnvironmentVariableW, so it is a UTF-16 literal; ASCII is checked too). The positive
// control -- the test build DOES carry it -- is what makes an absence in the shipped one mean
// something: a search that could not find the string anywhere would pass the gate by accident.
//
//   remote60_clip_image_build_gate_test [dir]   (default: this executable's directory)
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
int g_failed = 0;
int g_checks = 0;
void check(const std::string& what, bool ok) {
  ++g_checks;
  if (!ok) ++g_failed;
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
}

std::vector<char> read_all(const std::wstring& path) {
  std::ifstream in(path, std::ios::binary);
  return std::vector<char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool contains(const std::vector<char>& hay, const std::string& needle) {
  return std::search(hay.begin(), hay.end(), needle.begin(), needle.end()) != hay.end();
}

std::string utf16(const std::string& ascii) {
  std::string out;
  for (char c : ascii) {
    out.push_back(c);
    out.push_back('\0');
  }
  return out;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::wstring dir;
  if (argc >= 2) {
    dir = argv[1];
  } else {
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    dir = self;
    dir = dir.substr(0, dir.find_last_of(L"\\/"));
  }
  const auto shipped = read_all(dir + L"\\GNLinkStream.exe");
  const auto test = read_all(dir + L"\\GNLinkStreamClipSink.exe");
  check("both binaries were read", !shipped.empty() && !test.empty());
  const std::string name = "REMOTE60_CLIP_IMAGE_TEST_SINK_DIR";
  const std::string banner = "TEST SINK publisher";
  check("positive control: the test build carries the seam's variable (UTF-16)", contains(test, utf16(name)));
  check("positive control: the test build carries its banner", contains(test, banner));
  check("the shipped host does not carry the seam's variable (UTF-16)", !contains(shipped, utf16(name)));
  check("the shipped host does not carry the seam's variable (ASCII)", !contains(shipped, name));
  check("the shipped host does not carry the seam's banner", !contains(shipped, banner));
  // File copy R->P (t-zdmsd4gb step 2): the folder source of the same test build.
  const std::string fileName = "REMOTE60_FILE_COPY_TEST_SOURCE_DIR";
  const std::string fileBanner = "TEST SOURCE copy of";
  check("positive control: the test build carries the file source's variable (UTF-16)", contains(test, utf16(fileName)));
  check("positive control: the test build carries the file source's banner", contains(test, fileBanner));
  check("the shipped host does not carry the file source's variable (UTF-16)", !contains(shipped, utf16(fileName)));
  check("the shipped host does not carry the file source's variable (ASCII)", !contains(shipped, fileName));
  check("the shipped host does not carry the file source's banner", !contains(shipped, fileBanner));
  check("the shipped host does carry the product's file-copy R->P code", contains(shipped, "host copy offered files="));
  // And the product feature itself IS in the shipped host (the gate must not pass because the whole
  // feature was left out).
  check("the shipped host does carry the product's clip-image log tag", contains(shipped, "[native-video-host][clip-image]"));
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", g_failed ? "FAILED" : "PASSED", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
