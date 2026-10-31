from pathlib import Path
root=Path(r'D:/remote/remote')
out=root/'.claude/viewer_timer_probe'
out.mkdir(exist_ok=True)
s=(root/'apps/native_poc/src/viewer_udp_recovery_test.cpp').read_text(encoding='utf-8-sig')
s=s.replace('  ~FakeHost() { Stop(); }', '''  ~FakeHost() { Stop(); }
  void SendControlOnly() {
    UdpControlAckPacket p{};
    p.kind = static_cast<uint16_t>(UdpPacketKind::ControlAck);
    p.streamId = kUdpControlStreamClientToHost;
    p.messageSeq = 0;
    sockaddr_in peer{};
    { std::lock_guard<std::mutex> lk(peerMu_); peer = peer_; }
    sendto(sock_, reinterpret_cast<const char*>(&p), sizeof(p), 0,
           reinterpret_cast<const sockaddr*>(&peer), sizeof(peer));
  }''')
s=s[:s.index('\nint main() {')]+r'''
int main() {
  std::cout.setf(std::ios::unitbuf);
  WinsockScope ws;
  if (!ws.ok || FAILED(MFStartup(MF_VERSION))) return 2;
  for (int mode=0; mode<2; ++mode) {
    FakeHost host;
    ViewerRig rig;
    LossPlan plan;
    plan.honorKeyframeRequests = false;
    if (!host.Start(plan) || !rig.Connect(host.port(), true, 120000)) return 3;
    rig.ctx.control.udpControl.Configure([](const void*,size_t){return true;},
        kUdpControlStreamClientToHost,kUdpControlStreamHostToClient,1200);
    rig.ctx.control.overUdp.store(true);
    const auto start = qpc_now_us();
    uint64_t previous = start, maxGap=0, sent=0;
    while(qpc_now_us() - start < 1200000) {
      if(mode) {
        auto now=qpc_now_us();
        maxGap=std::max(maxGap,now-previous); previous=now;
        host.SendControlOnly(); ++sent;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    rig.Stop();
    std::printf("PROBE mode=%s durationUs=%llu nonVideoSent=%llu maxSendGapUs=%llu loops=%llu keyRetryCount=%llu keyWaitSinceUs=%llu\n",
      mode?"control-only":"quiet-timeouts",qpc_now_us()-start,sent,maxGap,
      rig.ctx.recvLive.loopIterations.load(),rig.gate.recoveryRetryCount,rig.gate.keyWaitSinceUs);
  }
  MFShutdown();
}
'''
(out/'probe.cpp').write_text(s,encoding='utf-8')
objdir=root/'build-local/apps/native_poc/remote60_viewer_udp_recovery_test.dir/Release'
objs=[p for p in objdir.glob('*.obj') if p.name.lower()!='viewer_udp_recovery_test.obj']
libs=['ws2_32','runtimeobject','windowsapp','advapi32','d3d11','dxgi','gdi32','user32','mfplat','mf','mfuuid','wmcodecdspuuid','ole32','winhttp','kernel32','winspool','shell32','oleaut32','uuid','comdlg32']
linkargs=['/NOLOGO','/SUBSYSTEM:CONSOLE','/MACHINE:X64',f'/OUT:"{out}/probe.exe"',f'"{out}/probe.obj"']+[f'"{p}"' for p in objs]+[f'"{root}/build-local/libs/capture/Release/capture.lib"',f'"{root}/build-local/libs/common/Release/common.lib"']+[x+'.lib' for x in libs]
(out/'link.rsp').write_text('\n'.join(linkargs),encoding='utf-8')
bat='''@echo off
call "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul
cl /nologo /c /std:c++20 /O2 /MT /EHsc /DWIN32 /D_WINDOWS /DNDEBUG /I"D:\\remote\\remote\\apps\\native_poc\\src" /I"D:\\remote\\remote\\libs\\capture\\src" /I"D:\\remote\\remote\\libs\\common\\include" /Fo"D:\\remote\\remote\\.claude\\viewer_timer_probe\\probe.obj" "D:\\remote\\remote\\.claude\\viewer_timer_probe\\probe.cpp"
if errorlevel 1 exit /b 1
link @"D:\\remote\\remote\\.claude\\viewer_timer_probe\\link.rsp"
'''
(out/'build.bat').write_text(bat,encoding='ascii')
print(out)
