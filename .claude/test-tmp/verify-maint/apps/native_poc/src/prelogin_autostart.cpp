// Live registry implementation for the prelogin per-user autostart cleanup (prelogin_autostart.hpp).

#include "prelogin_autostart.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace remote60::native_poc::prelogin {

bool RemoveUserRunValues(void* rootKey, const wchar_t* runSubkey, const wchar_t* const* valueNames,
                         size_t count) {
  HKEY key = nullptr;
  const LSTATUS open = RegOpenKeyExW(reinterpret_cast<HKEY>(rootKey), runSubkey, 0,
                                     KEY_SET_VALUE | KEY_QUERY_VALUE, &key);
  if (open == ERROR_FILE_NOT_FOUND) return true;  // no Run key at all -> nothing to remove
  if (open != ERROR_SUCCESS) return false;
  bool allGone = true;
  for (size_t i = 0; i < count; ++i) {
    const LSTATUS del = RegDeleteValueW(key, valueNames[i]);
    if (del != ERROR_SUCCESS && del != ERROR_FILE_NOT_FOUND) allGone = false;
  }
  RegCloseKey(key);
  return allGone;
}

}  // namespace remote60::native_poc::prelogin
