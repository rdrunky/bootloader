#include <errno.h>
#include <string.h>

#include "private.h"

static uint8_t *writable(struct dt *dt)
{
	return (uint8_t *)(uintptr_t)dt->blob;
}

static uint32_t free_space(const struct dt *dt)
{
	return dt->size - (dt->strings + dt->strings_size);
}

static void store_header(struct dt *dt)
{
	uint8_t *blob = writable(dt);

	put_be32(blob + HEADER_STRUCTS, dt->structs);
	put_be32(blob + HEADER_STRINGS, dt->strings);
	put_be32(blob + HEADER_STRUCTS_SIZE, dt->structs_size);
	put_be32(blob + HEADER_STRINGS_SIZE, dt->strings_size);
}

size_t dt_used_size(const struct dt *dt)
{
	return HEADER_SIZE + (size_t)(dt->reservation_count + 1) * RESERVATION_SIZE + dt->structs_size +
	       dt->strings_size;
}

int dt_copy(struct dt *dt, const struct dt *source, void *buffer, size_t capacity)
{
	uint32_t reservations = (source->reservation_count + 1) * RESERVATION_SIZE;
	uint32_t structs = HEADER_SIZE + reservations;
	uint32_t strings = structs + source->structs_size;
	uint8_t *out = buffer;

	if (source->blob == NULL || buffer == NULL) {
		return -EINVAL;
	}

	if (capacity < dt_used_size(source) || capacity > DT_SIZE_MAX) {
		return -ENOSPC;
	}

	memset(out, 0, capacity);
	put_be32(out + HEADER_MAGIC, DT_MAGIC);
	put_be32(out + HEADER_TOTAL_SIZE, (uint32_t)capacity);
	put_be32(out + HEADER_STRUCTS, structs);
	put_be32(out + HEADER_STRINGS, strings);
	put_be32(out + HEADER_RESERVATIONS, HEADER_SIZE);
	put_be32(out + HEADER_VERSION, VERSION_REQUIRED);
	put_be32(out + HEADER_LAST_COMPATIBLE, VERSION_COMPATIBLE);
	put_be32(out + HEADER_BOOT_CPU, get_be32(source->blob + HEADER_BOOT_CPU));
	put_be32(out + HEADER_STRINGS_SIZE, source->strings_size);
	put_be32(out + HEADER_STRUCTS_SIZE, source->structs_size);

	memcpy(out + HEADER_SIZE, source->blob + source->reservations, reservations);
	memcpy(out + structs, source->blob + source->structs, source->structs_size);
	memcpy(out + strings, source->blob + source->strings, source->strings_size);

	return dt_open(dt, out, capacity);
}

static int resize_at(struct dt *dt, uint32_t offset, uint32_t old_length, uint32_t new_length)
{
	uint8_t *blob = writable(dt);
	uint32_t at = dt->structs + offset;
	uint32_t end = dt->strings + dt->strings_size;

	if (new_length > old_length && new_length - old_length > free_space(dt)) {
		return -ENOSPC;
	}

	memmove(blob + at + new_length, blob + at + old_length, end - at - old_length);
	if (new_length < old_length) {
		memset(blob + end - (old_length - new_length), 0, old_length - new_length);
	}

	dt->structs_size = dt->structs_size - old_length + new_length;
	dt->strings = dt->strings - old_length + new_length;
	store_header(dt);

	return 0;
}

static int string_offset(struct dt *dt, const char *name, uint32_t *offset)
{
	const char *strings = (const char *)dt->blob + dt->strings;
	uint32_t length = (uint32_t)strlen(name) + 1;
	uint32_t at = 0;

	while (at < dt->strings_size) {
		if (strcmp(strings + at, name) == 0) {
			*offset = at;
			return 0;
		}

		at += (uint32_t)strnlen(strings + at, dt->strings_size - at) + 1;
	}

	if (length > free_space(dt)) {
		return -ENOSPC;
	}

	memcpy(writable(dt) + dt->strings + dt->strings_size, name, length);
	*offset = dt->strings_size;
	dt->strings_size += length;
	store_header(dt);

	return 0;
}

static int properties_end(const struct dt *dt, int offset)
{
	for (;;) {
		int next;
		int tag = dt_token(dt, offset, &next);

		if (tag != TOKEN_PROP && tag != TOKEN_NOP) {
			return tag < 0 ? tag : offset;
		}

		offset = next;
	}
}

static int find_property(const struct dt *dt, int node, const char *name)
{
	int property;

	for (property = dt_first_property(dt, node); property >= 0; property = dt_next_property(dt, property)) {
		const char *candidate;

		if (dt_property_at(dt, property, &candidate, NULL) != NULL && strcmp(candidate, name) == 0) {
			return property;
		}
	}

	return property;
}

int dt_set_property(struct dt *dt, int node, const char *name, const void *value, uint32_t length)
{
	uint32_t name_offset;
	uint8_t *entry;
	int property;
	int next;
	int err;

	if (name == NULL || *name == '\0' || length > DT_SIZE_MAX || dt_token(dt, node, &next) != TOKEN_BEGIN_NODE) {
		return -EINVAL;
	}

	property = find_property(dt, node, name);
	if (property >= 0) {
		uint32_t old = get_be32(dt_structs(dt) + property + PROP_LENGTH);

		err = resize_at(dt, (uint32_t)property + PROP_HEADER_SIZE, align_token(old), align_token(length));
	} else {
		err = string_offset(dt, name, &name_offset);
		if (err == 0) {
			property = properties_end(dt, next);
			err = property < 0 ? property : resize_at(dt, (uint32_t)property, 0, PROP_HEADER_SIZE + align_token(length));
		}

		if (err == 0) {
			entry = writable(dt) + dt->structs + property;
			put_be32(entry, TOKEN_PROP);
			put_be32(entry + PROP_NAME, name_offset);
		}
	}

	if (err != 0) {
		return err;
	}

	entry = writable(dt) + dt->structs + property;
	put_be32(entry + PROP_LENGTH, length);
	memcpy(entry + PROP_HEADER_SIZE, value, length);
	memset(entry + PROP_HEADER_SIZE + length, 0, align_token(length) - length);

	return 0;
}

int dt_set_string(struct dt *dt, int node, const char *name, const char *value)
{
	return dt_set_property(dt, node, name, value, (uint32_t)strlen(value) + 1);
}

int dt_set_u32(struct dt *dt, int node, const char *name, uint32_t value)
{
	uint8_t cell[sizeof(uint32_t)];

	put_be32(cell, value);

	return dt_set_property(dt, node, name, cell, sizeof(cell));
}

int dt_set_range(struct dt *dt, int node, const char *name, uint64_t base, uint64_t size)
{
	uint8_t cells[2 * sizeof(uint64_t)];

	put_be64(cells, base);
	put_be64(cells + sizeof(uint64_t), size);

	return dt_set_property(dt, node, name, cells, sizeof(cells));
}

static int end_of_node(const struct dt *dt, int node)
{
	int depth = 0;
	int next;
	int tag = dt_token(dt, node, &next);

	if (tag != TOKEN_BEGIN_NODE) {
		return tag < 0 ? tag : -EINVAL;
	}

	for (;;) {
		int offset = next;

		tag = dt_token(dt, offset, &next);
		switch (tag) {
		case TOKEN_BEGIN_NODE:
			depth++;
			break;
		case TOKEN_END_NODE:
			if (depth-- == 0) {
				return offset;
			}
			break;
		case TOKEN_PROP:
		case TOKEN_NOP:
			break;
		default:
			return tag < 0 ? tag : -EBADMSG;
		}
	}
}

int dt_add_node(struct dt *dt, int parent, const char *name)
{
	uint32_t length = (uint32_t)strlen(name) + 1;
	int end = end_of_node(dt, parent);
	uint8_t *entry;
	int err;

	if (end < 0) {
		return end;
	}

	if (length == 1 || strchr(name, '/') != NULL) {
		return -EINVAL;
	}

	err = resize_at(dt, (uint32_t)end, 0, TOKEN_SIZE + align_token(length) + TOKEN_SIZE);
	if (err != 0) {
		return err;
	}

	entry = writable(dt) + dt->structs + end;
	put_be32(entry, TOKEN_BEGIN_NODE);
	memcpy(entry + TOKEN_SIZE, name, length);
	memset(entry + TOKEN_SIZE + length, 0, align_token(length) - length);
	put_be32(entry + TOKEN_SIZE + align_token(length), TOKEN_END_NODE);

	return end;
}

static int child_named(const struct dt *dt, int node, const char *name, size_t length)
{
	int child;

	for (child = dt_first_child(dt, node); child >= 0; child = dt_next_sibling(dt, child)) {
		const char *candidate = dt_name(dt, child);

		if (candidate != NULL && strncmp(candidate, name, length) == 0 && candidate[length] == '\0') {
			return child;
		}
	}

	return child;
}

int dt_ensure(struct dt *dt, const char *path)
{
	char name[DT_NAME_MAX];
	int node;

	if (path == NULL || path[0] != '/') {
		return -EINVAL;
	}

	for (node = dt_root(dt); node >= 0 && *path != '\0';) {
		size_t length;
		int child;

		path += strspn(path, "/");
		length = strcspn(path, "/");
		if (length == 0) {
			break;
		}

		if (length >= sizeof(name)) {
			return -ENAMETOOLONG;
		}

		child = child_named(dt, node, path, length);
		if (child == -ENOENT) {
			memcpy(name, path, length);
			name[length] = '\0';
			child = dt_add_node(dt, node, name);
		}

		node = child;
		path += length;
	}

	return node;
}
