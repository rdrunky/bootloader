#include <sys/shell.h>

#include "private.h"

int main(void)
{
	security_init();
	autoboot();
	shell_run("boot> ");
}
