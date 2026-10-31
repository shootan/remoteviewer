#define NOMINMAX
#include <windows.h>
#include <cstdio>
static int executionRequests=0;
EXECUTION_STATE spy_execution(EXECUTION_STATE){++executionRequests;return ES_CONTINUOUS;}
#define REMOTE60_OS_INPUT_RECORD_ONLY 1
#define SetThreadExecutionState spy_execution
#include "host_log_d9.hpp"
#undef SetThreadExecutionState
int main(){
  static_assert(remote60::native_poc::os_input::kRecordOnly);
  int ctor=0;
  {remote60::native_poc::HostPowerKeepalive p;ctor=executionRequests;p.SetStreaming(true,true);}
  std::printf("REVIEW record_input_build=1 ctor_power_requests=%d total_power_requests=%d actual_input_power_calls=0\n",ctor,executionRequests);
  return ctor==1&&executionRequests==3?0:1;
}
