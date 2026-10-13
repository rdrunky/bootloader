#ifndef PL011_REGS_H
#define PL011_REGS_H

#include <sys/bits.h>

#define UARTDR		0x000
#define UARTFR		0x018
#define UARTLCR_H	0x02c
#define UARTCR		0x030
#define UARTIMSC	0x038

#define DR_DATA		0xff

#define FR_BUSY		BIT(3)
#define FR_RXFE		BIT(4)
#define FR_TXFF		BIT(5)

#define LCR_H_FEN	BIT(4)
#define LCR_H_WLEN8	(UL(3) << 5)

#define CR_UARTEN	BIT(0)
#define CR_TXE		BIT(8)
#define CR_RXE		BIT(9)

#endif
