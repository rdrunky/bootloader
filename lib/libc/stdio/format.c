#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "../private.h"

#define FLAG_LEFT	0x01
#define FLAG_PLUS	0x02
#define FLAG_SPACE	0x04
#define FLAG_ALT	0x08
#define FLAG_ZERO	0x10

#define NO_VALUE	(-1)
#define FIELD_MAX	4096
#define BUFFER_SIZE	64
#define DIGITS_MAX	24

enum length {
	LENGTH_CHAR,
	LENGTH_SHORT,
	LENGTH_INT,
	LENGTH_LONG,
	LENGTH_LONG_LONG,
};

struct spec {
	unsigned int	flags;
	int		width;
	int		precision;
	enum length	length;
};

struct args {
	va_list		list;
};

struct output {
	format_sink	sink;
	void		*context;
	size_t		used;
	size_t		total;
	char		buffer[BUFFER_SIZE];
};

struct integer {
	uint64_t	magnitude;
	unsigned int	base;
	bool		upper;
	bool		is_signed;
	bool		negative;
};

static void out_flush(struct output *out)
{
	if (out->used > 0) {
		out->sink(out->context, out->buffer, out->used);
		out->used = 0;
	}
}

static void out_char(struct output *out, char c)
{
	if (out->used == sizeof(out->buffer)) {
		out_flush(out);
	}

	out->buffer[out->used++] = c;
	out->total++;
}

static void out_repeat(struct output *out, char c, int count)
{
	for (; count > 0; count--) {
		out_char(out, c);
	}
}

static void out_bytes(struct output *out, const char *data, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++) {
		out_char(out, data[i]);
	}
}

static int parse_decimal(const char **p)
{
	int value = 0;

	for (; **p >= '0' && **p <= '9'; (*p)++) {
		if (value < FIELD_MAX) {
			value = value * 10 + (**p - '0');
		}
	}

	return value < FIELD_MAX ? value : FIELD_MAX;
}

static int clamp_field(int value)
{
	return value < FIELD_MAX ? value : FIELD_MAX;
}

static void parse_flags(const char **p, struct spec *spec)
{
	for (;; (*p)++) {
		switch (**p) {
		case '-':
			spec->flags |= FLAG_LEFT;
			break;
		case '+':
			spec->flags |= FLAG_PLUS;
			break;
		case ' ':
			spec->flags |= FLAG_SPACE;
			break;
		case '#':
			spec->flags |= FLAG_ALT;
			break;
		case '0':
			spec->flags |= FLAG_ZERO;
			break;
		default:
			return;
		}
	}
}

static void parse_width(const char **p, struct spec *spec, struct args *args)
{
	if (**p == '*') {
		int width = va_arg(args->list, int);

		if (width < 0) {
			spec->flags |= FLAG_LEFT;
			width = width < -FIELD_MAX ? FIELD_MAX : -width;
		}

		spec->width = clamp_field(width);
		(*p)++;
	} else if (**p >= '0' && **p <= '9') {
		spec->width = parse_decimal(p);
	}
}

static void parse_precision(const char **p, struct spec *spec, struct args *args)
{
	if (**p != '.') {
		return;
	}

	(*p)++;
	if (**p == '*') {
		int precision = va_arg(args->list, int);

		spec->precision = precision < 0 ? NO_VALUE : clamp_field(precision);
		(*p)++;
	} else {
		spec->precision = parse_decimal(p);
	}
}

static void parse_length(const char **p, struct spec *spec)
{
	switch (**p) {
	case 'h':
		spec->length = (*p)[1] == 'h' ? LENGTH_CHAR : LENGTH_SHORT;
		*p += spec->length == LENGTH_CHAR ? 2 : 1;
		break;
	case 'l':
		spec->length = (*p)[1] == 'l' ? LENGTH_LONG_LONG : LENGTH_LONG;
		*p += spec->length == LENGTH_LONG_LONG ? 2 : 1;
		break;
	case 'z':
	case 't':
	case 'j':
		spec->length = LENGTH_LONG;
		(*p)++;
		break;
	default:
		break;
	}
}

static void parse_spec(const char **p, struct spec *spec, struct args *args)
{
	spec->flags = 0;
	spec->width = NO_VALUE;
	spec->precision = NO_VALUE;
	spec->length = LENGTH_INT;

	parse_flags(p, spec);
	parse_width(p, spec, args);
	parse_precision(p, spec, args);
	parse_length(p, spec);
}

static int64_t fetch_signed(struct args *args, enum length length)
{
	switch (length) {
	case LENGTH_CHAR:
		return (signed char)va_arg(args->list, int);
	case LENGTH_SHORT:
		return (short)va_arg(args->list, int);
	case LENGTH_INT:
		return va_arg(args->list, int);
	case LENGTH_LONG:
		return va_arg(args->list, long);
	default:
		return va_arg(args->list, long long);
	}
}

static uint64_t fetch_unsigned(struct args *args, enum length length)
{
	switch (length) {
	case LENGTH_CHAR:
		return (unsigned char)va_arg(args->list, unsigned int);
	case LENGTH_SHORT:
		return (unsigned short)va_arg(args->list, unsigned int);
	case LENGTH_INT:
		return va_arg(args->list, unsigned int);
	case LENGTH_LONG:
		return va_arg(args->list, unsigned long);
	default:
		return va_arg(args->list, unsigned long long);
	}
}

static void emit_integer(struct output *out, const struct spec *spec, const struct integer *number)
{
	const char *alphabet = number->upper ? "0123456789ABCDEF" : "0123456789abcdef";
	char digits[DIGITS_MAX];
	char prefix[2];
	size_t digit_count = 0;
	size_t prefix_count = 0;
	uint64_t rest = number->magnitude;
	int zeros = 0;
	int pad;

	if (rest != 0 || spec->precision != 0) {
		do {
			digits[digit_count++] = alphabet[rest % number->base];
			rest /= number->base;
		} while (rest != 0);
	}

	if (number->is_signed) {
		if (number->negative) {
			prefix[prefix_count++] = '-';
		} else if ((spec->flags & FLAG_PLUS) != 0) {
			prefix[prefix_count++] = '+';
		} else if ((spec->flags & FLAG_SPACE) != 0) {
			prefix[prefix_count++] = ' ';
		}
	} else if ((spec->flags & FLAG_ALT) != 0 && number->base == 16 && number->magnitude != 0) {
		prefix[prefix_count++] = '0';
		prefix[prefix_count++] = number->upper ? 'X' : 'x';
	}

	if (spec->precision > (int)digit_count) {
		zeros = spec->precision - (int)digit_count;
	}

	if ((spec->flags & FLAG_ALT) != 0 && number->base == 8 && zeros == 0 &&
	    (digit_count == 0 || digits[digit_count - 1] != '0')) {
		zeros = 1;
	}

	pad = spec->width - ((int)prefix_count + zeros + (int)digit_count);
	if (pad < 0) {
		pad = 0;
	}

	if ((spec->flags & FLAG_ZERO) != 0 && (spec->flags & FLAG_LEFT) == 0 && spec->precision == NO_VALUE) {
		zeros += pad;
		pad = 0;
	}

	if ((spec->flags & FLAG_LEFT) == 0) {
		out_repeat(out, ' ', pad);
	}

	out_bytes(out, prefix, prefix_count);
	out_repeat(out, '0', zeros);
	while (digit_count > 0) {
		out_char(out, digits[--digit_count]);
	}

	if ((spec->flags & FLAG_LEFT) != 0) {
		out_repeat(out, ' ', pad);
	}
}

static void emit_text(struct output *out, const struct spec *spec, const char *text, size_t len)
{
	int pad = spec->width > (int)len ? spec->width - (int)len : 0;

	if ((spec->flags & FLAG_LEFT) == 0) {
		out_repeat(out, ' ', pad);
	}

	out_bytes(out, text, len);

	if ((spec->flags & FLAG_LEFT) != 0) {
		out_repeat(out, ' ', pad);
	}
}

static void convert_signed(struct output *out, const struct spec *spec, struct args *args)
{
	int64_t value = fetch_signed(args, spec->length);
	struct integer number = {
		.magnitude = value < 0 ? -(uint64_t)value : (uint64_t)value,
		.base = 10,
		.is_signed = true,
		.negative = value < 0,
	};

	emit_integer(out, spec, &number);
}

static unsigned int conversion_base(char conversion)
{
	switch (conversion) {
	case 'u':
		return 10;
	case 'o':
		return 8;
	default:
		return 16;
	}
}

static void convert_unsigned(struct output *out, const struct spec *spec, struct args *args, char conversion)
{
	struct integer number = {
		.magnitude = fetch_unsigned(args, spec->length),
		.base = conversion_base(conversion),
		.upper = conversion == 'X',
	};

	emit_integer(out, spec, &number);
}

static void convert_pointer(struct output *out, const struct spec *spec, struct args *args)
{
	uintptr_t value = (uintptr_t)va_arg(args->list, void *);
	struct spec alt = *spec;
	struct integer number = { .magnitude = value, .base = 16 };

	if (value == 0) {
		emit_text(out, spec, "(nil)", 5);
		return;
	}

	alt.flags |= FLAG_ALT;
	emit_integer(out, &alt, &number);
}

static void convert_string(struct output *out, const struct spec *spec, struct args *args)
{
	const char *text = va_arg(args->list, const char *);
	size_t len;

	if (text == NULL) {
		text = "(null)";
	}

	len = spec->precision != NO_VALUE ? strnlen(text, (size_t)spec->precision) : strlen(text);
	emit_text(out, spec, text, len);
}

static bool convert(struct output *out, const struct spec *spec, struct args *args, char conversion)
{
	switch (conversion) {
	case 'd':
	case 'i':
		convert_signed(out, spec, args);
		return true;
	case 'u':
	case 'o':
	case 'x':
	case 'X':
		convert_unsigned(out, spec, args, conversion);
		return true;
	case 'p':
		convert_pointer(out, spec, args);
		return true;
	case 's':
		convert_string(out, spec, args);
		return true;
	case 'c': {
		char c = (char)va_arg(args->list, int);

		emit_text(out, spec, &c, 1);
		return true;
	}
	case '%':
		out_char(out, '%');
		return true;
	default:
		return false;
	}
}

int format_output(format_sink sink, void *context, const char *fmt, va_list ap)
{
	struct output out = { .sink = sink, .context = context };
	struct args args;

	va_copy(args.list, ap);

	while (*fmt != '\0') {
		const char *start = fmt;
		struct spec spec;

		if (*fmt != '%') {
			out_char(&out, *fmt++);
			continue;
		}

		fmt++;
		parse_spec(&fmt, &spec, &args);

		if (*fmt == '\0') {
			out_bytes(&out, start, (size_t)(fmt - start));
			break;
		}

		if (!convert(&out, &spec, &args, *fmt)) {
			out_bytes(&out, start, (size_t)(fmt - start) + 1);
		}

		fmt++;
	}

	out_flush(&out);
	va_end(args.list);

	return out.total > INT32_MAX ? INT32_MAX : (int)out.total;
}
