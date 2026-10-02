#include "sandbox.hpp"

namespace blockos::sandbox {
namespace { Policy g[32]{}; uint64_t next_token = 1; }
static size_t slen(const char* s, size_t n) { if (!s) return 0; size_t i=0; while (i<n && s[i]) ++i; return i; }
static Policy* find(uint64_t t) { for (auto& p:g) if (p.active&&p.token==t) return &p; return nullptr; }
uint64_t create(uint32_t rights,const char* root){for(auto& p:g)if(!p.active){p={};p.active=true;p.token=next_token++;p.rights=rights; if(root){size_t n=slen(root,sizeof(p.root)-1);for(size_t i=0;i<n;i++)p.root[i]=root[i];p.root[n]=0;}return p.token;}return 0;}
bool destroy(uint64_t t){Policy*p=find(t);if(!p)return false;*p={};return true;}
bool allow(uint64_t t,uint32_t r){Policy*p=find(t);return p&&(p->rights&r)==r;}
bool check(uint64_t t,uint32_t r){return allow(t,r);}
bool path_allowed(uint64_t t,const char* path,size_t len){Policy*p=find(t);if(!p||!path)return false;if(p->rights&Admin)return true;size_t rn=slen(p->root,sizeof(p->root));if(rn==0)return true;if(len<rn)return false;for(size_t i=0;i<rn;i++)if(path[i]!=p->root[i])return false;return true;}
}
