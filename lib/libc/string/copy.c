#include <string.h>

size_t strlcpy(char *dst, const char *src, size_t size)
{
	size_t len = strlen(src);

	if (size > 0) {
		size_t keep = len < size - 1 ? len : size - 1;

		memcpy(dst, src, keep);
		dst[keep] = '\0';
	}

	return len;
}

size_t strlcat(char *dst, const char *src, size_t size)
{
	size_t used = strnlen(dst, size);

	if (used == size) {
		return size + strlen(src);
	}

	return used + strlcpy(dst + used, src, size - used);
}
