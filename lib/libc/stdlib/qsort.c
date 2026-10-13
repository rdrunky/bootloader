#include <stdint.h>
#include <stdlib.h>

typedef int (*comparator)(const void *, const void *);

static void swap(uint8_t *a, uint8_t *b, size_t size)
{
	while (size-- > 0) {
		uint8_t tmp = *a;

		*a++ = *b;
		*b++ = tmp;
	}
}

static void sift_down(uint8_t *base, size_t root, size_t count, size_t size, comparator compare)
{
	for (;;) {
		size_t child = 2 * root + 1;

		if (child >= count) {
			return;
		}

		if (child + 1 < count && compare(base + child * size, base + (child + 1) * size) < 0) {
			child++;
		}

		if (compare(base + root * size, base + child * size) >= 0) {
			return;
		}

		swap(base + root * size, base + child * size, size);
		root = child;
	}
}

void qsort(void *base, size_t count, size_t size, comparator compare)
{
	uint8_t *items = base;
	size_t i;

	if (count < 2 || size == 0) {
		return;
	}

	for (i = count / 2; i > 0; i--) {
		sift_down(items, i - 1, count, size, compare);
	}

	for (i = count - 1; i > 0; i--) {
		swap(items, items + i * size, size);
		sift_down(items, 0, i, size, compare);
	}
}
