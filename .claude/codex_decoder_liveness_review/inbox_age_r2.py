from pathlib import Path
import ast,subprocess
base=Path(__file__).parent
s=(base/'inbox_age_build.py').read_text(encoding='utf-8')
s=s.replace("'inbox_8868_age'","'inbox_79b_age'").replace("'8868fb50bcd1489fa276b357e4393b05c1251f66'","'79b5c25744252f20a482fe6d6ddf51e38c99f85e'")
t=ast.parse(s)
arg=next(n.args[0] for n in ast.walk(t) if isinstance(n,ast.Call) and isinstance(n.func,ast.Attribute)
         and n.func.attr=='write_text' and n.args and isinstance(n.args[0],ast.Constant)
         and isinstance(n.args[0].value,str) and 'int main()' in n.args[0].value)
cpp=arg.value.replace('sink.OnEncodedH264Frame(f(303,0xA1));','auto key=f(303,0xA1); key.header.seq=1; sink.OnEncodedH264Frame(std::move(key));')
cpp=cpp.replace('delta.header.flags=0;','delta.header.seq=2; delta.header.flags=0;')
lines=s.splitlines(keepends=True)
a=sum(len(x) for x in lines[:arg.lineno-1])+arg.col_offset
b=sum(len(x) for x in lines[:arg.end_lineno-1])+arg.end_col_offset
s=s[:a]+repr(cpp)+s[b:]
out=base/'inbox_age_r2_build.py';out.write_text(s,encoding='utf-8')
subprocess.run(['python',str(out)],check=True)
