#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int headfd(int fd,long lines){if(lines==0)return 0;char b[2048];long count=0;for(;;){ssize_t n=read(fd,b,sizeof b);if(n<=0)return n<0?1:0;for(ssize_t i=0;i<n;i++){if(write(1,b+i,1)!=1)return 1;if(b[i]=='\n'&&++count>=lines)return 0;}}}
int main(int argc,char**argv){long lines=10;int i=1;if(i<argc&&!strcmp(argv[i],"-n")&&i+1<argc){lines=strtol(argv[i+1],0,10);i+=2;}else if(i<argc&&argv[i][0]=='-'&&argv[i][1]>='0'&&argv[i][1]<='9'){lines=strtol(argv[i]+1,0,10);i++;}if(lines<0)return 2;if(i==argc)return headfd(0,lines);int rc=0;for(;i<argc;i++){if(!strcmp(argv[i],"-")){if(headfd(0,lines))rc=1;}else{int fd=open(argv[i],O_RDONLY);if(fd<0){write(2,"head: open failed\n",18);rc=1;continue;}if(headfd(fd,lines))rc=1;close(fd);}}return rc;}
