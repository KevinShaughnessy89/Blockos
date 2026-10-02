#include "ahci.hpp"
#include "pci.hpp"
#include <stdint.h>
#include <stddef.h>

namespace ahci {
namespace {
struct HbaPort { volatile uint32_t clb, clbu, fb, fbu, is, ie, cmd, rsv0, tfd, sig, ssts, sctl, serr, sact, ci, sntf, fbs, devslp; uint32_t rsv1[10]; uint32_t vendor[4]; };
struct Hba { volatile uint32_t cap, ghc, is, pi, vs, ccc_ctl, ccc_pts, em_loc, em_ctl, cap2, bohc; uint8_t rsv[0xA0-0x2C]; uint8_t ports_raw[32*0x80]; };
struct CmdHeader { uint8_t cfl,pmp,rw,prdtl; uint32_t prdbc; uint64_t ctba; uint64_t rsv[2]; } __attribute__((packed));
struct Prdt { uint64_t dba; uint32_t rsv; uint32_t dbc_i; } __attribute__((packed));
struct CmdTable { uint8_t cfis[64]; uint8_t acmd[16]; uint8_t rsv[48]; Prdt prdt[8]; } __attribute__((packed,aligned(128)));
static Hba* hba=nullptr; static bool ready=false; static PortInfo info[32]{};
static CmdHeader headers[32][32] __attribute__((aligned(1024)));
static CmdTable tables[32][32] __attribute__((aligned(128)));
static void delay() { for(volatile uint32_t i=0;i<2000;++i) asm volatile("pause"); }
static bool wait(volatile HbaPort* p){for(uint32_t i=0;i<1000000;++i){uint32_t t=p->tfd;if((t&0x88)==0)return true;delay();}return false;}
static bool scan(){for(uint16_t b=0;b<256;++b)for(uint8_t s=0;s<32;++s)for(uint8_t f=0;f<8;++f){if(pci_cfg_read16((uint8_t)b,s,f,0)==0xFFFF)continue;uint8_t cls=pci_cfg_read8((uint8_t)b,s,f,0x0B), sub=pci_cfg_read8((uint8_t)b,s,f,0x0A), pi=pci_cfg_read8((uint8_t)b,s,f,0x09);if(cls==1&&sub==6&&pi==1){uint64_t bar=pci_read_bar((uint8_t)b,s,f,5);if(bar){hba=reinterpret_cast<Hba*>(static_cast<uintptr_t>(bar));return true;}}}return false;}
static bool prepare(uint8_t port){if(!hba||port>=32||!(hba->pi&(1u<<port)))return false;volatile HbaPort* p=reinterpret_cast<volatile HbaPort*>(reinterpret_cast<uint8_t*>(hba)+0x100+port*0x80);p->cmd&=~1u; p->cmd&=~(1u<<4); uint64_t clb=(uint64_t)(uintptr_t)&headers[port][0];p->clb=(uint32_t)clb;p->clbu=(uint32_t)(clb>>32);for(unsigned i=0;i<32;++i){headers[port][i]={};headers[port][i].ctba=(uint64_t)(uintptr_t)&tables[port][i];}uint64_t fb=(uint64_t)(uintptr_t)&tables[port][0].cfis; p->fb=(uint32_t)fb;p->fbu=(uint32_t)(fb>>32);p->serr=0xFFFFFFFFu;p->is=0xFFFFFFFFu;p->cmd|=(1u<<4)|1u;return wait(p);}
static bool xfer(uint8_t port,uint64_t lba,uint16_t n,void* buf,bool writeop){if(!ready||!buf||!n||port>=32||!info[port].disk)return false;volatile HbaPort*p=reinterpret_cast<volatile HbaPort*>(reinterpret_cast<uint8_t*>(hba)+0x100+port*0x80);if(!prepare(port))return false;unsigned slot=0;while(slot<32&&(p->sact&(1u<<slot)))++slot;if(slot>=32)return false;CmdHeader&ch=headers[port][slot];CmdTable&ct=tables[port][slot];for(size_t i=0;i<sizeof(ct);++i)reinterpret_cast<uint8_t*>(&ct)[i]=0;ch.cfl=5;ch.rw=writeop?1:0;ch.prdtl=(uint8_t)(((uint32_t(n)*512)+0x3FFFFF)/0x400000);if(ch.prdtl>8)return false;for(uint8_t i=0;i<ch.prdtl;++i){uint32_t bytes=(uint32_t(n)*512>i*0x400000)?((uint32_t(n)*512-i*0x400000>0x400000)?0x400000:uint32_t(n)*512-i*0x400000):0;ct.prdt[i].dba=(uint64_t)(uintptr_t)((uint8_t*)buf+i*0x400000);ct.prdt[i].dbc_i=(bytes?bytes:1)-1;}uint8_t*f=ct.cfis;f[0]=0x27;f[1]=0x80;f[2]=writeop?0x35:0x25;f[7]=0x40;for(int i=0;i<6;++i)f[4+i]=(uint8_t)(lba>>(8*i));f[12]=n&0xFF;f[13]=(n>>8)&0xFF; p->is=0xFFFFFFFFu;p->ci|=(1u<<slot);for(uint32_t t=0;t<2000000;++t){if(!(p->ci&(1u<<slot))){return (p->is&(1u<<30|1u<<19))==0;} if(p->is&(1u<<30))break;delay();}return false;}
}
bool init(){if(ready)return true;if(!scan())return false;uint32_t pi=hba->pi;for(uint8_t p=0;p<32;++p)if(pi&(1u<<p)){volatile HbaPort*hp=reinterpret_cast<volatile HbaPort*>(reinterpret_cast<uint8_t*>(hba)+0x100+p*0x80);uint32_t s=hp->ssts;bool det=(s&0xF)==3,ipm=((s>>8)&0xF)==1;info[p]={reinterpret_cast<uintptr_t>(hp),p,true,det&&ipm&&hp->sig==0x00000101};if(info[p].disk)prepare(p);}ready=true;return true;}
bool available(){return ready;}
size_t ports(PortInfo*out,size_t cap){size_t n=0;for(unsigned i=0;i<32&&n<cap;++i)if(info[i].implemented)out[n++]=info[i];return n;}
bool read(uint8_t p,uint64_t l,uint16_t n,void*b){return xfer(p,l,n,b,false);} bool write(uint8_t p,uint64_t l,uint16_t n,const void*b){return xfer(p,l,n,const_cast<void*>(b),true);}
}
