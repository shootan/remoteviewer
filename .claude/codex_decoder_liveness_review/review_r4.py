from pathlib import Path
import subprocess, os

base = Path(__file__).parent
old = (base/'reproduce.py').read_text(encoding='utf-8')
tag = "(root/'candidate_probe.cpp').write_text(r'''"
i = old.index(tag)
j = old.index("''',encoding='utf-8')",i)
cpp = old[i+len(tag):j]
cpp = cpp.replace('return leaked?1:0;',r'''
  const auto bound = s.LocalSelectionBoundFor(42);
  bool candidateOk = bound == 101;
  { std::lock_guard<std::mutex> l(fake_ndk::mu());
    for(const auto& q:fake_ndk::controls().queued) if(!q.empty() && q.back()==0xB2) candidateOk=false;
  }
  std::printf("CONTRACT first queued generation binds: %s bound=%llu\n",candidateOk?"PASS":"FAIL",(unsigned long long)bound);

  // Actual owner change at the same input-dequeue barrier.
  size_t before;
  { std::lock_guard<std::mutex> l(fake_ndk::mu()); before=fake_ndk::controls().queued.size(); }
  AndroidVideoDecoderSink own;
  own.Bind(100); own.SetSurface(nullptr,reinterpret_cast<jobject>(0x303));
  fake_ndk::barrier().held.store(true);
  own.OnEncodedH264FrameAs(100,f(0,0xD4));
  bool ownerHeld=wait([]{return fake_ndk::barrier().waiting.load()>0;});
  own.Bind(200);
  fake_ndk::barrier().held.store(false);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  own.OnEncodedH264FrameAs(200,f(0,0xE5));
  wait([&]{std::lock_guard<std::mutex> l(fake_ndk::mu()); return fake_ndk::controls().queued.size()>before;});
  bool ownerOk=ownerHeld;
  { std::lock_guard<std::mutex> l(fake_ndk::mu());
    const auto& q=fake_ndk::controls().queued;
    for(size_t k=before;k<q.size();++k) if(q[k].empty() || q[k].back()!=0xE5) ownerOk=false;
    ownerOk=ownerOk && q.size()>before;
  }
  std::printf("CONTRACT owner change during dequeue: %s\n",ownerOk?"PASS":"FAIL");
  own.SetSurface(nullptr,nullptr);

  AndroidVideoDecoderSink retry;
  retry.SetSurface(nullptr,reinterpret_cast<jobject>(0x404));
  retry.PrepareForWindowSelection(90);
  ControlWindowSelectedMessage ack{}; ack.flags=1; ack.streamGeneration=303;
  retry.OnWindowSelectionControlResultFor(ack,90);
  { std::lock_guard<std::mutex> l(fake_ndk::mu()); fake_ndk::controls().createFails=true; }
  retry.OnEncodedH264Frame(f(303,0xC3));
  wait([&]{return retry.DebugStatus().find("codecFail=1 ")!=std::string::npos;});
  for(int k=0;k<40;++k) {
    auto delta=f(303,0x44); delta.header.flags=0; delta.payload={0,0,0,1,0x41,0x44};
    retry.OnEncodedH264Frame(std::move(delta));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  auto status=retry.DebugStatus();
  std::printf("DELTA FAILURE SNAPSHOT %s\n",status.c_str());
  auto field=[&](const char* key) { auto at=status.find(key); return at==std::string::npos?0ull:std::stoull(status.substr(at+std::strlen(key))); };
  bool retryOk=field("codecFail=")<=4;
  std::printf("CONTRACT two seconds, one IDR plus 40 deltas: failures=%llu timerRetries=%llu %s\n",
    field("codecFail="),field("codecRetries="),retryOk?"PASS":"FAIL (deltas bypass retry cadence and budget)");
  retry.SetSurface(nullptr,nullptr);
  { std::lock_guard<std::mutex> l(fake_ndk::mu()); fake_ndk::controls().createFails=false; }
  return candidateOk && ownerOk && retryOk ? 0:1;
''')
cpp=cpp.replace('#include <cstdio>','#include <cstdio>\n#include <cstring>')
cpp=cpp.replace('candidate 202 admitted while candidate 101\'s dequeue was held', 'generation 202 passed RX Admit while generation 101 had a queue reservation')
cpp=cpp.replace('obsolete candidate queued=%d', 'generation101_observed=%d')
cpp=cpp.replace('std::puts(leaked?"REPRODUCED: gate changed before old candidate was queued":"NOT REPRODUCED");', 'std::puts("raw generation101 observation above is not the r4 verdict; the contract verdict follows");')
# Read bound before detach (detach does not reset the gate, but keep this explicit).
script = old[:i] + "(root/'candidate_probe.cpp').write_text(" + repr(cpp) + ",encoding='utf-8')" + old[j+len("''',encoding='utf-8')"):]
script = script.replace("'r3_7a79'","'r4_0a108'").replace("'7a79b09df02017831515d342733452809b022fd2'","'0a108b39830664e28ae36d79372912eae894b9bf'")
script = script.replace("p = root/'fake_ndk.cpp'", "for name in ['selection_ack_gate.hpp','decoder_pending_slot.hpp']:\n    freeze('apps/native_poc/src/'+name,name)\np = root/'fake_ndk.cpp'")
script=script.replace("s = s.replace(needle,needle+", "if 'fake_ndk::hold_if(fake_ndk::kHoldDequeueInput)' not in s:\n    s = s.replace(needle,needle+")
script=script.replace('timeout=15','timeout=20').replace('[-3500:]','[-4500:]')
generated=base/'r4_build_probe.py'
prior=base/'r4_0a108'/'candidate_probe.log'
if prior.exists() and not (prior.parent/'initial_probe.log').exists():
    (prior.parent/'initial_probe.log').write_bytes(prior.read_bytes())
generated.write_text(script,encoding='utf-8')
subprocess.run(['python',str(generated)],check=True)
