#include <string.h>

size_t strlen(const char *s)
{
	const char *end = s;

	while (*end != '\0') {
		end++;
	}

	return (size_t)(end - s);
}

size_t strnlen(const char *s, size_t max)
{
	size_t n = 0;

	while (n < max && s[n] != '\0') {
		n++;
	}

	return n;
}
