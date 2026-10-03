#include "stdio.h"
#include "stdlib.h"
#include "unistd.h"
#include "fcntl.h"
#include "errno.h"
#include "string.h"
#include <stdint.h>


static void putc_buf(char* buf, size_t size, size_t* pos, char c){if(*pos+1<size)buf[*pos]=c;(*pos)++;}
static void puts_buf(char* buf,size_t size,size_t*pos,const char*s){if(!s)s="(null)";while(*s)putc_buf(buf,size,pos,*s++);}
static void put_uint(char*buf,size_t size,size_t*pos,unsigned long v,int base,int upper){char t[32];int n=0;const char*d=upper?"0123456789ABCDEF":"0123456789abcdef";if(!v){putc_buf(buf,size,pos,'0');return;}while(v&&n<(int)sizeof(t)){t[n++]=d[v%(unsigned)base];v/=(unsigned)base;}while(n)putc_buf(buf,size,pos,t[--n]);}
static void put_int(char*buf,size_t size,size_t*pos,long v){if(v<0){putc_buf(buf,size,pos,'-');put_uint(buf,size,pos,(unsigned long)(-(unsigned long)v),10,0);}else put_uint(buf,size,pos,(unsigned long)v,10,0);}
int vsnprintf(char*buf,size_t size,const char*fmt,va_list ap){size_t pos=0;for(const char*f=fmt;f&&*f;f++){if(*f!='%'){putc_buf(buf,size,&pos,*f);continue;}++f;int l=0;if(*f=='l'){l=1;++f;}switch(*f){case 'd':case 'i':l?put_int(buf,size,&pos,va_arg(ap,long)):put_int(buf,size,&pos,(long)va_arg(ap,int));break;case 'u':l?put_uint(buf,size,&pos,va_arg(ap,unsigned long),10,0):put_uint(buf,size,&pos,(unsigned long)va_arg(ap,unsigned int),10,0);break;case 'x':l?put_uint(buf,size,&pos,va_arg(ap,unsigned long),16,0):put_uint(buf,size,&pos,(unsigned long)va_arg(ap,unsigned int),16,0);break;case 'X':l?put_uint(buf,size,&pos,va_arg(ap,unsigned long),16,1):put_uint(buf,size,&pos,(unsigned long)va_arg(ap,unsigned int),16,1);break;case 'p':puts_buf(buf,size,&pos,"0x");put_uint(buf,size,&pos,(unsigned long)(uintptr_t)va_arg(ap,void*),16,0);break;case 's':puts_buf(buf,size,&pos,va_arg(ap,const char*));break;case 'c':putc_buf(buf,size,&pos,(char)va_arg(ap,int));break;case '%':putc_buf(buf,size,&pos,'%');break;default:putc_buf(buf,size,&pos,'%');if(l)putc_buf(buf,size,&pos,'l');if(*f)putc_buf(buf,size,&pos,*f);break;}}if(size)buf[pos<size?pos:size-1]=0;return (int)pos;}
int snprintf(char*buf,size_t size,const char*fmt,...){va_list ap;va_start(ap,fmt);int r=vsnprintf(buf,size,fmt,ap);va_end(ap);return r;}
int printf(const char*fmt,...){char b[512];va_list ap;va_start(ap,fmt);int n=vsnprintf(b,sizeof(b),fmt,ap);va_end(ap);size_t w=n>=(int)sizeof(b)?sizeof(b)-1:(size_t)n;if(w)write(1,b,w);return n;}

static unsigned char in_buf[BUFSIZ], out_buf[BUFSIZ], err_buf[BUFSIZ];
static FILE in_file={0,1,in_buf,sizeof(in_buf),0,0,0};
static FILE out_file={1,2,out_buf,sizeof(out_buf),0,0,0};
static FILE err_file={2,2,err_buf,sizeof(err_buf),0,0,0};
FILE *stdin=&in_file; FILE *stdout=&out_file; FILE *stderr=&err_file;
static int push_raw(FILE*f){if(!f||!f->buf||!f->len)return 0;ssize_t n=write(f->fd,f->buf,f->len);if(n<0)return EOF;f->len=0;f->pos=0;return 0;}
int fflush(FILE*f){if(!f)return -1;return push_raw(f);}
static FILE* alloc_stream(int fd,int flags){FILE*f=(FILE*)malloc(sizeof(FILE));if(!f)return 0;memset(f,0,sizeof(*f));f->fd=fd;f->flags=(unsigned)flags;f->buf=(unsigned char*)malloc(BUFSIZ);f->buf_size=f->buf?BUFSIZ:0;if(!f->buf){free(f);return 0;}return f;}
static int mode_flags(const char*m,int*flags){if(!m||!m[0])return -1;int f=0;switch(m[0]){case 'r':f=O_RDONLY;break;case 'w':f=O_WRONLY|O_CREAT|O_TRUNC;break;case 'a':f=O_WRONLY|O_CREAT|O_APPEND;break;default:return -1;}if(m[1]=='+')f=(f&~3)|O_RDWR;return f;}
FILE* fopen(const char*path,const char*mode){int f=0;if(mode_flags(mode,&f)<0){errno=22;return 0;}int fd=open(path,f,0666);if(fd<0)return 0;FILE*s=alloc_stream(fd,2);if(!s){close(fd);return 0;}return s;}
FILE* fdopen(int fd,const char*mode){int f=0;if(mode_flags(mode,&f)<0){errno=22;return 0;}return alloc_stream(fd,2);}
int fclose(FILE*f){if(!f||f==stdin||f==stdout||f==stderr){errno=22;return -1;}int rc=fflush(f);int c=close(f->fd);free(f->buf);free(f);return rc<0?rc:c;}
size_t fread(void*ptr,size_t sz,size_t nm,FILE*f){if(!f||!ptr||!sz)return 0;size_t want=sz*nm,got=0;if(f->len&&f->fd==0){}ssize_t n=read(f->fd,ptr,want);if(n<0){f->flags|=4;return 0;}if(n==0)f->flags|=1;got=(size_t)n;return got/sz;}
size_t fwrite(const void*ptr,size_t sz,size_t nm,FILE*f){if(!f||!ptr||!sz)return 0;size_t n=sz*nm;const unsigned char*p=ptr;size_t done=0;if(!f->buf)return 0;while(n){size_t room=f->buf_size-f->len;size_t k=n<room?n:room;memcpy(f->buf+f->len,p,k);f->len+=k;p+=k;n-=k;done+=k;if(f->len==f->buf_size&&push_raw(f)<0)break;}return done/sz;}
int fgetc(FILE*f){unsigned char c;return fread(&c,1,1,f)==1?c:EOF;}int getc(FILE*f){return fgetc(f);}int fputc(int c,FILE*f){unsigned char x=(unsigned char)c;return fwrite(&x,1,1,f)==1?x:EOF;}int putc(int c,FILE*f){return fputc(c,f);}
char*fgets(char*s,int n,FILE*f){if(!s||n<=0||!f)return 0;int i=0;while(i<n-1){int c=fgetc(f);if(c==EOF)break;s[i++]=(char)c;if(c=='\n')break;}if(i==0)return 0;s[i]=0;return s;}
int fputs(const char*s,FILE*f){if(!s)return EOF;size_t n=strlen(s);return fwrite(s,1,n,f)==n?0:EOF;}int puts(const char*s){if(fputs(s,stdout)==EOF)return EOF;return fputc('\n',stdout);}
int fileno(FILE*f){if(!f){errno=22;return -1;}return f->fd;}
int fseek(FILE*f,off_t off,int whence){if(!f){errno=22;return -1;}if(fflush(f)<0)return -1;off_t r=lseek(f->fd,off,whence);if(r<0)return -1;f->offset=r;f->flags&=~1u;return 0;}
off_t ftell(FILE*f){if(!f){errno=22;return -1;}return lseek(f->fd,0,SEEK_CUR);}
void rewind(FILE*f){if(f)fseek(f,0,SEEK_SET);}
int feof(FILE*f){return f?(f->flags&1)!=0:0;}int ferror(FILE*f){return f?(f->flags&4)!=0:1;}void clearerr(FILE*f){if(f)f->flags&=~5u;}
int setvbuf(FILE*f,char*b,int mode,size_t n){(void)mode;if(!f)return -1;if(fflush(f)<0)return -1;if(f->buf&&f!=stdin&&f!=stdout&&f!=stderr)free(f->buf);if(mode==_IONBF){f->buf=0;f->buf_size=0;return 0;}f->buf=b;if(!f->buf)f->buf=(unsigned char*)malloc(n?n:BUFSIZ);f->buf_size=f->buf?(n?n:BUFSIZ):0;return f->buf?0:-1;}
int vfprintf(FILE*f,const char*fmt,va_list ap){char local[4096];va_list aq;va_copy(aq,ap);int n=vsnprintf(local,sizeof(local),fmt,aq);va_end(aq);if(n<0)return -1;if((size_t)n<sizeof(local))return fwrite(local,1,(size_t)n,f)==(size_t)n?n:-1;char*buf=(char*)malloc((size_t)n+1);if(!buf)return -1;va_copy(aq,ap);vsnprintf(buf,(size_t)n+1,fmt,aq);va_end(aq);int r=fwrite(buf,1,(size_t)n,f)==(size_t)n?n:-1;free(buf);return r;}
int fprintf(FILE*f,const char*fmt,...){va_list ap;va_start(ap,fmt);int r=vfprintf(f,fmt,ap);va_end(ap);return r;}
void perror(const char*s){fprintf(stderr,"%s: errno=%d\\n",s?s:"",errno);}
