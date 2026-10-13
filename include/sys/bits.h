#ifndef SYS_BITS_H
#define SYS_BITS_H

#ifdef __ASSEMBLER__
#define _AC(x, suffix)	x
#else
#define _AC(x, suffix)	x##suffix
#endif

#define UL(x)		_AC(x, UL)

#define BIT(n)		(UL(1) << (n))
#define GENMASK(h, l)	(((UL(1) << ((h) - (l) + 1)) - 1) << (l))

#endif
