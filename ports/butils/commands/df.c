#include <stdio.h>
#include <sys/statvfs.h>
#include <unistd.h>
int main(int argc,char **argv){if(argc==1){argv[1]="/";argc=2;}int rc=0;for(int i=1;i<argc;i++){struct statvfs s;if(statvfs(argv[i],&s)<0){printf("df: cannot read filesystem: %s\n",argv[i]);rc=1;continue;}if(s.f_blocks==0){write(2,"df: BlockOS libc does not expose filesystem capacity yet\n",57);rc=1;continue;}unsigned long total=(unsigned long)(s.f_blocks*s.f_frsize),freeb=(unsigned long)(s.f_bavail*s.f_frsize),used=total-(unsigned long)(s.f_bfree*s.f_frsize);printf("%s: total=%lu used=%lu available=%lu bytes\n",argv[i],total,used,freeb);}return rc;}
