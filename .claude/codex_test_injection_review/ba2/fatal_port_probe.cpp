
#include <cstdio>
#include "host_args.hpp"
int main(){
 char self[]="host",addr[]="--bind-address",loop[]="127.0.0.1",port[]="--bind-port",zero[]="0",power[]="--host-power-management",off[]="off";
 char* a[]={self,addr,loop,port,zero,power,off};
 auto args=remote60::native_poc::parse_args(7,a);
 std::printf("REVIEW failed_free_port_zero actual_bind_port=%u candidates=%zu power_off=%d\n",args.bindPort,args.bindPortCandidates.size(),!args.hostPower.enabled);
 return args.bindPort==43000&&args.bindPortCandidates.empty()?0:1;
}
