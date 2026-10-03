#include "ctype.h"
#define EOF (-1)
static int c0(int c){return c==EOF?0:(unsigned char)c;}
int isdigit(int c){c=c0(c);return c>='0'&&c<='9';}
int islower(int c){c=c0(c);return c>='a'&&c<='z';}
int isupper(int c){c=c0(c);return c>='A'&&c<='Z';}
int isalpha(int c){return islower(c)||isupper(c);}
int isalnum(int c){return isalpha(c)||isdigit(c);}
int isblank(int c){return c==' '||c=='\t';}
int iscntrl(int c){c=c0(c);return c<32||c==127;}
int isgraph(int c){c=c0(c);return c>32&&c<127;}
int isprint(int c){c=c0(c);return c>=32&&c<127;}
int ispunct(int c){return isgraph(c)&&!isalnum(c);}
int isspace(int c){c=c0(c);return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\v'||c=='\f';}
int isxdigit(int c){return isdigit(c)||((c0(c)>='a'&&c0(c)<='f')||(c0(c)>='A'&&c0(c)<='F'));}
int tolower(int c){return isupper(c)?c-'A'+'a':c;}
int toupper(int c){return islower(c)?c-'a'+'A':c;}
