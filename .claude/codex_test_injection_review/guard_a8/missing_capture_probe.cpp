
#include <cstdio>
#include "e2e_injection_guard.hpp"
namespace e=remote60::native_poc::e2e;
int main(){
 const std::string off=e::e2e_power_off_line()+"\n"+e::e2e_injection_off_line()+"\n";
 bool readFailed=false;
 auto get=[&]{return readFailed?e::e2e_read_text_file(L"D:/remote/remote/.claude/codex_test_injection_review/guard_a8/capture_missing_for_review.log"):off;};
 std::string why;bool initial=e::e2e_wait_injection_off(get,100,&why);
 e::E2eInjectionMonitor mon(get,nullptr,"review-read-status",false);mon.StopPolling();
 readFailed=true; // the caller knows the job ended but its final file read is unsuccessful
 mon.Finalize(true); // exact current caller's outputComplete argument: host/job ended, no read result
 const int all=e::e2e_injection_final_check_all();
 int finalState=e::e2e_injection_state(get(),&why);
 std::printf("REVIEW initial_OFF=%d final_state=%d final_read_bytes=%zu finalized_launches=%d missing_capture_passed=%d\n",initial,finalState,get().size(),all,initial&&finalState==0&&all==1);
 std::puts("REVIEW actual_Host_Viewer_input_power_clipboard_network_APIs=0 (one unsuccessful local file read)");
 return initial&&finalState==0&&all==1?0:1;
}
