import io

p = 'apps/native_poc/src/client_shell_main.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''  spec.parentPid = GetCurrentProcessId();
  // No ready event: this process is not elevated and does not need to wait for permission to
  // leave. The updater stops the client the same way it stops any other product process.
  spec.readyEventName.clear();'''
new = '''  spec.parentPid = GetCurrentProcessId();
  // Without a credential there is nothing to withhold, so there is nothing to wait for: this
  // process is not elevated and does not need permission to leave, and the updater stops it the
  // same way it stops any other product process.
  //
  // With one, the handshake is the only place the update can still be stopped. The download takes
  // minutes and the user can sign out or sign in as somebody else while it runs; the
  // acknowledgement is checked against the owner and withheld if it changed, and an updater that
  // is not acknowledged stops nothing. That is why the client now takes part in a handshake it
  // used to skip.
  spec.readyEventName =
      spec.credentialPipeName.empty()
          ? std::wstring()
          : upd::make_ready_event_name(GetCurrentProcessId(), GetTickCount64());'''
assert d.count(old) == 1, 'ready name'
d = d.replace(old, new, 1)

old2 = '''  auto credentialServer = std::make_shared<upd::CredentialServer>();
  if (!spec.credentialPipeName.empty()) {
    std::string error;
    if (!credentialServer->Create(spec.credentialPipeName, &error)) {
      log_line("update: could not open the credential channel -- " + error);
      post_status("error", "업데이트를 시작하지 못했습니다.");
      return;
    }
  }'''
new2 = '''  auto credentialServer = std::make_shared<upd::CredentialServer>();
  HANDLE readyEvent = nullptr;
  HANDLE ackEvent = nullptr;
  if (!spec.credentialPipeName.empty()) {
    std::string error;
    if (!credentialServer->Create(spec.credentialPipeName, &error)) {
      log_line("update: could not open the credential channel -- " + error);
      post_status("error", "업데이트를 시작하지 못했습니다.");
      return;
    }
    // Both made before the updater starts, and from one stem, so an attempt cannot end up
    // answering on another's channel.
    const std::wstring ackName = upd::make_ack_event_name(spec.readyEventName);
    ackEvent = CreateEventW(nullptr, TRUE, FALSE, ackName.c_str());
    readyEvent = CreateEventW(nullptr, TRUE, FALSE, spec.readyEventName.c_str());
    if (!readyEvent || !ackEvent) {
      if (readyEvent) CloseHandle(readyEvent);
      if (ackEvent) CloseHandle(ackEvent);
      credentialServer->Close();
      log_line("update: could not create the handshake events");
      post_status("error", "업데이트를 시작하지 못했습니다.");
      return;
    }
  }'''
assert d.count(old2) == 1, 'events'
d = d.replace(old2, new2, 1)

old3 = '''    HANDLE elevated = info.hProcess;
    info.hProcess = nullptr;  // owned by the thread now
    std::thread([elevated, credentialServer, launchEndpoint]() {'''
new3 = '''    HANDLE elevated = info.hProcess;
    info.hProcess = nullptr;  // owned by the thread now
    std::thread([elevated, credentialServer, launchEndpoint, readyEvent, ackEvent,
                 readyName = spec.readyEventName]() {'''
assert d.count(old3) == 1, 'thread capture'
d = d.replace(old3, new3, 1)

old4 = '''      if (delivered) {
        log_line("update: the updater received its credential");
      } else {'''
new4 = '''      if (delivered) {
        log_line("update: the updater received its credential");
        // And now the veto. The updater signals when it has a verified download and waits to be
        // told it may proceed; the owner is checked at that moment, because that is the last
        // point at which anything can still be withheld.
        std::string why;
        const upd::HandoffStep step = upd::await_handoff(
            readyEvent, ackEvent, elevated, readyName, 10 * 60 * 1000, nullptr, &why, [&]() {
              std::string checkSession;
              std::string checkOwner;
              uint64_t checkEpoch = 0;
              {
                std::lock_guard<std::mutex> lock(gStateMu);
                checkSession = gSessionToken;
                checkOwner = gAccountId;
                checkEpoch = gOwnerEpoch;
              }
              const upd::UpdateEndpoint atReady =
                  remote60::native_poc::directory::update_endpoint_for(
                      remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
                      configured_directory_url(), "windows",
                      checkSession.empty() ? std::string()
                                           : "Authorization: Bearer " + checkSession,
                      checkOwner, checkEpoch);
              return atReady.same_owner_as(launchEndpoint) && atReady.url == launchEndpoint.url;
            });
        log_line(std::string("update: handoff ") + upd::handoff_step_name(step) + ": " + why);
        if (step != upd::HandoffStep::ExitNow) {
          post_status("error", "업데이트가 진행되지 않았습니다. " + why);
        }
      } else {'''
assert d.count(old4) == 1, 'veto'
d = d.replace(old4, new4, 1)

old5 = '''      credentialServer->Close();
      CloseHandle(elevated);'''
new5 = '''      credentialServer->Close();'''
assert d.count(old5) == 1, 'close order'
d = d.replace(old5, new5, 1)

old6 = '''        post_status("error", "업데이트를 시작하지 못했습니다. 다시 로그인한 뒤 시도해 주세요.");
      }
    }).detach();'''
new6 = '''        post_status("error", "업데이트를 시작하지 못했습니다. 다시 로그인한 뒤 시도해 주세요.");
      }
      // Closed last: the handle is what pins the process id, and await_handoff watches it.
      CloseHandle(elevated);
      if (readyEvent) CloseHandle(readyEvent);
      if (ackEvent) CloseHandle(ackEvent);
    }).detach();'''
assert d.count(old6) == 1, 'cleanup'
d = d.replace(old6, new6, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok client ready')
