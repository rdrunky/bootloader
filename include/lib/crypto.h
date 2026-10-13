#ifndef LIB_CRYPTO_H
#define LIB_CRYPTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SHA256_SIZE	32
#define SHA256_BLOCK	64
#define SHA256_WORDS	8

struct sha256 {
	uint32_t	state[SHA256_WORDS];
	uint64_t	length;
	uint8_t		block[SHA256_BLOCK];
	size_t		used;
};

#define SHA512_SIZE	64
#define SHA512_BLOCK	128
#define SHA512_WORDS	8

struct sha512 {
	uint64_t	state[SHA512_WORDS];
	uint64_t	length;
	uint8_t		block[SHA512_BLOCK];
	size_t		used;
};

#define ED25519_PUBLIC_KEY_SIZE	32
#define ED25519_SIGNATURE_SIZE	64

void sha256_init(struct sha256 *context);
void sha256_update(struct sha256 *context, const void *data, size_t length);
void sha256_final(struct sha256 *context, uint8_t digest[SHA256_SIZE]);
void sha256(const void *data, size_t length, uint8_t digest[SHA256_SIZE]);

void sha512_init(struct sha512 *context);
void sha512_update(struct sha512 *context, const void *data, size_t length);
void sha512_final(struct sha512 *context, uint8_t digest[SHA512_SIZE]);
void sha512(const void *data, size_t length, uint8_t digest[SHA512_SIZE]);

bool ed25519_verify(const uint8_t public_key[ED25519_PUBLIC_KEY_SIZE], const void *message, size_t length,
		    const uint8_t signature[ED25519_SIGNATURE_SIZE]);

#endif
