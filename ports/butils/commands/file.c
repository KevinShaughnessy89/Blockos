#include <fcntl.h>
#include <string.h>
#include <unistd.h>
static const char *kind(const unsigned char*b,long n){if(n>=4&&b[0]==0x7f&&b[1]=='E'&&b[2]=='L'&&b[3]=='F')return "ELF executable";if(n>=8&&!memcmp(b,"\x89PNG\r\n\x1a\n",8))return "PNG image";if(n>=3&&b[0]==0xff&&b[1]==0xd8&&b[2]==0xff)return "JPEG image";if(n>=2&&b[0]=='P'&&(b[1]=='3'||b[1]=='6'))return "Netpbm image";for(long i=0;i<n;i++)if(b[i]==0)return "data";return "text or unknown data";}
int main(int argc,char **argv){if(argc<2){write(2,"Usage: file FILE ...\n",21);return 2;}int rc=0;for(int i=1;i<argc;i++){int fd=open(argv[i],O_RDONLY);if(fd<0){write(1,argv[i],strlen(argv[i]));write(1,": cannot open\n",14);rc=1;continue;}unsigned char b[16];long n=read(fd,b,sizeof b);close(fd);write(1,argv[i],strlen(argv[i]));write(1,": ",2);const char*k=kind(b,n);write(1,k,strlen(k));write(1,"\n",1);}return rc;}
