#include <fcntl.h>
#include <string.h>
#include <unistd.h>
static int copyfd(int in) { char b[4096]; for (;;) { ssize_t n=read(in,b,sizeof b); if(n==0)return 0; if(n<0)return 1; ssize_t p=0; while(p<n){ssize_t w=write(1,b+p,(size_t)(n-p));if(w<=0)return 1;p+=w;} } }
int main(int argc,char **argv){int rc=0;if(argc==1)return copyfd(0);for(int i=1;i<argc;i++){if(!strcmp(argv[i],"-")){if(copyfd(0))rc=1;continue;}int fd=open(argv[i],O_RDONLY);if(fd<0){write(2,"cat: cannot open file\n",22);rc=1;continue;}if(copyfd(fd))rc=1;close(fd);}return rc;}
