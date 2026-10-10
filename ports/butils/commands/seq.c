#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
int main(int argc,char**argv){long a=1,step=1,b;if(argc==2)b=strtol(argv[1],0,10);else if(argc==3){a=strtol(argv[1],0,10);b=strtol(argv[2],0,10);}else if(argc==4){a=strtol(argv[1],0,10);step=strtol(argv[2],0,10);b=strtol(argv[3],0,10);}else{write(2,"Usage: seq [FIRST [INCREMENT]] LAST\n",36);return 2;}if(!step||(step>0&&a>b)||(step<0&&a<b))return 0;for(long x=a;(step>0?x<=b:x>=b);){printf("%ld\n",x);if((step>0&&x>LONG_MAX-step)||(step<0&&x<LONG_MIN-step))break;x+=step;}return 0;}
