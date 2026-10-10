#include <fcntl.h>
#include <unistd.h>
int main(int argc,char**argv){int fds[32],nf=0,rc=0;for(int i=1;i<argc&&nf<32;i++){int fd=open(argv[i],O_WRONLY|O_CREAT|O_TRUNC,0666);if(fd<0)rc=1;else fds[nf++]=fd;}char b[4096];for(;;){ssize_t n=read(0,b,sizeof b);if(n<=0)break;ssize_t p=0;while(p<n){ssize_t w=write(1,b+p,(size_t)(n-p));if(w<=0){rc=1;break;}p+=w;}for(int i=0;i<nf;i++){p=0;while(p<n){ssize_t w=write(fds[i],b+p,(size_t)(n-p));if(w<=0){rc=1;break;}p+=w;}}}for(int i=0;i<nf;i++)close(fds[i]);return rc;}
