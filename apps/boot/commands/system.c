#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/power.h>
#include <sys/shell.h>
#include <sys/time.h>

#include "../private.h"

COMMAND(help, "[command]", "list the commands or describe one")
{
	const struct command *list[SHELL_COMMANDS_MAX];
	const struct command *command;
	size_t width = 0;
	size_t count;
	size_t i;

	if (argc > 2) {
		return SHELL_USAGE;
	}

	if (argc == 2) {
		command = shell_find(argv[1]);
		if (command == NULL) {
			printf("help: no command '%s'\n", argv[1]);
			return -ECANCELED;
		}

		printf("%s%s%s\n  %s\n", command->name, *command->usage != '\0' ? " " : "", command->usage,
		       command->summary);
		return 0;
	}

	count = shell_commands(list, SHELL_COMMANDS_MAX);
	for (i = 0; i < count; i++) {
		size_t length = strlen(list[i]->name);

		width = length > width ? length : width;
	}

	for (i = 0; i < count; i++) {
		printf("  %-*s  %s\n", (int)width, list[i]->name, list[i]->summary);
	}

	return 0;
}

COMMAND(echo, "[text...]", "print the arguments")
{
	int i;

	for (i = 1; i < argc; i++) {
		printf("%s%s", argv[i], i + 1 < argc ? " " : "");
	}

	printf("\n");

	return 0;
}

COMMAND(uptime, "", "show the time since the counter started")
{
	uint64_t us = time_us();

	if (argc != 1) {
		return SHELL_USAGE;
	}

	printf("%lu.%06lu s\n", us / USEC_PER_SEC, us % USEC_PER_SEC);

	return 0;
}

COMMAND(recovery, "", "open the recovery menu")
{
	if (argc != 1) {
		return SHELL_USAGE;
	}

	recovery_run("requested from the shell");

	return 0;
}

COMMAND(diagnostics, "", "test memory, disks, files and the heap")
{
	if (argc != 1) {
		return SHELL_USAGE;
	}

	return diagnostics_run() == 0 ? 0 : -ECANCELED;
}

COMMAND(reset, "", "restart the machine")
{
	if (argc != 1) {
		return SHELL_USAGE;
	}

	system_reset();
}

COMMAND(poweroff, "", "turn the machine off")
{
	if (argc != 1) {
		return SHELL_USAGE;
	}

	system_poweroff();
}
