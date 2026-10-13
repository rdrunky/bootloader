#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/console.h>
#include <sys/power.h>

#include "private.h"

#define MENU_ENTRIES_MAX	9

static size_t show_menu(const char *reason, const struct boot_config *config)
{
	size_t count = config != NULL ? config->count : 0;
	size_t i;

	if (count > MENU_ENTRIES_MAX) {
		count = MENU_ENTRIES_MAX;
	}

	printf("\nrecovery\n%s\n\n", reason);

	for (i = 0; i < count; i++) {
		printf("  %zu  start %s\n", i + 1, config->entries[i].name);
	}

	printf("  d  run diagnostics\n");
	printf("  s  open the shell\n");
	printf("  r  restart\n");
	printf("  p  power off\n\nchoose: ");

	return count;
}

static const char *diagnostics_outcome(int err)
{
	if (err == 0) {
		return "diagnostics passed";
	}

	return err == -ECANCELED ? "diagnostics were stopped" : "diagnostics found problems";
}

void recovery_run(const char *why)
{
	const struct boot_config *config = autoboot_config();
	char reason[RECOVERY_REASON_MAX];

	strlcpy(reason, why, sizeof(reason));

	for (;;) {
		size_t entries = show_menu(reason, config);
		int key = tolower(console_getc());

		printf("%c\n", isprint(key) ? key : ' ');

		if (key >= '1' && key < '1' + (int)entries) {
			const struct boot_entry *entry = &config->entries[key - '1'];

			autoboot_entry(config, entry);
			snprintf(reason, sizeof(reason), "%s did not start, %s", entry->name, boot_error());
		} else if (key == 'd') {
			strlcpy(reason, diagnostics_outcome(diagnostics_run()), sizeof(reason));
		} else if (key == 's') {
			return;
		} else if (key == 'r') {
			system_reset();
		} else if (key == 'p') {
			system_poweroff();
		} else {
			strlcpy(reason, "choose one of the listed keys", sizeof(reason));
		}
	}
}
