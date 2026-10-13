#include <compiler.h>
#include <stdio.h>
#include <sys/console.h>

#include "../private.h"

static void console_sink(void *context __unused, const char *data, size_t len)
{
	console_write(data, len);
}

int vprintf(const char *fmt, va_list ap)
{
	return format_output(console_sink, NULL, fmt, ap);
}

int printf(const char *fmt, ...)
{
	va_list ap;
	int total;

	va_start(ap, fmt);
	total = vprintf(fmt, ap);
	va_end(ap);

	return total;
}
