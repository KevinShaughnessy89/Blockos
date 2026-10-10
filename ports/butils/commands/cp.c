#include <fcntl.h>
#include <unistd.h>
#include <string.h>
int main(int argc,char **argv){if(argc!=3){write(2,"Usage: cp SOURCE DEST\n",22);return 2;}int in=open(argv[1],O_RDONLY);if(in<0){write(2,"cp: cannot open source\n",23);return 1;}int out=open(argv[2],O_WRONLY|O_CREAT|O_TRUNC,0666);if(out<0){close(in);write(2,"cp: cannot open destination\n",28);return 1;}char b[4096];int rc=0;for(;;){ssize_t n=read(in,b,sizeof b);if(n==0)break;if(n<0){rc=1;break;}ssize_t p=0;while(p<n){ssize_t w=write(out,b+p,(size_t)(n-p));if(w<=0){rc=1;break;}p+=w;}if(rc)break;}close(in);close(out);return rc;}
