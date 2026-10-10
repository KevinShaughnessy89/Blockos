#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int tailfd(int fd,long want){size_t cap=8192,n=0;char*b=malloc(cap);if(!b)return 1;for(;;){if(n==cap){size_t nc=cap*2;char*q=realloc(b,nc);if(!q){free(b);return 1;}b=q;cap=nc;}ssize_t r=read(fd,b+n,cap-n);if(r<=0)break;n+=(size_t)r;}size_t start=n;if(want>0){long seen=0;while(start>0){--start;if(b[start]=='\n'&&start+1<n){seen++;if(seen>=want){start++;break;}}}if(start==0&&n&&b[0]=='\n'&&want==0)start=n;}if(start<n)write(1,b+start,n-start);free(b);return 0;}
int main(int argc,char**argv){long lines=10;int i=1;if(i<argc&&!strcmp(argv[i],"-n")&&i+1<argc){lines=strtol(argv[i+1],0,10);i+=2;}else if(i<argc&&argv[i][0]=='-'&&argv[i][1]>='0'&&argv[i][1]<='9'){lines=strtol(argv[i]+1,0,10);i++;}if(lines<0)return 2;if(i==argc)return tailfd(0,lines);int rc=0;for(;i<argc;i++){if(!strcmp(argv[i],"-")){if(tailfd(0,lines))rc=1;}else{int fd=open(argv[i],O_RDONLY);if(fd<0){write(2,"tail: open failed\n",18);rc=1;continue;}if(tailfd(fd,lines))rc=1;close(fd);}}return rc;}
