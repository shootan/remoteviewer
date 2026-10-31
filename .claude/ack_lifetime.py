import io

BS = chr(92)

# ================================================================== pure: monotonic remaining
p = 'apps/native_poc/src/update_handoff.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''HandoffStep await_handoff(void* readyEvent, void* ackEvent, void* bootstrap,
                          const std::wstring& readyName, uint32_t timeoutMs, std::string* detail);'''
new = '''/**
 * Milliseconds left of `timeoutMs`, given a monotonic clock.
 *
 * Its own function so the arithmetic can be tested at the values that break it. The wait used to
 * compute `deadline = GetTickCount() + timeoutMs` and compare -- both 32-bit, so on a machine up
 * for 49.7 days the sum wraps past zero, `deadline > now` is false immediately, and the wait ends
 * at once reporting that the updater ran out of time. It would have been reported as a flaky
 * update on long-lived machines and nothing else.
 *
 * `startedAt` and `now` come from a 64-bit monotonic source. Zero means the time is up.
 */
uint32_t remaining_ms(uint64_t startedAt, uint64_t now, uint32_t timeoutMs);

/**
 * Runs the wait to a decision, and acknowledges when the decision is to stand down.
 *
 * Handles are `void*` so this header stays free of windows.h; they are HANDLEs. `bootstrap` may be
 * null when there is nothing to watch besides the event.
 *
 * `now` supplies the monotonic clock, so a test can run this at values a real machine would take
 * weeks to reach. Empty uses the real one.
 *
 * ExitNow is returned ONLY when the acknowledgement was actually delivered. A caller that cannot
 * answer must not leave: the updater is waiting to be told it may go ahead, and a caller that
 * departs without answering leaves it with a product it is not allowed to stop and nobody to
 * stop it for.
 */
HandoffStep await_handoff(void* readyEvent, void* ackEvent, void* bootstrap,
                          const std::wstring& readyName, uint32_t timeoutMs,
                          const std::function<uint64_t()>& now, std::string* detail);'''
assert old in d, 'await decl'
d = d.replace(old, new, 1)
old = '''#include <cstdint>
#include <string>
#include <vector>'''
new = '''#include <cstdint>
#include <functional>
#include <string>
#include <vector>'''
assert old in d, 'includes'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

p = 'apps/native_poc/src/update_handoff.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''std::wstring make_alive_mutex_name(const std::wstring& readyEventName) {'''
new = '''uint32_t remaining_ms(uint64_t startedAt, uint64_t now, uint32_t timeoutMs) {
  // Subtraction first, and in 64 bits. The old form added the timeout to the clock and compared
  // the sum, which overflows a 32-bit tick count on a machine that has been up seven weeks --
  // and then every wait ends immediately, reporting a timeout that never happened.
  if (now <= startedAt) return timeoutMs;  // a clock that went backwards is not evidence of delay
  const uint64_t elapsed = now - startedAt;
  if (elapsed >= timeoutMs) return 0;
  return static_cast<uint32_t>(timeoutMs - elapsed);
}

std::wstring make_alive_mutex_name(const std::wstring& readyEventName) {'''
assert old in d, 'alive name def'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ================================================================== the wait
p = 'apps/native_poc/src/update_handoff_wait.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''HandoffStep await_handoff(void* readyEvent, void* ackEvent, void* bootstrap,
                          const std::wstring& readyName, uint32_t timeoutMs, std::string* detail) {'''
new = '''HandoffStep await_handoff(void* readyEvent, void* ackEvent, void* bootstrap,
                          const std::wstring& readyName, uint32_t timeoutMs,
                          const std::function<uint64_t()>& now, std::string* detail) {
  const std::function<uint64_t()> clock =
      now ? now : std::function<uint64_t()>([]() { return static_cast<uint64_t>(GetTickCount64()); });'''
assert old in d, 'signature'
d = d.replace(old, new, 1)

old = '''  const DWORD deadline = GetTickCount() + timeoutMs;'''
new = '''  const uint64_t startedAt = clock();'''
assert old in d, 'deadline'
d = d.replace(old, new, 1)

old = '''    const DWORD now = GetTickCount();
    const DWORD remaining = (deadline > now) ? (deadline - now) : 0;'''
new = '''    const DWORD remaining = remaining_ms(startedAt, clock(), timeoutMs);'''
assert old in d, 'remaining'
d = d.replace(old, new, 1)

old = '''    const bool timedOut = (waited == WAIT_TIMEOUT);'''
new = ''
if old in d:
    d = d.replace(old, '', 1)

old = '''    const bool outOfTime = remaining == 0;'''
new = '''    const bool outOfTime = remaining == 0;
    if (waited == WAIT_FAILED) {
      // The wait itself could not be performed -- a closed handle, an invalid one. Ended here and
      // named: without this the loop would spin on the same failure for the rest of the deadline,
      // burning a core and then reporting a timeout that hides the real cause.
      if (alive) CloseHandle(alive);
      if (detail) *detail = "the wait failed (error " + std::to_string(GetLastError()) + ")";
      return HandoffStep::KeepRunning;
    }'''
assert old in d, 'outOfTime'
d = d.replace(old, new, 1)

old = '''  // The answer, and it goes out before this returns. An updater waiting to be acknowledged finds
  // it here or not at all -- and not at all means the caller is staying, which means the updater
  // must not stop it.
  if (step == HandoffStep::ExitNow && ack) SetEvent(ack);'''
new = '''  // The answer, and whether it was actually delivered decides the verdict.
  //
  // Setting it and hoping was not enough. If the answer does not arrive, the updater will not go
  // ahead -- it is waiting to be told it may -- and a caller that has already decided to leave
  // would then be gone while the product it was meant to hand over to has stood down. Agreeing to
  // exit is only safe when the agreement was received.
  if (step == HandoffStep::ExitNow) {
    if (!ack || !SetEvent(ack)) {
      if (detail) {
        *detail = "the updater was ready, but the acknowledgement could not be delivered, so "
                  "this attempt is closed and nothing will be stopped";
      }
      return HandoffStep::KeepRunning;
    }
  }'''
assert old in d, 'ack set'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('monotonic clock, WAIT_FAILED ends, and ExitNow requires a delivered answer')
