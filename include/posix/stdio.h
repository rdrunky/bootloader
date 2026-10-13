#ifndef STDIO_H
#define STDIO_H

#include <compiler.h>
#include <stdarg.h>
#include <stddef.h>

int printf(const char *fmt, ...) __printflike(1, 2);
int vprintf(const char *fmt, va_list ap);
int snprintf(char *buf, size_t size, const char *fmt, ...) __printflike(3, 4);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

int putchar(int c);

#endif
