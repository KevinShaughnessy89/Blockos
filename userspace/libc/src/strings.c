#include "strings.h"
#include "string.h"
#include <stddef.h>
static unsigned char lc(unsigned char c){return c>='A'&&c<='Z'?(unsigned char)(c-'A'+'a'):c;}
int strcasecmp(const char*a,const char*b){while(*a&&*b){unsigned char x=lc((unsigned char)*a),y=lc((unsigned char)*b);if(x!=y)return x<y?-1:1;++a;++b;}return lc((unsigned char)*a)==lc((unsigned char)*b)?0:(*a?1:-1);}
int strncasecmp(const char*a,const char*b,size_t n){while(n&&*a&&*b){unsigned char x=lc((unsigned char)*a),y=lc((unsigned char)*b);if(x!=y)return x<y?-1:1;++a;++b;--n;}return n? (lc((unsigned char)*a)==lc((unsigned char)*b)?0:(*a?1:-1)) : 0;}
void *memmem(const void*h,size_t hl,const void*n,size_t nl){if(!nl)return (void*)h;if(nl>hl)return 0;const unsigned char*hp=h;const unsigned char*np=n;for(size_t i=0;i+nl<=hl;++i)if(memcmp(hp+i,np,nl)==0)return (void*)(hp+i);return 0;}
int ffs(int x){if(!x)return 0;int n=1;while(!(x&1)){x>>=1;++n;}return n;}
