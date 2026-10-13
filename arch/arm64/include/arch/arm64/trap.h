#ifndef ARCH_ARM64_TRAP_H
#define ARCH_ARM64_TRAP_H

#define TRAP_X0			0
#define TRAP_X30		240
#define TRAP_SP			248
#define TRAP_ELR		256
#define TRAP_SPSR		264
#define TRAP_ESR		272
#define TRAP_FAR		280
#define TRAP_VECTOR		288
#define TRAP_FRAME_SIZE		304

#define VECTOR_GROUP_SIZE	4
#define VECTOR_GROUP_NESTED	1

#ifndef __ASSEMBLER__

#include <stddef.h>
#include <stdint.h>

struct trap_frame {
	uint64_t	x[31];
	uint64_t	sp;
	uint64_t	elr;
	uint64_t	spsr;
	uint64_t	esr;
	uint64_t	far;
	uint64_t	vector;
};

_Static_assert(offsetof(struct trap_frame, x[30]) == TRAP_X30, "x30 slot");
_Static_assert(offsetof(struct trap_frame, sp) == TRAP_SP, "sp slot");
_Static_assert(offsetof(struct trap_frame, elr) == TRAP_ELR, "elr slot");
_Static_assert(offsetof(struct trap_frame, spsr) == TRAP_SPSR, "spsr slot");
_Static_assert(offsetof(struct trap_frame, esr) == TRAP_ESR, "esr slot");
_Static_assert(offsetof(struct trap_frame, far) == TRAP_FAR, "far slot");
_Static_assert(offsetof(struct trap_frame, vector) == TRAP_VECTOR, "vector slot");
_Static_assert(sizeof(struct trap_frame) <= TRAP_FRAME_SIZE, "trap frame size");
_Static_assert(TRAP_FRAME_SIZE % 16 == 0, "trap frame alignment");

extern uintptr_t trap_fixup;

void trap_handler(struct trap_frame *frame);

#endif

#endif
