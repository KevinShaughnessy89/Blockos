#include <string.h>
#include <unistd.h>
int main(int argc,char**argv){if(argc!=2){write(2,"Usage: readlink PATH\n",21);return 2;}char b[512];ssize_t n=readlink(argv[1],b,sizeof b);if(n<0){write(2,"readlink: failed\n",17);return 1;}write(1,b,(size_t)n);write(1,"\n",1);return 0;}
