#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/macros.h>
#include <sys/shell.h>

#define SMALL_BUFFER	16
#define FRAME_BYTES	512
#define INSTRUCTION_RET	0xd65f03c0u

struct fault {
	const char	*name;
	void		(*trigger)(void);
};

static volatile uintptr_t zero;
static volatile size_t oversize;

static void fault_null(void)
{
	*(volatile uint64_t *)zero = 1;
}

static unsigned int recurse(unsigned int depth)
{
	volatile uint8_t frame[FRAME_BYTES];

	frame[0] = (uint8_t)depth;
	if (zero == 0) {
		return recurse(depth + 1) + frame[0];
	}

	return frame[0];
}

static void fault_stack(void)
{
	recurse(0);
}

static void fault_write(void)
{
	*(volatile uint32_t *)(uintptr_t)fault_write = 0;
}

static void fault_exec(void)
{
	uint32_t *code = malloc(SMALL_BUFFER);

	if (code != NULL) {
		code[0] = INSTRUCTION_RET;
		((void (*)(void))(uintptr_t)code)();
	}
}

static void fault_smash(void)
{
	char buffer[SMALL_BUFFER];

	oversize = sizeof(buffer) * 3;
	memset(buffer, 'x', oversize);
}

static void fault_overflow(void)
{
	char *data = malloc(SMALL_BUFFER);

	if (data != NULL) {
		memset(data, 'x', SMALL_BUFFER + 1 + zero);
		free(data);
	}
}

static void fault_double(void)
{
	char *volatile data = malloc(SMALL_BUFFER);

	free(data);
	free(data);
}

static void fault_brk(void)
{
	__builtin_trap();
}

static void fault_undefined(void)
{
	__asm__ volatile(".inst 0x00000000");
}

static const struct fault faults[] = {
	{ "null", fault_null },
	{ "stack", fault_stack },
	{ "write", fault_write },
	{ "exec", fault_exec },
	{ "smash", fault_smash },
	{ "overflow", fault_overflow },
	{ "double", fault_double },
	{ "brk", fault_brk },
	{ "undefined", fault_undefined },
};

DEVELOPER_COMMAND(fault, "null|stack|write|exec|smash|overflow|double|brk|undefined",
		  "trigger a fault to check the handlers")
{
	size_t i;

	if (argc != 2) {
		return SHELL_USAGE;
	}

	for (i = 0; i < ARRAY_SIZE(faults); i++) {
		if (strcmp(argv[1], faults[i].name) == 0) {
			faults[i].trigger();
			printf("fault: %s did not trap\n", faults[i].name);
			return -ECANCELED;
		}
	}

	return SHELL_USAGE;
}
