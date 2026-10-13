#include <arch.h>
#include <arch/arm64/esr.h>
#include <arch/arm64/trap.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys.h>
#include <sys/macros.h>
#include <sys/symbols.h>

#define BACKTRACE_FRAMES	4096
#define BACKTRACE_LINES		32
#define INSTRUCTION_SIZE	4
#define FRAME_RECORD_SIZE	16
#define GENERAL_REGISTERS	30
#define REGISTERS_PER_LINE	3

struct exception_class {
	uint8_t		ec;
	const char	*name;
};

struct fault_class {
	uint8_t		fsc;
	uint8_t		mask;
	bool		leveled;
	const char	*name;
};

struct stack {
	const char	*name;
	const char	*guard;
	const char	*bottom;
	const char	*top;
};

extern const char __stack_guard[];
extern const char __stack_bottom[];
extern const char __stack_top[];
extern const char __exception_stack_guard[];
extern const char __exception_stack_bottom[];
extern const char __exception_stack_top[];

uintptr_t trap_fixup;

static const char *const vector_kinds[VECTOR_GROUP_SIZE] = {
	"synchronous exception", "irq", "fiq", "serror",
};

static const char *const vector_origins[] = {
	"el1", "el1 exception handler", "el0", "el0 aarch32",
};

static const struct exception_class classes[] = {
	{ ESR_EC_UNKNOWN, "undefined instruction" },
	{ ESR_EC_WFX, "wfi or wfe" },
	{ ESR_EC_ILLEGAL_STATE, "illegal execution state" },
	{ ESR_EC_SVC64, "svc" },
	{ ESR_EC_HVC64, "hvc" },
	{ ESR_EC_SMC64, "smc" },
	{ ESR_EC_SYSREG, "system register access" },
	{ ESR_EC_PC_ALIGN, "pc alignment fault" },
	{ ESR_EC_SP_ALIGN, "sp alignment fault" },
	{ ESR_EC_SERROR, "serror" },
};

static const struct fault_class fault_classes[] = {
	{ 0x00, ESR_FSC_TYPE_MASK, true, "address size fault" },
	{ 0x04, ESR_FSC_TYPE_MASK, true, "translation fault" },
	{ 0x08, ESR_FSC_TYPE_MASK, true, "access flag fault" },
	{ 0x0c, ESR_FSC_TYPE_MASK, true, "permission fault" },
	{ 0x10, ESR_FSC_MASK, false, "external abort" },
	{ 0x11, ESR_FSC_MASK, false, "tag check fault" },
	{ 0x14, ESR_FSC_TYPE_MASK, true, "external abort on table walk" },
	{ 0x18, ESR_FSC_MASK, false, "parity or ecc error" },
	{ 0x1c, ESR_FSC_TYPE_MASK, true, "parity or ecc error on table walk" },
	{ 0x21, ESR_FSC_MASK, false, "alignment fault" },
	{ 0x30, ESR_FSC_MASK, false, "tlb conflict" },
};

static const struct stack stacks[] = {
	{ "stack", __stack_guard, __stack_bottom, __stack_top },
	{ "exception stack", __exception_stack_guard, __exception_stack_bottom, __exception_stack_top },
};

static const char *class_name(unsigned int ec)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(classes); i++) {
		if (classes[i].ec == ec) {
			return classes[i].name;
		}
	}

	return "unlisted exception class";
}

static void print_fault(unsigned int fsc)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(fault_classes); i++) {
		const struct fault_class *class = &fault_classes[i];

		if ((fsc & class->mask) != class->fsc) {
			continue;
		}

		printf("%s", class->name);
		if (class->leveled) {
			printf(" at level %u", fsc & ESR_FSC_LEVEL_MASK);
		}
		return;
	}

	printf("fault status %#x", fsc);
}

static const struct stack *guard_hit(uint64_t address)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(stacks); i++) {
		if (address >= (uintptr_t)stacks[i].guard && address < (uintptr_t)stacks[i].bottom) {
			return &stacks[i];
		}
	}

	return NULL;
}

static bool frame_on_stack(uintptr_t fp)
{
	size_t i;

	if ((fp & (FRAME_RECORD_SIZE - 1)) != 0) {
		return false;
	}

	for (i = 0; i < ARRAY_SIZE(stacks); i++) {
		if (fp >= (uintptr_t)stacks[i].bottom && fp + FRAME_RECORD_SIZE <= (uintptr_t)stacks[i].top) {
			return true;
		}
	}

	return false;
}

static void print_symbol(uint64_t address, bool returning)
{
	uint64_t site = returning ? address - INSTRUCTION_SIZE : address;
	uintptr_t offset;
	const char *name = symbol_lookup(site, &offset);

	if (name != NULL) {
		printf("  %s+%#lx", name, offset + (address - site));
	}
}

static void print_frame(uint64_t address, bool returning)
{
	printf("  %016lx", address);
	print_symbol(address, returning);
	printf("\n");
}

static void print_repeats(unsigned int repeats)
{
	if (repeats > 0) {
		printf("  (repeated %u more times)\n", repeats);
	}
}

void arch_backtrace(uintptr_t fp)
{
	unsigned int repeats = 0;
	unsigned int printed = 0;
	unsigned int depth;
	uint64_t previous = 0;

	for (depth = 0; depth < BACKTRACE_FRAMES && printed < BACKTRACE_LINES && frame_on_stack(fp); depth++) {
		const uint64_t *record = (const uint64_t *)fp;

		if (record[1] == 0) {
			break;
		}

		if (record[1] == previous) {
			repeats++;
		} else {
			print_repeats(repeats);
			print_frame(record[1], true);
			previous = record[1];
			repeats = 0;
			printed++;
		}

		if (record[0] <= fp) {
			break;
		}

		fp = record[0];
	}

	print_repeats(repeats);
}

static void report_abort(const struct trap_frame *frame, bool data)
{
	unsigned int iss = ESR_ISS(frame->esr);
	const struct stack *overflow = guard_hit(frame->far);

	printf("  %s: ", data ? "data abort" : "instruction abort");
	if (data) {
		printf("%s, ", (iss & ESR_ISS_WNR) != 0 ? "write" : "read");
	}

	print_fault(ESR_ISS_FSC(iss));
	printf(", address %016lx\n", frame->far);

	if (overflow != NULL) {
		printf("  %s overflow\n", overflow->name);
	}
}

static void report_cause(const struct trap_frame *frame)
{
	unsigned int ec = ESR_EC(frame->esr);

	switch (ec) {
	case ESR_EC_DABT_CURRENT:
	case ESR_EC_DABT_LOWER:
		report_abort(frame, true);
		break;
	case ESR_EC_IABT_CURRENT:
	case ESR_EC_IABT_LOWER:
		report_abort(frame, false);
		break;
	case ESR_EC_BRK64:
		printf("  brk #%u\n", (unsigned int)(frame->esr & ESR_IMM16_MASK));
		break;
	default:
		printf("  %s (ec %#x)\n", class_name(ec), ec);
		break;
	}
}

static void print_registers(const struct trap_frame *frame)
{
	unsigned int i;

	printf("  pc  %016lx", frame->elr);
	print_symbol(frame->elr, false);
	printf("\n  lr  %016lx", frame->x[30]);
	print_symbol(frame->x[30], true);
	printf("\n  sp  %016lx  esr %016lx  spsr %016lx\n", frame->sp, frame->esr, frame->spsr);

	for (i = 0; i < GENERAL_REGISTERS; i += REGISTERS_PER_LINE) {
		printf("  x%-2u %016lx  x%-2u %016lx  x%-2u %016lx\n",
		       i, frame->x[i], i + 1, frame->x[i + 1], i + 2, frame->x[i + 2]);
	}
}

static void report(const struct trap_frame *frame)
{
	unsigned int kind = frame->vector % VECTOR_GROUP_SIZE;
	unsigned int origin = (frame->vector / VECTOR_GROUP_SIZE) % ARRAY_SIZE(vector_origins);

	printf("\nexception: %s at %s\n", vector_kinds[kind], vector_origins[origin]);

	if (kind == 0) {
		report_cause(frame);
	}

	print_registers(frame);

	printf("backtrace:\n");
	print_frame(frame->elr, false);
	arch_backtrace(frame->x[29]);
}

void trap_handler(struct trap_frame *frame)
{
	static unsigned int depth;

	if (frame->vector == 0 && trap_fixup != 0 && ESR_EC(frame->esr) == ESR_EC_DABT_CURRENT) {
		frame->elr = trap_fixup;
		return;
	}

	switch (++depth) {
	case 1:
		report(frame);
		break;
	case 2:
		printf("\nnested exception: esr %016lx elr %016lx far %016lx\n", frame->esr, frame->elr, frame->far);
		break;
	default:
		halt();
	}

	printf("halted\n");
	halt();
}
