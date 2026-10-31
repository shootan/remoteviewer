#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

// Runs a program under a minimal debugger and reports what actually killed it.
//
// TEST SUPPORT. Not part of the product, not in any payload.
//
// It exists because 0xC0000409 is not one thing. It is STATUS_STACK_BUFFER_OVERRUN by name, and
// that name is misleading: __fastfail raises it for a whole family of reasons and the FIRST
// EXCEPTION PARAMETER is the one that says which. A stack cookie failure (2) and an ordinary
// abort() (7) arrive as the same status code, and guessing between them sends an investigation in
// opposite directions -- one is memory corruption, the other is a thrown exception nobody caught.
//
// __fastfail deliberately bypasses SEH and vectored handlers, so nothing inside the process can
// observe it. A debugger can: it sees the exception before termination, with the parameters
// intact, and can walk the stack of the thread that raised it.
//
// Usage: crash_probe <exe> [args...]
// Exit:  the child's exit code, or 90+ when the probe itself could not run.

#include <windows.h>
#include <dbghelp.h>

#include <cstdio>
#include <stdexcept>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#pragma comment(lib, "dbghelp.lib")

namespace {

/** The documented FAST_FAIL_* codes that matter here. Others are printed as numbers. */
const char* fast_fail_name(ULONG_PTR code) {
  switch (code) {
    case 0: return "LEGACY_GS_VIOLATION";
    case 1: return "VTGUARD_CHECK_FAILURE";
    case 2: return "STACK_COOKIE_CHECK_FAILURE";
    case 3: return "CORRUPT_LIST_ENTRY";
    case 4: return "INCORRECT_STACK";
    case 5: return "INVALID_ARG";
    case 6: return "GS_COOKIE_INIT";
    case 7: return "FATAL_APP_EXIT";  // abort(), std::terminate() -> abort()
    case 8: return "RANGE_CHECK_FAILURE";
    case 9: return "UNSAFE_REGISTRY_ACCESS";
    case 18: return "INVALID_FAST_FAIL_CODE";
    default: return "(not one of the common codes)";
  }
}

std::string module_of(HANDLE process, DWORD64 address, DWORD64* offsetOut) {
  const DWORD64 base = SymGetModuleBase64(process, address);
  if (offsetOut) *offsetOut = base ? address - base : 0;
  if (!base) return "(unknown module)";
  IMAGEHLP_MODULE64 info{};
  info.SizeOfStruct = sizeof(info);
  if (!SymGetModuleInfo64(process, base, &info)) return "(unnamed module)";
  return info.ModuleName;
}

void print_frame(HANDLE process, int index, DWORD64 address) {
  DWORD64 offset = 0;
  const std::string module = module_of(process, address, &offset);

  unsigned char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(char)]{};
  auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
  symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
  symbol->MaxNameLen = MAX_SYM_NAME;
  DWORD64 displacement = 0;
  const bool named = SymFromAddr(process, address, &displacement, symbol) != FALSE;

  std::printf("  #%-2d 0x%016llx  %s+0x%llx", index, static_cast<unsigned long long>(address),
              module.c_str(), static_cast<unsigned long long>(offset));
  if (named) {
    std::printf("  %s+0x%llx", symbol->Name, static_cast<unsigned long long>(displacement));
    IMAGEHLP_LINE64 line{};
    line.SizeOfStruct = sizeof(line);
    DWORD lineDisplacement = 0;
    if (SymGetLineFromAddr64(process, address, &lineDisplacement, &line)) {
      std::printf("  [%s:%lu]", line.FileName, line.LineNumber);
    }
  }
  std::printf("\n");
}

void walk_stack(HANDLE process, HANDLE thread) {
  CONTEXT context{};
  context.ContextFlags = CONTEXT_FULL;
  if (!GetThreadContext(thread, &context)) {
    std::printf("  (the thread context could not be read: %lu)\n", GetLastError());
    return;
  }
  STACKFRAME64 frame{};
  frame.AddrPC.Offset = context.Rip;
  frame.AddrPC.Mode = AddrModeFlat;
  frame.AddrFrame.Offset = context.Rbp;
  frame.AddrFrame.Mode = AddrModeFlat;
  frame.AddrStack.Offset = context.Rsp;
  frame.AddrStack.Mode = AddrModeFlat;

  for (int i = 0; i < 48; ++i) {
    if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, nullptr,
                     SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
      break;
    }
    if (frame.AddrPC.Offset == 0) break;
    print_frame(process, i, frame.AddrPC.Offset);
  }
}

}  // namespace

int main(int argc, char** argv) {
  // A known-answer mode, so the probe's reading of an unhandled C++ exception can be checked
  // against a case whose cause is not in doubt. Run as: crash_probe <this exe> --selftest-uncaught
  if (argc >= 2 && std::string(argv[1]) == "--selftest-uncaught") {
    std::printf("selftest: throwing std::length_error, nothing catches it\n");
    std::fflush(stdout);
    throw std::length_error("string too long");
  }
  if (argc < 2) {
    std::printf("usage: crash_probe <exe> [args...]\n");
    return 90;
  }

  std::string command;
  for (int i = 1; i < argc; ++i) {
    if (i > 1) command += " ";
    const std::string part(argv[i]);
    command += (part.find(' ') != std::string::npos) ? ("\"" + part + "\"") : part;
  }
  std::vector<char> mutableCommand(command.begin(), command.end());
  mutableCommand.push_back('\0');

  STARTUPINFOA si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  // DEBUG_ONLY_THIS_PROCESS: the program under test starts fixture children of its own, and those
  // are not what is being investigated. Debugging them too would serialise the very timing the
  // failure depends on.
  if (!CreateProcessA(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE,
                      DEBUG_ONLY_THIS_PROCESS, nullptr, nullptr, &si, &pi)) {
    std::printf("crash_probe: could not start the child: %lu\n", GetLastError());
    return 91;
  }

  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);

  HANDLE childProcess = nullptr;
  bool symbolsReady = false;
  std::map<DWORD, HANDLE> threads;
  DWORD childExit = 0;
  int exceptions = 0;

  for (bool running = true; running;) {
    DEBUG_EVENT ev{};
    if (!WaitForDebugEvent(&ev, 600000)) {
      std::printf("crash_probe: no debug event for ten minutes; giving up\n");
      TerminateProcess(pi.hProcess, 92);
      return 92;
    }
    DWORD continueStatus = DBG_EXCEPTION_NOT_HANDLED;

    switch (ev.dwDebugEventCode) {
      case CREATE_PROCESS_DEBUG_EVENT: {
        childProcess = ev.u.CreateProcessInfo.hProcess;
        threads[ev.dwThreadId] = ev.u.CreateProcessInfo.hThread;
        if (ev.u.CreateProcessInfo.hFile) CloseHandle(ev.u.CreateProcessInfo.hFile);
        break;
      }
      case LOAD_DLL_DEBUG_EVENT: {
        if (ev.u.LoadDll.hFile) CloseHandle(ev.u.LoadDll.hFile);
        break;
      }
      case CREATE_THREAD_DEBUG_EVENT:
        threads[ev.dwThreadId] = ev.u.CreateThread.hThread;
        break;
      case EXIT_THREAD_DEBUG_EVENT:
        threads.erase(ev.dwThreadId);
        break;
      case EXCEPTION_DEBUG_EVENT: {
        const EXCEPTION_RECORD& record = ev.u.Exception.ExceptionRecord;
        // The loader fires one of these at startup by design; it is not a failure.
        if (record.ExceptionCode == EXCEPTION_BREAKPOINT && exceptions == 0) {
          ++exceptions;
          continueStatus = DBG_CONTINUE;
          break;
        }
        ++exceptions;
        std::printf("\n==== exception in the child ====\n");
        std::printf("code           : 0x%08lX%s\n", record.ExceptionCode,
                    record.ExceptionCode == 0xC0000409 ? "  (STATUS_STACK_BUFFER_OVERRUN by name)"
                                                       : "");
        std::printf("chance         : %s\n", ev.u.Exception.dwFirstChance ? "first" : "second");
        std::printf("flags          : 0x%08lX%s\n", record.ExceptionFlags,
                    (record.ExceptionFlags & EXCEPTION_NONCONTINUABLE) ? " (noncontinuable)" : "");
        std::printf("address        : 0x%016llx\n",
                    reinterpret_cast<unsigned long long>(record.ExceptionAddress));
        std::printf("parameters (%lu):\n", record.NumberParameters);
        for (DWORD i = 0; i < record.NumberParameters && i < EXCEPTION_MAXIMUM_PARAMETERS; ++i) {
          std::printf("  [%lu] 0x%llx", i,
                      static_cast<unsigned long long>(record.ExceptionInformation[i]));
          if (i == 0 && record.ExceptionCode == 0xC0000409) {
            // THE answer to "which kind of 0xC0000409 is this".
            std::printf("  = FAST_FAIL_%s", fast_fail_name(record.ExceptionInformation[i]));
          }
          if (record.ExceptionCode == 0xE06D7363) {
            // The MSVC C++ exception. [0] is the EH magic, [1] the exception object, [2] its
            // ThrowInfo and [3] the module the throw was compiled into.
            if (i == 0) std::printf("  = the MSVC C++ throw magic");
            if (i == 1) std::printf("  = the thrown object");
            if (i == 2) std::printf("  = its ThrowInfo");
            if (i == 3) std::printf("  = the module the throw is in");
          }
          std::printf("\n");
        }
        // Initialised here, not at start-up: by now every module the child uses is mapped, so
        // an invading SymInitialize picks them all up and the search path takes the PDB sitting
        // next to the binary.
        if (childProcess && !symbolsReady) {
          wchar_t image[MAX_PATH * 2]{};
          DWORD imageSize = static_cast<DWORD>(std::size(image));
          std::string searchPath;
          if (QueryFullProcessImageNameW(childProcess, 0, image, &imageSize)) {
            std::wstring full(image, imageSize);
            const size_t cut = full.find_last_of(L'\\');
            if (cut != std::wstring::npos) {
              const std::wstring dir = full.substr(0, cut);
              searchPath.assign(dir.begin(), dir.end());
            }
          }
          symbolsReady =
              SymInitialize(childProcess, searchPath.empty() ? nullptr : searchPath.c_str(),
                            TRUE) != FALSE;
          if (symbolsReady) SymRefreshModuleList(childProcess);
          std::printf("symbols        : %s (search path %s)\n",
                      symbolsReady ? "loaded" : "NOT loaded",
                      searchPath.empty() ? "(default)" : searchPath.c_str());
        }
        if (childProcess && threads.count(ev.dwThreadId)) {
          std::printf("stack of thread %lu:\n", ev.dwThreadId);
          walk_stack(childProcess, threads[ev.dwThreadId]);
        }
        std::printf("================================\n\n");
        break;
      }
      case OUTPUT_DEBUG_STRING_EVENT:
        continueStatus = DBG_CONTINUE;
        break;
      case EXIT_PROCESS_DEBUG_EVENT:
        childExit = ev.u.ExitProcess.dwExitCode;
        running = false;
        break;
      default:
        break;
    }
    ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, continueStatus);
  }

  if (childProcess) SymCleanup(childProcess);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  std::printf("crash_probe: child exited with 0x%08lX (%lu)\n", childExit, childExit);
  return static_cast<int>(childExit);
}
