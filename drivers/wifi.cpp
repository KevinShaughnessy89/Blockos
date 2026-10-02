#include "wifi.hpp"
#include "pci.hpp"
namespace wifi {
namespace { Ops g_ops{}; bool g_have=false; uint16_t g_vendor=0xFFFF,g_device=0xFFFF; bool g_ready=false;
bool scan_pci(){for(uint16_t b=0;b<256&&!g_have;++b)for(uint8_t s=0;s<32&&!g_have;++s)for(uint8_t f=0;f<8;++f){uint16_t v=pci_cfg_read16((uint8_t)b,s,f,0);if(v==0xFFFF)continue;uint8_t cls=pci_cfg_read8((uint8_t)b,s,f,0x0B),sub=pci_cfg_read8((uint8_t)b,s,f,0x0A);if(cls==0x02&&sub==0x80){g_vendor=v;g_device=pci_cfg_read16((uint8_t)b,s,f,2);g_ready=true;return true;}}return false;}}
bool init(){if(g_ready)return true;return scan_pci();} bool available(){return g_ready&&g_ops.associate&&g_ops.send&&g_ops.recv;} bool register_ops(const Ops&o){if(!o.associate||!o.send||!o.recv)return false;g_ops=o;g_ready=true;return true;} bool associate(const char*s,const char*p){return available()&&g_ops.associate(g_ops.ctx,s,p);} bool send(const void*f,size_t n){return available()&&g_ops.send(g_ops.ctx,f,n);} int receive(void*f,size_t n){return available()?g_ops.recv(g_ops.ctx,f,n):-1;} uint16_t vendor_id(){return g_vendor;} uint16_t device_id(){return g_device;}
}
