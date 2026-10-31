#include <cstdio>
#include <cstdlib>
#include "host_args.hpp"
int main(int argc,char** argv) {
  // Child-process environment only. No host startup, socket, UI or input injection is executed.
  _putenv_s("REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE","1");
  const auto a=remote60::native_poc::parse_args(argc,argv);
  const char* force=std::getenv("REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE");
  std::printf("PARSE codec=%s transport=%s enableInputInjection=%d mode=%s forceEncoded=%s\n",
      a.codec.c_str(),a.transport.c_str(),a.enableInputInjection,a.inputInjectionMode.c_str(),
      force&&*force?force:"<unset>");
  return 0;
}
