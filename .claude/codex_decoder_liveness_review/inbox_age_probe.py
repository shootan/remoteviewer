from pathlib import Path
import ast, subprocess
base=Path(__file__).parent
s=(base/'r5_build_probe.py').read_text(encoding='utf-8')
s=s.replace("'r5_ac587'","'inbox_8868_age'").replace("'ac587dde7ad87f35e00547e3992677a5554c9c3d'","'8868fb50bcd1489fa276b357e4393b05c1251f66'")
t=ast.parse(s)
arg=next(n.args[0] for n in ast.walk(t) if isinstance(n,ast.Call) and isinstance(n.func,ast.Attribute)
         and n.func.attr=='write_text' and n.args and isinstance(n.args[0],ast.Constant)
         and isinstance(n.args[0].value,str) and 'int main()' in n.args[0].value)
cpp=arg.value[:arg.value.index('int main()')]+r'''
int main() {
  AndroidVideoDecoderSink sink;
  sink.SetSurface(nullptr,reinterpret_cast<jobject>(0x101));
  sink.PrepareForWindowSelection(90);
  ControlWindowSelectedMessage ack{}; ack.flags=1; ack.streamGeneration=303;
  sink.OnWindowSelectionControlResultFor(ack,90);
  { std::lock_guard<std::mutex> l(fake_ndk::mu()); fake_ndk::controls().inputAvailable=false; }
  auto t0=std::chrono::steady_clock::now();
  sink.OnEncodedH264Frame(f(303,0xA1));
  bool held=wait([&]{return sink.DebugStatus().find("pending=1 ")!=std::string::npos;});
  if(!held) { std::puts("INVALID no held frame"); return 2; }
  std::this_thread::sleep_for(std::chrono::milliseconds(250));
  auto delta=f(303,0xB2); delta.header.flags=0; delta.payload={0,0,0,1,0x41,0xB2};
  sink.OnEncodedH264Frame(std::move(delta));
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  { std::lock_guard<std::mutex> l(fake_ndk::mu()); fake_ndk::controls().inputAvailable=true; }
  bool queued=wait([]{std::lock_guard<std::mutex> l(fake_ndk::mu());return !fake_ndk::controls().queued.empty();});
  auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-t0).count();
  std::printf("held=%d queued=%d actual IDR age at least 280ms, observed elapsed=%lldms\n",held,queued,(long long)ms);
  std::printf("%s\n",sink.LivenessTraceLine().c_str());
  sink.SetSurface(nullptr,nullptr);
  return queued?0:2;
}
'''
lines=s.splitlines(keepends=True)
a=sum(len(x) for x in lines[:arg.lineno-1])+arg.col_offset
b=sum(len(x) for x in lines[:arg.end_lineno-1])+arg.end_col_offset
s=s[:a]+repr(cpp)+s[b:]
s=s.replace("'/DNOMINMAX'","'/DNOMINMAX','/DGNLINK_LIVENESS_TRACE=1'")
out=base/'inbox_age_build.py';out.write_text(s,encoding='utf-8')
subprocess.run(['python',str(out)],check=True)
