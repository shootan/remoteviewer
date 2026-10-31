#include <cstdio>
#include "os_input_record_match.hpp"
namespace e2e=remote60::native_poc::e2e;
uint64_t infer(const std::string& input){
    uint64_t epoch = 0;
    std::string epochFrom;
    {
      const std::string out = input;
      const size_t at = out.rfind("[native-video-host][control] session epoch=");
      size_t connected = 0;
      for (size_t i = out.find("[native-video-host] client connected transport="); i != std::string::npos;
           i = out.find("[native-video-host] client connected transport=", i + 1)) {
        ++connected;
      }
      if (at != std::string::npos) {
        epoch = e2e::field_u64(out.substr(at), "session epoch=");
        epochFrom = "a later session's [control] session epoch= line";
      } else if (connected == 1 && out.find("[native-video-host][standby] first session by activation") == std::string::npos) {
        epoch = 1;
        epochFrom = "the first session (one 'client connected', no epoch line): the initial epoch 1";
      }
    }

return epoch;
}
bool normal(const std::string& out,unsigned long code){return !(out.find("unsupported codec") != std::string::npos || out.find("FATAL:") != std::string::npos ||
      out.find("[native-video-host] waiting client bindPort=") == std::string::npos ||
      !(code == 0xFFFFFFFFu || code == 0));}

int main(){int n=0,bad=0;auto ck=[&](const char* name,bool ok){++n;bad+=!ok;std::printf("%s %s\n",ok?"PASS":"FAIL",name);};
 std::string first="[native-video-host] client connected transport=udp\n";
 ck("first ordinary -> 1",infer(first)==1);
 ck("no metadata -> invalid",infer("")==0);
 ck("two first-connected lines -> invalid",infer(first+first)==0);
 ck("standby first -> invalid",infer(first+"[native-video-host][standby] first session by activation\n")==0);
 ck("later authoritative epoch -> 2",infer(first+"[native-video-host][control] session epoch=2 peer=new\n")==2);
 ck("explicit zero does not fall back -> invalid",infer(first+"[native-video-host][control] session epoch=0\n")==0);
 ck("unreadable explicit epoch -> invalid",infer(first+"[native-video-host][control] session epoch=bad\n")==0);
 ck("latest explicit epoch",infer(first+"[native-video-host][control] session epoch=2\n[native-video-host][control] session epoch=3\n")==3);
 std::string ready="[native-video-host] waiting client bindPort=12345\n";
 ck("normal ready / code 0",normal(ready,0));ck("normal ready / deadline sentinel",normal(ready,0xffffffffu));
 ck("codec rejection -> invalid",!normal(ready+"unsupported codec: h264\n",11));
 ck("fatal -> invalid",!normal(ready+"FATAL:\n",0));
 ck("no startup ready -> invalid",!normal("",0));ck("unexpected exit -> invalid",!normal(ready,11));
 std::printf("%d checks, %d failed\n",n,bad);return bad?1:0;}
