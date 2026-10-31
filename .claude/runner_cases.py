import io

p = 'apps/native_poc/src/update_credential_hops_test.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''#include "update_credential_channel.hpp"'''
new = '''#include "update_credential_channel.hpp"
#include "update_endpoint.hpp"'''
assert d.count(old) == 1, 'include'
d = d.replace(old, new, 1)

anchor = '''  // ---------------------------------------------------------------- teardown, path-confirmed'''
added = '''  // ------------------------------------------------- the owner decides, across real processes
  //
  // The decision itself has unit coverage. What was missing is what the PROCESSES do when it goes
  // each way -- and the two outcomes are supposed to look completely different from the outside:
  // one ends with a worker holding a credential, the other with a bootstrap that gave up and no
  // worker at all.
  //
  // The endpoints here are built by the product's own builder, so the comparison under test is
  // the one the client and the host run.
  {
    using remote60::native_poc::directory::update_endpoint_for;

    const auto atLaunch = update_endpoint_for("", "https://rem.example", "windows",
                                              "Authorization: Bearer aaa", "alice", 7);

    struct Case {
      const char* name;
      remote60::native_poc::update::UpdateEndpoint now;
      bool shouldSend;
    };
    const Case cases[] = {
        {"unchanged", atLaunch, true},
        {"same account signed out and back in",
         update_endpoint_for("", "https://rem.example", "windows",
                             "Authorization: Bearer bbb", "alice", 8),
         false},
        {"a different account on the same server",
         update_endpoint_for("", "https://rem.example", "windows",
                             "Authorization: Bearer ccc", "bob", 7),
         false},
        {"signed out entirely",
         update_endpoint_for("", "https://rem.example", "windows", "", "", 0), false},
        {"the server was changed",
         update_endpoint_for("", "https://other.example", "windows",
                             "Authorization: Bearer aaa", "alice", 7),
         false},
    };

    for (const Case& c : cases) {
      const std::wstring recordPath = dir + L"\\\\owner-" + std::to_wstring(&c - cases) + L".txt";
      const std::wstring pipeName =
          make_credential_pipe_name(GetCurrentProcessId(), GetTickCount64());
      CredentialServer server;
      std::string error;
      server.Create(pipeName, &error);

      Child bootstrap;
      const std::wstring command = L"\\"" + helper + L"\\" --role bootstrap --in \\"" + pipeName +
                                   L"\\" --child \\"" + helper + L"\\" --record \\"" + recordPath +
                                   L"\\" --deadline 2500";
      start(command, &bootstrap);

      // Exactly the rule the product applies before sending, run here against real processes.
      std::string payload;
      if (c.now.same_owner_as(atLaunch) && c.now.url == atLaunch.url &&
          c.now.origin == atLaunch.origin && !c.now.credentialHeader.empty()) {
        payload = remote60::native_poc::update::encode_update_descriptor(c.now);
      }

      bool served = false;
      if (!payload.empty()) served = server.Serve(bootstrap.pi.hProcess, payload, 5000, &error);
      const DWORD code = wait_for(bootstrap.pi.hProcess, 15000);
      server.Close();
      const std::string record = read_all(recordPath);

      if (c.shouldSend) {
        check((std::string(c.name) + ": the credential goes out").c_str(), served, error);
        check((std::string(c.name) + ": and a worker receives it").c_str(),
              record.find("worker-received=") != std::string::npos);
        check((std::string(c.name) + ": and the bootstrap finishes cleanly").c_str(), code == 0,
              std::to_string(code));
      } else {
        check((std::string(c.name) + ": nothing is sent").c_str(), !served && payload.empty());
        // The visible end of it: the bootstrap waits, gets nothing, and gives up without ever
        // starting a worker. An attempt whose owner is gone installs nothing.
        check((std::string(c.name) + ": the bootstrap gives up").c_str(), code == 5,
              std::to_string(code));
        check((std::string(c.name) + ": and no worker was started").c_str(),
              record.find("worker-received=") == std::string::npos);
      }
    }
  }

  // ------------------------------------------------- a descriptor for a different url is refused
  //
  // The frame arrived over a channel that proved who was listening; the arguments proved nothing.
  // Neither is trusted alone, so a disagreement between them ends the run rather than being
  // resolved in favour of one of them.
  {
    using remote60::native_poc::directory::update_endpoint_for;
    const auto endpoint = update_endpoint_for("", "https://rem.example", "windows",
                                              "Authorization: Bearer aaa", "alice", 7);

    const std::wstring recordPath = dir + L"\\\\mismatch.txt";
    const std::wstring pipeName =
        make_credential_pipe_name(GetCurrentProcessId(), GetTickCount64());
    CredentialServer server;
    std::string error;
    server.Create(pipeName, &error);

    Child bootstrap;
    // The worker is told to expect a different url than the frame names.
    const std::wstring command = L"\\"" + helper + L"\\" --role bootstrap --in \\"" + pipeName +
                                 L"\\" --child \\"" + helper + L"\\" --record \\"" + recordPath +
                                 L"\\" --expect-url \\"https://elsewhere.example\\" --deadline 5000";
    start(command, &bootstrap);

    std::string payload = remote60::native_poc::update::encode_update_descriptor(endpoint);
    server.Serve(bootstrap.pi.hProcess, payload, 5000, &error);
    wait_for(bootstrap.pi.hProcess, 15000);
    server.Close();

    const std::string record = read_all(recordPath);
    check("a frame naming another url is refused by the worker",
          record.find("worker-url-mismatch=1") != std::string::npos, record.substr(0, 200));
    check("...and the credential is not treated as received",
          record.find("worker-received=") == std::string::npos);
  }

  // ------------------------------------------------- the frame carries who authorised the run
  {
    using remote60::native_poc::directory::update_endpoint_for;
    const auto endpoint = update_endpoint_for("", "https://rem.example", "windows",
                                              "Authorization: Bearer aaa", "alice", 7);
    const std::wstring recordPath = dir + L"\\\\owner-frame.txt";
    const std::wstring pipeName =
        make_credential_pipe_name(GetCurrentProcessId(), GetTickCount64());
    CredentialServer server;
    std::string error;
    server.Create(pipeName, &error);

    Child bootstrap;
    const std::wstring command = L"\\"" + helper + L"\\" --role bootstrap --in \\"" + pipeName +
                                 L"\\" --child \\"" + helper + L"\\" --record \\"" + recordPath +
                                 L"\\" --expect-url \\"https://rem.example\\" --deadline 5000";
    start(command, &bootstrap);

    std::string payload = remote60::native_poc::update::encode_update_descriptor(endpoint);
    const bool served = server.Serve(bootstrap.pi.hProcess, payload, 5000, &error);
    wait_for(bootstrap.pi.hProcess, 15000);
    server.Close();

    const std::string record = read_all(recordPath);
    check("a matching frame is accepted", served && record.find("worker-received=") !=
                                              std::string::npos, error);
    check("...and the worker reads the owner out of the frame, not out of its arguments",
          record.find("worker-owner=alice/7") != std::string::npos, record.substr(0, 200));
  }

''' + anchor
assert d.count(anchor) == 1, 'anchor'
d = d.replace(anchor, added, 1)

# teardown for the new files
old_td = '''    for (const wchar_t* name : {L"ok.log", L"noack.log"}) {'''
new_td = '''    for (const wchar_t* name : {L"owner-0.txt", L"owner-1.txt", L"owner-2.txt", L"owner-3.txt",
                                L"owner-4.txt", L"mismatch.txt", L"owner-frame.txt"}) {
      DeleteFileW((dir + L"\\\\" + name + L".bootstrap").c_str());
      DeleteFileW((dir + L"\\\\" + name + L".worker").c_str());
    }
    for (const wchar_t* name : {L"ok.log", L"noack.log"}) {'''
assert d.count(old_td) == 1, 'teardown'
d = d.replace(old_td, new_td, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok test')
