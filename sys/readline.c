#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/console.h>
#include <sys/readline.h>
#include <sys/shell.h>
#include <sys/time.h>

#define HISTORY_DEPTH		16
#define MATCHES_MAX		64
#define ESCAPE_TIMEOUT_US	50000

#define CONTROL(key)		((key) & 0x1f)
#define KEY_ESCAPE		0x1b
#define KEY_BACKSPACE		0x7f

enum key {
	KEY_NONE = 0x100,
	KEY_UP,
	KEY_DOWN,
	KEY_LEFT,
	KEY_RIGHT,
	KEY_HOME,
	KEY_END,
	KEY_DELETE,
};

struct line {
	const char	*prompt;
	char		*buffer;
	size_t		size;
	size_t		length;
	size_t		cursor;
	unsigned int	recall;
};

static char history[HISTORY_DEPTH][SHELL_LINE_MAX];
static unsigned int history_count;
static int previous_end;

static int next_byte(void)
{
	uint64_t start = time_us();

	while (time_us() - start < ESCAPE_TIMEOUT_US) {
		int c = console_poll();

		if (c >= 0) {
			return c;
		}
	}

	return -1;
}

static int escape_sequence(void)
{
	int c = next_byte();
	int digit;

	if (c != '[' && c != 'O') {
		return KEY_NONE;
	}

	c = next_byte();
	switch (c) {
	case 'A':
		return KEY_UP;
	case 'B':
		return KEY_DOWN;
	case 'C':
		return KEY_RIGHT;
	case 'D':
		return KEY_LEFT;
	case 'H':
		return KEY_HOME;
	case 'F':
		return KEY_END;
	default:
		break;
	}

	if (c < '1' || c > '8') {
		return KEY_NONE;
	}

	digit = c;
	if (next_byte() != '~') {
		return KEY_NONE;
	}

	switch (digit) {
	case '1':
	case '7':
		return KEY_HOME;
	case '4':
	case '8':
		return KEY_END;
	case '3':
		return KEY_DELETE;
	default:
		return KEY_NONE;
	}
}

static int read_key(void)
{
	int c = console_getc();

	return c == KEY_ESCAPE ? escape_sequence() : c;
}

static void redraw(const struct line *line)
{
	printf("\r%s%.*s\x1b[K", line->prompt, (int)line->length, line->buffer);

	if (line->cursor < line->length) {
		printf("\x1b[%uD", (unsigned int)(line->length - line->cursor));
	}
}

static void replace(struct line *line, const char *text)
{
	line->length = strnlen(text, line->size - 1);
	memcpy(line->buffer, text, line->length);
	line->cursor = line->length;
	redraw(line);
}

static void insert(struct line *line, const char *text, size_t count)
{
	if (count > line->size - 1 - line->length) {
		count = line->size - 1 - line->length;
	}

	if (count == 0) {
		return;
	}

	memmove(line->buffer + line->cursor + count, line->buffer + line->cursor, line->length - line->cursor);
	memcpy(line->buffer + line->cursor, text, count);
	line->length += count;
	line->cursor += count;

	if (line->cursor == line->length) {
		printf("%.*s", (int)count, text);
	} else {
		redraw(line);
	}
}

static void erase(struct line *line, size_t from, size_t to)
{
	if (from >= to) {
		return;
	}

	memmove(line->buffer + from, line->buffer + to, line->length - to);
	line->length -= to - from;
	line->cursor = from;
	redraw(line);
}

static size_t word_start(const struct line *line)
{
	size_t at = line->cursor;

	while (at > 0 && line->buffer[at - 1] == ' ') {
		at--;
	}

	while (at > 0 && line->buffer[at - 1] != ' ') {
		at--;
	}

	return at;
}

static void recall(struct line *line, int direction)
{
	unsigned int available = history_count < HISTORY_DEPTH ? history_count : HISTORY_DEPTH;

	if (direction < 0 && line->recall < available) {
		line->recall++;
	} else if (direction > 0 && line->recall > 0) {
		line->recall--;
	} else {
		return;
	}

	if (line->recall == 0) {
		replace(line, "");
	} else {
		replace(line, history[(history_count - line->recall) % HISTORY_DEPTH]);
	}
}

static void remember(const char *text)
{
	const char *latest = history_count > 0 ? history[(history_count - 1) % HISTORY_DEPTH] : NULL;

	if (*text == '\0' || (latest != NULL && strcmp(latest, text) == 0)) {
		return;
	}

	strlcpy(history[history_count % HISTORY_DEPTH], text, SHELL_LINE_MAX);
	history_count++;
}

static size_t common_prefix(const char **matches, size_t count)
{
	size_t length = strlen(matches[0]);
	size_t i;

	for (i = 1; i < count; i++) {
		size_t same = 0;

		while (same < length && matches[i][same] == matches[0][same]) {
			same++;
		}

		length = same;
	}

	return length;
}

static void complete(struct line *line)
{
	const char *matches[MATCHES_MAX];
	size_t count;
	size_t shared;
	size_t i;

	if (memchr(line->buffer, ' ', line->cursor) != NULL) {
		return;
	}

	count = shell_complete(line->buffer, line->cursor, matches, MATCHES_MAX);
	if (count == 0) {
		return;
	}

	shared = common_prefix(matches, count);
	if (shared > line->cursor) {
		insert(line, matches[0] + line->cursor, shared - line->cursor);
		if (count == 1) {
			insert(line, " ", 1);
		}
		return;
	}

	printf("\n");
	for (i = 0; i < count; i++) {
		printf("%s%s", matches[i], i + 1 < count ? "  " : "\n");
	}

	redraw(line);
}

static bool edit(struct line *line, int key)
{
	switch (key) {
	case '\r':
	case '\n':
		return false;
	case KEY_BACKSPACE:
	case CONTROL('H'):
		if (line->cursor > 0) {
			erase(line, line->cursor - 1, line->cursor);
		}
		break;
	case KEY_DELETE:
		if (line->cursor < line->length) {
			erase(line, line->cursor, line->cursor + 1);
		}
		break;
	case KEY_LEFT:
	case CONTROL('B'):
		if (line->cursor > 0) {
			line->cursor--;
			redraw(line);
		}
		break;
	case KEY_RIGHT:
	case CONTROL('F'):
		if (line->cursor < line->length) {
			line->cursor++;
			redraw(line);
		}
		break;
	case KEY_HOME:
	case CONTROL('A'):
		line->cursor = 0;
		redraw(line);
		break;
	case KEY_END:
	case CONTROL('E'):
		line->cursor = line->length;
		redraw(line);
		break;
	case KEY_UP:
	case CONTROL('P'):
		recall(line, -1);
		break;
	case KEY_DOWN:
	case CONTROL('N'):
		recall(line, 1);
		break;
	case CONTROL('U'):
		erase(line, 0, line->cursor);
		break;
	case CONTROL('K'):
		line->length = line->cursor;
		redraw(line);
		break;
	case CONTROL('W'):
		erase(line, word_start(line), line->cursor);
		break;
	case CONTROL('L'):
		printf("\x1b[H\x1b[2J");
		redraw(line);
		break;
	case '\t':
		complete(line);
		break;
	default:
		if (key < KEY_NONE && isprint(key)) {
			char c = (char)key;

			insert(line, &c, 1);
		}
		break;
	}

	return true;
}

int readline(const char *prompt, char *buffer, size_t size)
{
	struct line line = { .prompt = prompt, .buffer = buffer, .size = size };
	int key;

	if (size == 0) {
		return -EINVAL;
	}

	printf("%s", prompt);

	for (;;) {
		key = read_key();

		if (key == '\n' && previous_end == '\r' && line.length == 0) {
			previous_end = 0;
			continue;
		}

		if (key == CONTROL('C')) {
			printf("^C\n");
			previous_end = 0;
			return -ECANCELED;
		}

		if (!edit(&line, key)) {
			break;
		}

		previous_end = 0;
	}

	previous_end = key;
	buffer[line.length] = '\0';
	printf("\n");
	remember(buffer);

	return (int)line.length;
}
