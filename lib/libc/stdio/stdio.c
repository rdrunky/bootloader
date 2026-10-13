#include <stdio.h>
#include <sys/console.h>

int putchar(int c)
{
	console_putc(c);

	return (unsigned char)c;
}
