#include <errno.h>
#include <lib/devicetree.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/endian.h>
#include <sys/macros.h>

#include "harness.h"

#define HEADER_SIZE		40
#define RESERVATION_SIZE	16
#define STRUCTS_MAX		8192
#define STRINGS_MAX		1024
#define BLOB_MAX		16384
#define RESERVATIONS_MAX	4
#define FUZZ_ROUNDS		20000
#define WALK_DEPTH		66

#define TOKEN_BEGIN_NODE	1
#define TOKEN_END_NODE		2
#define TOKEN_PROP		3
#define TOKEN_END		9

#define OFFSET_TOTAL_SIZE	4
#define OFFSET_STRUCTS		8
#define OFFSET_STRINGS		12
#define OFFSET_VERSION		20
#define OFFSET_STRINGS_SIZE	32
#define OFFSET_STRUCTS_SIZE	36

struct builder {
	uint8_t		structs[STRUCTS_MAX];
	uint32_t	structs_len;
	char		strings[STRINGS_MAX];
	uint32_t	strings_len;
	uint64_t	reservations[RESERVATIONS_MAX][2];
	unsigned int	reservation_count;
	uint8_t		blob[BLOB_MAX];
	uint32_t	blob_len;
};

static struct builder builder;
static uint64_t random_state = 0x2545f4914f6cdd1dULL;

static struct builder *builder_reset(void)
{
	memset(&builder, 0, sizeof(builder));

	return &builder;
}

static void emit(struct builder *b, uint32_t value)
{
	put_be32(b->structs + b->structs_len, value);
	b->structs_len += 4;
}

static void emit_bytes(struct builder *b, const void *data, size_t len)
{
	memcpy(b->structs + b->structs_len, data, len);
	b->structs_len += (uint32_t)len;

	while (b->structs_len % 4 != 0) {
		b->structs[b->structs_len++] = 0;
	}
}

static uint32_t name_offset(struct builder *b, const char *name)
{
	uint32_t offset = 0;

	while (offset < b->strings_len) {
		if (strcmp(b->strings + offset, name) == 0) {
			return offset;
		}
		offset += (uint32_t)strlen(b->strings + offset) + 1;
	}

	memcpy(b->strings + b->strings_len, name, strlen(name) + 1);
	b->strings_len += (uint32_t)strlen(name) + 1;

	return offset;
}

static void begin(struct builder *b, const char *name)
{
	emit(b, TOKEN_BEGIN_NODE);
	emit_bytes(b, name, strlen(name) + 1);
}

static void end(struct builder *b)
{
	emit(b, TOKEN_END_NODE);
}

static void property(struct builder *b, const char *name, const void *value, size_t len)
{
	emit(b, TOKEN_PROP);
	emit(b, (uint32_t)len);
	emit(b, name_offset(b, name));
	emit_bytes(b, value, len);
}

static void property_string(struct builder *b, const char *name, const char *value)
{
	property(b, name, value, strlen(value) + 1);
}

static void property_cells(struct builder *b, const char *name, unsigned int count, ...)
{
	uint8_t data[64];
	unsigned int i;
	va_list ap;

	va_start(ap, count);
	for (i = 0; i < count; i++) {
		put_be32(data + i * 4, va_arg(ap, uint32_t));
	}
	va_end(ap);

	property(b, name, data, count * 4);
}

static void reserve(struct builder *b, uint64_t base, uint64_t size)
{
	b->reservations[b->reservation_count][0] = base;
	b->reservations[b->reservation_count][1] = size;
	b->reservation_count++;
}

static uint8_t *finish(struct builder *b)
{
	uint32_t reservations = HEADER_SIZE;
	uint32_t structs = reservations + (b->reservation_count + 1) * RESERVATION_SIZE;
	uint32_t strings;
	unsigned int i;

	emit(b, TOKEN_END);
	strings = structs + b->structs_len;
	b->blob_len = strings + b->strings_len;

	put_be32(b->blob, DT_MAGIC);
	put_be32(b->blob + OFFSET_TOTAL_SIZE, b->blob_len);
	put_be32(b->blob + OFFSET_STRUCTS, structs);
	put_be32(b->blob + OFFSET_STRINGS, strings);
	put_be32(b->blob + 16, reservations);
	put_be32(b->blob + OFFSET_VERSION, 17);
	put_be32(b->blob + 24, 16);
	put_be32(b->blob + OFFSET_STRINGS_SIZE, b->strings_len);
	put_be32(b->blob + OFFSET_STRUCTS_SIZE, b->structs_len);

	for (i = 0; i < b->reservation_count; i++) {
		put_be64(b->blob + reservations + i * RESERVATION_SIZE, b->reservations[i][0]);
		put_be64(b->blob + reservations + i * RESERVATION_SIZE + 8, b->reservations[i][1]);
	}

	memcpy(b->blob + structs, b->structs, b->structs_len);
	memcpy(b->blob + strings, b->strings, b->strings_len);

	return b->blob;
}

static uint8_t *sample_tree(struct builder *b)
{
	begin(b, "");
	property_cells(b, "#address-cells", 1, 2);
	property_cells(b, "#size-cells", 1, 2);
	property_string(b, "compatible", "test,board");

	begin(b, "aliases");
	property_string(b, "serial0", "/soc/uart@7e201000");
	end(b);

	begin(b, "chosen");
	property_string(b, "stdout-path", "serial0:115200n8");
	end(b);

	begin(b, "memory@0");
	property_string(b, "device_type", "memory");
	property_cells(b, "reg", 4, 0, 0, 0, 0x40000000);
	end(b);

	begin(b, "soc");
	property_string(b, "compatible", "simple-bus");
	property_cells(b, "#address-cells", 1, 1);
	property_cells(b, "#size-cells", 1, 1);
	property_cells(b, "ranges", 4, 0x7e000000, 0, 0xfe000000, 0x1800000);

	begin(b, "uart@7e201000");
	property(b, "compatible", "arm,pl011\0arm,primecell", 24);
	property_cells(b, "reg", 2, 0x7e201000, 0x200);
	property_cells(b, "phandle", 1, 7);
	end(b);

	begin(b, "gpio@7e200000");
	property_string(b, "status", "disabled");
	property_cells(b, "reg", 2, 0x7e200000, 0xb4);
	end(b);

	end(b);

	begin(b, "isolated");
	property_cells(b, "#address-cells", 1, 1);
	property_cells(b, "#size-cells", 1, 1);
	begin(b, "device@10");
	property_cells(b, "reg", 2, 0x10, 0x4);
	end(b);
	end(b);

	end(b);

	reserve(b, 0x3b400000, 0x4c00000);

	return finish(b);
}

static int open_sample(struct dt *dt)
{
	struct builder *b = builder_reset();
	uint8_t *blob = sample_tree(b);

	return dt_open(dt, blob, b->blob_len);
}

TEST(devicetree, sample_opens)
{
	struct dt dt;

	CHECK_EQ(open_sample(&dt), 0);
	CHECK_EQ(dt_root(&dt), 0);
	CHECK_STR(dt_name(&dt, dt_root(&dt)), "");
	CHECK_EQ(dt.reservation_count, 1);
}

TEST(devicetree, translates_through_ranges)
{
	struct dt dt;
	uint64_t base;
	uint64_t size;
	int uart;

	CHECK_EQ(open_sample(&dt), 0);
	uart = dt_find(&dt, "/soc/uart@7e201000");
	CHECK(uart >= 0);
	CHECK_EQ(dt_reg(&dt, uart, 0, &base, &size), 0);
	CHECK_EQ(base, 0xfe201000);
	CHECK_EQ(size, 0x200);
	CHECK_EQ(dt_reg(&dt, uart, 1, &base, &size), -ENOENT);
}

TEST(devicetree, refuses_to_translate_without_ranges)
{
	struct dt dt;
	uint64_t base;
	uint64_t size;
	int device;

	CHECK_EQ(open_sample(&dt), 0);
	device = dt_find(&dt, "/isolated/device");
	CHECK(device >= 0);
	CHECK_EQ(dt_reg(&dt, device, 0, &base, &size), -ENXIO);
}

TEST(devicetree, reads_root_level_reg)
{
	struct dt dt;
	uint64_t base;
	uint64_t size;

	CHECK_EQ(open_sample(&dt), 0);
	CHECK_EQ(dt_reg(&dt, dt_find(&dt, "/memory"), 0, &base, &size), 0);
	CHECK_EQ(base, 0);
	CHECK_EQ(size, 0x40000000);
}

TEST(devicetree, matches_unit_addresses)
{
	struct dt dt;
	int uart;

	CHECK_EQ(open_sample(&dt), 0);
	uart = dt_find(&dt, "/soc/uart@7e201000");
	CHECK(uart >= 0);
	CHECK_EQ(dt_find(&dt, "/soc/uart"), uart);
	CHECK_EQ(dt_find(&dt, "//soc//uart"), uart);
	CHECK_EQ(dt_find(&dt, "/soc/uar"), -ENOENT);
	CHECK_EQ(dt_find(&dt, "/soc/uart@7e201001"), -ENOENT);
	CHECK_EQ(dt_find(&dt, "/"), 0);
	CHECK_EQ(dt_find(&dt, ""), -EINVAL);
}

TEST(devicetree, resolves_aliases_and_stdout)
{
	struct dt dt;
	int uart;

	CHECK_EQ(open_sample(&dt), 0);
	uart = dt_find(&dt, "/soc/uart");
	CHECK_EQ(dt_find(&dt, "serial0"), uart);
	CHECK_EQ(dt_stdout(&dt), uart);
	CHECK_EQ(dt_find(&dt, "serial1"), -ENOENT);
}

TEST(devicetree, walks_family)
{
	struct dt dt;
	int soc;
	int uart;
	int gpio;

	CHECK_EQ(open_sample(&dt), 0);
	soc = dt_find(&dt, "/soc");
	uart = dt_first_child(&dt, soc);
	gpio = dt_next_sibling(&dt, uart);
	CHECK_STR(dt_name(&dt, uart), "uart@7e201000");
	CHECK_STR(dt_name(&dt, gpio), "gpio@7e200000");
	CHECK_EQ(dt_next_sibling(&dt, gpio), -ENOENT);
	CHECK_EQ(dt_first_child(&dt, uart), -ENOENT);
	CHECK_EQ(dt_parent(&dt, uart), soc);
	CHECK_EQ(dt_parent(&dt, soc), 0);
	CHECK_EQ(dt_parent(&dt, 0), -ENOENT);
	CHECK_EQ(dt_next_sibling(&dt, soc), dt_find(&dt, "/isolated"));
}

TEST(devicetree, reads_typed_properties)
{
	struct dt dt;
	uint32_t cells;
	uint64_t wide;
	int uart;

	CHECK_EQ(open_sample(&dt), 0);
	uart = dt_find(&dt, "/soc/uart");
	CHECK_EQ(dt_read_u32(&dt, 0, "#address-cells", &cells), 0);
	CHECK_EQ(cells, 2);
	CHECK_EQ(dt_read_u32(&dt, uart, "reg", &cells), -ERANGE);
	CHECK_EQ(dt_read_u32(&dt, uart, "missing", &cells), -ENOENT);
	CHECK_EQ(dt_read_u64(&dt, uart, "phandle", &wide), 0);
	CHECK_EQ(wide, 7);
	CHECK_STR(dt_read_string(&dt, 0, "compatible"), "test,board");
	CHECK(dt_read_string(&dt, uart, "reg") == NULL);
	CHECK_EQ(dt_address_cells(&dt, dt_find(&dt, "/soc")), 1);
	CHECK_EQ(dt_size_cells(&dt, dt_find(&dt, "/aliases")), 1);
}

TEST(devicetree, matches_compatible_lists)
{
	struct dt dt;
	int uart;

	CHECK_EQ(open_sample(&dt), 0);
	uart = dt_find(&dt, "/soc/uart");
	CHECK(dt_compatible(&dt, uart, "arm,pl011"));
	CHECK(dt_compatible(&dt, uart, "arm,primecell"));
	CHECK(!dt_compatible(&dt, uart, "arm,pl01"));
	CHECK_EQ(dt_find_compatible(&dt, -1, "arm,primecell"), uart);
	CHECK_EQ(dt_find_compatible(&dt, uart, "arm,primecell"), -ENOENT);
}

TEST(devicetree, honours_status)
{
	struct dt dt;

	CHECK_EQ(open_sample(&dt), 0);
	CHECK(dt_enabled(&dt, dt_find(&dt, "/soc/uart")));
	CHECK(!dt_enabled(&dt, dt_find(&dt, "/soc/gpio")));
	CHECK(!dt_enabled(&dt, -1));
}

TEST(devicetree, finds_phandles)
{
	struct dt dt;

	CHECK_EQ(open_sample(&dt), 0);
	CHECK_EQ(dt_find_phandle(&dt, 7), dt_find(&dt, "/soc/uart"));
	CHECK_EQ(dt_find_phandle(&dt, 8), -ENOENT);
	CHECK_EQ(dt_find_phandle(&dt, 0), -EINVAL);
}

TEST(devicetree, lists_properties_in_order)
{
	const char *name;
	struct dt dt;
	uint32_t len;
	int property;

	CHECK_EQ(open_sample(&dt), 0);
	property = dt_first_property(&dt, 0);
	CHECK(dt_property_at(&dt, property, &name, &len) != NULL);
	CHECK_STR(name, "#address-cells");
	CHECK_EQ(len, 4);
	property = dt_next_property(&dt, dt_next_property(&dt, property));
	CHECK(dt_property_at(&dt, property, &name, &len) != NULL);
	CHECK_STR(name, "compatible");
	CHECK_EQ(dt_next_property(&dt, property), -ENOENT);
}

TEST(devicetree, reads_reservations)
{
	struct dt dt;
	uint64_t base;
	uint64_t size;

	CHECK_EQ(open_sample(&dt), 0);
	CHECK_EQ(dt_reservation(&dt, 0, &base, &size), 0);
	CHECK_EQ(base, 0x3b400000);
	CHECK_EQ(size, 0x4c00000);
	CHECK_EQ(dt_reservation(&dt, 1, &base, &size), -ENOENT);
}

static int open_patched(uint32_t offset, uint32_t value)
{
	struct builder *b = builder_reset();
	uint8_t *blob = sample_tree(b);
	struct dt dt;

	put_be32(blob + offset, value);

	return dt_open(&dt, blob, b->blob_len);
}

static uint32_t sample_field(uint32_t offset)
{
	struct builder *b = builder_reset();

	return get_be32(sample_tree(b) + offset);
}

TEST(devicetree, rejects_corrupt_headers)
{
	uint32_t structs = sample_field(OFFSET_STRUCTS);
	uint32_t total = sample_field(OFFSET_TOTAL_SIZE);
	uint32_t strings_size = sample_field(OFFSET_STRINGS_SIZE);

	CHECK_EQ(open_patched(0, 0xd00dfeee), -ENOEXEC);
	CHECK_EQ(open_patched(OFFSET_VERSION, 16), -ENOTSUP);
	CHECK_EQ(open_patched(OFFSET_TOTAL_SIZE, total + 1), -EBADMSG);
	CHECK_EQ(open_patched(OFFSET_TOTAL_SIZE, 39), -EBADMSG);
	CHECK_EQ(open_patched(OFFSET_STRUCTS, structs + 2), -EBADMSG);
	CHECK_EQ(open_patched(OFFSET_STRUCTS, 0), -EBADMSG);
	CHECK_EQ(open_patched(OFFSET_STRUCTS_SIZE, 0xfffffffc), -EBADMSG);
	CHECK_EQ(open_patched(OFFSET_STRINGS_SIZE, strings_size + 1), -EBADMSG);
	CHECK_EQ(open_patched(16, total), -EBADMSG);
}

TEST(devicetree, rejects_corrupt_tokens)
{
	uint32_t structs = sample_field(OFFSET_STRUCTS);

	CHECK_EQ(open_patched(structs, 5), -EBADMSG);
	CHECK_EQ(open_patched(structs + 8, 0xfffffff0), -EBADMSG);
	CHECK_EQ(open_patched(structs + 12, 0x10000), -EBADMSG);
	CHECK_EQ(open_patched(OFFSET_STRUCTS_SIZE, 8), -EBADMSG);
}

static int open_built(struct builder *b)
{
	struct dt dt;
	uint8_t *blob = finish(b);

	return dt_open(&dt, blob, b->blob_len);
}

TEST(devicetree, rejects_bad_nesting)
{
	struct builder *b;
	int i;

	b = builder_reset();
	begin(b, "");
	end(b);
	end(b);
	CHECK_EQ(open_built(b), -EBADMSG);

	b = builder_reset();
	begin(b, "");
	end(b);
	begin(b, "");
	end(b);
	CHECK_EQ(open_built(b), -EBADMSG);

	b = builder_reset();
	property_cells(b, "#size-cells", 1, 1);
	begin(b, "");
	end(b);
	CHECK_EQ(open_built(b), -EBADMSG);

	b = builder_reset();
	begin(b, "root");
	end(b);
	CHECK_EQ(open_built(b), -EBADMSG);

	b = builder_reset();
	begin(b, "");
	CHECK_EQ(open_built(b), -EBADMSG);

	b = builder_reset();
	begin(b, "");
	for (i = 0; i < 70; i++) {
		begin(b, "n");
	}
	for (i = 0; i < 70; i++) {
		end(b);
	}
	end(b);
	CHECK_EQ(open_built(b), -EBADMSG);
}

TEST(devicetree, rejects_unterminated_name)
{
	struct builder *b = builder_reset();
	uint8_t *blob;
	struct dt dt;

	begin(b, "");
	emit(b, TOKEN_BEGIN_NODE);
	emit_bytes(b, "abcd", 4);
	blob = finish(b);
	put_be32(blob + OFFSET_STRUCTS_SIZE, b->structs_len - 4);

	CHECK_EQ(dt_open(&dt, blob, b->blob_len), -EBADMSG);
}

TEST(devicetree, failed_open_is_inert)
{
	struct dt dt;
	uint64_t base;
	uint64_t size;
	uint32_t len;

	CHECK_EQ(open_patched(0, 0), -ENOEXEC);
	CHECK_EQ(dt_open(&dt, NULL, 4096), -EINVAL);
	CHECK(dt_root(&dt) < 0);
	CHECK(dt_find(&dt, "/") < 0);
	CHECK(dt_find(&dt, "serial0") < 0);
	CHECK(dt_stdout(&dt) < 0);
	CHECK(dt_property(&dt, 0, "compatible", &len) == NULL);
	CHECK(dt_reg(&dt, 0, 0, &base, &size) < 0);
	CHECK(dt_reservation(&dt, 0, &base, &size) < 0);
	CHECK(dt_name(&dt, 0) == NULL);
}

static uint8_t *load_virt(size_t *size)
{
	uint8_t *blob = test_load(TEST_DTB, size);
	uint32_t structs_end;
	uint32_t strings_end;

	if (blob == NULL || *size < HEADER_SIZE) {
		free(blob);
		return NULL;
	}

	structs_end = get_be32(blob + OFFSET_STRUCTS) + get_be32(blob + OFFSET_STRUCTS_SIZE);
	strings_end = get_be32(blob + OFFSET_STRINGS) + get_be32(blob + OFFSET_STRINGS_SIZE);
	*size = structs_end > strings_end ? structs_end : strings_end;
	put_be32(blob + OFFSET_TOTAL_SIZE, (uint32_t)*size);

	return blob;
}

TEST(devicetree, virt_describes_console_memory_and_firmware)
{
	uint8_t *blob;
	uint64_t base;
	uint64_t size;
	uint32_t phandle;
	size_t length;
	struct dt dt;
	int node;

	blob = load_virt(&length);
	CHECK(blob != NULL);
	CHECK_EQ(dt_open(&dt, blob, length), 0);

	node = dt_stdout(&dt);
	CHECK_STR(dt_name(&dt, node), "pl011@9000000");
	CHECK(dt_compatible(&dt, node, "arm,pl011"));
	CHECK_EQ(dt_reg(&dt, node, 0, &base, &size), 0);
	CHECK_EQ(base, 0x9000000);
	CHECK_EQ(size, 0x1000);

	CHECK_EQ(dt_reg(&dt, dt_find(&dt, "/memory"), 0, &base, &size), 0);
	CHECK_EQ(base, 0x40000000);
	CHECK_EQ(size, 0x40000000);

	CHECK(dt_find_compatible(&dt, -1, "arm,armv8-timer") >= 0);
	CHECK_STR(dt_read_string(&dt, dt_find(&dt, "/psci"), "method"), "hvc");

	CHECK_EQ(dt_read_u32(&dt, 0, "interrupt-parent", &phandle), 0);
	CHECK(dt_compatible(&dt, dt_find_phandle(&dt, phandle), "arm,cortex-a15-gic"));

	free(blob);
}

TEST(devicetree, virt_parents_agree_with_walk)
{
	int path[WALK_DEPTH];
	uint8_t *blob;
	size_t length;
	struct dt dt;
	int depth = 0;
	int count = 0;
	int node;

	blob = load_virt(&length);
	CHECK(blob != NULL);
	CHECK_EQ(dt_open(&dt, blob, length), 0);

	for (node = dt_root(&dt); node >= 0; node = dt_next_node(&dt, node, &depth)) {
		CHECK(depth >= 0 && depth < WALK_DEPTH);
		path[depth] = node;
		if (depth > 0) {
			CHECK_EQ(dt_parent(&dt, node), path[depth - 1]);
		}
		count++;
	}

	CHECK_EQ(node, -ENOENT);
	CHECK(count > 20);

	free(blob);
}

static bool reopen(const struct dt *dt)
{
	struct dt check;

	return dt_open(&check, dt->blob, dt->size) == 0;
}

TEST(devicetree, edits_chosen_in_a_copy)
{
	struct dt source;
	struct dt dt;
	uint8_t *buffer;
	const uint8_t *range;
	size_t length;
	uint32_t size;
	uint8_t *blob = load_virt(&length);
	int chosen;
	int map;

	CHECK(blob != NULL);
	CHECK_EQ(dt_open(&source, blob, length), 0);
	buffer = malloc(dt_used_size(&source) + 4096);
	CHECK(buffer != NULL);
	CHECK_EQ(dt_copy(&dt, &source, buffer, dt_used_size(&source) + 4096), 0);
	CHECK_EQ(dt_stdout(&dt), dt_stdout(&source));

	chosen = dt_ensure(&dt, "/chosen");
	CHECK(chosen >= 0);
	CHECK_EQ(dt_set_string(&dt, chosen, "boot-args", "console=serial quiet"), 0);
	CHECK_EQ(dt_set_u32(&dt, dt_find(&dt, "/chosen"), "boot-verified", 1), 0);
	map = dt_ensure(&dt, "/chosen/memory-map");
	CHECK(map >= 0);
	CHECK_EQ(dt_set_range(&dt, map, "ramdisk", 0x48000000, 0x123456), 0);
	CHECK(reopen(&dt));

	CHECK_STR(dt_read_string(&dt, dt_find(&dt, "/chosen"), "boot-args"), "console=serial quiet");
	CHECK_EQ(dt_set_string(&dt, dt_find(&dt, "/chosen"), "boot-args", "x"), 0);
	CHECK_STR(dt_read_string(&dt, dt_find(&dt, "/chosen"), "boot-args"), "x");
	CHECK_EQ(dt_set_string(&dt, dt_find(&dt, "/chosen"), "boot-args", "a much longer list of arguments"), 0);
	CHECK_STR(dt_read_string(&dt, dt_find(&dt, "/chosen"), "boot-args"), "a much longer list of arguments");

	range = dt_property(&dt, dt_find(&dt, "/chosen/memory-map"), "ramdisk", &size);
	CHECK(range != NULL);
	CHECK_EQ(size, 16);
	CHECK_EQ(get_be64(range), 0x48000000);
	CHECK_EQ(get_be64(range + 8), 0x123456);
	CHECK_EQ(dt_ensure(&dt, "/chosen/memory-map"), dt_find(&dt, "/chosen/memory-map"));
	CHECK_STR(dt_name(&dt, dt_stdout(&dt)), "pl011@9000000");
	CHECK(dt_compatible(&dt, dt_find(&dt, "/psci"), "arm,psci-1.0"));
	CHECK(reopen(&dt));

	free(buffer);
	free(blob);
}

TEST(devicetree, stops_cleanly_when_full)
{
	struct dt source;
	struct dt dt;
	uint8_t *buffer;
	size_t length;
	uint8_t *blob = load_virt(&length);
	unsigned int added = 0;
	int err = 0;

	CHECK(blob != NULL);
	CHECK_EQ(dt_open(&source, blob, length), 0);
	CHECK_EQ(dt_copy(&dt, &source, blob, dt_used_size(&source) - 1), -ENOSPC);
	buffer = malloc(dt_used_size(&source) + 512);
	CHECK(buffer != NULL);
	CHECK_EQ(dt_copy(&dt, &source, buffer, dt_used_size(&source) + 512), 0);

	while (err == 0 && added < 1000) {
		char name[16];

		snprintf(name, sizeof(name), "node%u", added++);
		err = dt_add_node(&dt, dt_root(&dt), name);
		if (err >= 0) {
			err = dt_set_u32(&dt, err, "value", added);
		}
	}

	CHECK_EQ(err, -ENOSPC);
	CHECK(added > 5);
	CHECK(reopen(&dt));
	CHECK_EQ(dt_set_string(&dt, dt_root(&dt), "huge", "this does not fit into the few bytes that are left over"),
		 -ENOSPC);
	CHECK(reopen(&dt));
	CHECK_EQ(dt_add_node(&dt, dt_root(&dt), "bad/name"), -EINVAL);
	CHECK_EQ(dt_ensure(&dt, "relative"), -EINVAL);

	free(buffer);
	free(blob);
}

static void mutate(uint8_t *blob, size_t size)
{
	static const uint32_t interesting[] = {
		0, 1, 2, 3, 4, 9, 40, 64, 0x7fffffff, 0x80000000, 0xfffffffc, 0xffffffff,
	};
	unsigned int count = 1 + test_random(&random_state) % 4;

	while (count-- > 0) {
		uint32_t at = test_random(&random_state) % (uint32_t)size;
		uint32_t word = at & ~3u;

		switch (test_random(&random_state) % 4) {
		case 0:
			blob[at] ^= (uint8_t)(1u << (test_random(&random_state) % 8));
			break;
		case 1:
			blob[at] = (uint8_t)test_random(&random_state);
			break;
		case 2:
			if (word + 4 <= size) {
				put_be32(blob + word, interesting[test_random(&random_state) % ARRAY_SIZE(interesting)]);
			}
			break;
		default:
			if (word + 4 <= size) {
				put_be32(blob + word, test_random(&random_state));
			}
			break;
		}
	}
}

static unsigned int exercise(const struct dt *dt)
{
	unsigned int touched = 0;
	uint64_t base;
	uint64_t size;
	uint32_t value;
	int depth = 0;
	int visited = 0;
	int node;

	for (node = dt_root(dt); node >= 0 && visited < 4096; node = dt_next_node(dt, node, &depth), visited++) {
		const char *name = dt_name(dt, node);
		int property;

		touched += name != NULL ? (unsigned int)strlen(name) : 0;

		for (property = dt_first_property(dt, node); property >= 0; property = dt_next_property(dt, property)) {
			const char *key;
			uint32_t len;
			const uint8_t *data = dt_property_at(dt, property, &key, &len);

			touched += (unsigned int)strlen(key) + (len > 0 ? data[len - 1] : 0);
		}

		touched += (unsigned int)dt_reg(dt, node, 0, &base, &size);
		touched += (unsigned int)dt_parent(dt, node);
		touched += (unsigned int)dt_first_child(dt, node);
		touched += (unsigned int)dt_next_sibling(dt, node);
		touched += dt_enabled(dt, node);
		touched += dt_compatible(dt, node, "arm,pl011");
		touched += (unsigned int)dt_read_u32(dt, node, "phandle", &value);
		touched += dt_read_string(dt, node, "status") != NULL;
	}

	touched += (unsigned int)dt_stdout(dt);
	touched += (unsigned int)dt_find(dt, "/chosen");
	touched += (unsigned int)dt_find(dt, "serial0/x");
	touched += (unsigned int)dt_find_phandle(dt, 1);
	touched += (unsigned int)dt_find_compatible(dt, -1, "arm,armv8-timer");
	touched += (unsigned int)dt_reservation(dt, 0, &base, &size);

	return touched;
}

static bool fuzz(const uint8_t *original, size_t size, unsigned int rounds)
{
	uint8_t *work = test_guarded(size);
	volatile unsigned int sink = 0;
	unsigned int round;

	if (work == NULL) {
		return false;
	}

	for (round = 0; round < rounds; round++) {
		struct dt dt;

		memcpy(work, original, size);
		mutate(work, size);
		dt_open(&dt, work, size);
		sink += exercise(&dt);
	}

	test_release(work, size);

	return true;
}

TEST(devicetree, survives_fuzzed_sample)
{
	struct builder *b = builder_reset();
	uint8_t *blob = sample_tree(b);

	CHECK(fuzz(blob, b->blob_len, FUZZ_ROUNDS));
}

TEST(devicetree, survives_fuzzed_virt)
{
	size_t length;
	uint8_t *blob = load_virt(&length);

	CHECK(blob != NULL);
	CHECK(fuzz(blob, length, FUZZ_ROUNDS));
	free(blob);
}
