import io

src='apps/native_poc/src/update_effects.cpp'
d=io.open(src,encoding='utf-8').read()

start=d.find('// ---------------------------------------------------------------- process identity')
assert start!=-1,'identity section'
end=d.find('// ---------------------------------------------------------------- file helpers')
assert end!=-1 and end>start,'file helpers'
block=d[start:end]
assert 'process_identity_matches' in block and 'capture_process_identity' in block

d=d[:start]+'''// Process identity lives in update_process_identity.cpp.
//
// It moved because update_relaunch.cpp needs to ask "is this still the process I captured?" and
// linking all of this file into that test to get one comparison would have dragged the whole file
// and registry layer in with it. What actually happened first was worse: rather than link it, the
// liveness check was left unwired.

'''+d[end:]
io.open(src,'w',encoding='utf-8',newline='').write(d)

out='apps/native_poc/src/update_process_identity.cpp'
io.open(out,'w',encoding='utf-8',newline='').write('''// Whether a process is still the one that was captured.
//
// Its own translation unit because two separate places need the same answer and must give the same
// answer: the quiesce/rollback side in update_effects.cpp, and the relaunch side, which has to
// decide whether something it is about to start is already running. Two implementations of "the
// same process" would eventually disagree, and the disagreement would show up as a second copy of
// the product.
//
// A PID is not an identity. It is reused, sometimes within seconds, so everything here compares the
// creation time -- which is unique per process for as long as anyone can hold a handle to it -- and
// the image path on top of that.

#include <windows.h>

#include <iterator>
#include <string>

#include "update_effects.hpp"

namespace remote60::native_poc::update {
namespace {

uint64_t creation_time_of(HANDLE process) {
  FILETIME created{}, exited{}, kernel{}, user{};
  if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) return 0;
  ULARGE_INTEGER v{};
  v.LowPart = created.dwLowDateTime;
  v.HighPart = created.dwHighDateTime;
  return v.QuadPart;
}

std::wstring image_path_of(HANDLE process) {
  wchar_t buffer[MAX_PATH * 2]{};
  DWORD size = static_cast<DWORD>(std::size(buffer));
  if (!QueryFullProcessImageNameW(process, 0, buffer, &size)) return {};
  return std::wstring(buffer, size);
}

}  // namespace

bool capture_process_identity(uint32_t pid, ProcessTarget* out) {
  if (!out) return false;
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!h) return false;
  out->pid = pid;
  out->creationTime = creation_time_of(h);
  out->imagePath = image_path_of(h);
  CloseHandle(h);
  // A creation time of zero would make every later comparison vacuous, so it is treated as a
  // failure to identify rather than as an identity.
  return out->creationTime != 0;
}

bool process_identity_matches(void* handle, const ProcessTarget& target) {
  HANDLE h = static_cast<HANDLE>(handle);
  if (!h) return false;
  if (target.creationTime == 0) return false;  // nothing to compare against
  if (creation_time_of(h) != target.creationTime) return false;
  // The creation time alone is already decisive in practice; the path is compared too because it
  // costs nothing and makes a deliberately crafted collision harder.
  if (!target.imagePath.empty() && image_path_of(h) != target.imagePath) return false;
  return true;
}

}  // namespace remote60::native_poc::update
''')

p='apps/native_poc/CMakeLists.txt'
c=io.open(p,encoding='utf-8').read()
n=c.count('  src/update_effects.cpp\n')
c=c.replace('  src/update_effects.cpp\n','  src/update_effects.cpp\n  src/update_process_identity.cpp\n')
old='''  src/update_relaunch.cpp
  src/update_relaunch_plan.cpp
  src/update_health.cpp
  src/update_health_log.cpp
  src/update_relaunch_test.cpp
)'''
new='''  src/update_relaunch.cpp
  src/update_relaunch_plan.cpp
  src/update_health.cpp
  src/update_health_log.cpp
  # The real identity comparison, not a stand-in. The liveness check decides whether to start
  # something, and a test answering that question its own way would be testing its own answer.
  src/update_process_identity.cpp
  src/update_relaunch_test.cpp
)'''
assert old in c,'relaunch test sources'
c=c.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(c)
print('identity extracted; targets updated:',n)
