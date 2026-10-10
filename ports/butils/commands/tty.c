#include <unistd.h>
int main(void){if(!isatty(0)){write(2,"not a tty\n",10);return 1;}write(1,"console0\n",9);return 0;}
