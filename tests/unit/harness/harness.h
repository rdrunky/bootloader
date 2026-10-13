#ifndef HARNESS_H
#define HARNESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct test {
	const char	*suite;
	const char	*name;
	void		(*run)(void);
	struct test	*next;
};

void test_register(struct test *test);
void test_fail(const char *file, int line, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
void *test_load(const char *path, size_t *size);
void *test_guarded(size_t size);
void test_release(void *data, size_t size);
uint32_t test_random(uint64_t *state);

#define TEST(suite, name)								\
	static void suite##_##name(void);						\
	static struct test suite##_##name##_entry = { #suite, #name, suite##_##name, NULL }; \
	__attribute__((constructor)) static void suite##_##name##_register(void)	\
	{										\
		test_register(&suite##_##name##_entry);					\
	}										\
	static void suite##_##name(void)

#define CHECK(condition) do {								\
	if (!(condition)) {								\
		test_fail(__FILE__, __LINE__, "%s", #condition);			\
		return;									\
	}										\
} while (0)

#define CHECK_EQ(actual, expected) do {							\
	long long _actual = (long long)(actual);					\
	long long _expected = (long long)(expected);					\
	if (_actual != _expected) {							\
		test_fail(__FILE__, __LINE__, "%s is %lld (%#llx), expected %lld (%#llx)", \
			  #actual, _actual, (unsigned long long)_actual,		\
			  _expected, (unsigned long long)_expected);			\
		return;									\
	}										\
} while (0)

#define CHECK_STR(actual, expected) do {						\
	const char *_actual = (actual);							\
	const char *_expected = (expected);						\
	if (_actual == NULL || strcmp(_actual, _expected) != 0) {			\
		test_fail(__FILE__, __LINE__, "%s is \"%s\", expected \"%s\"",		\
			  #actual, _actual != NULL ? _actual : "(null)", _expected);	\
		return;									\
	}										\
} while (0)

#endif
