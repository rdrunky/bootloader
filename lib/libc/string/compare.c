#include <ctype.h>
#include <string.h>

static int difference(unsigned char a, unsigned char b)
{
	return (int)a - (int)b;
}

int strcmp(const char *a, const char *b)
{
	while (*a != '\0' && *a == *b) {
		a++;
		b++;
	}

	return difference((unsigned char)*a, (unsigned char)*b);
}

int strncmp(const char *a, const char *b, size_t n)
{
	for (; n > 0; n--, a++, b++) {
		if (*a != *b || *a == '\0') {
			return difference((unsigned char)*a, (unsigned char)*b);
		}
	}

	return 0;
}

int strncasecmp(const char *a, const char *b, size_t n)
{
	for (; n > 0; n--, a++, b++) {
		int x = tolower((unsigned char)*a);
		int y = tolower((unsigned char)*b);

		if (x != y || x == '\0') {
			return difference((unsigned char)x, (unsigned char)y);
		}
	}

	return 0;
}
