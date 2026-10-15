#ifndef SYS_CONSOLE_H
#define SYS_CONSOLE_H

#include <stddef.h>

#define CONSOLE_HISTORY_SIZE	0x10000

struct console {
	const char	*name;
	void		(*write)(struct console *console, const char *data, size_t len);
	int		(*read)(struct console *console);
	void		(*flush)(struct console *console);
	struct console	*next;
};

void console_init(void);
void console_register(struct console *console);

void console_replay(struct console *console);
size_t console_history(char *buffer, size_t size);

void console_write(const char *data, size_t len);
void console_putc(int c);
int console_poll(void);
int console_getc(void);
void console_flush(void);

#endif
