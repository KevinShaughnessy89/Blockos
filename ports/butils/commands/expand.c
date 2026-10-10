#include <string.h>
#include <unistd.h>
int main(int argc,char **argv){(void)argv;if(argc>1){write(2,"Usage: expand < FILE\n",21);return 2;}char b[1];int col=0;for(;;){ssize_t n=read(0,b,1);if(n<=0)break;if(b[0]=='\t'){int k=8-(col%8);while(k--){write(1," ",1);col++;}}else{write(1,b,1);if(b[0]=='\n')col=0;else if(b[0]=='\b'){if(col)col--;}else col++;}}return 0;}
