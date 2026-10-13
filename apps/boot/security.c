#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/fuse.h>
#include <sys/shell.h>

#include "private.h"

#define FUSE_VALUE_MAX		16
#define POLICY_FUSE		"policy"
#define ROLLBACK_FUSE		"rollback"
#define SIGNATURE_SUFFIX	".sig"
#define SIGNATURE_PATH_MAX	(CONFIG_PATH_MAX + sizeof(SIGNATURE_SUFFIX))

extern const uint8_t trusted_key[ED25519_PUBLIC_KEY_SIZE];

static bool locked;
static uint32_t minimum_version;

static int read_fuse(const char *name, char value[FUSE_VALUE_MAX])
{
	size_t length = 0;
	int err = fuse_read(name, value, FUSE_VALUE_MAX - 1, &length);

	if (err != 0) {
		return err;
	}

	while (length > 0 && (value[length - 1] == '\n' || value[length - 1] == '\0')) {
		length--;
	}

	if (memchr(value, '\0', length) != NULL) {
		return -EINVAL;
	}

	value[length] = '\0';

	return 0;
}

static void read_policy(void)
{
	char value[FUSE_VALUE_MAX];
	int err = read_fuse(POLICY_FUSE, value);

	if (err == -ENODEV || err == -ENOENT) {
		locked = false;
	} else if (err == 0 && strcmp(value, "developer") == 0) {
		locked = false;
	} else if (err == 0 && strcmp(value, "locked") == 0) {
		locked = true;
	} else {
		printf("policy fuse unreadable\n");
		locked = true;
	}
}

static void read_rollback(void)
{
	char value[FUSE_VALUE_MAX];
	uint64_t number;
	int err = read_fuse(ROLLBACK_FUSE, value);

	if (err == -ENODEV || err == -ENOENT) {
		return;
	}

	if (err != 0 || parse_number(value, &number) != 0 || number > UINT32_MAX) {
		printf("rollback fuse unreadable\n");
		minimum_version = UINT32_MAX;
		return;
	}

	minimum_version = (uint32_t)number;
	printf("rollback %u\n", minimum_version);
}

void security_init(void)
{
	read_policy();
	if (locked) {
		shell_lock();
	}

	printf("%s\n", security_policy());
	read_rollback();
}

bool security_locked(void)
{
	return locked;
}

uint32_t security_minimum_version(void)
{
	return minimum_version;
}

const char *security_policy(void)
{
	return locked ? "locked" : "developer";
}

const char *signature_name(enum signature signature)
{
	switch (signature) {
	case SIGNATURE_VALID:
		return "signed";
	case SIGNATURE_INVALID:
		return "bad signature";
	default:
		return "not signed";
	}
}

enum signature security_check(struct fs *fs, const char *path, const void *data, size_t length)
{
	uint8_t signature[ED25519_SIGNATURE_SIZE + 1];
	char signature_path[SIGNATURE_PATH_MAX];
	size_t done;
	int err;

	if (snprintf(signature_path, sizeof(signature_path), "%s%s", path, SIGNATURE_SUFFIX) >=
	    (int)sizeof(signature_path)) {
		return SIGNATURE_INVALID;
	}

	err = fs_read(fs, signature_path, 0, signature, sizeof(signature), &done);
	if (err == -ENOENT) {
		return SIGNATURE_MISSING;
	}

	if (err != 0 || done != ED25519_SIGNATURE_SIZE) {
		return SIGNATURE_INVALID;
	}

	return ed25519_verify(trusted_key, data, length, signature) ? SIGNATURE_VALID : SIGNATURE_INVALID;
}
