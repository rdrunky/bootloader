#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/macros.h>
#include <sys/mman.h>
#include <unistd.h>

#include "harness.h"

static struct test *tests;
static struct test **tail = &tests;
static struct test *current;
static bool current_failed;

void test_register(struct test *test)
{
	*tail = test;
	tail = &test->next;
}

void test_fail(const char *file, int line, const char *fmt, ...)
{
	va_list ap;

	if (!current_failed) {
		printf("FAIL %s.%s\n", current->suite, current->name);
		current_failed = true;
	}

	printf("     %s:%d: ", file, line);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	putchar('\n');
}

void *test_load(const char *path, size_t *size)
{
	FILE *file = fopen(path, "rb");
	void *data = NULL;
	long length;

	*size = 0;
	if (file == NULL) {
		return NULL;
	}

	if (fseek(file, 0, SEEK_END) == 0 && (length = ftell(file)) > 0 && fseek(file, 0, SEEK_SET) == 0) {
		data = malloc((size_t)length);
		if (data != NULL && fread(data, 1, (size_t)length, file) == (size_t)length) {
			*size = (size_t)length;
		} else {
			free(data);
			data = NULL;
		}
	}

	fclose(file);

	return data;
}

uint32_t test_random(uint64_t *state)
{
	*state ^= *state << 13;
	*state ^= *state >> 7;
	*state ^= *state << 17;

	return (uint32_t)*state;
}

static size_t guarded_span(size_t size)
{
	size_t page = (size_t)sysconf(_SC_PAGESIZE);

	return ((size + page - 1) / page + 2) * page;
}

void *test_guarded(size_t size)
{
	size_t page = (size_t)sysconf(_SC_PAGESIZE);
	size_t span = guarded_span(size);
	uint8_t *base = mmap(NULL, span, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (base == MAP_FAILED) {
		return NULL;
	}

	if (mprotect(base + page, span - 2 * page, PROT_READ | PROT_WRITE) != 0) {
		munmap(base, span);
		return NULL;
	}

	return base + span - page - size;
}

void test_release(void *data, size_t size)
{
	size_t page = (size_t)sysconf(_SC_PAGESIZE);
	size_t span = guarded_span(size);

	if (data != NULL) {
		munmap((uint8_t *)data + size + page - span, span);
	}
}

static void write_text(const char *text)
{
	ssize_t written = write(STDOUT_FILENO, text, strlen(text));

	(void)written;
}

static void crashed(int signal)
{
	static const char *const names[] = {
		[SIGILL] = "illegal instruction",
		[SIGBUS] = "bus error",
		[SIGFPE] = "arithmetic exception",
		[SIGSEGV] = "segmentation fault",
	};

	write_text("CRASH ");
	write_text(current != NULL ? current->suite : "?");
	write_text(".");
	write_text(current != NULL ? current->name : "?");
	write_text(": ");
	write_text(names[signal] != NULL ? names[signal] : "signal");
	write_text("\n");
	_exit(EXIT_FAILURE);
}

static void catch_crashes(void)
{
	static const int signals[] = { SIGILL, SIGBUS, SIGFPE, SIGSEGV };
	struct sigaction action = { .sa_handler = crashed };
	size_t i;

	for (i = 0; i < ARRAY_SIZE(signals); i++) {
		sigaction(signals[i], &action, NULL);
	}
}

static bool selected(const struct test *test, const char *filter)
{
	return filter == NULL || strstr(test->suite, filter) != NULL || strstr(test->name, filter) != NULL;
}

int main(int argc, char **argv)
{
	const char *filter = argc > 1 ? argv[1] : NULL;
	unsigned int passed = 0;
	unsigned int failed = 0;

	setvbuf(stdout, NULL, _IONBF, 0);
	catch_crashes();

	for (current = tests; current != NULL; current = current->next) {
		if (!selected(current, filter)) {
			continue;
		}

		current_failed = false;
		current->run();

		if (current_failed) {
			failed++;
		} else {
			passed++;
		}
	}

	printf("%u passed, %u failed\n", passed, failed);

	return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
