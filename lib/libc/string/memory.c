#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define WORD_SIZE	sizeof(uint64_t)
#define WORD_MASK	(WORD_SIZE - 1)

static bool aligned(const void *p)
{
	return ((uintptr_t)p & WORD_MASK) == 0;
}

void *memcpy(void *dst, const void *src, size_t n)
{
	uint8_t *d = dst;
	const uint8_t *s = src;

	if ((((uintptr_t)d ^ (uintptr_t)s) & WORD_MASK) == 0) {
		while (n > 0 && !aligned(d)) {
			*d++ = *s++;
			n--;
		}

		for (; n >= WORD_SIZE; n -= WORD_SIZE, d += WORD_SIZE, s += WORD_SIZE) {
			*(uint64_t *)d = *(const uint64_t *)s;
		}
	}

	while (n-- > 0) {
		*d++ = *s++;
	}

	return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
	uint8_t *d = dst;
	const uint8_t *s = src;

	if (d <= s || d >= s + n) {
		return memcpy(dst, src, n);
	}

	while (n-- > 0) {
		d[n] = s[n];
	}

	return dst;
}

void *memset(void *dst, int c, size_t n)
{
	uint8_t *d = dst;
	uint64_t word = (uint8_t)c * 0x0101010101010101ULL;

	while (n > 0 && !aligned(d)) {
		*d++ = (uint8_t)c;
		n--;
	}

	for (; n >= WORD_SIZE; n -= WORD_SIZE, d += WORD_SIZE) {
		*(uint64_t *)d = word;
	}

	while (n-- > 0) {
		*d++ = (uint8_t)c;
	}

	return dst;
}

void explicit_bzero(void *dst, size_t n)
{
	memset(dst, 0, n);
	__asm__ volatile("" : : "r"(dst) : "memory");
}

int memcmp(const void *a, const void *b, size_t n)
{
	const uint8_t *x = a;
	const uint8_t *y = b;

	for (; n > 0; n--, x++, y++) {
		if (*x != *y) {
			return *x < *y ? -1 : 1;
		}
	}

	return 0;
}

void *memchr(const void *s, int c, size_t n)
{
	const uint8_t *p = s;

	for (; n > 0; n--, p++) {
		if (*p == (uint8_t)c) {
			return (void *)p;
		}
	}

	return NULL;
}
