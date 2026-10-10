#include <string.h>
#include <unistd.h>
int main(int argc, char **argv) { const char *s=argc>1?argv[1]:"y"; size_t n=strlen(s); for (;;) { if(write(1,s,n)!=(long)n || write(1,"\n",1)!=1)return 1; } }
