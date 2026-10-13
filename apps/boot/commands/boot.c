#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/shell.h>

#include "../private.h"

static int boot_entry_named(const char *name)
{
	const struct boot_config *config = autoboot_config();
	const struct boot_entry *entry;

	if (config == NULL) {
		printf("boot: no boot.cfg was found\n");
		return -ECANCELED;
	}

	entry = config_find(config, name);
	if (entry == NULL) {
		printf("boot: no entry '%s' in %s:%s\n", name != NULL ? name : "default", config->device, config->path);
		return -ECANCELED;
	}

	return autoboot_entry(config, entry);
}

COMMAND(boot, "[entry] | kernel [-r ramdisk] [-a arguments] [-m normal|recovery]", "start a kernel or a boot entry")
{
	struct boot_request request = { .mode = "normal" };
	int i;

	if (argc == 1) {
		return boot_entry_named(NULL);
	}

	if (argc == 2 && strchr(argv[1], ':') == NULL) {
		return boot_entry_named(argv[1]);
	}

	if (security_locked()) {
		printf("boot: a locked machine starts only entries of a signed boot.cfg\n");
		return -ECANCELED;
	}

	for (i = 1; i < argc; i++) {
		bool valued = i + 1 < argc;

		if (strcmp(argv[i], "-r") == 0 && valued) {
			request.ramdisk = argv[++i];
		} else if (strcmp(argv[i], "-a") == 0 && valued) {
			request.arguments = argv[++i];
		} else if (strcmp(argv[i], "-m") == 0 && valued) {
			request.mode = argv[++i];
		} else if (argv[i][0] != '-' && request.kernel == NULL) {
			request.kernel = argv[i];
		} else {
			return SHELL_USAGE;
		}
	}

	if (request.kernel == NULL || !boot_mode_known(request.mode)) {
		return SHELL_USAGE;
	}

	return boot_start(&request);
}

COMMAND(entries, "", "list the entries of the boot configuration")
{
	const struct boot_config *config = autoboot_config();
	const struct boot_entry *chosen;
	size_t i;

	if (argc != 1) {
		return SHELL_USAGE;
	}

	if (config == NULL) {
		printf("no boot.cfg was found\n");
		return 0;
	}

	chosen = config_find(config, NULL);
	printf("%s:%s, %s\n", config->device, config->path,
	       config->detected ? "detected without boot.cfg" : signature_name(config->signature));
	for (i = 0; i < config->count; i++) {
		const struct boot_entry *entry = &config->entries[i];

		printf("  %c %-16s %-8s %s\n", entry == chosen ? '*' : ' ', entry->name, entry->mode, entry->kernel);
	}

	return 0;
}

static int slot_named(const char *name)
{
	unsigned int slot;

	for (slot = 0; slot < SLOT_COUNT; slot++) {
		if (name[0] == layout_slot_name(slot) && name[1] == '\0') {
			return (int)slot;
		}
	}

	return -1;
}

COMMAND(slot, "[a|b|confirm]", "show the a/b slots, make one active or confirm the running one")
{
	int slot;
	int err;

	if (argc > 2) {
		return SHELL_USAGE;
	}

	if (!layout_slotted(ROLE_SYSTEM)) {
		printf("slot: no a/b layout\n");
		return -ECANCELED;
	}

	if (argc == 2) {
		slot = slot_named(argv[1]);
		if (strcmp(argv[1], "confirm") == 0) {
			err = slots_confirm(layout_slot());
		} else if (slot >= 0) {
			err = slots_activate((unsigned int)slot);
		} else {
			return SHELL_USAGE;
		}

		if (err != 0) {
			return err;
		}
	}

	slots_print();

	return 0;
}
