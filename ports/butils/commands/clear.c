#include <unistd.h>
int main(void){const char s[]="\033[2J\033[H";return write(1,s,sizeof(s)-1)==(long)(sizeof(s)-1)?0:1;}
