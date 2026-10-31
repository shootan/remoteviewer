
#include "native_video_client_session.hpp"
#include <type_traits>
using remote60::native_poc::ClientSessionController;
template<class T,class=void> struct Publish:std::false_type{};
template<class T> struct Publish<T,std::void_t<decltype(&T::TestPublishKey)>>:std::true_type{};
template<class T,class=void> struct Request:std::false_type{};
template<class T> struct Request<T,std::void_t<decltype(&T::TestRequestKey)>>:std::true_type{};
template<class T,class=void> struct Configure:std::false_type{};
template<class T> struct Configure<T,std::void_t<decltype(&T::TestConfigureKeyframeLimiter)>>:std::true_type{};
#ifdef EXPECT_HOOKS
static_assert(Publish<ClientSessionController>::value&&Request<ClientSessionController>::value&&Configure<ClientSessionController>::value);
#else
static_assert(!Publish<ClientSessionController>::value&&!Request<ClientSessionController>::value&&!Configure<ClientSessionController>::value);
#endif
