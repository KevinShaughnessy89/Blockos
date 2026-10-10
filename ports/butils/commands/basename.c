#include <string.h>
#include <unistd.h>
int main(int argc,char **argv){if(argc<2||argc>3){write(2,"Usage: basename PATH [SUFFIX]\n",30);return 2;}char *p=argv[1];size_t n=strlen(p);while(n>1&&p[n-1]=='/')p[--n]=0;if(n==1&&p[0]=='/'){write(1,"/\n",2);return 0;}while(n&&p[n-1]!='/')n--;char *b=argv[1]+n;size_t l=strlen(b);if(argc==3){size_t s=strlen(argv[2]);if(s<=l&&strcmp(b+l-s,argv[2])==0)b[l-s]=0;}write(1,b,strlen(b));write(1,"\n",1);return 0;}
