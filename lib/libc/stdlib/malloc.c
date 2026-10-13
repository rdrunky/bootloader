#include <debug.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mem.h>

#define ALIGNMENT	16

static const char *heap_error(int err)
{
	switch (err) {
	case -EOVERFLOW:
		return "heap buffer overflow";
	case -EUCLEAN:
		return "heap corruption or foreign pointer";
	case -EINVAL:
		return "double free";
	case -EFAULT:
		return "pointer outside the heap";
	default:
		return strerror(-err);
	}
}

void *malloc(size_t size)
{
	struct heap *heap = mem_heap();

	return heap != NULL ? heap_alloc(heap, size, ALIGNMENT) : NULL;
}

void *aligned_alloc(size_t alignment, size_t size)
{
	struct heap *heap = mem_heap();

	return heap != NULL ? heap_alloc(heap, size, alignment) : NULL;
}

void *calloc(size_t count, size_t size)
{
	void *data;

	if (size != 0 && count > SIZE_MAX / size) {
		return NULL;
	}

	data = malloc(count * size);
	if (data != NULL) {
		memset(data, 0, count * size);
	}

	return data;
}

void *realloc(void *pointer, size_t size)
{
	struct heap *heap = mem_heap();
	void *original = pointer;
	int err;

	if (pointer == NULL) {
		return malloc(size);
	}

	if (size == 0) {
		free(pointer);
		return NULL;
	}

	err = heap != NULL ? heap_resize(heap, &pointer, size) : -EFAULT;
	if (err == -ENOMEM) {
		return NULL;
	}

	if (err != 0) {
		panic("realloc(%p): %s", original, heap_error(err));
	}

	return pointer;
}

void free(void *pointer)
{
	struct heap *heap = mem_heap();
	int err;

	if (pointer == NULL) {
		return;
	}

	err = heap != NULL ? heap_free(heap, pointer) : -EFAULT;
	if (err != 0) {
		panic("free(%p): %s", pointer, heap_error(err));
	}
}
