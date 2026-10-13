#ifndef COMPILER_H
#define COMPILER_H

#define __aligned(n)		__attribute__((aligned(n)))
#define __noreturn		__attribute__((noreturn))
#define __printflike(f, a)	__attribute__((format(printf, f, a)))
#define __section(name)		__attribute__((section(name)))
#define __unused		__attribute__((unused))
#define __used			__attribute__((used))

#define READ_ONCE(x)		(*(const volatile __typeof__(x) *)&(x))
#define WRITE_ONCE(x, v)	(*(volatile __typeof__(x) *)&(x) = (v))

#endif
