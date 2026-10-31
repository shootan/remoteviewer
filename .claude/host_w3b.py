import io

p = 'apps/native_poc/src/host_app_main.cpp'
d = io.open(p, encoding='utf-8').read()

# --- the check result becomes a choice when there is something to choose
old = '''    case kUpdateCheckDoneMessage: {
      // Ownership of the string transfers here. Shown from the UI thread, which is the reason
      // the worker posts instead of calling MessageBox itself.
      std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lParam));
      if (text) {
        MessageBoxW(window, text->c_str(), kProductName, MB_OK | MB_ICONINFORMATION);
      }
      return 0;
    }'''
new = '''    case kUpdateCheckDoneMessage: {
      // Ownership of the string transfers here. Shown from the UI thread, which is the reason
      // the worker posts instead of calling MessageBox itself.
      std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lParam));
      if (!text) return 0;
      // wParam is set when there is actually a newer version. Only then is there a decision to
      // make; the other four outcomes are statements, and offering to "update" after "could not
      // reach the server" would be offering something that cannot happen.
      if (wParam != 0) {
        const int answer = MessageBoxW(window, text->c_str(), kProductName,
                                       MB_YESNO | MB_ICONQUESTION);
        // Later is an ordinary answer, not a dismissal to work around. Nothing happens and the
        // product keeps running exactly as it was.
        if (answer == IDYES) start_update_handoff(window);
        else append_host_app_log("[host-app] update: the user chose later");
        return 0;
      }
      MessageBoxW(window, text->c_str(), kProductName, MB_OK | MB_ICONINFORMATION);
      return 0;
    }

    case kUpdateHandoffDoneMessage: {
      std::unique_ptr<std::string> detail(reinterpret_cast<std::string*>(lParam));
      append_host_app_log("[host-app] update: " + (detail ? *detail : std::string("no detail")));
      if (wParam != 0) {
        // The updater holds the lock and has a verified download, so leaving is safe and
        // necessary -- this process is one of the files it is about to replace.
        DestroyWindow(window);
        return 0;
      }
      // Every other outcome means nothing on disk changed. Say so rather than leaving the user
      // wondering whether an update half happened.
      MessageBoxW(window,
                  L"업데이트를 시작하지 못했습니다.\\n\\n"
                  L"설치된 버전은 그대로이며 계속 사용할 수 있습니다.",
                  kProductName, MB_OK | MB_ICONINFORMATION);
      return 0;
    }'''
assert old in d, 'check done anchor'
d = d.replace(old, new, 1)

# --- the worker has to say whether there is a decision to make
old = '''      [window](upd::CheckResult result) {
        std::wstring text;
        switch (result.outcome) {
          case upd::CheckOutcome::UpdateAvailable:
            text = L"A newer version is available: " + widen(result.availableVersion) +
                   L"\\n\\nInstalling it from here is not implemented yet.";
            break;'''
new = '''      [window](upd::CheckResult result) {
        std::wstring text;
        switch (result.outcome) {
          case upd::CheckOutcome::UpdateAvailable:
            text = L"새 버전 " + widen(result.availableVersion) + L" 이 있습니다.\\n\\n"
                   L"지금 업데이트하시겠습니까?\\n"
                   L"업데이트 중에는 원격 연결이 잠시 끊깁니다.";
            break;'''
assert old in d, 'available text anchor'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('host choice and verdict handler added')
