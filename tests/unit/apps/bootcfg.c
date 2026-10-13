#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../../apps/boot/private.h"
#include "harness.h"

#define FUZZ_ROUNDS	20000

static struct boot_config config;

static int parse(const char *text, unsigned int *line)
{
	return config_parse(text, strlen(text), &config, line);
}

TEST(config, reads_entries_and_defaults)
{
	unsigned int line = 99;
	const struct boot_entry *entry;

	CHECK_EQ(parse("# boot menu\r\n"
		       "default recovery\n"
		       "timeout 7\n"
		       "\n"
		       "entry system\n"
		       "\tkernel /kernel\n"
		       "\tramdisk /ramdisk\n"
		       "\targs console=serial quiet   \n"
		       "entry recovery\n"
		       "  kernel virtio0p2:/boot/kernel\n"
		       "  mode recovery\n", &line), 0);
	CHECK_EQ(config.count, 2);
	CHECK_EQ(config.timeout, 7);

	entry = config_find(&config, NULL);
	CHECK(entry != NULL);
	CHECK_STR(entry->name, "recovery");
	CHECK_STR(entry->kernel, "virtio0p2:/boot/kernel");
	CHECK_STR(entry->mode, "recovery");
	CHECK_STR(entry->ramdisk, "");

	entry = config_find(&config, "system");
	CHECK(entry != NULL);
	CHECK_STR(entry->ramdisk, "/ramdisk");
	CHECK_STR(entry->arguments, "console=serial quiet");
	CHECK_STR(entry->mode, "normal");
	CHECK(config_find(&config, "missing") == NULL);
}

TEST(config, picks_first_entry_without_default)
{
	unsigned int line;

	CHECK_EQ(parse("entry one\nkernel /a\nentry two\nkernel /b\ntimeout never\n", &line), 0);
	CHECK_STR(config_find(&config, NULL)->name, "one");
	CHECK_EQ(config.timeout, TIMEOUT_NEVER);
	CHECK_EQ(parse("entry one\nkernel /a\n", &line), 0);
	CHECK_EQ(config.timeout, 3);
}

TEST(config, reports_the_broken_line)
{
	unsigned int line;

	CHECK_EQ(parse("entry one\nkernel /a\nfrobnicate yes\n", &line), -EINVAL);
	CHECK_EQ(line, 3);
	CHECK_EQ(parse("kernel /a\n", &line), -EINVAL);
	CHECK_EQ(line, 1);
	CHECK_EQ(parse("entry one\nkernel /a\nentry one\nkernel /b\n", &line), -EEXIST);
	CHECK_EQ(line, 3);
	CHECK_EQ(parse("entry one\nmode sideways\n", &line), -EINVAL);
	CHECK_EQ(line, 2);
	CHECK_EQ(parse("entry one\nramdisk /r\n", &line), -EINVAL);
	CHECK_EQ(line, 1);
	CHECK_EQ(parse("default two\nentry one\nkernel /a\n", &line), -ENOENT);
	CHECK_EQ(line, 1);
	CHECK_EQ(parse("timeout 601\nentry one\nkernel /a\n", &line), -ERANGE);
	CHECK_EQ(parse("timeout -1\nentry one\nkernel /a\n", &line), -EINVAL);
	CHECK_EQ(parse("entry bad:name\nkernel /a\n", &line), -EINVAL);
	CHECK_EQ(parse("entry one\nkernel /a\x01\n", &line), -EINVAL);
	CHECK_EQ(line, 2);
	CHECK_EQ(parse("# nothing\n", &line), -ENOENT);
	CHECK_EQ(parse("entry one\nkernel\n", &line), -EINVAL);
}

#define EMPTY_DIGEST "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
#define ABC_DIGEST "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"

TEST(config, reads_digests_and_policy)
{
	const struct boot_entry *entry;
	unsigned int line;

	CHECK_EQ(parse("verify required\nentry one\nkernel /k\nkernel-sha256 " EMPTY_DIGEST "\n"
		       "ramdisk /r\nramdisk-sha256 " ABC_DIGEST "\nentry two\nkernel /k\n", &line), 0);
	CHECK(config.verify_required);
	entry = config_find(&config, "one");
	CHECK(entry->kernel_digest.present);
	CHECK_EQ(entry->kernel_digest.value[0], 0xe3);
	CHECK_EQ(entry->kernel_digest.value[31], 0x55);
	CHECK(entry->ramdisk_digest.present);
	CHECK_EQ(entry->ramdisk_digest.value[0], 0xba);
	CHECK(!config_find(&config, "two")->kernel_digest.present);

	CHECK_EQ(parse("entry one\nkernel /k\n", &line), 0);
	CHECK(!config.verify_required);
	CHECK_EQ(parse("verify sometimes\nentry one\nkernel /k\n", &line), -EINVAL);
	CHECK_EQ(line, 1);
	CHECK_EQ(parse("entry one\nkernel /k\nkernel-sha256 " EMPTY_DIGEST "0\n", &line), -EINVAL);
	CHECK_EQ(line, 3);
	CHECK_EQ(parse("entry one\nkernel /k\nkernel-sha256 x3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\n",
		       &line), -EINVAL);
	CHECK_EQ(parse("entry one\nkernel /k\nramdisk-sha256 " EMPTY_DIGEST "\n", &line), -EINVAL);
	CHECK_EQ(line, 1);
}

TEST(config, reads_the_version)
{
	unsigned int line;

	CHECK_EQ(parse("entry one\nkernel /k\n", &line), 0);
	CHECK_EQ(config.version, 0);
	CHECK_EQ(parse("version 7\nentry one\nkernel /k\n", &line), 0);
	CHECK_EQ(config.version, 7);
	CHECK_EQ(parse("entry one\nkernel /k\nversion 4294967295\n", &line), 0);
	CHECK_EQ(config.version, 4294967295U);
	CHECK_EQ(parse("version 4294967296\nentry one\nkernel /k\n", &line), -ERANGE);
	CHECK_EQ(line, 1);
	CHECK_EQ(parse("version 12345678901\nentry one\nkernel /k\n", &line), -ERANGE);
	CHECK_EQ(parse("version 000000000000042\nentry one\nkernel /k\n", &line), 0);
	CHECK_EQ(config.version, 42);
	CHECK_EQ(parse("version 1x\nentry one\nkernel /k\n", &line), -EINVAL);
	CHECK_EQ(parse("version -1\nentry one\nkernel /k\n", &line), -EINVAL);
	CHECK_EQ(parse("version\nentry one\nkernel /k\n", &line), -EINVAL);
}

TEST(config, refuses_oversized_input)
{
	char *text = malloc(CONFIG_SIZE_MAX + 2);
	unsigned int line;
	char value[CONFIG_TEXT_MAX + 16];
	char document[CONFIG_TEXT_MAX + 64];

	CHECK(text != NULL);
	memset(text, '#', CONFIG_SIZE_MAX + 1);
	CHECK_EQ(config_parse(text, CONFIG_SIZE_MAX + 1, &config, &line), -EFBIG);
	free(text);

	memset(value, 'k', sizeof(value) - 1);
	value[sizeof(value) - 1] = '\0';
	snprintf(document, sizeof(document), "entry one\nkernel /%s\n", value);
	CHECK_EQ(parse(document, &line), -EINVAL);
	CHECK_EQ(line, 2);
}

TEST(config, refuses_too_many_entries)
{
	char document[4096];
	size_t length = 0;
	unsigned int line;
	unsigned int i;

	for (i = 0; i <= CONFIG_ENTRIES_MAX; i++) {
		length += (size_t)snprintf(document + length, sizeof(document) - length, "entry e%u\nkernel /k\n", i);
	}

	CHECK_EQ(parse(document, &line), -E2BIG);
	CHECK_EQ(line, CONFIG_ENTRIES_MAX * 2 + 1);
}

static uint64_t random_state = 0xbb67ae8584caa73bULL;

TEST(config, survives_fuzzed_text)
{
	static const char seed[] = "default system\ntimeout 2\n\nentry system\n\tkernel /payload\n\tramdisk /ramdisk\n"
				   "\targs console=serial quiet\n\tmode normal\nentry recovery\n\tkernel /k\n";
	static const char alphabet[] = "entry kernel\n \t:/#default timeout never 0123456789\r\x01\xff";
	size_t length = sizeof(seed) - 1;
	char *work = test_guarded(length);
	unsigned int round;

	CHECK(work != NULL);

	for (round = 0; round < FUZZ_ROUNDS; round++) {
		unsigned int line = 0;
		unsigned int changes = 1 + test_random(&random_state) % 6;
		int err;

		memcpy(work, seed, length);
		while (changes-- > 0) {
			work[test_random(&random_state) % length] = alphabet[test_random(&random_state) % (sizeof(alphabet) - 1)];
		}

		err = config_parse(work, length, &config, &line);
		if (err == 0) {
			const struct boot_entry *entry = config_find(&config, NULL);

			CHECK(entry != NULL);
			CHECK(entry->kernel[0] != '\0');
			CHECK(boot_mode_known(entry->mode));
		} else {
			CHECK(line <= 12);
		}
	}

	test_release(work, length);
}
