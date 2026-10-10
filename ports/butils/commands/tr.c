#include <string.h>
#include <unistd.h>
int main(int argc,char**argv){int del=0,i=1;if(i<argc&&!strcmp(argv[i],"-d")){del=1;i++;}if(argc-i<1||argc-i>(del?1:2))return 2;const char*a=argv[i],*b=del?"":argv[i+1];char c;while(read(0,&c,1)==1){const char*p=strchr(a,(unsigned char)c);if(p){if(del)continue;size_t k=(size_t)(p-a);c=b[ k<strlen(b)?k:(strlen(b)?strlen(b)-1:0)];if(!*b)continue;}write(1,&c,1);}return 0;}
