
#include <cstdio>
#include "host_power_policy.hpp"
#include "e2e_injection_guard.hpp"
int main(){
 char self[]="host",title[]="--capture-window-title",token[]="--host-power-management",off[]="off";
 char* a[]={self,title,token};char* b[]={self,title,token,off};
 auto x=remote60::native_poc::bind_host_power_policy(3,a);auto y=remote60::native_poc::bind_host_power_policy(4,b);
 std::printf("REVIEW power_looking_title_alone ok=%d enabled=%d\n",x.ok,x.enabled);
 std::printf("REVIEW power_looking_title_then_unrelated_off ok=%d enabled=%d from_cli=%d\n",y.ok,y.enabled,y.fromCommandLine);

 remote60::native_poc::HostPowerPolicy op;op.enabled=false;op.fromCommandLine=true;
 remote60::native_poc::HostPowerPolicy on;
 const std::string log=remote60::native_poc::host_power_policy_line(op)+"\ninput injection disabled (unsupported mode) mode=none\n"+remote60::native_poc::host_power_policy_line(on)+"\n";
 std::string why;
 int gate=remote60::native_poc::e2e::e2e_injection_state(log,&why);
 bool later=remote60::native_poc::e2e::e2e_injection_contradicted(log);
 std::printf("REVIEW power_off_then_on gate=%d later_contradiction=%d\n",gate,later);
 return !x.ok&&y.ok&&!y.enabled&&gate==1&&!later?0:1; // 0 = parser token ambiguity reproduced
}
