#ifndef LIB_DEVICETREE_H
#define LIB_DEVICETREE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DT_MAGIC		0xd00dfeed
#define DT_SIZE_MAX		0x1000000
#define DT_NAME_MAX		64
#define DT_CELL_SIZE		4
#define DT_CELLS_MAX		2

struct dt {
	const uint8_t	*blob;
	uint32_t	size;
	uint32_t	structs;
	uint32_t	structs_size;
	uint32_t	strings;
	uint32_t	strings_size;
	uint32_t	reservations;
	uint32_t	reservation_count;
};

int dt_open(struct dt *dt, const void *blob, size_t limit);

int dt_root(const struct dt *dt);
int dt_next_node(const struct dt *dt, int node, int *depth);
int dt_first_child(const struct dt *dt, int node);
int dt_next_sibling(const struct dt *dt, int node);
int dt_parent(const struct dt *dt, int node);
const char *dt_name(const struct dt *dt, int node);

int dt_find(const struct dt *dt, const char *path);
int dt_find_compatible(const struct dt *dt, int after, const char *compatible);
int dt_find_phandle(const struct dt *dt, uint32_t phandle);
int dt_stdout(const struct dt *dt);

int dt_first_property(const struct dt *dt, int node);
int dt_next_property(const struct dt *dt, int property);
const void *dt_property_at(const struct dt *dt, int property, const char **name, uint32_t *len);
const void *dt_property(const struct dt *dt, int node, const char *name, uint32_t *len);

int dt_read_u32(const struct dt *dt, int node, const char *name, uint32_t *value);
int dt_read_u64(const struct dt *dt, int node, const char *name, uint64_t *value);
const char *dt_read_string(const struct dt *dt, int node, const char *name);
bool dt_compatible(const struct dt *dt, int node, const char *compatible);
bool dt_enabled(const struct dt *dt, int node);

uint32_t dt_address_cells(const struct dt *dt, int node);
uint32_t dt_size_cells(const struct dt *dt, int node);
uint64_t dt_read_cells(const void *cells, uint32_t count);
int dt_reg(const struct dt *dt, int node, unsigned int index, uint64_t *base, uint64_t *size);
int dt_translate(const struct dt *dt, int node, uint64_t *address);
int dt_reservation(const struct dt *dt, unsigned int index, uint64_t *base, uint64_t *size);

size_t dt_used_size(const struct dt *dt);
int dt_copy(struct dt *dt, const struct dt *source, void *buffer, size_t capacity);
int dt_ensure(struct dt *dt, const char *path);
int dt_add_node(struct dt *dt, int parent, const char *name);
int dt_set_property(struct dt *dt, int node, const char *name, const void *value, uint32_t length);
int dt_set_string(struct dt *dt, int node, const char *name, const char *value);
int dt_set_u32(struct dt *dt, int node, const char *name, uint32_t value);
int dt_set_range(struct dt *dt, int node, const char *name, uint64_t base, uint64_t size);

#endif
