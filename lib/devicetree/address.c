#include <errno.h>

#include "private.h"

#define ADDRESS_CELLS_DEFAULT	2
#define SIZE_CELLS_DEFAULT	1

struct cells {
	uint32_t	child;
	uint32_t	parent;
	uint32_t	size;
};

uint32_t dt_address_cells(const struct dt *dt, int node)
{
	uint32_t cells;

	return dt_read_u32(dt, node, "#address-cells", &cells) == 0 ? cells : ADDRESS_CELLS_DEFAULT;
}

uint32_t dt_size_cells(const struct dt *dt, int node)
{
	uint32_t cells;

	return dt_read_u32(dt, node, "#size-cells", &cells) == 0 ? cells : SIZE_CELLS_DEFAULT;
}

uint64_t dt_read_cells(const void *cells, uint32_t count)
{
	const uint8_t *data = cells;
	uint64_t value = 0;
	uint32_t i;

	for (i = 0; i < count; i++) {
		value = value << 32 | get_be32(data + i * DT_CELL_SIZE);
	}

	return value;
}

static bool address_cells_supported(uint32_t cells)
{
	return cells >= 1 && cells <= DT_CELLS_MAX;
}

static int translate_range(const uint8_t *ranges, uint32_t len, const struct cells *cells, uint64_t *address)
{
	uint32_t stride = (cells->child + cells->parent + cells->size) * DT_CELL_SIZE;
	uint32_t at;

	for (at = 0; len - at >= stride; at += stride) {
		const uint8_t *entry = ranges + at;
		uint64_t child = dt_read_cells(entry, cells->child);
		uint64_t parent = dt_read_cells(entry + cells->child * DT_CELL_SIZE, cells->parent);
		uint64_t size = dt_read_cells(entry + (cells->child + cells->parent) * DT_CELL_SIZE, cells->size);

		if (*address >= child && *address - child < size) {
			*address = parent + (*address - child);
			return 0;
		}
	}

	return -ENXIO;
}

static int translate_bus(const struct dt *dt, int bus, int parent, uint64_t *address)
{
	struct cells cells;
	uint32_t len;
	const uint8_t *ranges = dt_property(dt, bus, "ranges", &len);

	if (ranges == NULL) {
		return -ENXIO;
	}

	if (len == 0) {
		return 0;
	}

	cells.child = dt_address_cells(dt, bus);
	cells.parent = dt_address_cells(dt, parent);
	cells.size = dt_size_cells(dt, bus);

	if (!address_cells_supported(cells.child) || !address_cells_supported(cells.parent) ||
	    cells.size == 0 || cells.size > DT_CELLS_MAX) {
		return -ENOTSUP;
	}

	return translate_range(ranges, len, &cells, address);
}

int dt_translate(const struct dt *dt, int node, uint64_t *address)
{
	int bus = dt_parent(dt, node);
	unsigned int level;

	for (level = 0; bus >= 0 && level < DEPTH_MAX; level++) {
		int parent = dt_parent(dt, bus);
		int err;

		if (parent < 0) {
			return parent == -ENOENT ? 0 : parent;
		}

		err = translate_bus(dt, bus, parent, address);
		if (err != 0) {
			return err;
		}

		bus = parent;
	}

	return bus < 0 ? bus : -EBADMSG;
}

int dt_reg(const struct dt *dt, int node, unsigned int index, uint64_t *base, uint64_t *size)
{
	int bus = dt_parent(dt, node);
	uint32_t address_cells;
	uint32_t size_cells;
	uint32_t stride;
	uint32_t len;
	const uint8_t *reg;
	uint64_t address;
	int err;

	if (bus < 0) {
		return bus;
	}

	address_cells = dt_address_cells(dt, bus);
	size_cells = dt_size_cells(dt, bus);
	if (!address_cells_supported(address_cells) || size_cells > DT_CELLS_MAX) {
		return -ENOTSUP;
	}

	reg = dt_property(dt, node, "reg", &len);
	if (reg == NULL) {
		return -ENOENT;
	}

	stride = (address_cells + size_cells) * DT_CELL_SIZE;
	if (index >= len / stride) {
		return -ENOENT;
	}

	reg += index * stride;
	address = dt_read_cells(reg, address_cells);

	err = dt_translate(dt, node, &address);
	if (err != 0) {
		return err;
	}

	*base = address;
	*size = dt_read_cells(reg + address_cells * DT_CELL_SIZE, size_cells);

	return 0;
}

int dt_reservation(const struct dt *dt, unsigned int index, uint64_t *base, uint64_t *size)
{
	const uint8_t *entry;

	if (index >= dt->reservation_count) {
		return -ENOENT;
	}

	entry = dt->blob + dt->reservations + index * RESERVATION_SIZE;
	*base = get_be64(entry);
	*size = get_be64(entry + 8);

	return 0;
}
