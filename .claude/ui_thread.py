import io

p = 'apps/native_poc/src/client_shell_main.cpp'
d = io.open(p, encoding='utf-8').read()

# ---- the server must outlive the call, so it is owned by the thread that serves it
old = '''  upd::CredentialServer credentialServer;
  if (!spec.credentialPipeName.empty()) {
    std::string error;
    if (!credentialServer.Create(spec.credentialPipeName, &error)) {'''
new = '''  auto credentialServer = std::make_shared<upd::CredentialServer>();
  if (!spec.credentialPipeName.empty()) {
    std::string error;
    if (!credentialServer->Create(spec.credentialPipeName, &error)) {'''
assert d.count(old) == 1, 'server owner'
d = d.replace(old, new, 1)

start = d.find('  if (launched && info.hProcess && !spec.credentialPipeName.empty()) {')
assert start > 0, 'serve block'
end = d.find('  if (info.hProcess) CloseHandle(info.hProcess);', start)
assert end > start, 'serve block end'

new_block = '''  if (launched && info.hProcess && !spec.credentialPipeName.empty()) {
    // Off the UI thread. This wait can last as long as an elevation prompt plus a slow start, and
    // it used to sit inline in the page-message handler -- so the window stopped answering for up
    // to two minutes while it ran. Overlapped I/O does not help when the caller waits for it on
    // the thread that has to keep painting.
    //
    // The process handle and the pipe move with it: the handle is what pins the process id the
    // check compares against, so it cannot be closed here.
    HANDLE elevated = info.hProcess;
    info.hProcess = nullptr;  // owned by the thread now
    std::thread([elevated, credentialServer, launchEndpoint]() {
      // Re-read rather than reused: the user may have signed out, signed back in, or changed
      // servers between deciding to update and answering the consent prompt.
      std::string nowSession;
      std::string nowOwner;
      uint64_t nowEpoch = 0;
      {
        std::lock_guard<std::mutex> lock(gStateMu);
        nowSession = gSessionToken;
        nowOwner = gAccountId;
        nowEpoch = gOwnerEpoch;
      }
      const upd::UpdateEndpoint nowEndpoint =
          remote60::native_poc::directory::update_endpoint_for(
              remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
              configured_directory_url(), "windows",
              nowSession.empty() ? std::string() : "Authorization: Bearer " + nowSession, nowOwner,
              nowEpoch);

      std::string payload;
      std::string why;
      // The owner, not the destination. Comparing the url and the origin asks whether the SERVER
      // changed; sign out and back in on the same server and both are identical while the token
      // is a different one -- so that comparison would hand an attempt authorised by one sign-in
      // the credential of the next.
      if (!nowEndpoint.same_owner_as(launchEndpoint)) {
        why = "the signed-in owner changed after the updater was started";
      } else if (nowEndpoint.url != launchEndpoint.url ||
                 nowEndpoint.origin != launchEndpoint.origin) {
        why = "the update endpoint changed after the updater was started";
      } else if (nowEndpoint.credentialHeader.empty()) {
        why = "there is no longer a session to authorise the update with";
      } else {
        payload = upd::encode_update_descriptor(nowEndpoint);
        if (payload.empty()) why = "the credential descriptor could not be built";
      }

      bool delivered = false;
      if (!payload.empty()) {
        std::string error;
        delivered = credentialServer->Serve(elevated, payload, 120000, &error);
        if (!delivered) why = error;
      }
      credentialServer->Close();
      CloseHandle(elevated);

      if (delivered) {
        log_line("update: the updater received its credential");
      } else {
        // Distinct from "started". The updater is running and will stop without installing
        // anything; saying the update began would leave the user waiting for a result that is
        // not coming.
        log_line("update: no credential was delivered -- " + why);
        post_status("error", "업데이트를 시작하지 못했습니다. 다시 로그인한 뒤 시도해 주세요.");
      }
    }).detach();
  }

'''
d = d[:start] + new_block + d[end:]
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok client thread')
