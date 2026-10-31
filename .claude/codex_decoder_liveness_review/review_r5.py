from pathlib import Path
import subprocess

base=Path(__file__).parent
s=(base/'r4_build_probe.py').read_text(encoding='utf-8')
s=s.replace("'r4_0a108'","'r5_ac587'").replace("'0a108b39830664e28ae36d79372912eae894b9bf'","'ac587dde7ad87f35e00547e3992677a5554c9c3d'")
# The generated script contains the C++ as a Python literal. Replace it as an AST literal, without executing it.
import ast
t=ast.parse(s)
target=None
for n in ast.walk(t):
    if isinstance(n,ast.Call) and isinstance(n.func,ast.Attribute) and n.func.attr=='write_text' and n.args:
        if isinstance(n.args[0],ast.Constant) and isinstance(n.args[0].value,str) and 'int main()' in n.args[0].value:
            target=n.args[0]; break
assert target is not None
cpp=target.value
needle='  retry.SetSurface(nullptr,nullptr);'
assert cpp.count(needle)==1
cpp=cpp.replace(needle,r'''
  // IDRs may bypass the time slot, but all such attempts must consume the same finite budget.
  for(int k=0;k<30;++k) {
    retry.OnEncodedH264Frame(f(303,0xC3));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  status=retry.DebugStatus();
  bool terminalOk=status.find("codecState=failed_terminal")!=std::string::npos && field("codecFail=")==21;
  std::printf("CONTRACT IDRs share budget: %s failures=%llu retry=%llu\n",terminalOk?"PASS":"FAIL",field("codecFail="),field("codecRetries="));
  { std::lock_guard<std::mutex> l(fake_ndk::mu()); fake_ndk::controls().createFails=false; }
  auto failures=field("codecFail=");
  for(int k=0;k<10;++k) {
    retry.OnEncodedH264Frame(f(303,0xC3));
    auto d=f(303,0x44); d.header.flags=0; d.payload={0,0,0,1,0x41,0x44};
    retry.OnEncodedH264Frame(std::move(d));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  status=retry.DebugStatus();
  bool quietOk=status.find("codecState=failed_terminal")!=std::string::npos && field("codecFail=")==failures;
  std::printf("CONTRACT terminal IDR/delta do not restart: %s\n",quietOk?"PASS":"FAIL");
  size_t oldQ;
  { std::lock_guard<std::mutex> l(fake_ndk::mu()); oldQ=fake_ndk::controls().queued.size(); }
  retry.SetSurface(nullptr,reinterpret_cast<jobject>(0x505));
  retry.OnEncodedH264Frame(f(303,0xC3));
  bool recovered=wait([&]{std::lock_guard<std::mutex> l(fake_ndk::mu()); return fake_ndk::controls().queued.size()>oldQ;});
  std::printf("CONTRACT new surface resets terminal and queues IDR: %s\n",recovered?"PASS":"FAIL");
  retryOk=retryOk && terminalOk && quietOk && recovered;
  retry.SetSurface(nullptr,nullptr);
''')
lines=s.splitlines(keepends=True)
offset=sum(len(x) for x in lines[:target.lineno-1])+target.col_offset
end=sum(len(x) for x in lines[:target.end_lineno-1])+target.end_col_offset
s=s[:offset]+repr(cpp)+s[end:]
out=base/'r5_build_probe.py'
out.write_text(s,encoding='utf-8')
subprocess.run(['python',str(out)],check=True)
