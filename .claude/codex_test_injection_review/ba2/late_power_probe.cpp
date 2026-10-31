
#include <cstdio>
#include "e2e_injection_guard.hpp"
namespace e=remote60::native_poc::e2e;
int main(){
 const std::string input="input injection disabled (unsupported mode) mode=none\n";
 remote60::native_poc::HostPowerPolicy on;
 const std::string before=e::e2e_power_off_line()+"\n"+input;
 const std::string after=before+remote60::native_poc::host_power_policy_line(on)+"\n";
 std::string why;
 int a=e::e2e_injection_state(before,&why),b=e::e2e_injection_state(after,&why);
 bool late=e::e2e_injection_contradicted(after);
 std::printf("REVIEW initial_OFF=%d full_state_after_ON=%d post_run_contradicted=%d\n",a,b,late);
 return a==1&&b==-1&&!late?0:1;
}
