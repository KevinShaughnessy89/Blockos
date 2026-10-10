#include <string.h>
#include <unistd.h>
int main(int argc,char **argv){if(argc!=2){write(2,"Usage: dirname PATH\n",20);return 2;}char *p=argv[1];size_t n=strlen(p);while(n>1&&p[n-1]=='/')p[--n]=0;while(n&&p[n-1]!='/')n--;if(!n){write(1,".\n",2);return 0;}while(n>1&&p[n-1]=='/')n--;if(n==0){write(1,".\n",2);return 0;}write(1,p,n);write(1,"\n",1);return 0;}
