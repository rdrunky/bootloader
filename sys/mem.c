#include <arch.h>
#include <errno.h>
#include <platform.h>
#include <stdio.h>
#include <string.h>
#include <sys/devicetree.h>
#include <sys/format.h>
#include <sys/macros.h>
#include <sys/mem.h>

#define HEAP_SIZE_MIN	(1ULL << 20)
#define HEAP_SIZE_MAX	(64ULL << 20)
#define HEAP_ALIGN	(2ULL << 20)

static const char owner_boot[] = "boot";
static const char owner_devicetree[] = "devicetree";
static const char owner_firmware[] = "firmware";
static const char owner_heap[] = "heap";
static const char owner_reserved[] = "reserved";

static struct memmap map;
static struct heap heap;
static bool heap_ready;

static bool is_memory_node(const struct dt *dt, int node)
{
	const char *type = dt_read_string(dt, node, "device_type");

	return type != NULL && strcmp(type, "memory") == 0 && dt_enabled(dt, node);
}

static void add(uint64_t base, uint64_t size)
{
	int err = memmap_add(&map, base, size);

	if (err != 0) {
		printf("memory %#lx-%#lx %s\n", base, base + size, strerror(-err));
	}
}

static void reserve(uint64_t base, uint64_t size, const char *owner)
{
	int err = memmap_reserve(&map, base, size, owner);

	if (err != 0) {
		printf("memory %#lx-%#lx %s %s\n", base, base + size, owner, strerror(-err));
	}
}

static void add_memory(const struct dt *dt)
{
	uint64_t base;
	uint64_t size;
	unsigned int i;
	int node;

	for (node = dt_root(dt); node >= 0; node = dt_next_node(dt, node, NULL)) {
		if (!is_memory_node(dt, node)) {
			continue;
		}

		for (i = 0; dt_reg(dt, node, i, &base, &size) == 0; i++) {
			add(base, size);
		}
	}
}

static void reserve_firmware(const struct dt *dt)
{
	uint64_t base;
	uint64_t size;
	unsigned int i;
	int child;

	for (i = 0; dt_reservation(dt, i, &base, &size) == 0; i++) {
		reserve(base, size, owner_reserved);
	}

	for (child = dt_first_child(dt, dt_find(dt, "/reserved-memory")); child >= 0;
	     child = dt_next_sibling(dt, child)) {
		const char *owner = dt_property(dt, child, "no-map", NULL) != NULL ? owner_firmware : owner_reserved;

		for (i = 0; dt_reg(dt, child, i, &base, &size) == 0; i++) {
			reserve(base, size, owner);
		}
	}

	reserve((uintptr_t)dt->blob, dt->size, owner_devicetree);
}

static void build_map(const struct dt *dt)
{
	uint64_t base;
	uint64_t size;

	memmap_init(&map);

	if (dt != NULL) {
		add_memory(dt);
	}

	if (memmap_total(&map) == 0) {
		platform_memory(&base, &size);
		printf("memory undescribed, assuming %#lx-%#lx\n", base, base + size);
		add(base, size);
	}

	if (!memmap_contains(&map, arch_image_start(), arch_image_size())) {
		printf("image outside the known memory\n");
	}

	reserve(arch_image_start(), arch_image_size(), owner_boot);

	if (dt != NULL) {
		reserve_firmware(dt);
	}
}

static int map_outside_memory(uint64_t base, uint64_t size, unsigned int flags)
{
	uint64_t start = ALIGN_DOWN(base, MEMMAP_PAGE_SIZE);
	uint64_t end = ALIGN_UP(base + size, MEMMAP_PAGE_SIZE);

	if (memmap_contains(&map, start, end - start)) {
		return 0;
	}

	return arch_mmu_map(start, end - start, flags);
}

static void enable_mmu(const struct dt *dt)
{
	size_t i;
	int err = arch_mmu_map_image();

	for (i = 0; i < map.count && err == 0; i++) {
		const struct memmap_segment *segment = &map.segments[i];

		if (segment->owner != owner_boot && segment->owner != owner_firmware) {
			err = arch_mmu_map(segment->base, segment->end - segment->base, 0);
		}
	}

	if (err == 0 && dt != NULL) {
		err = map_outside_memory((uintptr_t)dt->blob, dt->size, ARCH_MAP_READONLY);
	}

	if (err == 0) {
		err = arch_mmu_enable();
	}

	if (err != 0) {
		printf("mmu %s\n", strerror(-err));
		return;
	}

	printf("mmu ok\n");
}

static void setup_heap(void)
{
	uint64_t size = memmap_available(&map) / 8;
	uint64_t base;
	int err;

	if (size > HEAP_SIZE_MAX) {
		size = HEAP_SIZE_MAX;
	}

	size = ALIGN_DOWN(size, HEAP_ALIGN);
	if (size < HEAP_SIZE_MIN) {
		size = HEAP_SIZE_MIN;
	}

	err = memmap_alloc_top(&map, size, HEAP_ALIGN, owner_heap, &base);
	if (err == 0) {
		err = heap_init(&heap, (void *)(uintptr_t)base, (size_t)size);
	}

	if (err != 0) {
		printf("heap %s\n", strerror(-err));
		return;
	}

	heap_ready = true;
}

void mem_init(void)
{
	const struct dt *dt = devicetree();
	char total[SIZE_TEXT];

	build_map(dt);
	format_size(total, sizeof(total), memmap_total(&map));
	printf("memory %s ok\n", total);

	enable_mmu(dt);
	setup_heap();
}

const struct memmap *mem_map(void)
{
	return &map;
}

struct heap *mem_heap(void)
{
	return heap_ready ? &heap : NULL;
}

int mem_claim(uint64_t base, uint64_t size, const char *owner)
{
	int err = memmap_release_owned(&map, base, size, owner);

	return err != 0 ? err : memmap_claim(&map, base, size, owner);
}

int mem_allocate(uint64_t size, uint64_t align, const char *owner, uint64_t *base)
{
	return memmap_alloc(&map, size, align, owner, base);
}

int mem_allocate_random(uint64_t size, uint64_t align, const char *owner, uint64_t random, uint64_t *base)
{
	return memmap_alloc_random(&map, size, align, owner, random, base);
}

int mem_release(uint64_t base, uint64_t size, const char *owner)
{
	return memmap_release_owned(&map, base, size, owner);
}

void mem_release_owner(const char *owner)
{
	if (map.count > 0) {
		memmap_release_owned(&map, map.segments[0].base, map.segments[map.count - 1].end - map.segments[0].base,
				     owner);
	}
}

int mem_map_device(uint64_t base, uint64_t size)
{
	uint64_t start = ALIGN_DOWN(base, MEMMAP_PAGE_SIZE);

	if (size == 0 || base + size < base) {
		return -EINVAL;
	}

	return arch_mmu_map(start, ALIGN_UP(base + size, MEMMAP_PAGE_SIZE) - start, ARCH_MAP_DEVICE);
}
