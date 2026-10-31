from pathlib import Path
import subprocess,os

base=Path(__file__).with_name('c_r2_review.py').read_text(encoding='utf-8')
base=base.replace('d50415e94df845911a7d650c928d3bd23f554250','2b492ef92b899394f08f4b6fcc46fdd33309baea').replace('c_d504_review','c_2b492_review')
exec(compile(base,'r2-counterexamples-on-r3','exec'))

guard=r'''
#include "native_video_client_session.hpp"
#include <type_traits>
using remote60::native_poc::ClientSessionController;
template<class T,class=void> struct Publish:std::false_type{};
template<class T> struct Publish<T,std::void_t<decltype(&T::TestPublishKey)>>:std::true_type{};
template<class T,class=void> struct Request:std::false_type{};
template<class T> struct Request<T,std::void_t<decltype(&T::TestRequestKey)>>:std::true_type{};
template<class T,class=void> struct Configure:std::false_type{};
template<class T> struct Configure<T,std::void_t<decltype(&T::TestConfigureKeyframeLimiter)>>:std::true_type{};
#ifdef EXPECT_HOOKS
static_assert(Publish<ClientSessionController>::value&&Request<ClientSessionController>::value&&Configure<ClientSessionController>::value);
#else
static_assert(!Publish<ClientSessionController>::value&&!Request<ClientSessionController>::value&&!Configure<ClientSessionController>::value);
#endif
'''
(root/'guard_probe.cpp').write_text(guard,encoding='utf-8')
common_args=session_args[:session_args.index('android_decoder_inbox_session_test.cpp')]
common_args.remove('/DREMOTE60_CLIENT_SESSION_TEST_HOOKS=1')
for mode in ['production','test']:
    cmd=common_args+(['/DREMOTE60_CLIENT_SESSION_TEST_HOOKS=1','/DEXPECT_HOOKS=1'] if mode=='test' else [])+['/c','guard_probe.cpp','/Fo:guard_'+mode+'.obj']
    rr=subprocess.run(cmd,cwd=root,env=env,capture_output=True)
    (root/('guard_'+mode+'.log')).write_bytes(rr.stdout+rr.stderr)
    print('compile guard',mode,rr.returncode,flush=True)
    assert rr.returncode==0
