#include <errno.h>
#include <string.h>

#include "private.h"

#define RESERVATIONS_MAX	256
#define STDOUT_PATH_MAX		256

static bool block_fits(uint32_t offset, uint32_t length, uint32_t size)
{
	return offset >= HEADER_SIZE && offset <= size && length <= size - offset;
}

static bool string_valid(const struct dt *dt, uint32_t offset)
{
	const char *strings = (const char *)dt->blob + dt->strings;

	return offset < dt->strings_size && memchr(strings + offset, '\0', dt->strings_size - offset) != NULL;
}

int dt_token(const struct dt *dt, int offset, int *next)
{
	const uint8_t *structs = dt_structs(dt);
	uint32_t at = (uint32_t)offset;
	uint32_t tag;

	if (offset < 0 || at % TOKEN_SIZE != 0 || dt->structs_size < TOKEN_SIZE || at > dt->structs_size - TOKEN_SIZE) {
		return -EBADMSG;
	}

	tag = get_be32(structs + at);
	at += TOKEN_SIZE;

	switch (tag) {
	case TOKEN_BEGIN_NODE: {
		const uint8_t *end = memchr(structs + at, '\0', dt->structs_size - at);

		if (end == NULL) {
			return -EBADMSG;
		}

		at = (uint32_t)(end - structs) + 1;
		break;
	}
	case TOKEN_PROP: {
		const uint8_t *header = structs + offset;
		uint32_t len;

		if (dt->structs_size - at < PROP_HEADER_SIZE - TOKEN_SIZE) {
			return -EBADMSG;
		}

		len = get_be32(header + PROP_LENGTH);
		if (!string_valid(dt, get_be32(header + PROP_NAME))) {
			return -EBADMSG;
		}

		at += PROP_HEADER_SIZE - TOKEN_SIZE;
		if (len > dt->structs_size - at) {
			return -EBADMSG;
		}

		at += len;
		break;
	}
	case TOKEN_END_NODE:
	case TOKEN_NOP:
	case TOKEN_END:
		break;
	default:
		return -EBADMSG;
	}

	*next = (int)align_token(at);

	return (int)tag;
}

static int validate_structure(const struct dt *dt)
{
	const uint8_t *structs = dt_structs(dt);
	bool rooted = false;
	int offset = 0;
	int depth = 0;

	for (;;) {
		int next;
		int tag = dt_token(dt, offset, &next);

		switch (tag) {
		case TOKEN_BEGIN_NODE:
			if (rooted ? depth == 0 : structs[offset + TOKEN_SIZE] != '\0') {
				return -EBADMSG;
			}
			rooted = true;
			if (++depth > DEPTH_MAX) {
				return -EBADMSG;
			}
			break;
		case TOKEN_END_NODE:
			if (--depth < 0) {
				return -EBADMSG;
			}
			break;
		case TOKEN_PROP:
			if (depth == 0) {
				return -EBADMSG;
			}
			break;
		case TOKEN_NOP:
			break;
		case TOKEN_END:
			return rooted && depth == 0 ? 0 : -EBADMSG;
		default:
			return tag < 0 ? tag : -EBADMSG;
		}

		offset = next;
	}
}

static int count_reservations(struct dt *dt)
{
	uint32_t at = dt->reservations;
	uint32_t count;

	for (count = 0; count < RESERVATIONS_MAX; count++, at += RESERVATION_SIZE) {
		if (!block_fits(at, RESERVATION_SIZE, dt->size)) {
			return -EBADMSG;
		}

		if (get_be64(dt->blob + at) == 0 && get_be64(dt->blob + at + 8) == 0) {
			dt->reservation_count = count;
			return 0;
		}
	}

	return -EBADMSG;
}

static int check_layout(struct dt *dt)
{
	if (dt->structs % TOKEN_SIZE != 0 || dt->structs_size % TOKEN_SIZE != 0 || dt->reservations % 8 != 0) {
		return -EBADMSG;
	}

	if (!block_fits(dt->structs, dt->structs_size, dt->size) ||
	    !block_fits(dt->strings, dt->strings_size, dt->size)) {
		return -EBADMSG;
	}

	return 0;
}

int dt_open(struct dt *dt, const void *blob, size_t limit)
{
	const uint8_t *header = blob;
	int err;

	*dt = (struct dt){ 0 };

	if (blob == NULL || limit < HEADER_SIZE) {
		return -EINVAL;
	}

	if (get_be32(header + HEADER_MAGIC) != DT_MAGIC) {
		return -ENOEXEC;
	}

	if (get_be32(header + HEADER_VERSION) < VERSION_REQUIRED ||
	    get_be32(header + HEADER_LAST_COMPATIBLE) > VERSION_REQUIRED) {
		return -ENOTSUP;
	}

	dt->blob = header;
	dt->size = get_be32(header + HEADER_TOTAL_SIZE);
	dt->structs = get_be32(header + HEADER_STRUCTS);
	dt->structs_size = get_be32(header + HEADER_STRUCTS_SIZE);
	dt->strings = get_be32(header + HEADER_STRINGS);
	dt->strings_size = get_be32(header + HEADER_STRINGS_SIZE);
	dt->reservations = get_be32(header + HEADER_RESERVATIONS);

	if (dt->size < HEADER_SIZE || dt->size > limit || dt->size > DT_SIZE_MAX) {
		err = -EBADMSG;
	} else {
		err = check_layout(dt);
	}

	if (err == 0) {
		err = count_reservations(dt);
	}

	if (err == 0) {
		err = validate_structure(dt);
	}

	if (err != 0) {
		*dt = (struct dt){ 0 };
	}

	return err;
}

int dt_root(const struct dt *dt)
{
	int offset = 0;
	int next;
	int tag;

	while ((tag = dt_token(dt, offset, &next)) == TOKEN_NOP) {
		offset = next;
	}

	if (tag != TOKEN_BEGIN_NODE) {
		return tag < 0 ? tag : -EBADMSG;
	}

	return offset;
}

int dt_next_node(const struct dt *dt, int node, int *depth)
{
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
			if (depth != NULL) {
				(*depth)++;
			}
			return offset;
		case TOKEN_END_NODE:
			if (depth != NULL) {
				(*depth)--;
			}
			break;
		case TOKEN_PROP:
		case TOKEN_NOP:
			break;
		case TOKEN_END:
			return -ENOENT;
		default:
			return tag < 0 ? tag : -EBADMSG;
		}
	}
}

int dt_first_child(const struct dt *dt, int node)
{
	int depth = 0;
	int child = dt_next_node(dt, node, &depth);

	if (child < 0) {
		return child;
	}

	return depth == 1 ? child : -ENOENT;
}

int dt_next_sibling(const struct dt *dt, int node)
{
	int depth = 0;

	do {
		node = dt_next_node(dt, node, &depth);
		if (node < 0) {
			return node;
		}
	} while (depth > 0);

	return depth == 0 ? node : -ENOENT;
}

int dt_parent(const struct dt *dt, int node)
{
	int path[DEPTH_MAX + 1];
	int offset = dt_root(dt);
	int depth = 0;

	while (offset >= 0) {
		if (depth < 0 || depth > DEPTH_MAX) {
			return -EBADMSG;
		}

		path[depth] = offset;
		if (offset == node) {
			return depth > 0 ? path[depth - 1] : -ENOENT;
		}

		offset = dt_next_node(dt, offset, &depth);
	}

	return offset;
}

const char *dt_name(const struct dt *dt, int node)
{
	int next;

	if (dt_token(dt, node, &next) != TOKEN_BEGIN_NODE) {
		return NULL;
	}

	return (const char *)dt_structs(dt) + node + TOKEN_SIZE;
}

static bool name_matches(const char *name, const char *component, size_t len)
{
	if (strncmp(name, component, len) != 0) {
		return false;
	}

	return name[len] == '\0' || (name[len] == '@' && memchr(component, '@', len) == NULL);
}

static int find_child(const struct dt *dt, int node, const char *component, size_t len)
{
	int child;

	for (child = dt_first_child(dt, node); child >= 0; child = dt_next_sibling(dt, child)) {
		const char *name = dt_name(dt, child);

		if (name != NULL && name_matches(name, component, len)) {
			return child;
		}
	}

	return child;
}

static int find_path(const struct dt *dt, int node, const char *path)
{
	while (node >= 0) {
		size_t len;

		while (*path == '/') {
			path++;
		}

		if (*path == '\0') {
			break;
		}

		len = strcspn(path, "/");
		node = find_child(dt, node, path, len);
		path += len;
	}

	return node;
}

int dt_find(const struct dt *dt, const char *path)
{
	char alias[DT_NAME_MAX];
	const char *target;
	size_t len;

	if (path == NULL || *path == '\0') {
		return -EINVAL;
	}

	if (*path == '/') {
		return find_path(dt, dt_root(dt), path);
	}

	len = strcspn(path, "/");
	if (len >= sizeof(alias)) {
		return -ENAMETOOLONG;
	}

	memcpy(alias, path, len);
	alias[len] = '\0';

	target = dt_read_string(dt, find_path(dt, dt_root(dt), "/aliases"), alias);
	if (target == NULL || *target != '/') {
		return -ENOENT;
	}

	return find_path(dt, find_path(dt, dt_root(dt), target), path + len);
}

int dt_find_compatible(const struct dt *dt, int after, const char *compatible)
{
	int node = after >= 0 ? dt_next_node(dt, after, NULL) : dt_root(dt);

	for (; node >= 0; node = dt_next_node(dt, node, NULL)) {
		if (dt_compatible(dt, node, compatible)) {
			return node;
		}
	}

	return node;
}

int dt_find_phandle(const struct dt *dt, uint32_t phandle)
{
	int node;

	if (phandle == 0 || phandle == UINT32_MAX) {
		return -EINVAL;
	}

	for (node = dt_root(dt); node >= 0; node = dt_next_node(dt, node, NULL)) {
		uint32_t value;

		if (dt_read_u32(dt, node, "phandle", &value) == 0 && value == phandle) {
			return node;
		}
	}

	return node;
}

int dt_stdout(const struct dt *dt)
{
	char path[STDOUT_PATH_MAX];
	const char *value;
	size_t len;

	value = dt_read_string(dt, dt_find(dt, "/chosen"), "stdout-path");
	if (value == NULL) {
		return -ENOENT;
	}

	len = strcspn(value, ":");
	if (len == 0 || len >= sizeof(path)) {
		return -EINVAL;
	}

	memcpy(path, value, len);
	path[len] = '\0';

	return dt_find(dt, path);
}
