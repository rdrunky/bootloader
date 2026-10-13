#include <lib/crypto.h>
#include <string.h>
#include <sys/endian.h>

#define ROUNDS		80
#define WORD_SIZE	sizeof(uint64_t)
#define BLOCK_WORDS	(SHA512_BLOCK / WORD_SIZE)
#define LENGTH_OFFSET	112
#define PADDING_START	0x80

static const uint64_t initial[SHA512_WORDS] = {
	0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
	0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL,
};

static const uint64_t constants[ROUNDS] = {
	0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
	0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
	0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
	0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
	0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
	0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
	0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
	0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
	0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
	0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
	0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
	0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
	0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
	0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
	0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
	0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
	0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
	0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
	0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
	0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
};

static uint64_t rotate(uint64_t value, unsigned int count)
{
	return value >> count | value << (64 - count);
}

static void compress(uint64_t state[SHA512_WORDS], const uint8_t *block)
{
	uint64_t schedule[ROUNDS];
	uint64_t a = state[0];
	uint64_t b = state[1];
	uint64_t c = state[2];
	uint64_t d = state[3];
	uint64_t e = state[4];
	uint64_t f = state[5];
	uint64_t g = state[6];
	uint64_t h = state[7];
	unsigned int i;

	for (i = 0; i < BLOCK_WORDS; i++) {
		schedule[i] = get_be64(block + i * WORD_SIZE);
	}

	for (i = BLOCK_WORDS; i < ROUNDS; i++) {
		uint64_t s0 = rotate(schedule[i - 15], 1) ^ rotate(schedule[i - 15], 8) ^ schedule[i - 15] >> 7;
		uint64_t s1 = rotate(schedule[i - 2], 19) ^ rotate(schedule[i - 2], 61) ^ schedule[i - 2] >> 6;

		schedule[i] = schedule[i - 16] + s0 + schedule[i - 7] + s1;
	}

	for (i = 0; i < ROUNDS; i++) {
		uint64_t sum1 = rotate(e, 14) ^ rotate(e, 18) ^ rotate(e, 41);
		uint64_t choice = (e & f) ^ (~e & g);
		uint64_t first = h + sum1 + choice + constants[i] + schedule[i];
		uint64_t sum0 = rotate(a, 28) ^ rotate(a, 34) ^ rotate(a, 39);
		uint64_t majority = (a & b) ^ (a & c) ^ (b & c);
		uint64_t second = sum0 + majority;

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

void sha512_init(struct sha512 *context)
{
	memcpy(context->state, initial, sizeof(initial));
	context->length = 0;
	context->used = 0;
}

void sha512_update(struct sha512 *context, const void *data, size_t length)
{
	const uint8_t *bytes = data;

	context->length += length;

	if (context->used > 0) {
		size_t take = SHA512_BLOCK - context->used < length ? SHA512_BLOCK - context->used : length;

		memcpy(context->block + context->used, bytes, take);
		context->used += take;
		bytes += take;
		length -= take;

		if (context->used < SHA512_BLOCK) {
			return;
		}

		compress(context->state, context->block);
		context->used = 0;
	}

	for (; length >= SHA512_BLOCK; bytes += SHA512_BLOCK, length -= SHA512_BLOCK) {
		compress(context->state, bytes);
	}

	memcpy(context->block, bytes, length);
	context->used = length;
}

void sha512_final(struct sha512 *context, uint8_t digest[SHA512_SIZE])
{
	uint64_t bits = context->length * 8;
	unsigned int i;

	context->block[context->used++] = PADDING_START;
	if (context->used > LENGTH_OFFSET) {
		memset(context->block + context->used, 0, SHA512_BLOCK - context->used);
		compress(context->state, context->block);
		context->used = 0;
	}

	memset(context->block + context->used, 0, SHA512_BLOCK - WORD_SIZE - context->used);
	put_be64(context->block + SHA512_BLOCK - WORD_SIZE, bits);
	compress(context->state, context->block);

	for (i = 0; i < SHA512_WORDS; i++) {
		put_be64(digest + i * WORD_SIZE, context->state[i]);
	}

	memset(context, 0, sizeof(*context));
}

void sha512(const void *data, size_t length, uint8_t digest[SHA512_SIZE])
{
	struct sha512 context;

	sha512_init(&context);
	sha512_update(&context, data, length);
	sha512_final(&context, digest);
}
