#include <fcntl.h>
#include <unistd.h>
int main(int argc,char**argv){if(argc!=2)return 2;int fd=open(argv[1],O_RDONLY);if(fd<0)return 1;char b[1],run[1024];int n=0,rc=0;for(;;){ssize_t r=read(fd,b,1);if(r<=0)break;unsigned char c=(unsigned char)b[0];if((c>=32&&c<=126)||c=='\t'){if(n<(int)sizeof run-1)run[n++]=b[0];}else{if(n>=4){run[n]=0;write(1,run,n);write(1,"\n",1);}n=0;}}if(n>=4){write(1,run,n);write(1,"\n",1);}close(fd);return rc;}
