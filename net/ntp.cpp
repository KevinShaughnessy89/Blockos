#include "ntp.hpp"
#include "udp.hpp"
#include "dns.hpp"
#include <string.h>
extern "C" uint64_t timer_uptime_ms();
namespace blockos::net::ntp {
namespace {uint64_t g_epoch=0;uint64_t g_uptime_base=0;bool g_sync=false;bool g_done=false;uint64_t g_ts=0;uint64_t g_ticks(){return timer_uptime_ms();}void rx(const UdpDatagram&d){if(d.dst_port!=40000||d.length<48)return;const uint8_t*p=d.data;uint32_t sec=(uint32_t(p[40])<<24)|(uint32_t(p[41])<<16)|(uint32_t(p[42])<<8)|p[43];if(sec<2208988800u)return;g_ts=uint64_t(sec)-2208988800ull;g_done=true;}}
bool sync(const IPv4Address&server,uint64_t&unix_seconds,uint32_t loops){if(!udp_bind(40000,rx))return false;uint8_t p[48]{};p[0]=0x23;g_done=false;g_ts=0;if(!udp_send(server,40000,123,p,sizeof(p)))return false;for(uint32_t i=0;i<loops&&!g_done;i++){}if(!g_done)return false;g_epoch=g_ts;g_uptime_base=g_ticks();g_sync=true;unix_seconds=g_ts;return true;}
bool sync_name(const char*host,uint64_t&unix_seconds,uint32_t loops){uint32_t ip=0;if(!dns::resolve_a(host,ip,loops))return false;return sync({{uint8_t(ip>>24),uint8_t(ip>>16),uint8_t(ip>>8),uint8_t(ip)}},unix_seconds,loops);}
bool synchronized(){return g_sync;}
uint64_t unix_time_seconds(){if(!g_sync)return 0;uint64_t now=g_ticks();return g_epoch+(now-g_uptime_base)/1000;}
}
