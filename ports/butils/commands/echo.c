#include <string.h>
#include <unistd.h>
int main(int argc,char **argv){int i=1,nl=1,first=1;if(i<argc&&!strcmp(argv[i],"-n")){nl=0;i++;}if(i<argc&&!strcmp(argv[i],"--"))i++;for(;i<argc;i++){if(!first)write(1," ",1);write(1,argv[i],strlen(argv[i]));first=0;}if(nl)write(1,"\n",1);return 0;}
