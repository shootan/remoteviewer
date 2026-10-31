p = r'C:\work\gnlink-wt\apk-inbox-c\apps\native_poc\src\android_decoder_inbox_prefix_test.cpp'
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
s = raw.decode('utf-8').replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:90], s.count(a))
    s = s.replace(a, b)


rep('''    const size_t a = queued();
    send(next(g, true));
    wait_until([&] { return queued() > a; }, 2000);''', '''    const size_t a = queued();
    send(next(g, true));
    wait_until([&] { return queued() > a; }, 2000);
    // the codec shows its first picture (a format, then one output): the selection is on screen, as on a device. With
    // none at all the sink's bootstrap replay (apk-0232: re-queue the selection's IDR while no picture came) would
    // re-feed an OLD IDR in the middle of these schedules -- a stand-in artefact, not either policy.
    {
      std::lock_guard<std::mutex> lk(fake_ndk::mu());
      fake_ndk::Controls::Output fmt;
      fmt.formatChange = true;
      fmt.width = 1280;
      fmt.height = 720;
      fake_ndk::controls().outputs.push_back(fmt);
      fake_ndk::Controls::Output pic;
      pic.presentationTimeUs = 1;
      fake_ndk::controls().outputs.push_back(pic);
    }''')
rep('''  check("[P3 current] none corrupt", a0.corrupt == 0,
        show(a0) +''', '''  check("[P3 current] none corrupt", a0.corrupt == 0,
        show(a0) + " bootstrapReplay=" + std::to_string(field(s0, "bootstrapReplay")) +''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
