#include "blockos_https.h"
#include "blockos_tls_client.h"
#include "sys/socket.h"
#include "errno.h"
#include "string.h"
#include "stdio.h"
#include <stdint.h>
#include <stddef.h>

typedef long ssize_t;
extern int socket(int,int,int);
extern int connect(int,const struct sockaddr*,socklen_t);
extern int close(int);

struct blockos_https_client { int fd; void *tls; unsigned short port; char host[128]; };
static struct blockos_tls_client_ops g_tls;
int blockos_tls_register_backend(const struct blockos_tls_client_ops*ops){if(!ops||!ops->handshake||!ops->write||!ops->read||!ops->close)return -1;g_tls=*ops;return 0;}
int blockos_tls_backend_available(void){return g_tls.handshake&&g_tls.write&&g_tls.read&&g_tls.close;}
int blockos_https_tls_ready(void){return blockos_tls_backend_available();}
static unsigned short be16(unsigned short x){return (unsigned short)((x<<8)|(x>>8));}
static int parse_ipv4(const char*s,unsigned char out[4]){unsigned v=0;int part=0;for(size_t i=0;;i++){char c=s[i];if(c>='0'&&c<='9'){v=v*10u+(unsigned)(c-'0');if(v>255)return -1;}else if(c=='.'||c==0){if(part>=4)return -1;out[part++]=(unsigned char)v;v=0;if(!c)break;}else return -1;}return part==4?0:-1;}
struct blockos_https_client* blockos_https_open(const char*host,unsigned short port){if(!host||!blockos_tls_backend_available()){errno=ENOSYS;return 0;}unsigned char ip[4];if(parse_ipv4(host,ip)!=0){errno=EHOSTUNREACH;return 0;}struct blockos_https_client*c=0;/* storage is intentionally left to the caller's allocator in the normal BlockOS libc; this path requires malloc-capable libc. */
extern void* malloc(size_t);c=(struct blockos_https_client*)malloc(sizeof(*c));if(!c){errno=ENOMEM;return 0;}memset(c,0,sizeof(*c));c->fd=socket(AF_INET,SOCK_STREAM,0);if(c->fd<0){errno=ENOTSOCK;return 0;}struct sockaddr_in a;memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_port=be16(port);a.sin_addr.s_addr=((unsigned)ip[0]<<24)|((unsigned)ip[1]<<16)|((unsigned)ip[2]<<8)|ip[3];if(connect(c->fd,(const struct sockaddr*)&a,sizeof(a))<0){close(c->fd);return 0;}if(g_tls.handshake(&c->tls,c->fd,host)!=0){close(c->fd);return 0;}c->port=port;strncpy(c->host,host,sizeof(c->host)-1);return c;}
int blockos_https_request(struct blockos_https_client*c,const char*method,const char*path,const void*body,size_t body_len){if(!c||!method||!path||!c->tls)return -1;char h[1024];size_t mlen=strlen(method),plen=strlen(path),hlen=strlen(c->host);if(mlen+plen+hlen+128>=sizeof(h))return -1;int n=0;n+=snprintf(h+n,sizeof(h)-(size_t)n,"%s %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n",method,path,c->host);if(body&&body_len)n+=snprintf(h+n,sizeof(h)-(size_t)n,"Content-Length: %u\r\nContent-Type: application/octet-stream\r\n",(unsigned)body_len);n+=snprintf(h+n,sizeof(h)-(size_t)n,"\r\n");if(g_tls.write(c->tls,h,(size_t)n)!=(long)n)return -1;if(body&&body_len)if(g_tls.write(c->tls,body,body_len)!=(long)body_len)return -1;return 0;}
long blockos_https_read(struct blockos_https_client*c,void*out,size_t cap){return c&&c->tls?g_tls.read(c->tls,out,cap):-1;}
int blockos_https_close(struct blockos_https_client*c){if(!c)return -1;int r=0;if(c->tls)r=g_tls.close(c->tls);if(c->fd>=0)close(c->fd);extern void free(void*);free(c);return r;}
