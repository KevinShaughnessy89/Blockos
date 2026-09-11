#include "ufs.hpp"
#include "../drivers/virtio_blk.hpp"
#include <string.h>

namespace ufs {
namespace {
static bool g_init=false; static int g_disk=0; static Version g_ver=Version::NONE;
static uint32_t g_bsize=0; static uint64_t g_size=0, g_iblkno=0, g_ipg=0, g_cgsize=0;
static uint32_t g_inopb=0, g_ncg=0; static uint64_t g_sblock=0;

static uint32_t le32(const uint8_t*p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);} 
static uint64_t le64(const uint8_t*p){return (uint64_t)le32(p)|((uint64_t)le32(p+4)<<32);} 
static uint16_t le16(const uint8_t*p){return (uint16_t)p[0]|((uint16_t)p[1]<<8);} 
static uint64_t be64(const uint8_t*p){uint64_t v=0;for(int i=0;i<8;i++)v=(v<<8)|p[i];return v;}
static uint32_t be32(const uint8_t*p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static uint16_t be16(const uint8_t*p){return (uint16_t)(((uint16_t)p[0]<<8)|p[1]);}
static bool zero(const uint8_t*p,size_t n){for(size_t i=0;i<n;i++)if(p[i])return false;return true;}
static bool sector(uint64_t s,uint8_t*b){return virtio_blk::read_sector(s,b);}
static bool bytes_raw(uint64_t off,uint8_t*out,size_t n){
 uint8_t sec[512]; size_t done=0; while(done<n){uint64_t s=(off+done)/512;size_t so=(size_t)((off+done)%512);size_t take=512-so;if(take>n-done)take=n-done;if(!sector(s,sec))return false;memcpy(out+done,sec+so,take);done+=take;}return true;
}
static bool block(uint64_t b,uint8_t*out){return bytes_raw(b*(uint64_t)g_bsize,out,g_bsize);}
static bool super_at(uint64_t off,uint8_t*s){return bytes_raw(off,s,4096);}
static uint32_t magic_at(const uint8_t*s){return le32(s+1372);}
static bool parse_super(const uint8_t*s){
 uint32_t m=le32(s+1372); uint32_t mbe=be32(s+1372);
 bool u2=(m==0x19540119||mbe==0x19540119); bool u1=(m==0x00011954||mbe==0x00011954);
 if(!u1&&!u2)return false; g_ver=u2?Version::UFS2:Version::UFS1;
 // UFS superblock fields are historically native-endian. Prefer little endian, then big endian.
 uint32_t bs=le32(s+48), fs=le32(s+84), ipg=le32(s+120), ncg=le32(s+44), cgs=le32(s+92), ib=le32(s+56);
 if(bs<512||bs>65536||(bs&(bs-1))||ipg==0||ncg==0||fs==0){bs=be32(s+48);fs=be32(s+84);ipg=be32(s+120);ncg=be32(s+44);cgs=be32(s+92);ib=be32(s+56);} 
 if(bs<512||bs>65536||(bs&(bs-1))||ipg==0||ncg==0)return false;
 g_bsize=bs;g_size=fs;g_ipg=ipg;g_ncg=ncg;g_cgsize=cgs;g_iblkno=ib;g_inopb=bs/((g_ver==Version::UFS2)?512:128); return true;
}
static uint64_t cg_base(uint64_t cg){return cg*(g_cgsize?g_cgsize:g_bsize);}
static bool read_cg(uint64_t cg,uint8_t*out){return block(cg_base(cg)/g_bsize,out);}
static bool inode_location(uint64_t ino,uint64_t*off){
 if(ino<2||ino>=g_ipg*g_ncg)return false; uint64_t cgno=(ino-2)/g_ipg; uint64_t local=(ino-2)%g_ipg;
 // UFS cylinder groups place the inode table at cg inode block + cg base. cg_initediblk/cg_iblkoff vary, so use cg's classic cg_irotor/cg_initediblk fallback.
 uint8_t cgraw[2048]; if(!bytes_raw(cg_base(cgno)*1,cgraw,sizeof(cgraw))) return false;
 uint32_t magic=le32(cgraw); if(magic!=0x090255&&magic!=0x00090255){uint32_t bm=be32(cgraw);if(bm!=0x090255)return false;}
 // cg_irotor is not inode table start. The portable on-disk formula uses fs_ipg and cg_iblkoff only in UFS2.
 uint64_t table;
 if(g_ver==Version::UFS2){
   // UFS2 cg layout: cg_initediblk at offset 160, cg_iblkoff at 184 in common layout.
   uint32_t iboff=le32(cgraw+184); if(iboff==0||iboff>g_cgsize) iboff=be32(cgraw+184); table=cg_base(cgno)+iboff;
 } else {
   table=cg_base(cgno)+g_iblkno*g_bsize;
 }
 *off=table + local*(g_ver==Version::UFS2?512:128); return true;
}
static bool inode_raw(uint64_t ino,uint8_t*raw){uint64_t o;if(!inode_location(ino,&o))return false;return bytes_raw(o,raw,g_ver==Version::UFS2?512:128);}
static uint16_t mode_raw(const uint8_t*r){return le16(r);}
static uint64_t inode_size(const uint8_t*r){if(g_ver==Version::UFS2)return le64(r+16);return (uint64_t)le32(r+8)|((uint64_t)le32(r+12)<<32);}
static uint32_t uid_raw(const uint8_t*r){return g_ver==Version::UFS2?le32(r+4):le16(r+4);}
static uint32_t gid_raw(const uint8_t*r){return g_ver==Version::UFS2?le32(r+8):le16(r+20);}
static uint64_t direct_ptr(const uint8_t*r,int i){return g_ver==Version::UFS2?le64(r+112+i*8):le32(r+40+i*4);}
static uint64_t indir_ptr(const uint8_t*r,int i){return g_ver==Version::UFS2?le64(r+112+12*8+i*8):le32(r+40+12*4+i*4);}
static bool data_block(const uint8_t*r,uint64_t lbn,uint8_t*out){
 uint64_t ppb=g_bsize/((g_ver==Version::UFS2)?8:4); if(lbn<12){uint64_t p=direct_ptr(r,(int)lbn);if(!p)return false;return block(p,out);}
 lbn-=12; uint64_t ip=0,span=ppb;
 if(lbn<span){ip=indir_ptr(r,0);}
 else if((lbn-=span)<span*ppb){ip=indir_ptr(r,1);span*=ppb;}
 else {lbn-=span*ppb; if(lbn>=span*ppb)return false;ip=indir_ptr(r,2);span*=ppb;}
 if(!ip)return false; uint8_t*tab=new uint8_t[g_bsize]; if(!block(ip,tab)){delete[]tab;return false;}
 while(span>1){uint64_t idx=lbn/span; if(idx>=ppb){delete[]tab;return false;} uint64_t p=g_ver==Version::UFS2?le64(tab+idx*8):le32(tab+idx*4); if(!p){delete[]tab;return false;} delete[]tab; tab=new uint8_t[g_bsize];if(!block(p,tab)){delete[]tab;return false;}lbn%=span;span/=ppb;}
 uint64_t idx=lbn;uint64_t p=g_ver==Version::UFS2?le64(tab+idx*8):le32(tab+idx*4);delete[]tab;if(!p)return false;return block(p,out);
}
static bool dir_read(uint64_t ino,uint64_t pos,uint8_t*out,size_t n){return read_inode_data(ino,pos,out,n);}
}

bool init(int disk_id){g_init=false;g_disk=disk_id;uint8_t s[4096]; if(!virtio_blk::init()) return false; const uint64_t candidates[]={8192,65536}; for(size_t i=0;i<2;i++){if(super_at(candidates[i],s)&&parse_super(s)){g_sblock=candidates[i];g_init=true;return true;}}return false;}
bool is_initialized(){return g_init;} Version version(){return g_ver;} uint32_t block_size(){return g_bsize;} uint64_t total_blocks(){return g_size;}
bool read_bytes(uint64_t off,uint8_t*out,size_t n){return g_init&&out&&(n==0||bytes_raw(off,out,n));}
bool read_inode(uint64_t ino,InodeInfo*out){if(!g_init||!out)return false;uint8_t r[512];if(!inode_raw(ino,r))return false;out->inode=ino;out->mode=mode_raw(r);out->size=inode_size(r);out->uid=uid_raw(r);out->gid=gid_raw(r);out->blocks=g_ver==Version::UFS2?le64(r+104):((uint64_t)le32(r+28))*512;out->atime=g_ver==Version::UFS2?le64(r+32):le32(r+24);out->mtime=g_ver==Version::UFS2?le64(r+48):le32(r+32);out->ctime=g_ver==Version::UFS2?le64(r+64):le32(r+40);out->directory=(out->mode&0170000)==0040000;return true;}
bool read_inode_data(uint64_t ino,uint64_t off,uint8_t*out,size_t n){if(!g_init||!out)return false;uint8_t r[512];if(!inode_raw(ino,r))return false;uint64_t sz=inode_size(r);if(off>sz)return false;if(n>sz-off)n=(size_t)(sz-off);size_t done=0;uint8_t*buf=new uint8_t[g_bsize];while(done<n){uint64_t p=off+done;uint64_t lb=p/g_bsize;size_t bo=(size_t)(p%g_bsize),take=g_bsize-bo;if(take>n-done)take=n-done;if(!data_block(r,lb,buf)){delete[]buf;return false;}memcpy(out+done,buf+bo,take);done+=take;}delete[]buf;return true;}
bool directory_entry(uint64_t ino,size_t index,char*name,size_t ns,uint64_t*outino,bool*isd){if(!name||!ns||!outino)return false;InodeInfo ii;if(!read_inode(ino,&ii)||!ii.directory)return false;uint8_t h[8];uint64_t pos=0;size_t cur=0;while(pos+8<=ii.size){if(!dir_read(ino,pos,h,8))return false;uint64_t dino=(g_ver==Version::UFS2?le64(h):le32(h));uint16_t reclen=le16(h+(g_ver==Version::UFS2?8:4));uint8_t namlen;if(g_ver==Version::UFS2){if(reclen<8||reclen>ii.size-pos)break;uint8_t x[4];if(!dir_read(ino,pos+8,x,4))return false;namlen=x[0];}else{if(reclen<8||reclen>ii.size-pos)break;namlen=h[6];} if(dino&&namlen&&cur++==index){size_t take=namlen<ns-1?namlen:ns-1;if(!dir_read(ino,pos+(g_ver==Version::UFS2?12:8),(uint8_t*)name,take))return false;name[take]=0;*outino=dino;if(isd){InodeInfo di;*isd=read_inode(dino,&di)&&di.directory;}return true;}pos+=reclen;}return false;}
bool find_inode(const char*path,uint64_t*out){if(!path||!out||path[0]!='/')return false;if(path[1]==0){*out=2;return true;}uint64_t cur=2;char comp[256];size_t i=1;while(path[i]){while(path[i]=='/')i++;if(!path[i])break;size_t n=0;while(path[i]&&path[i]!='/'&&n<sizeof(comp)-1)comp[n++]=path[i++];comp[n]=0;bool found=false;for(size_t k=0;k<4096;k++){char nm[256];uint64_t ino;bool dir;if(!directory_entry(cur,k,nm,sizeof(nm),&ino,&dir))break;if(strcmp(nm,comp)==0){cur=ino;found=true;break;}}if(!found)return false;}*out=cur;return true;}
bool read_file(const char*path,uint8_t*out,size_t max,size_t*outsz){if(outsz)*outsz=0;uint64_t ino;if(!find_inode(path,&ino))return false;InodeInfo i;if(!read_inode(ino,&i)||i.directory)return false;size_t n=(i.size<max)?(size_t)i.size:max;if(!read_inode_data(ino,0,out,n))return false;if(outsz)*outsz=n;return true;}
}
