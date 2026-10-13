#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/readline.h>
#include <sys/shell.h>

enum separator {
	SEPARATOR_NONE,
	SEPARATOR_END,
	SEPARATOR_SEQUENCE,
	SEPARATOR_AND,
};

struct statement {
	char		*argv[SHELL_ARGS_MAX + 1];
	int		argc;
	enum separator	separator;
};

SET_DECLARE(commands, const struct command);

static bool locked;

void shell_lock(void)
{
	locked = true;
}

static bool available(const struct command *command)
{
	return !command->developer || !locked;
}

static const struct command *lookup(const char *name)
{
	const struct command *command;

	SET_FOREACH(command, commands) {
		if (strcmp(command->name, name) == 0) {
			return command;
		}
	}

	return NULL;
}

const struct command *shell_find(const char *name)
{
	const struct command *command = lookup(name);

	return command != NULL && available(command) ? command : NULL;
}

size_t shell_commands(const struct command **list, size_t max)
{
	const struct command *command;
	size_t count = 0;

	SET_FOREACH(command, commands) {
		size_t at = count < max ? count : max;

		if (!available(command)) {
			continue;
		}

		while (at > 0 && strcmp(list[at - 1]->name, command->name) > 0) {
			if (at < max) {
				list[at] = list[at - 1];
			}
			at--;
		}

		if (at < max) {
			list[at] = command;
		}

		count++;
	}

	return count < max ? count : max;
}

size_t shell_complete(const char *prefix, size_t length, const char **matches, size_t max)
{
	const struct command *list[SHELL_COMMANDS_MAX];
	size_t total = shell_commands(list, SHELL_COMMANDS_MAX);
	size_t count = 0;
	size_t i;

	for (i = 0; i < total && count < max; i++) {
		if (strncmp(list[i]->name, prefix, length) == 0) {
			matches[count++] = list[i]->name;
		}
	}

	return count;
}

static bool is_blank(char c)
{
	return c == ' ' || c == '\t';
}

static enum separator separator_at(const char *p, size_t *length)
{
	*length = 0;

	if (*p == '\0') {
		return SEPARATOR_END;
	}

	if (*p == ';') {
		*length = 1;
		return SEPARATOR_SEQUENCE;
	}

	if (p[0] == '&' && p[1] == '&') {
		*length = 2;
		return SEPARATOR_AND;
	}

	return SEPARATOR_NONE;
}

static char *parse_word(char *read, char **end, int *err)
{
	char *write = read;
	char quote = 0;
	size_t length;

	while (*read != '\0') {
		char c = *read;

		if (quote == 0 && (is_blank(c) || separator_at(read, &length) != SEPARATOR_NONE)) {
			break;
		}

		read++;

		if (c == quote) {
			quote = 0;
			continue;
		}

		if (quote == 0 && (c == '"' || c == '\'')) {
			quote = c;
			continue;
		}

		if (c == '\\' && quote != '\'' && *read != '\0') {
			c = *read++;
		}

		*write++ = c;
	}

	*end = write;
	*err = quote != 0 ? -EINVAL : 0;

	return read;
}

static char *parse_statement(char *read, struct statement *statement, int *err)
{
	size_t length;

	statement->argc = 0;
	*err = 0;

	for (;;) {
		char *word;
		char *end;

		while (is_blank(*read)) {
			read++;
		}

		statement->separator = separator_at(read, &length);
		if (statement->separator != SEPARATOR_NONE) {
			read += length;
			break;
		}

		if (statement->argc == SHELL_ARGS_MAX) {
			*err = -E2BIG;
			break;
		}

		word = read;
		read = parse_word(read, &end, err);
		if (*err != 0) {
			break;
		}

		statement->argv[statement->argc++] = word;
		statement->separator = separator_at(read, &length);
		read += statement->separator == SEPARATOR_NONE ? 1 : length;
		*end = '\0';

		if (statement->separator != SEPARATOR_NONE) {
			break;
		}
	}

	statement->argv[statement->argc] = NULL;

	return read;
}

static void print_usage(const struct command *command)
{
	printf("usage: %s%s%s\n", command->name, *command->usage != '\0' ? " " : "", command->usage);
}

static int run(int argc, char **argv)
{
	const struct command *command = lookup(argv[0]);
	int result;

	if (command == NULL) {
		printf("%s: unknown command, try help\n", argv[0]);
		return -ENOENT;
	}

	if (!available(command)) {
		printf("%s: only available on a developer machine\n", argv[0]);
		return -EPERM;
	}

	result = command->run(argc, argv);

	if (result == SHELL_USAGE) {
		print_usage(command);
	} else if (result < 0 && result != -ECANCELED) {
		printf("%s: %s\n", argv[0], strerror(-result));
	}

	return result;
}

static int execute(char *line)
{
	struct statement statement;
	bool skipping = false;
	int result = 0;
	int err;

	do {
		line = parse_statement(line, &statement, &err);
		if (err != 0) {
			printf("shell: %s\n", err == -E2BIG ? "too many arguments" : "unterminated quote");
			return err;
		}

		if (statement.argc > 0 && !skipping) {
			result = run(statement.argc, statement.argv);
		}

		skipping = statement.separator == SEPARATOR_AND && (skipping || result != 0);
	} while (statement.separator != SEPARATOR_END);

	return result;
}

void shell_run(const char *prompt)
{
	char line[SHELL_LINE_MAX];

	for (;;) {
		if (readline(prompt, line, sizeof(line)) > 0) {
			execute(line);
		}
	}
}
