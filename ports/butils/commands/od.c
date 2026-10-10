#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
static void octal(unsigned int x){char b[3];b[0]=(char)('0'+((x>>6)&7));b[1]=(char)('0'+((x>>3)&7));b[2]=(char)('0'+(x&7));write(1,b,3);}
int main(int argc,char**argv){int fd=0;if(argc>2)return 2;if(argc==2){fd=open(argv[1],O_RDONLY);if(fd<0)return 1;}unsigned long off=0;unsigned char b[16];for(;;){ssize_t n=read(fd,b,sizeof b);if(n<=0)break;printf("%lu: ",off);for(ssize_t i=0;i<n;i++){octal((unsigned int)b[i]);write(1," ",1);}write(1,"\n",1);off+=(unsigned long)n;}if(argc==2)close(fd);return 0;}
