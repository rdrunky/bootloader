#include <lib/crypto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/macros.h>

#include "harness.h"

#define SIGNATURE_SCALAR	(ED25519_SIGNATURE_SIZE / 2)

static void hex(const uint8_t *digest, size_t size, char *text)
{
	size_t i;

	for (i = 0; i < size; i++) {
		snprintf(text + i * 2, 3, "%02x", digest[i]);
	}
}

static bool digest_is(const void *data, size_t length, const char *expected)
{
	uint8_t digest[SHA256_SIZE];
	char text[SHA256_SIZE * 2 + 1];

	sha256(data, length, digest);
	hex(digest, sizeof(digest), text);

	return strcmp(text, expected) == 0;
}

static bool sha512_is(const void *data, size_t length, const char *expected)
{
	uint8_t digest[SHA512_SIZE];
	char text[SHA512_SIZE * 2 + 1];

	sha512(data, length, digest);
	hex(digest, sizeof(digest), text);

	return strcmp(text, expected) == 0;
}

struct signed_message {
	const char	*public_key;
	const char	*message;
	const char	*signature;
};

struct decoded_message {
	uint8_t		public_key[ED25519_PUBLIC_KEY_SIZE];
	uint8_t		message[16];
	size_t		length;
	uint8_t		signature[ED25519_SIGNATURE_SIZE];
};

static const struct signed_message rfc8032[] = {
	{ "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
	  "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
	  "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b" },
	{ "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
	  "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
	  "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00" },
	{ "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", "af82",
	  "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac"
	  "18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a" },
};

static const uint8_t group_order[] = {
	0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
	0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
};

static void unhex(const char *text, uint8_t *bytes, size_t size)
{
	size_t i;

	for (i = 0; i < size; i++) {
		unsigned int value;

		sscanf(text + i * 2, "%2x", &value);
		bytes[i] = (uint8_t)value;
	}
}

static void decode(const struct signed_message *vector, struct decoded_message *decoded)
{
	decoded->length = strlen(vector->message) / 2;
	unhex(vector->public_key, decoded->public_key, sizeof(decoded->public_key));
	unhex(vector->message, decoded->message, decoded->length);
	unhex(vector->signature, decoded->signature, sizeof(decoded->signature));
}

static bool verifies(const struct decoded_message *decoded)
{
	return ed25519_verify(decoded->public_key, decoded->message, decoded->length, decoded->signature);
}

static void flip(uint8_t *bytes, unsigned int bit)
{
	bytes[bit / 8] ^= (uint8_t)(1 << (bit % 8));
}

TEST(crypto, matches_sha256_reference_vectors)
{
	CHECK(digest_is("", 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
	CHECK(digest_is("abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
	CHECK(digest_is("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56,
			"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
	CHECK(digest_is("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrs"
			"mnopqrstnopqrstu", 112,
			"cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"));
}

TEST(crypto, hashes_a_million_letters_in_odd_pieces)
{
	uint8_t digest[SHA256_SIZE];
	char text[SHA256_SIZE * 2 + 1];
	char *letters = malloc(1000000);
	struct sha256 context;
	size_t offset = 0;
	size_t piece = 1;

	CHECK(letters != NULL);
	memset(letters, 'a', 1000000);
	CHECK(digest_is(letters, 1000000, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));

	sha256_init(&context);
	while (offset < 1000000) {
		size_t take = piece < 1000000 - offset ? piece : 1000000 - offset;

		sha256_update(&context, letters + offset, take);
		offset += take;
		piece = piece * 7 % 977 + 1;
	}

	sha256_final(&context, digest);
	hex(digest, sizeof(digest), text);
	CHECK_STR(text, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
	free(letters);
}

TEST(crypto, matches_sha512_reference_vectors)
{
	CHECK(sha512_is("", 0, "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
			       "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e"));
	CHECK(sha512_is("abc", 3, "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
				  "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"));
	CHECK(sha512_is("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrs"
			"mnopqrstnopqrstu", 112,
			"8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018"
			"501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909"));
}

TEST(crypto, sha512_hashes_a_million_letters_in_odd_pieces)
{
	static const char expected[] = "e718483d0ce769644e2e42c7bc15b4638e1f98b13b2044285632a803afa973eb"
				       "de0ff244877ea60a4cb0432ce577c31beb009c5c2c49aa2e4eadb217ad8cc09b";
	uint8_t digest[SHA512_SIZE];
	char text[SHA512_SIZE * 2 + 1];
	char *letters = malloc(1000000);
	struct sha512 context;
	size_t offset = 0;
	size_t piece = 1;

	CHECK(letters != NULL);
	memset(letters, 'a', 1000000);
	CHECK(sha512_is(letters, 1000000, expected));

	sha512_init(&context);
	while (offset < 1000000) {
		size_t take = piece < 1000000 - offset ? piece : 1000000 - offset;

		sha512_update(&context, letters + offset, take);
		offset += take;
		piece = piece * 7 % 977 + 1;
	}

	sha512_final(&context, digest);
	hex(digest, sizeof(digest), text);
	CHECK_STR(text, expected);
	free(letters);
}

TEST(crypto, sha512_handles_every_padding_boundary)
{
	static const struct {
		size_t		length;
		const char	*digest;
	} cases[] = {
		{ 111, "9a2a120825c2319867758ec277924f6faa254968bf752046dacdd948d8ad299b"
		       "10359fd04bfd7d3810b5fa1b16a294236138baff981cbb85248478053ac4d3dd" },
		{ 112, "a3722b515ef40c910f2419f6e0da8ca51d410114ce6272faae64045f9e9f630e"
		       "7fa8dd5a3243c9860b899d148c3da4bc0f9e07454542604d030bb55531fe0d5b" },
		{ 127, "1d5a8893e7b7ed83d485d26f88cfb846f3760279916976fe538e539fc16f7cd1"
		       "9ba3e1c2cd5fda78749a74205755cdf694e8fa90b2bfed8815f406af76c1d7bf" },
		{ 128, "e2e22f8422b54b06e35c3ea30a383d1de7a8fbc27992923074103117020d8dd7"
		       "024c3ecf7d6d1a15a6de5a75ff32fb486b9e8ced4c02ffe05822bf2cb734d0e0" },
		{ 129, "19015483ea99fa74728fd7b13adba6b729cfa4ab7c388573efa2641d2af0577f"
		       "24dd51acbaeec41a7a11d10f6c4e68a3b90e7b2348c678486a4365c9d9101cdd" },
		{ 239, "805c40517b44cef414a06c632b4ee9c21d34c85a84c3f9530274db86e2a3e38c"
		       "b0e54025666b925426bd6860f81538abbfab13011e85767d814a245d232a6044" },
		{ 240, "3e05caa7e0fddab685e6052bd902c316505180504344d00b1217e1349a532d1d"
		       "0e5ce4b24d69eea3aca369dc16d8bbdf872b2fe1770e0e1aad22f84e0519bc6c" },
	};
	char data[240];
	unsigned int i;

	memset(data, 'x', sizeof(data));
	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		CHECK(sha512_is(data, cases[i].length, cases[i].digest));
	}
}

TEST(crypto, handles_every_padding_boundary)
{
	static const struct {
		size_t		length;
		const char	*digest;
	} cases[] = {
		{ 55, "d5e285683cd4efc02d021a5c62014694958901005d6f71e89e0989fac77e4072" },
		{ 56, "04c26261370ee7541549d16dee320c723e3fd14671e66a099afe0a377c16888e" },
		{ 63, "75220b47218278e656f2013bb8f0c455a25eaf01e86c64924e9d48d89776d6f2" },
		{ 64, "7ce100971f64e7001e8fe5a51973ecdfe1ced42befe7ee8d5fd6219506b5393c" },
		{ 65, "9537c5fdf120482f7d58d25e9ed583f52c02b4e304ea814db1633ad565aed7e9" },
		{ 119, "000b48d4edf0fa7bee3c6236ecd2785baa5db4eeb8bb54341b029e0d9fa5fb0c" },
		{ 120, "13f05a0b594787f5ecd315edc96141bd3243203d1b7d4f0836f37308b276ba98" },
	};
	char data[120];
	unsigned int i;

	memset(data, 'x', sizeof(data));
	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		CHECK(digest_is(data, cases[i].length, cases[i].digest));
	}
}

TEST(crypto, ed25519_accepts_rfc8032_vectors)
{
	struct decoded_message decoded;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(rfc8032); i++) {
		decode(&rfc8032[i], &decoded);
		CHECK(verifies(&decoded));
	}
}

TEST(crypto, ed25519_accepts_messages_across_block_boundaries)
{
	static const struct {
		size_t		length;
		const char	*signature;
	} cases[] = {
		{ 31, "f3e1ce347a78b067f6e77a99269fa954067a8097db399745eb2ea0bb1a84c5d2"
		      "0ff92e127da46ced1de507d46d132ded85953a9225ba19f0a6a788cf22f1a005" },
		{ 32, "2c0197fe2ace8a72d494cc390fb4de0ea13c5daadc0d85a63c3f0f51094e2077"
		      "371b3b33716b79b5bec873e42518ffbdc07b6800bf73a21d3740456840859e05" },
		{ 33, "e6ed42fda7e19ab982fd0b58f11d7c4a0b6065716c8bad0b40302632ab38f445"
		      "3ea415055d92a53c7d3e7d24a275a17d87b4fc91550920fd3c8ca63ef8b4e603" },
		{ 63, "2ff5d43ebcf72c012c667db02abfd27d05b1cf9db2cbfaa09afdb98be04f26af"
		      "c1c3ff370b5f6bfb5d53228f41fe02f37d3a99d71f93fa74ac63796588e0b203" },
		{ 64, "2c517cb735854b4f03691396b00df20069dd08e8680e3da3630cc2f65248b639"
		      "635d9ee6d7e15149cf3e8c0de8fdc62b6d6d02e3031edb407a3a941d5776fc08" },
		{ 65, "945bf0ac8f143e706b09e3337597ede94a27dffc866d85be327666c842d6aad3"
		      "197bcd99b2644ce7e5c601bc3241237c0804590609eaab204e0314c3684f7d0f" },
		{ 127, "77ccc91fdfbc7f124a440ff4c7ae36df16952e9c9fa2e048470a5f3067390749"
		       "8c15be0b854c2383f31eb6e0399f008faf811e4d7a4c1a96b1b42dee0a247606" },
		{ 128, "da240f8d9956fb8194b565889776a3430fd01041cbc8df718832dae2885b59ba"
		       "4e765dfacef27d7076847ce93668b8ae3f6268603c2491bf0704391eec45ba04" },
		{ 129, "41016296af4a73be4347d03e719139128878d67070cf057a5a45a5d2b7fc7150"
		       "6b5c85123971d42cee12eefa929540e8c0c4dfec34702291773cff5fdb88cc0c" },
		{ 200, "f8c9a5343b8fa2c39668e5192f14fbc1ca8315b9afb7db458eba0ba6cd024846"
		       "86e7aad6800af157b179b31d630ddf5efc8aa6e685d50d1bf2e1076576979d03" },
		{ 1500, "6a73892420082a9d558b7d28c60a8e386479fdda9771532dbe325560dfeefacf"
			"3dcfeb3c6fcc852863b327a6a4d99a1f7f8a2120632de8a46a492b45c9d1d00a" },
	};
	uint8_t public_key[ED25519_PUBLIC_KEY_SIZE];
	uint8_t signature[ED25519_SIGNATURE_SIZE];
	uint8_t message[1500];
	unsigned int i;

	unhex("be0423c3ea44761433d1d74dd93ae52ecd725297b81d9865e711b3ef0c9db80e", public_key, sizeof(public_key));
	for (i = 0; i < sizeof(message); i++) {
		message[i] = (uint8_t)(i * 131 + 7);
	}

	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		unhex(cases[i].signature, signature, sizeof(signature));
		CHECK(ed25519_verify(public_key, message, cases[i].length, signature));
		CHECK(!ed25519_verify(public_key, message, cases[i].length - 1, signature));
	}
}

TEST(crypto, ed25519_rejects_every_flipped_bit)
{
	struct decoded_message decoded;
	unsigned int bit;

	decode(&rfc8032[2], &decoded);

	for (bit = 0; bit < sizeof(decoded.signature) * 8; bit++) {
		flip(decoded.signature, bit);
		CHECK(!verifies(&decoded));
		flip(decoded.signature, bit);
	}

	for (bit = 0; bit < sizeof(decoded.public_key) * 8; bit++) {
		flip(decoded.public_key, bit);
		CHECK(!verifies(&decoded));
		flip(decoded.public_key, bit);
	}

	for (bit = 0; bit < decoded.length * 8; bit++) {
		flip(decoded.message, bit);
		CHECK(!verifies(&decoded));
		flip(decoded.message, bit);
	}

	CHECK(verifies(&decoded));
}

TEST(crypto, ed25519_rejects_scalar_beyond_group_order)
{
	uint8_t identity[ED25519_PUBLIC_KEY_SIZE] = { 1 };
	uint8_t signature[ED25519_SIGNATURE_SIZE] = { 1 };
	struct decoded_message decoded;
	unsigned int carry = 0;
	unsigned int i;

	decode(&rfc8032[0], &decoded);
	for (i = 0; i < sizeof(group_order); i++) {
		carry += decoded.signature[SIGNATURE_SCALAR + i] + group_order[i];
		decoded.signature[SIGNATURE_SCALAR + i] = (uint8_t)carry;
		carry >>= 8;
	}
	CHECK(!verifies(&decoded));

	CHECK(ed25519_verify(identity, "boot", 4, signature));
	memcpy(signature + SIGNATURE_SCALAR, group_order, sizeof(group_order));
	CHECK(!ed25519_verify(identity, "boot", 4, signature));
}

TEST(crypto, ed25519_rejects_non_canonical_points)
{
	uint8_t identity[ED25519_PUBLIC_KEY_SIZE] = { 1 };
	uint8_t identity_above_prime[ED25519_PUBLIC_KEY_SIZE];
	uint8_t negative_zero[ED25519_PUBLIC_KEY_SIZE] = { 1 };
	uint8_t off_curve[ED25519_PUBLIC_KEY_SIZE] = { 2 };
	uint8_t signature[ED25519_SIGNATURE_SIZE] = { 1 };

	memset(identity_above_prime, 0xff, sizeof(identity_above_prime));
	identity_above_prime[0] = 0xee;
	identity_above_prime[ED25519_PUBLIC_KEY_SIZE - 1] = 0x7f;
	negative_zero[ED25519_PUBLIC_KEY_SIZE - 1] = 0x80;

	CHECK(ed25519_verify(identity, "boot", 4, signature));
	CHECK(!ed25519_verify(identity_above_prime, "boot", 4, signature));
	CHECK(!ed25519_verify(negative_zero, "boot", 4, signature));
	CHECK(!ed25519_verify(off_curve, "boot", 4, signature));

	memcpy(signature, identity_above_prime, sizeof(identity_above_prime));
	CHECK(!ed25519_verify(identity, "boot", 4, signature));
}

TEST(crypto, ed25519_rejects_random_signatures)
{
	struct decoded_message decoded;
	uint64_t state = 25519;
	unsigned int round;
	unsigned int i;

	decode(&rfc8032[1], &decoded);
	for (round = 0; round < 64; round++) {
		for (i = 0; i < sizeof(decoded.signature); i++) {
			decoded.signature[i] = (uint8_t)test_random(&state);
		}
		decoded.signature[ED25519_SIGNATURE_SIZE - 1] &= 0x0f;
		CHECK(!verifies(&decoded));
	}
}
