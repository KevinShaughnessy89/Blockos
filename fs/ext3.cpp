#include "ext3.hpp"
#include "../drivers/virtio_blk.hpp"

#include <stdint.h>
#include <stddef.h>

namespace {
static uint16_t le16(const uint8_t* p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t le32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t le64(const uint8_t* p) { return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32); }
static void cp(void* d, const void* s, size_t n) { uint8_t* a=(uint8_t*)d; const uint8_t* b=(const uint8_t*)s; for(size_t i=0;i<n;i++) a[i]=b[i]; }
static size_t slen(const char* s) { size_t n=0; if(!s) return 0; while(s[n]) ++n; return n; }
static bool eq(const char* a,const char* b) { if(!a||!b)return false; size_t i=0; while(a[i]||b[i]) {if(a[i]!=b[i])return false;++i;} return true; }
static uint64_t ceil_div(uint64_t a,uint64_t b) { return b ? (a+b-1)/b : 0; }
static uint64_t min64(uint64_t a,uint64_t b) { return a<b?a:b; }

/* ext3/ext2 inode flags. */
static const uint32_t EXT4_EXTENTS_FL = 0x00080000U;
static const uint16_t EXT3_SUPER_MAGIC = 0xEF53;
static const uint32_t EXT3_FEATURE_COMPAT_HAS_JOURNAL = 0x0004U;
static const uint32_t EXT3_FEATURE_INCOMPAT_FILETYPE = 0x0002U;
static const uint32_t EXT3_FEATURE_INCOMPAT_RECOVER = 0x0004U;
static const uint32_t EXT3_FEATURE_INCOMPAT_JOURNAL_DEV = 0x0008U;
static const uint32_t EXT3_FEATURE_RO_COMPAT_SPARSE_SUPER = 0x0001U;

static const uint32_t JBD2_MAGIC = 0xC03B3998U;
static const uint32_t JBD1_MAGIC = 0xC03B3998U;
static const uint32_t JBD_DESCRIPTOR = 1;
static const uint32_t JBD_COMMIT = 2;
static const uint32_t JBD_SUPERBLOCK_V1 = 3;
static const uint32_t JBD_SUPERBLOCK_V2 = 4;
static const uint32_t JBD_REVOKE = 5;
static const uint32_t JBD2_DESCRIPTOR = 1;
static const uint32_t JBD2_COMMIT = 2;
static const uint32_t JBD2_SUPERBLOCK_V1 = 3;
static const uint32_t JBD2_SUPERBLOCK_V2 = 4;
static const uint32_t JBD2_REVOKE = 5;
}

Ext3Reader::Ext3Reader()
    : sb{}, block_size(1024), inode_size(128), group_desc_block(0),
      group_desc_size(32), mounted(false), journal_present(false),
      journal_ino(0), journal_bsize(0), journal_first(0),
      journal_maxlen_value(0), journal_sequence_value(0) {}

bool Ext3Reader::read_bytes(uint64_t offset, void* buffer, size_t size) {
    if (!buffer || !size) return size == 0;
    uint8_t* dst=(uint8_t*)buffer;
    uint64_t sector=offset/512;
    uint32_t in= (uint32_t)(offset%512);
    while(size) {
        uint8_t tmp[512];
        if(!virtio_blk::read_sector(sector,tmp)) return false;
        size_t n=512-in; if(n>size)n=size;
        cp(dst,tmp+in,n); dst+=n; size-=n; ++sector; in=0;
    }
    return true;
}

bool Ext3Reader::read_block(uint64_t block, uint8_t* buffer) {
    if(!buffer || block_size<512 || (block_size%512)) return false;
    return read_bytes(block* (uint64_t)block_size, buffer, block_size);
}

bool Ext3Reader::mount() {
    mounted=false;
    journal_present=false;
    journal_ino=journal_bsize=journal_first=journal_maxlen_value=journal_sequence_value=0;
    if(!virtio_blk::init()) {
        /* Some BlockOS builds already initialize VirtIO and return false. */
    }

    uint8_t raw[1024];
    if(!read_bytes(1024,raw,sizeof(raw))) return false;
    cp(&sb,raw,sizeof(sb));
    if(sb.magic != EXT3_SUPER_MAGIC) return false;
    if(sb.log_block_size > 6) return false;
    block_size=1024U << sb.log_block_size;
    if(block_size<1024 || block_size>65536 || (block_size%512)) return false;
    if(!sb.blocks_per_group || !sb.inodes_per_group || !sb.inodes_count) return false;
    inode_size = (sb.rev_level >= 1 && sb.inode_size) ? sb.inode_size : 128;
    if(inode_size<128 || inode_size>block_size) return false;

    /* EXT3 itself does not require 64-byte group descriptors.  Old ext3
       normally uses 32-byte descriptors; accept a larger descriptor size
       only when the superblock advertises it through the extended field. */
    group_desc_size=32;
    if(sb.feature_incompat & EXT3_FEATURE_INCOMPAT_JOURNAL_DEV) return false;
    group_desc_block=(block_size==1024) ? 2 : 1;

    /* Reject ext4-only extents as a filesystem-wide incompatibility only if
       the caller actually encounters an extent inode.  Plain ext3 images
       normally have this feature clear. */
    if(sb.feature_incompat & ~EXT3_FEATURE_INCOMPAT_FILETYPE &
       ~EXT3_FEATURE_INCOMPAT_RECOVER & 0xFFFFFFFFU) {
        /* Unknown incompat features can change on-disk semantics. */
        return false;
    }

    mounted=true;
    Ext3Inode ji{};
    if(sb.feature_compat & EXT3_FEATURE_COMPAT_HAS_JOURNAL) {
        journal_ino=sb.reserved[0];
        /* s_journal_inum is at byte 224 of the 1024-byte superblock. */
        uint8_t fullsb[1024];
        if(read_bytes(1024,fullsb,sizeof(fullsb))) {
            journal_ino=le32(fullsb+224);
            if(journal_ino && read_inode(journal_ino,&ji)) {
                journal_present=parse_journal(ji);
            }
        }
    }
    return true;
}

bool Ext3Reader::read_group_desc(uint32_t group, uint8_t* out, size_t out_size) {
    if(!out || out_size<group_desc_size || !mounted) return false;
    uint64_t groups=ceil_div(sb.inodes_count,sb.inodes_per_group);
    if(group>=groups) return false;
    uint64_t byte=(uint64_t)group*group_desc_size;
    uint64_t block=group_desc_block + byte/block_size;
    uint32_t off=(uint32_t)(byte%block_size);
    uint8_t buf[65536];
    if(block_size>sizeof(buf)) return false;
    if(!read_block(block,buf)) return false;
    if((uint64_t)off+group_desc_size>block_size) return false;
    cp(out,buf+off,group_desc_size); return true;
}

bool Ext3Reader::read_inode_raw(uint32_t inode_num,uint8_t* out,size_t out_size) {
    if(!mounted || !out || out_size<inode_size || inode_num==0 || inode_num>sb.inodes_count) return false;
    uint32_t group=(inode_num-1)/sb.inodes_per_group;
    uint32_t index=(inode_num-1)%sb.inodes_per_group;
    uint8_t gd[64]; if(!read_group_desc(group,gd,sizeof(gd))) return false;
    uint32_t table=le32(gd+8);
    uint64_t off=(uint64_t)index*inode_size;
    uint64_t block=(uint64_t)table + off/block_size;
    uint32_t in=(uint32_t)(off%block_size);
    uint8_t buf[65536]; if(block_size>sizeof(buf)) return false;
    if(!read_block(block,buf)) return false;
    if((uint64_t)in+inode_size>block_size) return false;
    cp(out,buf+in,inode_size); return true;
}

bool Ext3Reader::read_inode(uint32_t inode_num,Ext3Inode* out) {
    if(!out) return false; uint8_t raw[4096]; if(inode_size>sizeof(raw)) return false;
    if(!read_inode_raw(inode_num,raw,sizeof(raw))) return false;
    cp(out,raw,sizeof(Ext3Inode)); return true;
}

bool Ext3Reader::get_indirect_block(uint64_t block,uint32_t depth,uint64_t index,uint64_t* physical) {
    if(!physical || depth==0 || depth>3 || !block) return false;
    uint64_t ppb=block_size/4;
    uint64_t cap=1; for(uint32_t i=0;i<depth;i++) cap*=ppb;
    if(index>=cap) return false;
    uint8_t buf[65536]; if(block_size>sizeof(buf) || !read_block(block,buf)) return false;
    uint64_t div=1; for(uint32_t i=1;i<depth;i++) div*=ppb;
    uint64_t slot=(index/div)%ppb;
    uint32_t next=le32(buf+slot*4); if(!next) return false;
    if(depth==1) { *physical=next; return true; }
    return get_indirect_block(next,depth-1,index%div,physical);
}

bool Ext3Reader::get_extent_block(const uint8_t* root,uint64_t logical,uint64_t* physical) {
    if(!root || !physical) return false;
    const uint16_t magic=le16(root); if(magic!=0xF30A) return false;
    const uint16_t entries=le16(root+2), max=le16(root+4), depth=le16(root+6);
    if(entries>max || entries>128) return false;
    if(depth==0) {
        for(uint16_t i=0;i<entries;i++) {
            const uint8_t* e=root+12+i*12;
            uint32_t first=le32(e); uint16_t len=le16(e+4);
            uint64_t start=((uint64_t)le16(e+6)<<32)|le32(e+8);
            if(logical>=first && logical<(uint64_t)first+len) { *physical=start+(logical-first); return true; }
        }
        return false;
    }
    /* Extent index node: binary-search-like linear selection. */
    uint16_t chosen=0; bool found=false;
    for(uint16_t i=0;i<entries;i++) {
        const uint8_t* e=root+12+i*12; uint32_t first=le32(e);
        if(first<=logical) { chosen=i; found=true; } else break;
    }
    if(!found) return false;
    const uint8_t* e=root+12+chosen*12;
    uint64_t child=((uint64_t)le16(e+8)<<32)|le32(e+4);
    uint8_t node[65536]; if(block_size>sizeof(node)||!read_block(child,node)) return false;
    return get_extent_block(node,logical,physical);
}

bool Ext3Reader::get_data_block(const Ext3Inode& inode,uint64_t logical,uint64_t* physical) {
    if(!physical) return false;
    if(inode.flags & EXT4_EXTENTS_FL) return get_extent_block(inode.block,logical,physical);
    const uint64_t ppb=block_size/4;
    if(logical<12) { *physical=((const uint32_t*)inode.block)[logical]; return *physical!=0; }
    logical-=12;
    if(logical<ppb) return get_indirect_block(le32(inode.block+48),1,logical,physical);
    logical-=ppb;
    uint64_t dbl=ppb*ppb;
    if(logical<dbl) return get_indirect_block(le32(inode.block+52),2,logical,physical);
    logical-=dbl;
    uint64_t tpl=dbl*ppb;
    if(logical<tpl) return get_indirect_block(le32(inode.block+56),3,logical,physical);
    return false;
}

bool Ext3Reader::load_file_by_inode(uint32_t inode_num,uint8_t* dest,size_t max_size) {
    if(!dest || !max_size || !mounted) return false;
    Ext3Inode ino{}; if(!read_inode(inode_num,&ino)) return false;
    bool regular=(ino.mode&0xF000)==0x8000; if(!regular) return false;
    uint64_t size=(uint64_t)ino.size_lo;
    if((sb.feature_ro_compat & 0x0002U) && inode_size>=112) size|=(uint64_t)ino.size_high_or_dir_acl<<32;
    size=min64(size,max_size);
    uint8_t buf[65536]; if(block_size>sizeof(buf)) return false;
    uint64_t done=0;
    while(done<size) {
        uint64_t l=done/block_size, in=done%block_size, phys=0;
        size_t n=(size_t)min64(block_size-in,size-done);
        if(!get_data_block(ino,l,&phys)) return false;
        if(!read_block(phys,buf)) return false;
        cp(dest+done,buf+in,n); done+=n;
    }
    return true;
}

bool Ext3Reader::read_directory(uint32_t inode_num,uint32_t wanted,char* name,size_t name_size,uint32_t* child_inode,uint8_t* file_type) {
    if(!name||name_size<1||!child_inode||!mounted)return false;
    Ext3Inode dir{}; if(!read_inode(inode_num,&dir))return false;
    if((dir.mode&0xF000)!=0x4000)return false;
    uint64_t size=dir.size_lo, pos=0; uint32_t idx=0;
    uint8_t buf[65536]; if(block_size>sizeof(buf))return false;
    while(pos<size) {
        uint64_t l=pos/block_size,in=pos%block_size,phys;
        if(!get_data_block(dir,l,&phys))return false;
        if(!read_block(phys,buf))return false;
        while(in<block_size && pos<size) {
            if(in+8>block_size)break;
            const uint8_t* p=buf+in; uint32_t ino=le32(p); uint16_t rec=le16(p+4); uint8_t nl=p[6];
            if(rec<8 || in+rec>block_size) return false;
            if(ino && nl && in+8+nl<=block_size) {
                if(idx==wanted) {
                    size_t n=nl; if(n>=name_size)n=name_size-1;
                    for(size_t i=0;i<n;i++)name[i]=(char)p[8+i]; name[n]=0;
                    *child_inode=ino; if(file_type)*file_type=(sb.feature_incompat&EXT3_FEATURE_INCOMPAT_FILETYPE)?p[7]:0; return true;
                }
                ++idx;
            }
            in+=rec; pos=(l*(uint64_t)block_size)+in;
            if(in>=block_size)break;
        }
        pos=((pos/block_size)+1)*block_size;
    }
    return false;
}

bool Ext3Reader::find_in_directory(uint32_t dir_inode,const char* component,uint32_t* child_inode) {
    if(!component||!child_inode)return false;
    uint32_t idx=0; char name[256]; uint32_t child; uint8_t type;
    while(read_directory(dir_inode,idx,name,sizeof(name),&child,&type)) {
        if(eq(name,component)) {*child_inode=child;return true;} ++idx;
    }
    return false;
}

bool Ext3Reader::path_next_component(const char* path,size_t* pos,char* component,size_t component_size) {
    if(!path||!pos||!component||component_size<2)return false;
    size_t p=*pos; while(path[p]=='/')++p; if(!path[p]){*pos=p;return false;}
    size_t n=0; while(path[p]&&path[p]!='/') {if(n+1>=component_size)return false;component[n++]=path[p++];}
    component[n]=0; *pos=p; return true;
}

uint32_t Ext3Reader::find_file_inode(const char* path) {
    if(!mounted||!path||path[0]!='/')return 0;
    size_t p=0; char comp[256]; uint32_t cur=2;
    while(path_next_component(path,&p,comp,sizeof(comp))) {
        if(eq(comp,"."))continue;
        if(eq(comp,"..")) { uint32_t up; if(find_in_directory(cur,"..",&up))cur=up; else return 0; continue; }
        uint32_t next; if(!find_in_directory(cur,comp,&next))return 0; cur=next;
    }
    return cur;
}

bool Ext3Reader::parse_journal(const Ext3Inode& ji) {
    if((ji.mode&0xF000)!=0x8000)return false;
    uint64_t sz=ji.size_lo; if(!sz)return false;
    uint8_t b[65536]; if(block_size>sizeof(b))return false;
    /* Journal inode's first block contains a JBD superblock for internal
       journals. We only need to locate it, never replay/write it here. */
    uint64_t phys; if(!get_data_block(ji,0,&phys)||!read_block(phys,b))return false;
    if(le32(b)!=JBD2_MAGIC && le32(b)!=JBD1_MAGIC)return false;
    uint32_t blocktype=le32(b+4); if(blocktype!=JBD_SUPERBLOCK_V1 && blocktype!=JBD_SUPERBLOCK_V2)return false;
    uint32_t bsize=le32(b+12), maxlen=le32(b+16), first=le32(b+20), seq=le32(b+24);
    if(!bsize || bsize!=block_size || !maxlen || first>=maxlen)return false;
    journal_bsize=bsize; journal_maxlen_value=maxlen; journal_first=first; journal_sequence_value=seq;
    return true;
}

bool Ext3Reader::is_mounted() const { return mounted; }
uint32_t Ext3Reader::get_block_size() const { return block_size; }
uint32_t Ext3Reader::get_inode_size() const { return inode_size; }
uint32_t Ext3Reader::get_inode_count() const { return sb.inodes_count; }
uint32_t Ext3Reader::get_block_count() const { return sb.blocks_count_lo; }
bool Ext3Reader::has_journal() const { return journal_present; }
uint32_t Ext3Reader::journal_inode() const { return journal_ino; }
uint32_t Ext3Reader::journal_block_size() const { return journal_bsize; }
uint32_t Ext3Reader::journal_first_block() const { return journal_first; }
uint32_t Ext3Reader::journal_maxlen() const { return journal_maxlen_value; }
uint32_t Ext3Reader::journal_sequence() const { return journal_sequence_value; }

Ext3Reader ext3_filesystem;
