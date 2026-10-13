#include <ctype.h>
#include <errno.h>
#include <string.h>

#include "private.h"

#define TIMEOUT_DEFAULT		3
#define TIMEOUT_MAX		600

struct parser {
	struct boot_config	*config;
	struct boot_entry	*entry;
	unsigned int		line;
};

static bool is_blank(char c)
{
	return c == ' ' || c == '\t';
}

static const char *trim(const char *text, size_t *length)
{
	while (*length > 0 && is_blank(text[0])) {
		text++;
		(*length)--;
	}

	while (*length > 0 && (is_blank(text[*length - 1]) || text[*length - 1] == '\r')) {
		(*length)--;
	}

	return text;
}

static bool printable(const char *text, size_t length)
{
	size_t i;

	for (i = 0; i < length; i++) {
		if (!isprint((unsigned char)text[i]) && text[i] != '\t') {
			return false;
		}
	}

	return true;
}

static int copy_value(char *target, size_t size, const char *value, size_t length)
{
	if (length == 0 || length >= size) {
		return -EINVAL;
	}

	memcpy(target, value, length);
	target[length] = '\0';

	return 0;
}

static bool keyword_is(const char *keyword, size_t length, const char *expected)
{
	return strlen(expected) == length && memcmp(keyword, expected, length) == 0;
}

static int parse_decimal(const char *value, size_t length, uint64_t limit, uint64_t *number)
{
	uint64_t result = 0;
	size_t i;

	if (length == 0) {
		return -EINVAL;
	}

	for (i = 0; i < length; i++) {
		uint64_t digit;

		if (!isdigit((unsigned char)value[i])) {
			return -EINVAL;
		}

		digit = (uint64_t)(value[i] - '0');
		if (result > (limit - digit) / 10) {
			return -ERANGE;
		}

		result = result * 10 + digit;
	}

	*number = result;

	return 0;
}

static int parse_timeout(const char *value, size_t length, int *timeout)
{
	uint64_t seconds;
	int err;

	if (keyword_is(value, length, "never")) {
		*timeout = TIMEOUT_NEVER;
		return 0;
	}

	err = parse_decimal(value, length, TIMEOUT_MAX, &seconds);
	if (err == 0) {
		*timeout = (int)seconds;
	}

	return err;
}

static int parse_version(const char *value, size_t length, uint32_t *version)
{
	uint64_t number;
	int err = parse_decimal(value, length, UINT32_MAX, &number);

	if (err == 0) {
		*version = (uint32_t)number;
	}

	return err;
}

static int start_entry(struct parser *parser, const char *name, size_t length)
{
	struct boot_config *config = parser->config;
	struct boot_entry *entry;
	int err;

	if (config->count == CONFIG_ENTRIES_MAX) {
		return -E2BIG;
	}

	entry = &config->entries[config->count];
	err = copy_value(entry->name, sizeof(entry->name), name, length);
	if (err != 0 || memchr(name, ' ', length) != NULL || memchr(name, ':', length) != NULL) {
		return -EINVAL;
	}

	if (config_find(config, entry->name) != NULL) {
		return -EEXIST;
	}

	strlcpy(entry->mode, "normal", sizeof(entry->mode));
	entry->line = parser->line;
	parser->entry = entry;
	config->count++;

	return 0;
}

static int set_digest(struct digest *digest, const char *value, size_t length)
{
	int err = parse_digest(value, length, digest->value);

	digest->present = err == 0;

	return err;
}

static int set_entry_value(struct boot_entry *entry, const char *keyword, size_t keyword_length, const char *value,
			   size_t length)
{
	if (keyword_is(keyword, keyword_length, "kernel-sha256")) {
		return set_digest(&entry->kernel_digest, value, length);
	}

	if (keyword_is(keyword, keyword_length, "ramdisk-sha256")) {
		return set_digest(&entry->ramdisk_digest, value, length);
	}

	if (keyword_is(keyword, keyword_length, "kernel")) {
		return copy_value(entry->kernel, sizeof(entry->kernel), value, length);
	}

	if (keyword_is(keyword, keyword_length, "ramdisk")) {
		return copy_value(entry->ramdisk, sizeof(entry->ramdisk), value, length);
	}

	if (keyword_is(keyword, keyword_length, "args")) {
		return copy_value(entry->arguments, sizeof(entry->arguments), value, length);
	}

	if (keyword_is(keyword, keyword_length, "mode")) {
		int err = copy_value(entry->mode, sizeof(entry->mode), value, length);

		return err == 0 && !boot_mode_known(entry->mode) ? -EINVAL : err;
	}

	return -EINVAL;
}

static int parse_line(struct parser *parser, const char *line, size_t length)
{
	struct boot_config *config = parser->config;
	size_t keyword_length = 0;
	const char *value;
	size_t value_length;

	while (keyword_length < length && !is_blank(line[keyword_length])) {
		keyword_length++;
	}

	value_length = length - keyword_length;
	value = trim(line + keyword_length, &value_length);

	if (keyword_is(line, keyword_length, "default")) {
		config->default_line = parser->line;
		return copy_value(config->default_entry, sizeof(config->default_entry), value, value_length);
	}

	if (keyword_is(line, keyword_length, "timeout")) {
		return parse_timeout(value, value_length, &config->timeout);
	}

	if (keyword_is(line, keyword_length, "version")) {
		return parse_version(value, value_length, &config->version);
	}

	if (keyword_is(line, keyword_length, "verify")) {
		if (keyword_is(value, value_length, "required") || keyword_is(value, value_length, "optional")) {
			config->verify_required = keyword_is(value, value_length, "required");
			return 0;
		}
		return -EINVAL;
	}

	if (keyword_is(line, keyword_length, "entry")) {
		return start_entry(parser, value, value_length);
	}

	if (parser->entry == NULL) {
		return -EINVAL;
	}

	return set_entry_value(parser->entry, line, keyword_length, value, value_length);
}

static int check_config(const struct boot_config *config, unsigned int *line)
{
	size_t i;

	if (config->count == 0) {
		*line = 0;
		return -ENOENT;
	}

	for (i = 0; i < config->count; i++) {
		const struct boot_entry *entry = &config->entries[i];

		if (entry->kernel[0] == '\0' || (entry->ramdisk_digest.present && entry->ramdisk[0] == '\0')) {
			*line = entry->line;
			return -EINVAL;
		}
	}

	if (config->default_entry[0] != '\0' && config_find(config, config->default_entry) == NULL) {
		*line = config->default_line;
		return -ENOENT;
	}

	return 0;
}

int config_parse(const char *text, size_t length, struct boot_config *config, unsigned int *line)
{
	struct parser parser = { .config = config };
	size_t offset = 0;

	memset(config, 0, sizeof(*config));
	config->timeout = TIMEOUT_DEFAULT;

	if (length > CONFIG_SIZE_MAX) {
		*line = 0;
		return -EFBIG;
	}

	while (offset < length) {
		const char *start = text + offset;
		const char *newline = memchr(start, '\n', length - offset);
		size_t raw = newline != NULL ? (size_t)(newline - start) : length - offset;
		size_t trimmed = raw;
		const char *content = trim(start, &trimmed);
		int err = 0;

		parser.line++;
		offset += raw + 1;

		if (trimmed == 0 || content[0] == '#') {
			continue;
		}

		if (!printable(content, trimmed)) {
			err = -EINVAL;
		} else {
			err = parse_line(&parser, content, trimmed);
		}

		if (err != 0) {
			*line = parser.line;
			return err;
		}
	}

	return check_config(config, line);
}

bool boot_mode_known(const char *mode)
{
	return strcmp(mode, "normal") == 0 || strcmp(mode, "recovery") == 0;
}

const struct boot_entry *config_find(const struct boot_config *config, const char *name)
{
	size_t i;

	if (name == NULL) {
		name = config->default_entry;
		if (name[0] == '\0') {
			return config->count > 0 ? &config->entries[0] : NULL;
		}
	}

	for (i = 0; i < config->count; i++) {
		if (strcmp(config->entries[i].name, name) == 0) {
			return &config->entries[i];
		}
	}

	return NULL;
}
