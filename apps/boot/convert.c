#include <ctype.h>
#include <errno.h>
#include <stdio.h>

#include "private.h"

static int digit_value(char c)
{
	if (isdigit((unsigned char)c)) {
		return c - '0';
	}

	if (isxdigit((unsigned char)c)) {
		return tolower((unsigned char)c) - 'a' + 10;
	}

	return -1;
}

int parse_number(const char *text, uint64_t *value)
{
	unsigned int base = 10;
	uint64_t result = 0;

	if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
		base = 16;
		text += 2;
	} else if (text[0] == '0' && (text[1] == 'b' || text[1] == 'B')) {
		base = 2;
		text += 2;
	}

	if (*text == '\0') {
		return -EINVAL;
	}

	for (; *text != '\0'; text++) {
		int digit = digit_value(*text);

		if (digit < 0 || (unsigned int)digit >= base) {
			return -EINVAL;
		}

		if (result > (UINT64_MAX - (uint64_t)digit) / base) {
			return -ERANGE;
		}

		result = result * base + (uint64_t)digit;
	}

	*value = result;

	return 0;
}

int parse_digest(const char *text, size_t length, uint8_t digest[SHA256_SIZE])
{
	size_t i;

	if (length != SHA256_SIZE * 2) {
		return -EINVAL;
	}

	for (i = 0; i < SHA256_SIZE; i++) {
		int high = digit_value(text[i * 2]);
		int low = digit_value(text[i * 2 + 1]);

		if (high < 0 || low < 0) {
			return -EINVAL;
		}

		digest[i] = (uint8_t)(high << 4 | low);
	}

	return 0;
}

void format_digest(const uint8_t digest[SHA256_SIZE], char *text)
{
	size_t i;

	for (i = 0; i < SHA256_SIZE; i++) {
		snprintf(text + i * 2, 3, "%02x", digest[i]);
	}
}
