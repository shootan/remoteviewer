
#include "udp_control_channel.hpp"
#include <type_traits>
template<class T,class=void>struct Has:std::false_type{};
template<class T>struct Has<T,std::void_t<decltype(&T::CloseDiagLine)>>:std::true_type{};
#ifdef REMOTE60_CONTROL_CLOSE_DIAG
static_assert(Has<remote60::native_poc::UdpControlChannel>::value);
#else
static_assert(!Has<remote60::native_poc::UdpControlChannel>::value);
#endif
