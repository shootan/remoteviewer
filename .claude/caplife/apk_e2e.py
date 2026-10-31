p = 'C:\\work\\gnlink-wt\\apk-prelogin-select\\apps\\native_poc\\src\\viewer_prelogin_login_e2e_test.cpp'
sec = open('D:\\remote\\remote\\.claude\\caplife\\apk_e2e_section.inc', encoding='utf-8').read().replace('\r\n', '\n')
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:80])
    s = s.replace(old, new)

rep('''#include "e2e_isolation.hpp"''', '''#include "e2e_isolation.hpp"
#include "selection_ack_gate.hpp"''')
rep('''  bool androidSession = false;''', '''  bool androidSession = false;
  bool androidSelect = false;  // apk-0232 r1: the app's desktop pick at the logon screen''')
rep('''    if (a == L"--android-session") androidSession = true;  // r5 G8: the shared session the Android app drives''',
    '''    if (a == L"--android-session") androidSession = true;  // r5 G8: the shared session the Android app drives
    if (a == L"--android-select") {  // apk-0232 r1: the same shared-session setup; its own steps below
      androidSelect = true;
      androidSession = true;
    }''')
rep('''    struct Sink : remote60::native_poc::ClientEncodedFrameSink {
      void OnEncodedH264Frame(remote60::native_poc::UdpH264AssembledFrame&& f) override { (void)f; }
      void OnVideoStreamReset() override {}
    } sink;''', '''    struct Sink : remote60::native_poc::ClientEncodedFrameSink {
      void OnEncodedH264Frame(remote60::native_poc::UdpH264AssembledFrame&& f) override { (void)f; }
      void OnVideoStreamReset() override {}
    } plainSink;
    // apk-0232 r1: the APK decoder sink's selection handling over the shared gate (see the --android-select steps).
    struct GateSink : remote60::native_poc::ClientEncodedFrameSink {
      std::mutex mu;
      remote60::native_poc::SelectionAckGate gate;
      uint64_t ready = 0, keysAfterPrepare = 0;
      void OnEncodedH264Frame(remote60::native_poc::UdpH264AssembledFrame&& f) override {
        std::lock_guard<std::mutex> l(mu);
        const bool key = (f.header.flags & 1u) != 0;
        if (key) ++keysAfterPrepare;
        if (!gate.Admit(f.header.streamGeneration, key)) return;
        if (key) gate.KeyDelivered(f.header.streamGeneration);
        if (gate.pending() != 0 && ready != gate.pending()) ready = gate.pending();
      }
      void OnVideoStreamReset() override {}
      uint64_t CurrentSelectionTag() override { std::lock_guard<std::mutex> l(mu); return gate.pending(); }
      uint64_t KeyframeOwedFor() override { std::lock_guard<std::mutex> l(mu); return gate.OwedToken(); }
      void OnWindowSelectionControlResultFor(const remote60::native_poc::ControlWindowSelectedMessage& m, uint64_t tag) override {
        std::lock_guard<std::mutex> l(mu);
        gate.OnAck((m.flags & 0x1u) != 0, m.streamGeneration, tag);
      }
      bool BeginLocalSelectionFor(uint64_t tag) override { std::lock_guard<std::mutex> l(mu); return gate.BeginLocal(tag); }
      uint64_t LocalSelectionBoundFor(uint64_t tag) override { std::lock_guard<std::mutex> l(mu); return gate.LocalBoundFor(tag); }
      void RefuseSelectionFor(uint64_t tag) override { std::lock_guard<std::mutex> l(mu); gate.Refuse(tag); }
      void Prepare(uint64_t sel) { std::lock_guard<std::mutex> l(mu); gate.Prepare(sel); ready = 0; keysAfterPrepare = 0; }
      uint64_t Ready() { std::lock_guard<std::mutex> l(mu); return ready; }
      uint64_t Bound(uint64_t sel) { std::lock_guard<std::mutex> l(mu); return gate.LocalBoundFor(sel); }
      uint64_t KeysAfterPrepare() { std::lock_guard<std::mutex> l(mu); return keysAfterPrepare; }
    } gateSink;
    remote60::native_poc::ClientEncodedFrameSink& sink =
        androidSelect ? static_cast<remote60::native_poc::ClientEncodedFrameSink&>(gateSink) : plainSink;''')
rep('''    if (foreignPeer) {
      // r13: main's udp-control-peer gate''', sec + '''      // r13: main's udp-control-peer gate''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
