#include <lib/crypto.h>
#include <string.h>
#include <sys/endian.h>

#define ROUNDS		64
#define LENGTH_OFFSET	56
#define WORD_SIZE	sizeof(uint32_t)
#define BLOCK_WORDS	(SHA256_BLOCK / WORD_SIZE)
#define PADDING_START	0x80

static const uint32_t initial[SHA256_WORDS] = {
	0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
};

static const uint32_t constants[ROUNDS] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static uint32_t rotate(uint32_t value, unsigned int count)
{
	return value >> count | value << (32 - count);
}

static void compress(uint32_t state[SHA256_WORDS], const uint8_t *block)
{
	uint32_t schedule[ROUNDS];
	uint32_t a = state[0];
	uint32_t b = state[1];
	uint32_t c = state[2];
	uint32_t d = state[3];
	uint32_t e = state[4];
	uint32_t f = state[5];
	uint32_t g = state[6];
	uint32_t h = state[7];
	unsigned int i;

	for (i = 0; i < BLOCK_WORDS; i++) {
		schedule[i] = get_be32(block + i * WORD_SIZE);
	}

	for (i = BLOCK_WORDS; i < ROUNDS; i++) {
		uint32_t s0 = rotate(schedule[i - 15], 7) ^ rotate(schedule[i - 15], 18) ^ schedule[i - 15] >> 3;
		uint32_t s1 = rotate(schedule[i - 2], 17) ^ rotate(schedule[i - 2], 19) ^ schedule[i - 2] >> 10;

		schedule[i] = schedule[i - 16] + s0 + schedule[i - 7] + s1;
	}

	for (i = 0; i < ROUNDS; i++) {
		uint32_t sum1 = rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25);
		uint32_t choice = (e & f) ^ (~e & g);
		uint32_t first = h + sum1 + choice + constants[i] + schedule[i];
		uint32_t sum0 = rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22);
		uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
		uint32_t second = sum0 + majority;

		h = g;
		g = f;
		f = e;
		e = d + first;
		d = c;
		c = b;
		b = a;
		a = first + second;
	}

	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;
	state[4] += e;
	state[5] += f;
	state[6] += g;
	state[7] += h;
}

void sha256_init(struct sha256 *context)
{
	memcpy(context->state, initial, sizeof(initial));
	context->length = 0;
	context->used = 0;
}

void sha256_update(struct sha256 *context, const void *data, size_t length)
{
	const uint8_t *bytes = data;

	context->length += length;

	if (context->used > 0) {
		size_t take = SHA256_BLOCK - context->used < length ? SHA256_BLOCK - context->used : length;

		memcpy(context->block + context->used, bytes, take);
		context->used += take;
		bytes += take;
		length -= take;

		if (context->used < SHA256_BLOCK) {
			return;
		}

		compress(context->state, context->block);
		context->used = 0;
	}

	for (; length >= SHA256_BLOCK; bytes += SHA256_BLOCK, length -= SHA256_BLOCK) {
		compress(context->state, bytes);
	}

	memcpy(context->block, bytes, length);
	context->used = length;
}

void sha256_final(struct sha256 *context, uint8_t digest[SHA256_SIZE])
{
	uint64_t bits = context->length * 8;
	unsigned int i;

	context->block[context->used++] = PADDING_START;
	if (context->used > LENGTH_OFFSET) {
		memset(context->block + context->used, 0, SHA256_BLOCK - context->used);
		compress(context->state, context->block);
		context->used = 0;
	}

	memset(context->block + context->used, 0, LENGTH_OFFSET - context->used);
	put_be64(context->block + LENGTH_OFFSET, bits);
	compress(context->state, context->block);

	for (i = 0; i < SHA256_WORDS; i++) {
		put_be32(digest + i * WORD_SIZE, context->state[i]);
	}

	memset(context, 0, sizeof(*context));
}

void sha256(const void *data, size_t length, uint8_t digest[SHA256_SIZE])
{
	struct sha256 context;

	sha256_init(&context);
	sha256_update(&context, data, length);
	sha256_final(&context, digest);
}
