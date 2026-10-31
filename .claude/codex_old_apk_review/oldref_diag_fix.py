from pathlib import Path
import subprocess,re,os,json,hashlib,difflib
p=Path(__file__).parent
setup=(p/'oldref_r1_checks.py').read_text(encoding='utf-8').split("if '--power-only' not in sys.argv:")[0]
exec(compile(setup,'oldref-build-setup-only','exec'))
paths=['apps/native_poc/src/udp_control_channel.cpp','apps/native_poc/src/udp_control_close_diag_test.cpp']
original={n:(root/Path(n).name).read_text(encoding='utf-8') for n in paths}
old=original[paths[0]]
start=old.index('        diag_.lost = true;')
end=old.index('\n#endif',start)
section=old[start:end]
new=old[:start]+'        if (!diag_.lost) {  // record the FIRST budget exhaustion, even if Tick runs again while closed\n'+''.join('  '+l+'\n' for l in section.splitlines())+'        }'+old[end:]
assert new.count('std::string line = "diag{ack seen="')==1
new=new.replace('std::string line = "diag{ack seen="','std::string line = "close-diag-first-loss-v1 diag{ack seen="')
test=original[paths[1]]
needle='    // the ACKs the peer did send, arriving only now:'
at=test.index(needle)
extra='''    // After close the receiver may still call Tick. The diagnostic must keep the FIRST loss, not a later sample.
    const size_t lostAt = line.find("lost{");
    const size_t agoAt = lostAt == std::string::npos ? std::string::npos : line.find(" agoMs=", lostAt);
    const std::string firstLoss = agoAt == std::string::npos ? "" : line.substr(lostAt, agoAt - lostAt);
    for (int i = 0; i < 2; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
      p.a.Tick();
    }
    const std::string laterLoss = p.a.CloseDiagLine();
    check("repeated Tick after close keeps the FIRST lost head / attempts / waited time", !firstLoss.empty() &&
          laterLoss.find(firstLoss) != std::string::npos, laterLoss);
'''
test=test[:at]+extra+test[at:]
changed={paths[0]:new,paths[1]:test}
patch=''
for n,content in changed.items():
 (root/Path(n).name).write_text(content,encoding='utf-8',newline='\n')
 patch+=''.join(difflib.unified_diff(original[n].splitlines(True),content.splitlines(True),fromfile='a/'+n,tofile='b/'+n))
(root/'first_loss_approved.patch').write_text(patch,encoding='utf-8',newline='\n')
buildrun('diag_unit_fixed',['udp_control_close_diag_test.cpp','udp_control_channel.cpp'],['/DREMOTE60_CONTROL_CLOSE_DIAG=1'])

# Strip only this feature's guarded regions. With it off the header has exactly the base product tokens.
def stripped(s):
 out=[];skip=False
 for line in s.splitlines():
  if line.strip()=='#ifdef REMOTE60_CONTROL_CLOSE_DIAG':assert not skip;skip=True;continue
  if skip and line.strip()=='#endif':skip=False;continue
  if not skip:out.append(line)
 assert not skip
 return '\n'.join(out)
def tokens(s):
 s=re.sub(r'/\*.*?\*/|//[^\n]*','',s,flags=re.S)
 return re.findall(r'\w+|[^\s]',s)
base_h=blob(wt,'53fced4','udp_control_channel.hpp').decode()
new_h=(root/'udp_control_channel.hpp').read_text(encoding='utf-8')
assert tokens(stripped(new_h))==tokens(base_h)
base_cpp=blob(wt,'53fced4','udp_control_channel.cpp').decode()
product_cpp=stripped(new)
# The sole unconditional formatting change from r1 is a braced early-return, semantically identical.
product_cpp=re.sub(r'if \(txQueue_\.empty\(\) \|\| txQueue_\.front\(\)\.seq != packet\.messageSeq\) \{\s*return;\s*\}',
                   'if (txQueue_.empty() || txQueue_.front().seq != packet.messageSeq) return;',product_cpp)
assert tokens(product_cpp)==tokens(base_cpp)
record={'patch_sha256':hashlib.sha256((root/'first_loss_approved.patch').read_bytes()).hexdigest(),'files':{n:hashlib.sha256(s.encode()).hexdigest() for n,s in changed.items()},'product_header_tokens_equal':True,'product_cpp_tokens_equal_after_early_return_brace_normalization':True}
(root/'fixed_source.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
print(json.dumps(record,indent=2))
