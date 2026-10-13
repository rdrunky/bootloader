#ifndef LIBC_PRIVATE_H
#define LIBC_PRIVATE_H

#include <stdarg.h>
#include <stddef.h>

typedef void (*format_sink)(void *context, const char *data, size_t len);

int format_output(format_sink sink, void *context, const char *fmt, va_list ap);

#endif
