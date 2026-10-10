#include <unistd.h>
int main(int argc,char**argv){if(argc!=2){write(2,"Usage: unlink FILE\n",19);return 2;}return unlink(argv[1])<0?1:0;}
