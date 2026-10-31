import io

# ---------------------------------------------------------------- link what the product decides with
p = 'apps/native_poc/CMakeLists.txt'
d = io.open(p, encoding='utf-8').read()
old = '''add_executable(remote60_update_handoff_process_test
  src/update_handoff.cpp
  src/update_credential_channel.cpp
  src/update_handoff_wait.cpp
  src/update_handoff_process_test.cpp
)'''
new = '''add_executable(remote60_update_handoff_process_test
  src/update_handoff.cpp
  src/update_credential_channel.cpp
  src/update_handoff_wait.cpp
  src/update_handoff_process_test.cpp
  src/update_endpoint.cpp
  src/url_origin.cpp
  src/directory_client.cpp
  src/winhttp_transport.cpp
)'''
assert d.count(old) == 1, 'cmake'
d = d.replace(old, new, 1)
old2 = '''target_include_directories(remote60_update_handoff_process_test PRIVATE src)
target_compile_definitions(remote60_update_handoff_process_test PRIVATE'''
new2 = '''target_include_directories(remote60_update_handoff_process_test PRIVATE src)
target_link_libraries(remote60_update_handoff_process_test PRIVATE common ws2_32 iphlpapi shell32 winhttp advapi32)
target_compile_definitions(remote60_update_handoff_process_test PRIVATE'''
assert d.count(old2) == 1, 'cmake libs'
d = d.replace(old2, new2, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok cmake')

# ---------------------------------------------------------------- the case, built from real states
p = 'apps/native_poc/src/update_handoff_process_test.cpp'
d = io.open(p, encoding='utf-8').read()

old3 = '''                       int ackOpenDelayMs = 0, bool withoutAckChannel = false,
                       bool ackUnwritable = false, bool ownerChanged = false) {'''
new3 = '''                       int ackOpenDelayMs = 0, bool withoutAckChannel = false,
                       bool ackUnwritable = false,
                       std::function<bool()> ownerStillValid = {}) {'''
assert d.count(old3) == 1, 'signature'
d = d.replace(old3, new3, 1)

old4 = '''    a.step = await_handoff(ready, withoutAckChannel ? nullptr : ack, pi.hProcess, readyName,
                           hostTimeoutMs, nullptr, &a.why,
                           ownerChanged ? std::function<bool()>([]() { return false; })
                                        : std::function<bool()>());'''
new4 = '''    a.step = await_handoff(ready, withoutAckChannel ? nullptr : ack, pi.hProcess, readyName,
                           hostTimeoutMs, nullptr, &a.why, ownerStillValid);'''
assert d.count(old4) == 1, 'call'
d = d.replace(old4, new4, 1)

old5 = '''    const Attempt a = run(0, 200, true, 15000, 4000, false, false, 0, false, false, true);
    check("an owner change withholds the acknowledgement", a.step != HandoffStep::ExitNow,
          std::string(handoff_step_name(a.step)) + ": " + a.why);
    check("...and says which of the two it was",
          a.why.find("owner") != std::string::npos, a.why);'''
new5 = '''    // The predicate is the product's own comparison over endpoints the product's own builder
    // produced -- not a stub that returns false. A stub would prove the wiring and nothing about
    // whether a real sign-out actually reads as a different owner.
    using remote60::native_poc::directory::update_endpoint_for;
    const auto atLaunch = update_endpoint_for("", "https://rem.example", "windows",
                                              "x-host-token: aaa", "alice|machine-1", 3);
    // What the host holds after sign_out(): the token is cleared and the epoch moves. The account
    // and the machine id are untouched, which is the case a url comparison cannot see.
    const auto afterSignOut =
        update_endpoint_for("", "https://rem.example", "windows", "", "alice|machine-1", 4);

    const Attempt a = run(0, 200, true, 15000, 4000, false, false, 0, false, false, [&]() {
      return afterSignOut.same_owner_as(atLaunch) && afterSignOut.url == atLaunch.url &&
             !afterSignOut.credentialHeader.empty();
    });
    check("a sign-out during the download withholds the acknowledgement",
          a.step != HandoffStep::ExitNow,
          std::string(handoff_step_name(a.step)) + ": " + a.why);
    check("...and says which of the two it was",
          a.why.find("owner") != std::string::npos, a.why);'''
assert d.count(old5) == 1, 'case'
d = d.replace(old5, new5, 1)

# ---------------------------------------------------------------- and the positive control
old6 = '''    check("...and the worker stood down instead of stopping the product",
          a.witness.find("worker stood down") != std::string::npos, a.witness);
  }'''
new6 = '''    check("...and the worker stood down instead of stopping the product",
          a.witness.find("worker stood down") != std::string::npos, a.witness);

    // Without this, every assertion above would also pass for a predicate that always says no --
    // and for a build where nothing can ever be acknowledged.
    const Attempt ok = run(0, 200, true, 15000, 4000, false, false, 0, false, false, [&]() {
      const auto unchanged = update_endpoint_for("", "https://rem.example", "windows",
                                                 "x-host-token: aaa", "alice|machine-1", 3);
      return unchanged.same_owner_as(atLaunch) && unchanged.url == atLaunch.url &&
             !unchanged.credentialHeader.empty();
    });
    check("an unchanged owner is acknowledged", ok.step == HandoffStep::ExitNow,
          std::string(handoff_step_name(ok.step)) + ": " + ok.why);
    check("...and the worker was told it may proceed",
          ok.witness.find("worker WOULD stop the product") != std::string::npos, ok.witness);

    // A re-login: same account, same machine, a NEW token and a moved epoch. The url and the
    // origin are identical, so only the owner comparison can refuse it.
    const auto afterRelogin = update_endpoint_for("", "https://rem.example", "windows",
                                                  "x-host-token: bbb", "alice|machine-1", 5);
    const Attempt again = run(0, 200, true, 15000, 4000, false, false, 0, false, false, [&]() {
      return afterRelogin.same_owner_as(atLaunch) && afterRelogin.url == atLaunch.url &&
             !afterRelogin.credentialHeader.empty();
    });
    check("a re-login during the download is also withheld", again.step != HandoffStep::ExitNow,
          std::string(handoff_step_name(again.step)) + ": " + again.why);
    check("...even though it has a perfectly good token",
          !afterRelogin.credentialHeader.empty());
  }'''
assert d.count(old6) == 1, 'positive'
d = d.replace(old6, new6, 1)

d = d.replace('#include "update_handoff.hpp"',
              '#include "directory_client.hpp"\n#include "update_handoff.hpp"', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok test')
