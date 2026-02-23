#include <windows.h>

#include <DbgHelp.h>
#include <csignal>
#include <crtdbg.h>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "host_non_runtime_paths.hpp"
#include "runtime_server.hpp"

#pragma comment(lib, "Dbghelp.lib")

namespace {

std::once_flag gSymInitOnce;
bool gSymReady = false;

std::string now_local_string() {
  const std::time_t tt = std::time(nullptr);
  std::tm tm{};
  localtime_s(&tm, &tt);
  char buf[32]{};
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
  return std::string(buf);
}

void append_fatal_log(const std::string& message) {
  try {
    std::filesystem::create_directories("logs");
    std::ofstream ofs("logs/host_fatal.log", std::ios::app);
    ofs << "[" << now_local_string() << "][host][fatal] " << message << "\n";
    ofs.flush();
  } catch (...) {
  }
  try {
    std::cerr << "[host][fatal] " << message << std::endl;
  } catch (...) {
  }
}

std::string narrow_wide(const wchar_t* w) {
  if (!w) return "";
  const int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  if (len <= 1) return "";
  std::string out(static_cast<size_t>(len), '\0');
  if (WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), len, nullptr, nullptr) <= 0) {
    return "";
  }
  out.resize(static_cast<size_t>(len - 1));
  return out;
}

std::string to_hex(uint64_t value) {
  std::ostringstream oss;
  oss << "0x" << std::hex << std::uppercase << value;
  return oss.str();
}

void ensure_symbolizer() {
  std::call_once(gSymInitOnce, []() {
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
    gSymReady = (SymInitialize(process, nullptr, TRUE) == TRUE);
    append_fatal_log(std::string("symbolizer_init ready=") + (gSymReady ? "1" : "0"));
  });
}

void append_stack_trace(const char* tag) {
  ensure_symbolizer();
  void* frames[64]{};
  const USHORT n = CaptureStackBackTrace(0, static_cast<DWORD>(std::size(frames)), frames, nullptr);
  std::ostringstream oss;
  oss << "stacktrace tag=" << (tag ? tag : "unknown") << " frames=" << n << " addrs=";
  for (USHORT i = 0; i < n; ++i) {
    if (i != 0) oss << ",";
    oss << to_hex(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(frames[i])));
  }
  append_fatal_log(oss.str());

  if (!gSymReady) return;

  HANDLE process = GetCurrentProcess();
  for (USHORT i = 0; i < n; ++i) {
    const DWORD64 addr = static_cast<DWORD64>(reinterpret_cast<uintptr_t>(frames[i]));
    char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
    auto* sym = reinterpret_cast<SYMBOL_INFO*>(buffer);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = MAX_SYM_NAME;
    DWORD64 disp = 0;
    std::ostringstream line;
    line << "stackframe[" << i << "] addr=" << to_hex(addr);
    if (SymFromAddr(process, addr, &disp, sym) == TRUE) {
      line << " symbol=" << sym->Name << "+0x" << std::hex << std::uppercase << disp << std::dec;
    }
    IMAGEHLP_LINE64 src{};
    src.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
    DWORD srcDisp = 0;
    if (SymGetLineFromAddr64(process, addr, &srcDisp, &src) == TRUE) {
      line << " src=" << src.FileName << ":" << src.LineNumber;
    }
    append_fatal_log(line.str());
  }
}

LONG WINAPI host_unhandled_exception_filter(EXCEPTION_POINTERS* ex) {
  const auto code = (ex && ex->ExceptionRecord) ? ex->ExceptionRecord->ExceptionCode : 0;
  const auto addr = (ex && ex->ExceptionRecord) ? ex->ExceptionRecord->ExceptionAddress : nullptr;
  append_fatal_log("unhandled_exception code=0x" + [&]() {
    char buf[32]{};
    std::snprintf(buf, sizeof(buf), "%08lX", static_cast<unsigned long>(code));
    return std::string(buf);
  }() + " address=" + [&]() {
    char buf[32]{};
    std::snprintf(buf, sizeof(buf), "%p", addr);
    return std::string(buf);
  }());
  append_stack_trace("unhandled_exception");
  return EXCEPTION_EXECUTE_HANDLER;
}

void on_signal_abort(int signum) {
  append_fatal_log("signal_abort signum=" + std::to_string(signum));
  append_stack_trace("signal_abort");
  std::_Exit(128 + signum);
}

void on_terminate() {
  append_fatal_log("std_terminate called");
  append_stack_trace("std_terminate");
  std::_Exit(99);
}

void on_purecall() {
  append_fatal_log("purecall invoked");
  append_stack_trace("purecall");
  std::_Exit(98);
}

void __cdecl on_invalid_parameter(const wchar_t* expression,
                                  const wchar_t* function,
                                  const wchar_t* file,
                                  unsigned int line,
                                  uintptr_t /*reserved*/) {
  append_fatal_log("invalid_parameter"
                   " expression=" + narrow_wide(expression) +
                   " function=" + narrow_wide(function) +
                   " file=" + narrow_wide(file) +
                   " line=" + std::to_string(line));
  append_stack_trace("invalid_parameter");
}

int __cdecl on_crt_report(int reportType, char* message, int* /*returnValue*/) {
  std::string msg = message ? message : "";
  if (msg.size() > 1200) msg.resize(1200);
  append_fatal_log("crt_report type=" + std::to_string(reportType) + " msg=" + msg);
  return FALSE;
}

int __cdecl on_crt_report_w(int reportType, wchar_t* message, int* /*returnValue*/) {
  std::string msg = narrow_wide(message);
  if (msg.size() > 1200) msg.resize(1200);
  append_fatal_log("crt_report_w type=" + std::to_string(reportType) + " msg=" + msg);
  return FALSE;
}

void install_fatal_handlers() {
  ensure_symbolizer();
  SetUnhandledExceptionFilter(host_unhandled_exception_filter);
  std::signal(SIGABRT, on_signal_abort);
  std::set_terminate(on_terminate);
  _set_purecall_handler(on_purecall);
  _set_invalid_parameter_handler(on_invalid_parameter);
  _set_abort_behavior(0, _WRITE_ABORT_MSG);
#if defined(_MSC_VER)
  _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
  _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
  _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
  _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
  _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, on_crt_report);
  _CrtSetReportHookW2(_CRT_RPTHOOK_INSTALL, on_crt_report_w);
#endif
  append_fatal_log("fatal_handlers_installed pid=" + std::to_string(GetCurrentProcessId()));
}

}  // namespace

int main(int argc, char** argv) {
  install_fatal_handlers();

  // Default behavior: no-arg launch runs persistent runtime server mode.
  if (argc <= 1) {
    return remote60::host::run_runtime_server_forever();
  }

  if (argc > 1 && std::string(argv[1]) == "--runtime-stream") {
    int seconds = 10;
    if (argc > 2) {
      const int parsed = std::atoi(argv[2]);
      // 0 means server-style run (no auto-timeout, wait/stream continuously)
      seconds = (parsed < 0) ? 0 : parsed;
    }
    return remote60::host::run_runtime_stream_once(seconds);
  }

  if (argc > 1 && std::string(argv[1]) == "--runtime-server") {
    return remote60::host::run_runtime_server_forever();
  }

  if (const auto rc = remote60::host::run_non_runtime_path(argc, argv); rc.has_value()) {
    return *rc;
  }

  std::cerr << "[host] unhandled mode\n";
  return 1;
}
