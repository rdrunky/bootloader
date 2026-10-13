#include <lib/cksum.h>
#include <stdbool.h>

#define CRC32_POLYNOMIAL	0xedb88320u

static uint32_t table[256];
static bool table_ready;

static void build_table(void)
{
	uint32_t index;

	for (index = 0; index < 256; index++) {
		uint32_t value = index;
		unsigned int bit;

		for (bit = 0; bit < 8; bit++) {
			value = (value & 1) != 0 ? CRC32_POLYNOMIAL ^ (value >> 1) : value >> 1;
		}

		table[index] = value;
	}

	table_ready = true;
}

uint32_t crc32_update(uint32_t crc, const void *data, size_t length)
{
	const uint8_t *bytes = data;

	if (!table_ready) {
		build_table();
	}

	crc = ~crc;
	while (length-- > 0) {
		crc = table[(crc ^ *bytes++) & 0xff] ^ (crc >> 8);
	}

	return ~crc;
}

uint32_t crc32(const void *data, size_t length)
{
	return crc32_update(0, data, length);
}
