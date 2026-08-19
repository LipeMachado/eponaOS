#ifndef STDLIB_H
#define STDLIB_H

#include <stdint.h>

#define NULL ((void*)0)
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* String functions */
int strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, int n);
void strcpy(char *dst, const char *src);
void strncpy(char *dst, const char *src, int n);
void strcat(char *dst, const char *src);
char *strchr(const char *s, int c);
int starts_with(const char *s, const char *prefix);
int ends_with(const char *s, const char *suffix);
void reverse_str(char *s);

/* Number conversion */
void itoa(int val, char *buf);
void utoa(uint32_t val, char *buf);
void itox(uint32_t val, char *buf);
int atoi(const char *s);

/* I/O helpers */
void puts(const char *s);
void putchar(char c);
int getchar(void);

/* printf-like functions */
void printf(const char *fmt, ...);
void sprintf(char *buf, const char *fmt, ...);

/* Memory */
void *malloc(uint32_t size);
void free(void *ptr);
void *calloc(uint32_t count, uint32_t size);

/* Process */
void exit(int code);
int fork(void);
void wait(int *status);

#endif
