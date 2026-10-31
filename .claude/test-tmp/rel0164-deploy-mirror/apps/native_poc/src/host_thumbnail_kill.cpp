#include "host_thumbnail_kill.hpp"

namespace remote60::native_poc {

bool terminate_and_confirm(HANDLE process, DWORD timeoutMs) {
  if (!process) return true;  // nothing to end, nothing to wait for
  // Already gone is the common case: a helper that answered has exited by the time we look.
  if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0) return true;
  TerminateProcess(process, 1);
  // The return value is the whole point of this function. Asking to terminate is not the same as
  // having terminated, and the caller behaves very differently depending on which it was.
  return WaitForSingleObject(process, timeoutMs) == WAIT_OBJECT_0;
}

}  // namespace remote60::native_poc
