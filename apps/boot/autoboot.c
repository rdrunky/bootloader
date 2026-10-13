#include <ctype.h>
#include <errno.h>
#include <lib/image.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/console.h>
#include <sys/macros.h>
#include <sys/time.h>

#include "private.h"

#define DETECT_TIMEOUT		3
#define SPEC_MAX		(BLOCKDEV_NAME_MAX + CONFIG_TEXT_MAX)

enum countdown {
	COUNTDOWN_EXPIRED,
	COUNTDOWN_SHELL,
	COUNTDOWN_RECOVERY,
};

static const char *const config_paths[] = { "/boot.cfg", "/boot/boot.cfg" };
static const char *const kernel_paths[] = { "/kernel", "/boot/kernel" };
static const char detected_name[] = "detected";

static struct boot_config *discovered;
static int config_slot;

static int read_text(struct fs *fs, const char *path, char **text, size_t *length)
{
	struct fs_stat stat;
	int err = fs_stat(fs, path, &stat);

	if (err != 0) {
		return err;
	}

	if (stat.kind != FS_KIND_FILE || stat.size > CONFIG_SIZE_MAX) {
		return -EINVAL;
	}

	*text = malloc((size_t)stat.size + 1);
	if (*text == NULL) {
		return -ENOMEM;
	}

	err = fs_read(fs, path, 0, *text, (size_t)stat.size, length);
	if (err != 0) {
		free(*text);
		return err;
	}

	(*text)[*length] = '\0';

	return 0;
}

static int read_config(const char *device, const char *path, struct fs *fs, struct boot_config *config,
		       unsigned int *line)
{
	enum signature signature;
	size_t length;
	char *text;
	int err = read_text(fs, path, &text, &length);

	if (err != 0) {
		return err;
	}

	signature = security_check(fs, path, text, length);
	if (signature != SIGNATURE_VALID && security_locked()) {
		printf("boot.cfg %s %s, refused\n", device, signature_name(signature));
		free(text);
		return -EACCES;
	}

	err = config_parse(text, length, config, line);
	free(text);
	if (err != 0) {
		return err;
	}

	if (config->version < security_minimum_version()) {
		printf("boot.cfg %s version %u below %u, %s\n", device, config->version, security_minimum_version(),
		       security_locked() ? "refused" : "allowed");
		if (security_locked()) {
			return -EACCES;
		}
	}

	strlcpy(config->device, device, sizeof(config->device));
	strlcpy(config->path, path, sizeof(config->path));
	config->signature = signature;

	return 0;
}

static bool load_config(const char *device, struct fs *fs, struct boot_config *config)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(config_paths); i++) {
		unsigned int line = 0;
		int err = read_config(device, config_paths[i], fs, config, &line);

		if (err == 0) {
			return true;
		}

		if (err == -ENOENT || err == -EACCES) {
			continue;
		}

		if (line != 0) {
			printf("boot.cfg %s line %u %s\n", device, line, strerror(-err));
		} else {
			printf("boot.cfg %s %s\n", device, strerror(-err));
		}
	}

	return false;
}

static bool valid_kernel(struct fs *fs, const char *path)
{
	uint8_t header[IMAGE_HEADER_SIZE];
	struct image_info info;
	size_t done;

	return fs_read(fs, path, 0, header, sizeof(header), &done) == 0 &&
	       image_parse(header, done, &info) == 0;
}

static bool detect_kernel(const char *device, struct fs *fs, struct boot_config *config)
{
	struct boot_entry *entry = &config->entries[0];
	size_t i;

	for (i = 0; i < ARRAY_SIZE(kernel_paths); i++) {
		if (!valid_kernel(fs, kernel_paths[i])) {
			continue;
		}

		memset(config, 0, sizeof(*config));
		strlcpy(config->device, device, sizeof(config->device));
		strlcpy(config->path, kernel_paths[i], sizeof(config->path));
		strlcpy(config->default_entry, detected_name, sizeof(config->default_entry));
		config->detected = true;
		config->timeout = DETECT_TIMEOUT;
		config->count = 1;

		strlcpy(entry->name, detected_name, sizeof(entry->name));
		strlcpy(entry->kernel, kernel_paths[i], sizeof(entry->kernel));
		strlcpy(entry->mode, "normal", sizeof(entry->mode));
		return true;
	}

	return false;
}

static bool probe_partition(struct blockdev *partition, const char *name,
			    bool (*probe)(const char *, struct fs *, struct boot_config *), struct boot_config *config)
{
	struct fs *fs;
	bool found;

	if (partition == NULL || fs_mount(partition, &fs) != 0) {
		return false;
	}

	found = probe(name, fs, config);
	fs_unmount(fs);

	return found;
}

static bool load_role(enum role role, unsigned int slot, struct boot_config *config)
{
	char name[LAYOUT_NAME_MAX];

	layout_name(role, slot, name, sizeof(name));

	return probe_partition(layout_partition(role, slot), name, load_config, config);
}

static bool search_slots(struct boot_config *config)
{
	int slot;

	while ((slot = slots_pick()) >= 0) {
		if (load_role(ROLE_SYSTEM, (unsigned int)slot, config)) {
			layout_select((unsigned int)slot);
			config_slot = slot;
			return true;
		}

		slots_fail((unsigned int)slot);
	}

	printf("no bootable slot\n");

	return false;
}

static bool search_layout(struct boot_config *config)
{
	config_slot = -1;

	if (!layout_present()) {
		return false;
	}

	if (layout_slotted(ROLE_SYSTEM)) {
		if (search_slots(config)) {
			return true;
		}
	} else if (load_role(ROLE_SYSTEM, 0, config)) {
		return true;
	}

	return load_role(ROLE_RECOVERY, 0, config);
}

static bool search(bool (*probe)(const char *, struct fs *, struct boot_config *), struct boot_config *config)
{
	struct blockdev *device;

	for (device = blockdev_list(); device != NULL; device = device->next) {
		if (!layout_member(device) && probe_partition(device, device->name, probe, config)) {
			return true;
		}
	}

	return false;
}

static bool search_kernels(struct boot_config *config)
{
	return !security_locked() && search(detect_kernel, config);
}

static enum countdown countdown(const char *name, int seconds)
{
	uint64_t start = time_us();

	printf("starting %s in %d s, r for recovery, any other key for the shell\n", name, seconds);

	while (time_us() - start < (uint64_t)seconds * USEC_PER_SEC) {
		int key = console_poll();

		if (key >= 0) {
			return tolower(key) == 'r' ? COUNTDOWN_RECOVERY : COUNTDOWN_SHELL;
		}
	}

	return COUNTDOWN_EXPIRED;
}

static void resolve(const struct boot_config *config, const char *path, char spec[SPEC_MAX])
{
	if (strchr(path, ':') != NULL) {
		strlcpy(spec, path, SPEC_MAX);
	} else {
		snprintf(spec, SPEC_MAX, "%s:%s", config->device, path);
	}
}

int autoboot_entry(const struct boot_config *config, const struct boot_entry *entry)
{
	struct boot_request request = {
		.mode = entry->mode,
		.kernel_digest = &entry->kernel_digest,
		.ramdisk_digest = &entry->ramdisk_digest,
		.verify_required = config->verify_required || security_locked(),
		.trusted = config->signature == SIGNATURE_VALID,
	};
	char kernel[SPEC_MAX];
	char ramdisk[SPEC_MAX];

	resolve(config, entry->kernel, kernel);
	request.kernel = kernel;

	if (entry->ramdisk[0] != '\0') {
		resolve(config, entry->ramdisk, ramdisk);
		request.ramdisk = ramdisk;
	}

	if (entry->arguments[0] != '\0') {
		request.arguments = entry->arguments;
	}

	return boot_start(&request);
}

const struct boot_config *autoboot_config(void)
{
	return discovered;
}

static void announce(const struct boot_config *config)
{
	if (config->detected) {
		printf("kernel %s:%s detected\n", config->device, config->path);
	} else {
		printf("boot.cfg %s %s\n", config->device, signature_name(config->signature));
	}
}

static void start_default(void)
{
	char reason[RECOVERY_REASON_MAX];

	for (;;) {
		const struct boot_entry *entry = config_find(discovered, NULL);
		struct boot_config *next;

		if (config_slot >= 0) {
			slots_attempt((unsigned int)config_slot);
		}

		autoboot_entry(discovered, entry);
		snprintf(reason, sizeof(reason), "%s did not start, %s", entry->name, boot_error());

		if (config_slot < 0) {
			break;
		}

		slots_fail((unsigned int)config_slot);
		next = malloc(sizeof(*next));
		if (next == NULL || !search_layout(next)) {
			free(next);
			break;
		}

		free(discovered);
		discovered = next;
		announce(discovered);
	}

	recovery_run(reason);
}

void autoboot(void)
{
	struct boot_config *config = malloc(sizeof(*config));
	const struct boot_entry *entry;

	if (config == NULL) {
		printf("autoboot %s\n", strerror(ENOMEM));
		return;
	}

	layout_scan();
	slots_load();

	if (!search_layout(config) && !search(load_config, config) && !search_kernels(config)) {
		free(config);
		printf("nothing to boot\n");
		return;
	}

	discovered = config;
	entry = config_find(config, NULL);
	announce(config);

	if (config->timeout == TIMEOUT_NEVER) {
		return;
	}

	switch (countdown(entry->name, config->timeout)) {
	case COUNTDOWN_EXPIRED:
		start_default();
		break;
	case COUNTDOWN_RECOVERY:
		recovery_run("requested from the countdown");
		break;
	case COUNTDOWN_SHELL:
		break;
	}
}
