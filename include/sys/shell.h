#ifndef SYS_SHELL_H
#define SYS_SHELL_H

#include <compiler.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/linkerset.h>

#define SHELL_ARGS_MAX		32
#define SHELL_LINE_MAX		256
#define SHELL_COMMANDS_MAX	64
#define SHELL_USAGE		1

struct command {
	const char	*name;
	const char	*usage;
	const char	*summary;
	int		(*run)(int argc, char **argv);
	bool		developer;
};

#define SHELL_COMMAND(name, usage, summary, developer)						\
	static int command_##name(int argc __unused, char **argv __unused);			\
	static const struct command command_##name##_entry = { #name, usage, summary, command_##name, developer }; \
	SET_ENTRY(commands, command_##name##_entry);						\
	static int command_##name(int argc __unused, char **argv __unused)

#define COMMAND(name, usage, summary)		SHELL_COMMAND(name, usage, summary, false)
#define DEVELOPER_COMMAND(name, usage, summary)	SHELL_COMMAND(name, usage, summary, true)

void shell_lock(void);
const struct command *shell_find(const char *name);
size_t shell_commands(const struct command **list, size_t max);
size_t shell_complete(const char *prefix, size_t length, const char **matches, size_t max);
void shell_run(const char *prompt) __noreturn;

#endif
