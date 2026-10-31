from pathlib import Path
import subprocess,os,json,hashlib

# Reuse the previous independent counterexample verbatim, changing only the pinned candidate and the removed diagnostic.
old=Path(__file__).with_name('c_review_probe.py').read_text(encoding='utf-8')
old=old.replace("rev = '5fe90a93985e2ac3cac922764d523de7d043920e'", "rev = 'd50415e94df845911a7d650c928d3bd23f554250'")
old=old.replace("'c_5fe90_review'", "'c_d504_review'")
needle='printf("ORDER 31st old delta99 after100: admitted=%d resyncs=%llu\\n",rr.admit==ChainAdmit::Admitted,(unsigned long long)r.orderResyncs());'
assert old.count(needle)==1
old=old.replace(needle,'printf("ORDER 31st old delta99 after100: admitted=%d (expected zero)\\n",rr.admit==ChainAdmit::Admitted);')
exec(compile(old,'c_review_probe_for_d504.py','exec'))
assert 'popped=14 key=1' in r.stdout.decode()
assert 'admitted=0 (expected zero)' in r.stdout.decode()
assert 'QUEUED tags: 1 3 4' in r.stdout.decode()

src='apps/native_poc/src/'
for name in ['native_video_client_session.hpp','native_video_client_session.cpp','native_video_client_shared_core.cpp','native_video_client_tcp_control.cpp','udp_control_channel.cpp','android_decoder_inbox_session_test.cpp']:
    (root/name).write_bytes(subprocess.check_output(['git','-C',str(repo),'show',rev+':'+src+name]))
(root/'entry_sink_adapter.hpp').write_bytes(subprocess.check_output(['git','-C',str(repo),'show',rev+':apps/android_direct_client/app/src/main/cpp/entry_sink_adapter.hpp']))

session_args=args[:args.index('probe.cpp')]
session_args[session_args.index('/MD')]='/MT'
session_args+=['/DREMOTE60_CLIENT_SESSION_TEST_HOOKS=1','/I'+str(repo/'libs/common/include')]
session_args+=['android_decoder_inbox_session_test.cpp','native_video_client_session.cpp','native_video_client_shared_core.cpp','native_video_client_tcp_control.cpp','udp_control_channel.cpp','android_video_decoder.cpp','fake_ndk.cpp','/Fe:session_r2.exe','/link','/LIBPATH:'+str(ms/'lib/x64'),'/LIBPATH:'+str(sdk/'Lib/10.0.26100.0/ucrt/x64'),'/LIBPATH:'+str(sdk/'Lib/10.0.26100.0/um/x64'),str(repo/'build/libs/common/Release/common.lib'),'ws2_32.lib','winhttp.lib','user32.lib','advapi32.lib','ole32.lib']
r=subprocess.run(session_args,cwd=root,env=env,capture_output=True)
(root/'session_build.log').write_bytes(r.stdout+r.stderr);print('session build',r.returncode,flush=True)
if r.returncode:print((r.stdout+r.stderr).decode(errors='replace')[-2500:]);raise SystemExit(r.returncode)
r=subprocess.run([str(root/'session_r2.exe')],cwd=root,env=env,capture_output=True,timeout=90,creationflags=subprocess.CREATE_NO_WINDOW)
(root/'session_r2.log').write_bytes(r.stdout+r.stderr)
print('session rc',r.returncode)
for line in r.stdout.decode(errors='replace').splitlines():
    if line.startswith(('PASS','FAIL','RESULT','ALL PASS')) or 'checks' in line:print(line)
assert r.returncode==0
names=['probe.cpp','probe.log','probe.exe','session_r2.exe','session_r2.log','android_video_decoder.cpp','android_video_decoder.hpp','decoder_chain_inbox.hpp','native_video_client_session.cpp','native_video_client_session.hpp']
hashes={n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in names}
(root/'SHA256SUMS.json').write_text(json.dumps(hashes,indent=2)+'\n',encoding='utf-8')
