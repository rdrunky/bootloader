#include <errno.h>
#include <lib/image.h>
#include <sys/endian.h>

#define HEADER_TEXT_OFFSET	8
#define HEADER_IMAGE_SIZE	16
#define HEADER_FLAGS		24
#define HEADER_MAGIC		56

#define TEXT_OFFSET_MAX		(512ULL << 20)
#define IMAGE_SIZE_MAX		(1ULL << 30)

int image_parse(const void *header, size_t length, struct image_info *info)
{
	const uint8_t *bytes = header;

	if (length < IMAGE_HEADER_SIZE || get_le32(bytes + HEADER_MAGIC) != IMAGE_MAGIC) {
		return -ENOEXEC;
	}

	info->text_offset = get_le64(bytes + HEADER_TEXT_OFFSET);
	info->image_size = get_le64(bytes + HEADER_IMAGE_SIZE);
	info->flags = get_le64(bytes + HEADER_FLAGS);

	if (info->image_size == 0) {
		info->text_offset = IMAGE_TEXT_OFFSET;
	}

	if ((info->flags & IMAGE_FLAG_BIG_ENDIAN) != 0) {
		return -ENOTSUP;
	}

	if (info->text_offset >= TEXT_OFFSET_MAX || info->text_offset % 8 != 0 || info->image_size > IMAGE_SIZE_MAX) {
		return -EINVAL;
	}

	return 0;
}
