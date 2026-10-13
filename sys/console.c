#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/console.h>
#include <sys/devicetree.h>
#include <sys/driver.h>

static struct console *consoles;
static char history[CONSOLE_HISTORY_SIZE];
static size_t history_length;

static void emit(struct console *console, const char *data, size_t len)
{
	while (len > 0) {
		const char *newline = memchr(data, '\n', len);
		size_t chunk = newline != NULL ? (size_t)(newline - data) : len;

		if (chunk > 0) {
			console->write(console, data, chunk);
		}

		if (newline == NULL) {
			return;
		}

		console->write(console, "\r\n", 2);
		data += chunk + 1;
		len -= chunk + 1;
	}
}

static void record(const char *data, size_t len)
{
	while (len-- > 0) {
		history[history_length++ % CONSOLE_HISTORY_SIZE] = *data++;
	}
}

static void history_spans(size_t limit, size_t *start, size_t *first, size_t *second)
{
	size_t kept = history_length < CONSOLE_HISTORY_SIZE ? history_length : CONSOLE_HISTORY_SIZE;

	if (kept > limit) {
		kept = limit;
	}

	*start = (history_length - kept) % CONSOLE_HISTORY_SIZE;
	*first = kept < CONSOLE_HISTORY_SIZE - *start ? kept : CONSOLE_HISTORY_SIZE - *start;
	*second = kept - *first;
}

static void replay(struct console *console)
{
	size_t start;
	size_t first;
	size_t second;

	history_spans(CONSOLE_HISTORY_SIZE, &start, &first, &second);
	emit(console, history + start, first);
	emit(console, history, second);
}

size_t console_history(char *buffer, size_t size)
{
	size_t start;
	size_t first;
	size_t second;

	history_spans(size, &start, &first, &second);
	memcpy(buffer, history + start, first);
	memcpy(buffer + first, history, second);

	return first + second;
}

void console_init(void)
{
	const struct dt *dt = devicetree();
	int node;
	int err;

	if (dt == NULL) {
		return;
	}

	node = dt_stdout(dt);
	if (node < 0) {
		printf("console missing from the device tree\n");
		return;
	}

	err = driver_probe(dt, node);
	if (err != 0 && err != -EEXIST) {
		printf("console %s %s\n", dt_name(dt, node), strerror(-err));
	}
}

void console_register(struct console *console)
{
	struct console **tail = &consoles;

	while (*tail != NULL) {
		tail = &(*tail)->next;
	}

	console->next = NULL;
	replay(console);
	*tail = console;

	printf("console %s ok\n", console->name);
}

void console_write(const char *data, size_t len)
{
	struct console *console;

	record(data, len);

	for (console = consoles; console != NULL; console = console->next) {
		emit(console, data, len);
	}
}

void console_putc(int c)
{
	char byte = (char)c;

	console_write(&byte, 1);
}

int console_poll(void)
{
	struct console *console;

	for (console = consoles; console != NULL; console = console->next) {
		int c = console->read != NULL ? console->read(console) : -1;

		if (c >= 0) {
			return c;
		}
	}

	return -1;
}

int console_getc(void)
{
	int c;

	do {
		c = console_poll();
	} while (c < 0);

	return c;
}

void console_flush(void)
{
	struct console *console;

	for (console = consoles; console != NULL; console = console->next) {
		if (console->flush != NULL) {
			console->flush(console);
		}
	}
}
