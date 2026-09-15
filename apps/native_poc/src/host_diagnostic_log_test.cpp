#include "host_diagnostic_log.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <memory>
#include <vector>

int wmain(int argc, wchar_t** argv) {
  if (argc != 2) return 2;  // Explicit isolated directory; never touch the live app's diagnostics.
  const std::filesystem::path dir(argv[1]);
  std::filesystem::create_directories(dir);
  const auto base = dir / L"mirror-test";
  using remote60::native_poc::HostDiagnosticLog;
  if (HostDiagnosticLog::Accept("[native-video-host] directory token=secret\n")) return 1;
  {
    HostDiagnosticLog sink(base.wstring());
    sink.Write("stamp ", "[native-video-host] directory token=secret\n");
    sink.Write("stamp ", "[native-video-host] wire seq=17 bytes=42\n");
  }
  std::ifstream file(base.wstring() + L".0.log", std::ios::binary);
  std::ostringstream content; content << file.rdbuf(); file.close();
  if (content.str().find("seq=17") == std::string::npos ||
      content.str().find("secret") != std::string::npos) return 1;
  {
    HostDiagnosticLog sink((dir / L"bounded-test").wstring());
    const std::string line = "[native-video-host] wire seq=1 " + std::string(4000, '1') + "\n";
    for (int i = 0; i < 5000; ++i) sink.Write("stamp ", line);
  }
  for (unsigned i = 0; i < 2; ++i) {
    const auto path = dir / (L"bounded-test." + std::to_wstring(i) + L".log");
    if (!std::filesystem::exists(path) || std::filesystem::file_size(path) > 8ULL * 1024 * 1024)
      return 1;
  }
  const auto banks = dir / L"banks";
  std::filesystem::create_directories(banks);
  {
    std::vector<std::unique_ptr<HostDiagnosticLog>> active;
    for (int i = 0; i < 5; ++i) {
      active.emplace_back(HostDiagnosticLog::Acquire(banks.wstring()));
      active.back()->Write("stamp ", "[native-video-host] wire seq=19\n");
    }
    // Four occupied banks; the fifth must not truncate or allocate another bank.
    size_t logs = 0;
    for (const auto& e : std::filesystem::directory_iterator(banks))
      if (e.path().extension() == L".log") ++logs;
    if (logs != 4) return 1;
  }
  for (int run = 0; run < 30; ++run) {
    std::unique_ptr<HostDiagnosticLog> sink(HostDiagnosticLog::Acquire(banks.wstring()));
    sink->Write("stamp ", "[native-video-host] wire seq=20\n");
  }
  size_t files = 0;
  for (const auto& e : std::filesystem::directory_iterator(banks)) { (void)e; ++files; }
  if (files > 12) return 1;  // 4 locks + at most 8 log segments, independent of run count.
  std::cout << "PASS: numeric sink, secrets excluded, segment cap, concurrent banks, restart bound\n";
  return 0;
}
