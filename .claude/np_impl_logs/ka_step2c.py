import os
R = r'C:\work\gnlink-wt\ka-pc\apps\native_poc'
rel_now = ''

def load(rel):
    b = open(os.path.join(R, rel), 'rb').read().decode('utf-8')
    return b.replace('\r\n', '\n'), ('\r\n' in b)

def save(rel, s, crlf):
    open(os.path.join(R, rel), 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))

def rep(s, a, b, cnt=1):
    assert s.count(a) == cnt, (rel_now, s.count(a), a[:90])
    return s.replace(a, b)

rel_now = r'src\native_video_client_main.cpp'
s, c = load(rel_now)
s = rep(s, '#include "viewer_startup.hpp"\n', '#include "viewer_startup.hpp"\n#include "viewer_managed_standby.hpp"\n')
s = rep(s, """#if defined(REMOTE60_VIEWER_KA_TEST_SEAM)
  // TEST BUILD ONLY (GNLinkViewerKaTest, ka-pc-impl r1 step 1)""", """  // ka-pc-impl r1 (KA ③): the client shell's managed standby viewer of one PC (viewer_managed_standby.hpp).
  {
    auto probe = std::make_unique<ViewerContext>();
    load_config(*probe, argc, argv);
    if (probe->args.managedStandby) {
      probe.reset();
      return run_managed_standby(
          [argc, argv](ViewerContext& c) {
            load_config(c, argc, argv);
            if (validate_codec_transport(c) != 0) return false;
            apply_initial_state(c);
            return true;
          },
          [](ViewerContext& c) { return run_session(c); });
    }
  }

#if defined(REMOTE60_VIEWER_KA_TEST_SEAM)
  // TEST BUILD ONLY (GNLinkViewerKaTest, ka-pc-impl r1 step 1)""")
save(rel_now, s, c)

rel_now = r'src\managed_viewer_ipc.hpp'
s, c = load(rel_now)
s = rep(s, '#include <cstdint>\n', '#include <cstdint>\n#include <cstdlib>\n')
s = rep(s, """  void Close() {
    open_.store(false);
    cv_.notify_all();
    if (writer_.joinable()) writer_.join();""", """  void Close() {
    open_.store(false);
    cv_.notify_all();
    if (writer_.joinable()) {
      // The writer drains what is queued (a Bye, say). A peer that stopped reading must not hold this: after a
      // bounded wait its blocked write is cancelled.
      {
        std::unique_lock<std::mutex> lock(mu_);
        cv_.wait_for(lock, std::chrono::milliseconds(1000), [&] { return writerDone_; });
        if (!writerDone_) CancelSynchronousIo(writer_.native_handle());
      }
      writer_.join();
    }""")
s = rep(s, """  void WriteLoop() {
    for (;;) {
      std::string frame;
      {
        std::unique_lock<std::mutex> lock(mu_);
        cv_.wait(lock, [&] { return !out_.empty() || !open_.load(); });
        if (out_.empty()) return;  // closed and drained""", """  void WriteLoop() {
    struct Done {
      Channel* c;
      ~Done() {
        std::lock_guard<std::mutex> lock(c->mu_);
        c->writerDone_ = true;
        c->cv_.notify_all();
      }
    } done{this};
    for (;;) {
      std::string frame;
      {
        std::unique_lock<std::mutex> lock(mu_);
        cv_.wait(lock, [&] { return !out_.empty() || !open_.load(); });
        if (out_.empty()) return;  // closed and drained""")
s = rep(s, """  std::string broken_;
  std::thread reader_;""", """  std::string broken_;
  bool writerDone_ = false;
  std::thread reader_;""")
save(rel_now, s, c)

rel_now = r'CMakeLists.txt'
s, c = load(rel_now)
s = rep(s, """  # The macro window is a page too, so the viewer needs the same shell the client uses.
  src/macro_shell_bridge.cpp
)""", """  # The macro window is a page too, so the viewer needs the same shell the client uses.
  src/macro_shell_bridge.cpp
  # ka-pc-impl r1 (KA ③): the managed standby mode -- one PC's lease, activated by the shell.
  src/viewer_managed_standby.cpp
)""")
save(rel_now, s, c)
print('ok')
