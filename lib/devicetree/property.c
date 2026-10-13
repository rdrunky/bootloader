#include <ctype.h>
#include <errno.h>
#include <string.h>

#include "private.h"

static int skip_to_property(const struct dt *dt, int offset)
{
	for (;;) {
		int next;
		int tag = dt_token(dt, offset, &next);

		if (tag == TOKEN_PROP) {
			return offset;
		}

		if (tag != TOKEN_NOP) {
			return tag < 0 ? tag : -ENOENT;
		}

		offset = next;
	}
}

int dt_first_property(const struct dt *dt, int node)
{
	int next;
	int tag = dt_token(dt, node, &next);

	if (tag != TOKEN_BEGIN_NODE) {
		return tag < 0 ? tag : -EINVAL;
	}

	return skip_to_property(dt, next);
}

int dt_next_property(const struct dt *dt, int property)
{
	int next;
	int tag = dt_token(dt, property, &next);

	if (tag != TOKEN_PROP) {
		return tag < 0 ? tag : -EINVAL;
	}

	return skip_to_property(dt, next);
}

const void *dt_property_at(const struct dt *dt, int property, const char **name, uint32_t *len)
{
	const uint8_t *structs = dt_structs(dt);
	int next;

	if (dt_token(dt, property, &next) != TOKEN_PROP) {
		return NULL;
	}

	if (name != NULL) {
		*name = (const char *)dt->blob + dt->strings + get_be32(structs + property + PROP_NAME);
	}

	if (len != NULL) {
		*len = get_be32(structs + property + PROP_LENGTH);
	}

	return structs + property + PROP_HEADER_SIZE;
}

const void *dt_property(const struct dt *dt, int node, const char *name, uint32_t *len)
{
	int property;

	if (name == NULL) {
		return NULL;
	}

	for (property = dt_first_property(dt, node); property >= 0; property = dt_next_property(dt, property)) {
		const char *candidate;
		uint32_t size;
		const void *value = dt_property_at(dt, property, &candidate, &size);

		if (strcmp(candidate, name) == 0) {
			if (len != NULL) {
				*len = size;
			}
			return value;
		}
	}

	return NULL;
}

int dt_read_u32(const struct dt *dt, int node, const char *name, uint32_t *value)
{
	uint32_t len;
	const uint8_t *data = dt_property(dt, node, name, &len);

	if (data == NULL) {
		return -ENOENT;
	}

	if (len != sizeof(uint32_t)) {
		return -ERANGE;
	}

	*value = get_be32(data);

	return 0;
}

int dt_read_u64(const struct dt *dt, int node, const char *name, uint64_t *value)
{
	uint32_t len;
	const uint8_t *data = dt_property(dt, node, name, &len);

	if (data == NULL) {
		return -ENOENT;
	}

	if (len == sizeof(uint64_t)) {
		*value = get_be64(data);
	} else if (len == sizeof(uint32_t)) {
		*value = get_be32(data);
	} else {
		return -ERANGE;
	}

	return 0;
}

const char *dt_read_string(const struct dt *dt, int node, const char *name)
{
	uint32_t len;
	const char *data = dt_property(dt, node, name, &len);
	uint32_t i;

	if (data == NULL) {
		return NULL;
	}

	for (i = 0; i < len; i++) {
		if (data[i] == '\0') {
			return data;
		}

		if (!isprint((unsigned char)data[i])) {
			return NULL;
		}
	}

	return NULL;
}

bool dt_compatible(const struct dt *dt, int node, const char *compatible)
{
	uint32_t len;
	const char *list = dt_property(dt, node, "compatible", &len);

	while (list != NULL && len > 0) {
		const char *end = memchr(list, '\0', len);

		if (end == NULL) {
			return false;
		}

		if (strcmp(list, compatible) == 0) {
			return true;
		}

		len -= (uint32_t)(end - list) + 1;
		list = end + 1;
	}

	return false;
}

bool dt_enabled(const struct dt *dt, int node)
{
	const char *status;

	if (dt_name(dt, node) == NULL) {
		return false;
	}

	if (dt_property(dt, node, "status", NULL) == NULL) {
		return true;
	}

	status = dt_read_string(dt, node, "status");

	return status != NULL && (strcmp(status, "okay") == 0 || strcmp(status, "ok") == 0);
}
