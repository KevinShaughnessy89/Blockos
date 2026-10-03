#pragma once
#include <stddef.h>
#include <stdarg.h>

typedef long off_t;
typedef struct FILE FILE;

struct FILE {
    int fd;
    unsigned flags;
    unsigned char *buf;
    size_t buf_size;
    size_t pos;
    size_t len;
    off_t offset;
};

#define EOF (-1)
#define BUFSIZ 4096
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int vfprintf(FILE*, const char*, va_list);
int fprintf(FILE*, const char*, ...);
int printf(const char*, ...);
int vsnprintf(char*, size_t, const char*, va_list);
int snprintf(char*, size_t, const char*, ...);
int fflush(FILE*);
int fclose(FILE*);
FILE* fopen(const char*, const char*);
FILE* fdopen(int, const char*);
size_t fread(void*, size_t, size_t, FILE*);
size_t fwrite(const void*, size_t, size_t, FILE*);
int fgetc(FILE*); int getc(FILE*);
int fputc(int, FILE*); int putc(int, FILE*);
char* fgets(char*, int, FILE*);
int fputs(const char*, FILE*);
int puts(const char*);
int fileno(FILE*);
int fseek(FILE*, off_t, int);
off_t ftell(FILE*);
void rewind(FILE*);
int feof(FILE*); int ferror(FILE*); void clearerr(FILE*);
int setvbuf(FILE*, char*, int, size_t);
void perror(const char*);
