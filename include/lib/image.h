#ifndef LIB_IMAGE_H
#define LIB_IMAGE_H

#include <sys/bits.h>

#define IMAGE_MAGIC		0x644d5241
#define IMAGE_HEADER_SIZE	64
#define IMAGE_TEXT_OFFSET	0x80000

#define IMAGE_FLAG_BIG_ENDIAN	BIT(0)
#define IMAGE_FLAG_PAGE_4K	BIT(1)
#define IMAGE_FLAG_ANYWHERE	BIT(3)

#ifndef __ASSEMBLER__

#include <stddef.h>
#include <stdint.h>

struct image_info {
	uint64_t	text_offset;
	uint64_t	image_size;
	uint64_t	flags;
};

int image_parse(const void *header, size_t length, struct image_info *info);

#endif

#endif
