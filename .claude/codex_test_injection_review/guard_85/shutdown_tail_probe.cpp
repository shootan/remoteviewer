
#include <cstdio>
#include <mutex>
#include "e2e_injection_guard.hpp"
namespace e=remote60::native_poc::e2e;
int main(){
 std::mutex mu;
 std::string log=e::e2e_power_off_line()+"\n"+e::e2e_injection_off_line()+"\n";
 auto get=[&]{std::lock_guard<std::mutex> l(mu);return log;};
 e::E2eInjectionMonitor mon(get,nullptr,"review-shutdown-tail");
 mon.Stop(); // exact current caller order: monitor stops while producer/reader can still add output
 {
  std::lock_guard<std::mutex> l(mu);
  remote60::native_poc::HostPowerPolicy on;
  log+=remote60::native_poc::host_power_policy_line(on)+"\n";
 }
 const int live=e::e2e_injection_final_check_all();
 const bool bad=e::e2e_injection_contradicted(get());
 std::printf("REVIEW final_live=%d complete_output_contradiction=%d shutdown_tail_missed=%d\n",live,bad,live==0&&bad);
 std::puts("REVIEW Host_Viewer_processes_network_input_power_APIs=0 (monitor thread only)");
 return live==0&&bad?0:1; // 0 = counterexample confirmed
}
