#include "nvme.hpp"
#include "pci.hpp"
#include <stdint.h>
#include <stddef.h>

namespace nvme {
namespace {
struct Cmd{uint32_t cdw0,nsid;uint64_t rsv2[2];uint64_t mptr,prp1,prp2;uint32_t cdw10,cdw11,cdw12,cdw13,cdw14,cdw15;}__attribute__((packed,aligned(4)));
struct Cpl{uint32_t dw0,dw1,dw2,dw3;}__attribute__((packed));
static volatile uint8_t* bar=nullptr; static uint64_t cap=0; static uint32_t db_stride=4; static bool ready=false; static uint16_t sq_tail=0, cq_head=0, cid=1; static uint8_t phase=1;
alignas(4096) static Cmd asq[64],ioq[128]; alignas(4096) static Cpl acq[64],ioq_cq[128]; alignas(4096) static uint8_t identify_buf[4096];
static void wr32(uint32_t off,uint32_t v){*reinterpret_cast<volatile uint32_t*>(bar+off)=v;} static uint32_t rd32(uint32_t off){return *reinterpret_cast<volatile uint32_t*>(bar+off);} static uint64_t rd64(uint32_t off){return *reinterpret_cast<volatile uint64_t*>(bar+off);} static void wait(){for(volatile uint32_t i=0;i<1000000;++i)asm volatile("pause");}
static bool find(){for(uint16_t b=0;b<256;++b)for(uint8_t s=0;s<32;++s)for(uint8_t f=0;f<8;++f){if(pci_cfg_read16((uint8_t)b,s,f,0)==0xFFFF)continue;if(pci_cfg_read8((uint8_t)b,s,f,0x0B)==1&&pci_cfg_read8((uint8_t)b,s,f,0x0A)==8&&pci_cfg_read8((uint8_t)b,s,f,0x09)==2){uint64_t x=pci_read_bar((uint8_t)b,s,f,0);if(x){bar=reinterpret_cast<volatile uint8_t*>(static_cast<uintptr_t>(x));return true;}}}return false;}
static bool submit(Cmd& c,bool io){uint16_t qid=io?1:0;Cmd*sq=io?ioq:asq;Cpl*cq=io?ioq_cq:acq;uint16_t qsz=io?128:64;uint16_t id=cid++;c.cdw0=(uint32_t(id)<<16)|(c.cdw0&0xFFFFu);sq[sq_tail]=c;sq_tail=(sq_tail+1)%qsz;uint32_t off=0x1000u+db_stride*(2u*qid);wr32(off,sq_tail);for(uint32_t t=0;t<2000000;++t){Cpl cp=cq[cq_head];uint16_t status=(uint16_t)(cp.dw3>>16);uint8_t ph=status&1u;if((status&1u)==phase && (uint16_t)(cp.dw3>>16)!=0){uint16_t sc=(status>>1)&0xFF;uint16_t sct=(status>>9)&7; cq_head=(cq_head+1)%qsz;if(cq_head==0)phase^=1;wr32(off+db_stride,cq_head);return sc==0&&sct==0;}wait();}return false;}
static bool setup(){cap=rd64(0);db_stride=4u<<((cap>>32)&0xFu);uint32_t cc=rd32(0x14);cc&=~1u;wr32(0x14,cc);for(uint32_t i=0;i<100000&& (rd32(0x1C)&1u);++i)wait();wr32(0x24,(63u<<16)|63u);*reinterpret_cast<volatile uint64_t*>(bar+0x28)=(uint64_t)(uintptr_t)&asq;*reinterpret_cast<volatile uint64_t*>(bar+0x30)=(uint64_t)(uintptr_t)&acq;cc=1u|(6u<<16)|(4u<<20);wr32(0x14,cc);for(uint32_t i=0;i<1000000;++i){if(rd32(0x1C)&1u)return true;wait();}return false;}
static bool create_io(){Cmd c{};c.cdw0=0x05;c.prp1=(uint64_t)(uintptr_t)&ioq_cq;c.cdw10=((127u)<<16)|1u;c.cdw11=1u; if(!submit(c,false))return false;c={};c.cdw0=0x01;c.prp1=(uint64_t)(uintptr_t)&ioq;c.cdw10=((127u)<<16)|1u;c.cdw11=1u;return submit(c,false);}
static bool identify_ctrl(){Cmd c{};c.cdw0=0x06;c.prp1=(uint64_t)(uintptr_t)&identify_buf;c.cdw10=1;return submit(c,false);}
}
bool init(){if(ready)return true;if(!find()||!setup())return false;if(!identify_ctrl())return false;if(!create_io())return false;ready=true;return true;}
bool available(){return ready;}
bool identify(uint8_t out_model[40],uint32_t& nsc){if(!ready||!out_model)return false;for(int i=0;i<40;++i)out_model[i]=identify_buf[24+i];nsc=*reinterpret_cast<uint32_t*>(identify_buf+516);return true;}
static bool rw(bool wr,uint32_t nsid,uint64_t lba,uint16_t blocks,const void*buf,size_t bs){if(!ready||!buf||!blocks||bs==0)return false;Cmd c{};c.cdw0=wr?1u:2u;c.nsid=nsid;c.prp1=(uint64_t)(uintptr_t)buf;c.cdw10=(uint32_t)lba;c.cdw11=(uint32_t)(lba>>32);c.cdw12=(uint32_t(blocks-1)&0xFFFFu);return submit(c,true);} 
bool read(uint32_t n,uint64_t l,uint16_t b,void*x,size_t s){return rw(false,n,l,b,x,s);} bool write(uint32_t n,uint64_t l,uint16_t b,const void*x,size_t s){return rw(true,n,l,b,x,s);} }
