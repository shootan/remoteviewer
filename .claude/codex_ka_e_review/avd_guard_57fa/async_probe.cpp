
#include <cstdio>
#include <thread>
#include <chrono>
#include "native_video_client_session.hpp"
#include "connect_op_gate.hpp"
namespace n=remote60::native_poc;
int main(){
 n::ClientSessionController c;n::ConnectOpGate g;std::atomic<bool> entered{false},finish{false};
 n::ClientSessionConnectArgs a;a.host="127.0.0.1";a.videoPort=9;a.controlPort=9;a.requireTcpControl=false;
 a.udpHandshake=[&](n::SocketHandle,const std::atomic<bool>& stop,std::string* err,uint32_t*){
  entered=true;while(!finish.load()&&!stop.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));
  if(err)*err="standby activation refused (test handshake)";return false;
 };
 auto op=g.Next();n::ConnectOpGate::Steps s;s.connect=[&]{return c.Connect(a);};s.generation=[&]{return c.ConnectionGeneration();};s.disconnect=[&]{c.Disconnect();};
 auto gen=g.Adopt(op,s);
 for(int i=0;i<3000&&!entered.load();++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
 auto before=c.Snapshot();finish=true;
 for(int i=0;i<3000&&c.Snapshot().state!=n::ClientSessionState::Error;++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
 auto after=c.Snapshot();bool ok=entered&&gen>0&&g.IsCurrent(op)&&before.state==n::ClientSessionState::Connecting&&after.state==n::ClientSessionState::Error;
 std::printf("Adopt gen=%llu current=%d before=%d after=%d error=%s PASS=%d\n",(unsigned long long)gen,g.IsCurrent(op),(int)before.state,(int)after.state,after.lastError.c_str(),ok);
 c.Disconnect();return ok?0:1;
}
