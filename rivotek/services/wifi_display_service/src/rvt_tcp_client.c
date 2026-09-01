#include "rvt_wifi_display_osal.h"

#if !RVT_TCP_ROLE_CLIENT
#error "rvt_tcp_client.c must only be built when RVT_TCP_ROLE_CLIENT is 1"
#endif

#define RVT_TCP_CHANNEL_IMPL_ROLE_INCLUDED 1
#include "rvt_tcp_channel_impl.c"
