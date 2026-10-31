# -*- coding: utf-8 -*-
import io

def edit(path, pairs):
    d = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(pairs):
        assert d.count(old) == 1, '%s pair %d: %s' % (path, i, old[:70])
        d = d.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(d)
    print('ok', path)


edit('apps/native_poc/src/host_app_main.cpp', [
 ('''  WSADATA wsa{};
  WSAStartup(MAKEWORD(2, 2), &wsa);

  WNDCLASSEXW wc{};''',
  '''  WSADATA wsa{};
  WSAStartup(MAKEWORD(2, 2), &wsa);

  // The three things this process owns, unwound in the order they depend on each other. Written
  // as scope guards rather than as lines at the end of the function because destructors run after
  // those lines, not before -- so a join written at the end would happen after WSACleanup, with
  // the uploader's worker still inside a socket call. Declared first is destroyed last.
  //
  // 1. sockets, 2. the window's GDI objects, 3. the uploader -- joined before either.
  struct WsaScope {
    ~WsaScope() { WSACleanup(); }
  } wsaScope;
  struct UiScope {
    ~UiScope() {
      if (g.font) DeleteObject(g.font);
      if (g.titleFont) DeleteObject(g.titleFont);
    }
  } uiScope;
  // Nothing in this product called log_upload_stop(); the static holding the worker was destroyed
  // with the thread still joinable, which is std::terminate. This is the call, on the message
  // loop's own thread and holding no lock -- the worker's auth callback marshals to this thread.
  const remote60::native_poc::LogUploadShutdown uploaderShutdown;

  WNDCLASSEXW wc{};'''),
 ('''  if (g.font) DeleteObject(g.font);
  if (g.titleFont) DeleteObject(g.titleFont);
  WSACleanup();
  return 0;
}''',
  '''  return 0;  // uploaderShutdown -> uiScope -> wsaScope, in that order
}'''),
])

edit('apps/native_poc/src/client_shell_main.cpp', [
 ('''  std::string socketError;
  if (!initialize_sockets(&socketError)) {
    MessageBoxW(nullptr, widen(socketError).c_str(), L"GNLink", MB_ICONERROR);
    return 1;
  }''',
  '''  std::string socketError;
  if (!initialize_sockets(&socketError)) {
    MessageBoxW(nullptr, widen(socketError).c_str(), L"GNLink", MB_ICONERROR);
    return 1;
  }

  // The uploader's worker is a std::thread inside a static, and nothing here used to stop it: the
  // static was destroyed with the thread still joinable, which is std::terminate(). Declared
  // after the sockets it needs and before anything that can configure it -- a sign-in arrives on
  // a page message, so from here on -- and it covers the early returns below as well as the end
  // of the message loop. This thread holds no lock at that point, which matters: the auth
  // callback runs on the worker and posts to this window.
  const remote60::native_poc::LogUploadShutdown uploaderShutdown;'''),
])
