#include <lib/crypto.h>
#include <string.h>
#include <sys/endian.h>

#define FIELD_BYTES		32
#define FIELD_BITS		(FIELD_BYTES * 8)
#define SCALAR_BYTES		32
#define SCALAR_BITS		(SCALAR_BYTES * 8)
#define SCALAR_WORDS		(SCALAR_BYTES / sizeof(uint64_t))
#define WIDE_SCALAR_BITS	(2 * SCALAR_BITS)
#define WORD_BITS		64
#define LIMBS			5
#define LIMB_BITS		51
#define LIMB_MASK		((UINT64_C(1) << LIMB_BITS) - 1)
#define REDUCTION		19
#define SIGN_BIT		0x80

struct field {
	uint64_t	limb[LIMBS];
};

struct point {
	struct field	x;
	struct field	y;
	struct field	z;
	struct field	t;
};

static const uint8_t base_point[FIELD_BYTES] = {
	0x58, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
	0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
	0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
	0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
};

static const uint8_t curve_d[FIELD_BYTES] = {
	0xa3, 0x78, 0x59, 0x13, 0xca, 0x4d, 0xeb, 0x75,
	0xab, 0xd8, 0x41, 0x41, 0x4d, 0x0a, 0x70, 0x00,
	0x98, 0xe8, 0x79, 0x77, 0x79, 0x40, 0xc7, 0x8c,
	0x73, 0xfe, 0x6f, 0x2b, 0xee, 0x6c, 0x03, 0x52,
};

static const uint8_t square_root_of_minus_one[FIELD_BYTES] = {
	0xb0, 0xa0, 0x0e, 0x4a, 0x27, 0x1b, 0xee, 0xc4,
	0x78, 0xe4, 0x2f, 0xad, 0x06, 0x18, 0x43, 0x2f,
	0xa7, 0xd7, 0xfb, 0x3d, 0x99, 0x00, 0x4d, 0x2b,
	0x0b, 0xdf, 0xc1, 0x4f, 0x80, 0x24, 0x83, 0x2b,
};

static const uint8_t inverse_exponent[FIELD_BYTES] = {
	0xeb, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f,
};

static const uint8_t root_exponent[FIELD_BYTES] = {
	0xfd, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0f,
};

static const uint8_t group_order[SCALAR_BYTES] = {
	0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
	0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
};

static const uint64_t twice_prime[LIMBS] = {
	0xfffffffffffdaULL, 0xffffffffffffeULL, 0xffffffffffffeULL, 0xffffffffffffeULL, 0xffffffffffffeULL,
};

static const struct field field_zero;
static const struct field field_one = { { 1 } };

static bool bit_set(const uint8_t *bytes, unsigned int bit)
{
	return ((bytes[bit / 8] >> (bit % 8)) & 1) != 0;
}

static void field_load(struct field *out, const uint8_t bytes[FIELD_BYTES])
{
	uint64_t w0 = get_le64(bytes);
	uint64_t w1 = get_le64(bytes + 8);
	uint64_t w2 = get_le64(bytes + 16);
	uint64_t w3 = get_le64(bytes + 24);

	out->limb[0] = w0 & LIMB_MASK;
	out->limb[1] = (w0 >> 51 | w1 << 13) & LIMB_MASK;
	out->limb[2] = (w1 >> 38 | w2 << 26) & LIMB_MASK;
	out->limb[3] = (w2 >> 25 | w3 << 39) & LIMB_MASK;
	out->limb[4] = (w3 >> 12) & LIMB_MASK;
}

static void field_carry(struct field *f)
{
	unsigned int i;

	for (i = 0; i < LIMBS - 1; i++) {
		f->limb[i + 1] += f->limb[i] >> LIMB_BITS;
		f->limb[i] &= LIMB_MASK;
	}

	f->limb[0] += REDUCTION * (f->limb[LIMBS - 1] >> LIMB_BITS);
	f->limb[LIMBS - 1] &= LIMB_MASK;
}

static void field_store(uint8_t bytes[FIELD_BYTES], const struct field *f)
{
	struct field t = *f;
	uint64_t overflow;
	unsigned int i;

	field_carry(&t);
	field_carry(&t);

	overflow = (t.limb[0] + REDUCTION) >> LIMB_BITS;
	for (i = 1; i < LIMBS; i++) {
		overflow = (t.limb[i] + overflow) >> LIMB_BITS;
	}

	t.limb[0] += REDUCTION * overflow;
	for (i = 0; i < LIMBS - 1; i++) {
		t.limb[i + 1] += t.limb[i] >> LIMB_BITS;
		t.limb[i] &= LIMB_MASK;
	}
	t.limb[LIMBS - 1] &= LIMB_MASK;

	put_le64(bytes, t.limb[0] | t.limb[1] << 51);
	put_le64(bytes + 8, t.limb[1] >> 13 | t.limb[2] << 38);
	put_le64(bytes + 16, t.limb[2] >> 26 | t.limb[3] << 25);
	put_le64(bytes + 24, t.limb[3] >> 39 | t.limb[4] << 12);
}

static void field_add(struct field *out, const struct field *a, const struct field *b)
{
	unsigned int i;

	for (i = 0; i < LIMBS; i++) {
		out->limb[i] = a->limb[i] + b->limb[i];
	}

	field_carry(out);
}

static void field_subtract(struct field *out, const struct field *a, const struct field *b)
{
	unsigned int i;

	for (i = 0; i < LIMBS; i++) {
		out->limb[i] = a->limb[i] + twice_prime[i] - b->limb[i];
	}

	field_carry(out);
}

static void field_negate(struct field *out, const struct field *a)
{
	field_subtract(out, &field_zero, a);
}

static unsigned __int128 mul(uint64_t a, uint64_t b)
{
	return (unsigned __int128)a * b;
}

static void field_multiply(struct field *out, const struct field *a, const struct field *b)
{
	const uint64_t *x = a->limb;
	const uint64_t *y = b->limb;
	uint64_t y1 = REDUCTION * y[1];
	uint64_t y2 = REDUCTION * y[2];
	uint64_t y3 = REDUCTION * y[3];
	uint64_t y4 = REDUCTION * y[4];
	unsigned __int128 column[LIMBS];
	unsigned __int128 carry = 0;
	unsigned int i;

	column[0] = mul(x[0], y[0]) + mul(x[1], y4) + mul(x[2], y3) + mul(x[3], y2) + mul(x[4], y1);
	column[1] = mul(x[0], y[1]) + mul(x[1], y[0]) + mul(x[2], y4) + mul(x[3], y3) + mul(x[4], y2);
	column[2] = mul(x[0], y[2]) + mul(x[1], y[1]) + mul(x[2], y[0]) + mul(x[3], y4) + mul(x[4], y3);
	column[3] = mul(x[0], y[3]) + mul(x[1], y[2]) + mul(x[2], y[1]) + mul(x[3], y[0]) + mul(x[4], y4);
	column[4] = mul(x[0], y[4]) + mul(x[1], y[3]) + mul(x[2], y[2]) + mul(x[3], y[1]) + mul(x[4], y[0]);

	for (i = 0; i < LIMBS; i++) {
		column[i] += carry;
		out->limb[i] = (uint64_t)column[i] & LIMB_MASK;
		carry = column[i] >> LIMB_BITS;
	}

	carry = carry * REDUCTION + out->limb[0];
	out->limb[0] = (uint64_t)carry & LIMB_MASK;
	out->limb[1] += (uint64_t)(carry >> LIMB_BITS);
}

static void field_power(struct field *out, const struct field *base, const uint8_t exponent[FIELD_BYTES])
{
	struct field power = field_one;
	int bit;

	for (bit = FIELD_BITS - 1; bit >= 0; bit--) {
		field_multiply(&power, &power, &power);
		if (bit_set(exponent, (unsigned int)bit)) {
			field_multiply(&power, &power, base);
		}
	}

	*out = power;
}

static bool field_equal(const struct field *a, const struct field *b)
{
	uint8_t left[FIELD_BYTES];
	uint8_t right[FIELD_BYTES];

	field_store(left, a);
	field_store(right, b);

	return memcmp(left, right, FIELD_BYTES) == 0;
}

static bool field_is_zero(const struct field *a)
{
	return field_equal(a, &field_zero);
}

static bool field_is_negative(const struct field *a)
{
	uint8_t bytes[FIELD_BYTES];

	field_store(bytes, a);

	return (bytes[0] & 1) != 0;
}

static bool recover_x(struct field *x, const struct field *y, bool negative)
{
	struct field y2;
	struct field u;
	struct field v;
	struct field v3;
	struct field check;
	struct field minus_u;
	struct field constant;

	field_multiply(&y2, y, y);
	field_subtract(&u, &y2, &field_one);
	field_load(&constant, curve_d);
	field_multiply(&v, &y2, &constant);
	field_add(&v, &v, &field_one);

	field_multiply(&v3, &v, &v);
	field_multiply(&v3, &v3, &v);
	field_multiply(x, &v3, &v3);
	field_multiply(x, x, &v);
	field_multiply(x, x, &u);
	field_power(x, x, root_exponent);
	field_multiply(x, x, &v3);
	field_multiply(x, x, &u);

	field_multiply(&check, x, x);
	field_multiply(&check, &check, &v);
	if (!field_equal(&check, &u)) {
		field_negate(&minus_u, &u);
		if (!field_equal(&check, &minus_u)) {
			return false;
		}

		field_load(&constant, square_root_of_minus_one);
		field_multiply(x, x, &constant);
	}

	if (field_is_zero(x) && negative) {
		return false;
	}

	if (field_is_negative(x) != negative) {
		field_negate(x, x);
	}

	return true;
}

static bool point_decode(struct point *point, const uint8_t bytes[FIELD_BYTES])
{
	uint8_t canonical[FIELD_BYTES];
	bool negative = (bytes[FIELD_BYTES - 1] & SIGN_BIT) != 0;

	field_load(&point->y, bytes);
	field_store(canonical, &point->y);
	canonical[FIELD_BYTES - 1] |= bytes[FIELD_BYTES - 1] & SIGN_BIT;
	if (memcmp(canonical, bytes, FIELD_BYTES) != 0 || !recover_x(&point->x, &point->y, negative)) {
		return false;
	}

	point->z = field_one;
	field_multiply(&point->t, &point->x, &point->y);

	return true;
}

static void point_encode(uint8_t bytes[FIELD_BYTES], const struct point *point)
{
	struct field inverse;
	struct field x;
	struct field y;

	field_power(&inverse, &point->z, inverse_exponent);
	field_multiply(&x, &point->x, &inverse);
	field_multiply(&y, &point->y, &inverse);
	field_store(bytes, &y);
	if (field_is_negative(&x)) {
		bytes[FIELD_BYTES - 1] |= SIGN_BIT;
	}
}

static void point_add(struct point *out, const struct point *p, const struct point *q)
{
	struct field a;
	struct field b;
	struct field c;
	struct field d;
	struct field e;
	struct field f;
	struct field g;
	struct field h;
	struct field t;

	field_subtract(&a, &p->y, &p->x);
	field_subtract(&t, &q->y, &q->x);
	field_multiply(&a, &a, &t);
	field_add(&b, &p->y, &p->x);
	field_add(&t, &q->y, &q->x);
	field_multiply(&b, &b, &t);
	field_load(&t, curve_d);
	field_add(&t, &t, &t);
	field_multiply(&c, &p->t, &q->t);
	field_multiply(&c, &c, &t);
	field_multiply(&d, &p->z, &q->z);
	field_add(&d, &d, &d);
	field_subtract(&e, &b, &a);
	field_subtract(&f, &d, &c);
	field_add(&g, &d, &c);
	field_add(&h, &b, &a);

	field_multiply(&out->x, &e, &f);
	field_multiply(&out->y, &g, &h);
	field_multiply(&out->t, &e, &h);
	field_multiply(&out->z, &f, &g);
}

static void point_negate(struct point *point)
{
	field_negate(&point->x, &point->x);
	field_negate(&point->t, &point->t);
}

static void point_combine(struct point *out, const uint8_t first_scalar[SCALAR_BYTES], const struct point *first,
			  const uint8_t second_scalar[SCALAR_BYTES], const struct point *second)
{
	struct point sum = { .y = field_one, .z = field_one };
	struct point both;
	int bit;

	point_add(&both, first, second);

	for (bit = SCALAR_BITS - 1; bit >= 0; bit--) {
		bool in_first = bit_set(first_scalar, (unsigned int)bit);
		bool in_second = bit_set(second_scalar, (unsigned int)bit);

		point_add(&sum, &sum, &sum);
		if (in_first && in_second) {
			point_add(&sum, &sum, &both);
		} else if (in_first) {
			point_add(&sum, &sum, first);
		} else if (in_second) {
			point_add(&sum, &sum, second);
		}
	}

	*out = sum;
}

static void scalar_load(uint64_t words[SCALAR_WORDS], const uint8_t bytes[SCALAR_BYTES])
{
	unsigned int i;

	for (i = 0; i < SCALAR_WORDS; i++) {
		words[i] = get_le64(bytes + i * sizeof(uint64_t));
	}
}

static bool scalar_below(const uint64_t value[SCALAR_WORDS], const uint64_t limit[SCALAR_WORDS])
{
	unsigned int i;

	for (i = SCALAR_WORDS; i > 0; i--) {
		if (value[i - 1] != limit[i - 1]) {
			return value[i - 1] < limit[i - 1];
		}
	}

	return false;
}

static void scalar_subtract(uint64_t value[SCALAR_WORDS], const uint64_t amount[SCALAR_WORDS])
{
	uint64_t borrow = 0;
	unsigned int i;

	for (i = 0; i < SCALAR_WORDS; i++) {
		uint64_t take = amount[i] + borrow;

		borrow = (take < borrow || value[i] < take) ? 1 : 0;
		value[i] -= take;
	}
}

static bool scalar_canonical(const uint8_t scalar[SCALAR_BYTES])
{
	uint64_t value[SCALAR_WORDS];
	uint64_t order[SCALAR_WORDS];

	scalar_load(value, scalar);
	scalar_load(order, group_order);

	return scalar_below(value, order);
}

static void scalar_reduce(uint8_t out[SCALAR_BYTES], const uint8_t wide[2 * SCALAR_BYTES])
{
	uint64_t value[SCALAR_WORDS] = { 0 };
	uint64_t order[SCALAR_WORDS];
	unsigned int i;
	int bit;

	scalar_load(order, group_order);

	for (bit = WIDE_SCALAR_BITS - 1; bit >= 0; bit--) {
		for (i = SCALAR_WORDS - 1; i > 0; i--) {
			value[i] = value[i] << 1 | value[i - 1] >> (WORD_BITS - 1);
		}
		value[0] = value[0] << 1 | (bit_set(wide, (unsigned int)bit) ? 1 : 0);

		if (!scalar_below(value, order)) {
			scalar_subtract(value, order);
		}
	}

	for (i = 0; i < SCALAR_WORDS; i++) {
		put_le64(out + i * sizeof(uint64_t), value[i]);
	}
}

bool ed25519_verify(const uint8_t public_key[ED25519_PUBLIC_KEY_SIZE], const void *message, size_t length,
		    const uint8_t signature[ED25519_SIGNATURE_SIZE])
{
	const uint8_t *encoded_r = signature;
	const uint8_t *s = signature + FIELD_BYTES;
	uint8_t digest[SHA512_SIZE];
	uint8_t k[SCALAR_BYTES];
	uint8_t check[FIELD_BYTES];
	struct sha512 context;
	struct point key;
	struct point base;
	struct point r;

	if (!scalar_canonical(s) || !point_decode(&key, public_key) || !point_decode(&base, base_point)) {
		return false;
	}

	sha512_init(&context);
	sha512_update(&context, encoded_r, FIELD_BYTES);
	sha512_update(&context, public_key, ED25519_PUBLIC_KEY_SIZE);
	sha512_update(&context, message, length);
	sha512_final(&context, digest);
	scalar_reduce(k, digest);

	point_negate(&key);
	point_combine(&r, s, &base, k, &key);
	point_encode(check, &r);

	return memcmp(check, encoded_r, FIELD_BYTES) == 0;
}
