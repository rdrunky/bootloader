#include <string.h>

char *strchr(const char *s, int c)
{
	for (;; s++) {
		if (*s == (char)c) {
			return (char *)s;
		}

		if (*s == '\0') {
			return NULL;
		}
	}
}

size_t strspn(const char *s, const char *accept)
{
	size_t n = 0;

	while (s[n] != '\0' && strchr(accept, s[n]) != NULL) {
		n++;
	}

	return n;
}

size_t strcspn(const char *s, const char *reject)
{
	size_t n = 0;

	while (s[n] != '\0' && strchr(reject, s[n]) == NULL) {
		n++;
	}

	return n;
}
