#include <fcntl.h>
#include <unistd.h>
#include <string.h>
int main(int argc,char**argv){if(argc<2){write(2,"Usage: touch FILE...\n",21);return 2;}int rc=0;for(int i=1;i<argc;i++){int fd=open(argv[i],O_WRONLY);if(fd<0)fd=open(argv[i],O_WRONLY|O_CREAT,0666);if(fd<0){write(2,"touch: cannot create file\n",26);rc=1;}else close(fd);}return rc;}
