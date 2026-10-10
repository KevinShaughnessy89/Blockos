#include <unistd.h>
#include <string.h>
int main(int argc,char**argv){if(argc!=3){write(2,"Usage: mv SOURCE DEST\n",22);return 2;}if(rename(argv[1],argv[2])<0){write(2,"mv: rename failed\n",18);return 1;}return 0;}
