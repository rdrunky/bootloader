#include <stdio.h>

#include "../private.h"

struct buffer {
	char	*data;
	size_t	size;
	size_t	length;
};

static void buffer_sink(void *context, const char *data, size_t len)
{
	struct buffer *buffer = context;
	size_t i;

	for (i = 0; i < len; i++) {
		if (buffer->length + 1 < buffer->size) {
			buffer->data[buffer->length] = data[i];
		}
		buffer->length++;
	}
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
	struct buffer buffer = { .data = buf, .size = size };
	int total = format_output(buffer_sink, &buffer, fmt, ap);

	if (size > 0) {
		buf[buffer.length < size ? buffer.length : size - 1] = '\0';
	}

	return total;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list ap;
	int total;

	va_start(ap, fmt);
	total = vsnprintf(buf, size, fmt, ap);
	va_end(ap);

	return total;
}
