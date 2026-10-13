#include <arch.h>
#include <errno.h>
#include <lib/devicetree.h>
#include <lib/image.h>
#include <lib/partition.h>
#include <stdio.h>
#include <string.h>
#include <sys/console.h>
#include <sys/devicetree.h>
#include <sys/driver.h>
#include <sys/format.h>
#include <sys/macros.h>
#include <sys/mem.h>
#include <sys/random.h>

#include "private.h"

#define KERNEL_ALIGN		(2ULL << 20)
#define DEVICETREE_SPARE	(64 * 1024)
#define ERROR_MAX		160
#define SEED_SIZE		RANDOM_READ_MAX

struct region {
	uint64_t	base;
	uint64_t	size;
	uint64_t	data;
	uint64_t	length;
};

struct boot_state {
	struct region		kernel;
	struct region		ramdisk;
	struct region		devicetree;
	struct region		log;
	struct dt		handoff;
	struct blockdev		*source;
	bool			verified;
};

static const char owner_kernel[] = "kernel";
static const char owner_ramdisk[] = "ramdisk";
static const char owner_devicetree[] = "handoff devicetree";
static const char owner_log[] = "boot log";

static char last_error[ERROR_MAX];

static int abandon(void)
{
	mem_release_owner(owner_kernel);
	mem_release_owner(owner_ramdisk);
	mem_release_owner(owner_devicetree);

	return -ECANCELED;
}

static void report(const char *what, const char *spec, const char *problem)
{
	if (spec != NULL) {
		snprintf(last_error, sizeof(last_error), "%s %s %s", what, spec, problem);
	} else {
		snprintf(last_error, sizeof(last_error), "%s %s", what, problem);
	}

	printf("%s\n", last_error);
}

static int fail(const char *what, const char *spec, int err)
{
	report(what, spec, strerror(-err));

	return abandon();
}

static int verify(const char *what, const char *spec, const struct region *region, const struct digest *digest,
		  bool required, bool *verified)
{
	char expected[SHA256_SIZE * 2 + 1];
	char found[SHA256_SIZE * 2 + 1];
	uint8_t actual[SHA256_SIZE];

	*verified = false;

	if (digest == NULL || !digest->present) {
		if (required) {
			report(what, spec, "sha256 required");
			return -EACCES;
		}
		return 0;
	}

	sha256((const void *)(uintptr_t)region->data, (size_t)region->length, actual);
	if (memcmp(actual, digest->value, SHA256_SIZE) != 0) {
		format_digest(digest->value, expected);
		format_digest(actual, found);
		report(what, spec, "sha256 mismatch");
		printf("sha256 %s expected\nsha256 %s found\n", expected, found);
		return -EACCES;
	}

	*verified = true;

	return 0;
}

static const char *load_status(bool digest_matched, bool trusted)
{
	if (!digest_matched) {
		return "loaded";
	}

	return trusted ? "verified" : "sha256 ok";
}

static void describe(const char *what, const struct region *region, bool digest_matched, bool trusted)
{
	char size[SIZE_TEXT];

	format_size(size, sizeof(size), region->length);
	printf("%s %s at %#lx %s\n", what, size, region->data, load_status(digest_matched, trusted));
}

static int open_file(const char *spec, struct location *location, uint64_t *size)
{
	struct fs_stat stat;
	int err = location_open(spec, location);

	if (err != 0) {
		return err;
	}

	err = fs_stat(location->fs, location->path, &stat);
	if (err == 0 && stat.kind != FS_KIND_FILE) {
		err = -EISDIR;
	}

	if (err != 0) {
		location_close(location);
		return err;
	}

	*size = stat.size;

	return 0;
}

static int read_into(struct location *location, const struct region *region)
{
	size_t done;
	int err = fs_read(location->fs, location->path, 0, (void *)(uintptr_t)region->data, (size_t)region->length,
			  &done);

	if (err != 0) {
		return err;
	}

	return done == region->length ? 0 : -EIO;
}

static int place_kernel(struct region *kernel, uint64_t flags)
{
	uint64_t random;

	if ((flags & IMAGE_FLAG_ANYWHERE) != 0 && random_read(&random, sizeof(random)) == 0) {
		return mem_allocate_random(kernel->size, KERNEL_ALIGN, owner_kernel, random, &kernel->base);
	}

	return mem_allocate(kernel->size, KERNEL_ALIGN, owner_kernel, &kernel->base);
}

static int load_kernel(const char *spec, struct boot_state *state)
{
	struct region *kernel = &state->kernel;
	uint8_t header[IMAGE_HEADER_SIZE];
	struct location location;
	struct image_info info;
	size_t done;
	int err = open_file(spec, &location, &kernel->length);

	if (err != 0) {
		return err;
	}

	state->source = location.device;
	err = fs_read(location.fs, location.path, 0, header, sizeof(header), &done);
	if (err == 0) {
		err = image_parse(header, done, &info);
	}

	if (err == 0) {
		kernel->size = info.text_offset + (info.image_size > kernel->length ? info.image_size : kernel->length);
		err = place_kernel(kernel, info.flags);
	}

	if (err == 0) {
		kernel->data = kernel->base + info.text_offset;
		err = read_into(&location, kernel);
	}

	location_close(&location);

	return err;
}

static int load_ramdisk(const char *spec, struct region *ramdisk)
{
	struct location location;
	int err = open_file(spec, &location, &ramdisk->length);

	if (err != 0) {
		return err;
	}

	ramdisk->size = ALIGN_UP(ramdisk->length == 0 ? 1 : ramdisk->length, MEMMAP_PAGE_SIZE);
	err = mem_allocate(ramdisk->size, MEMMAP_PAGE_SIZE, owner_ramdisk, &ramdisk->base);
	if (err == 0) {
		ramdisk->data = ramdisk->base;
		err = read_into(&location, ramdisk);
	}

	location_close(&location);

	return err;
}

static int describe_partitions(struct dt *dt, int chosen, const struct boot_state *state)
{
	unsigned int slot = layout_slot_of(state->source);
	char slot_name[] = { layout_slot_name(slot), '\0' };
	char uuid[PARTITION_GUID_TEXT];
	unsigned int role;
	int node;
	int err = 0;

	if (!layout_present()) {
		return 0;
	}

	if (layout_slotted(ROLE_SYSTEM)) {
		err = dt_set_string(dt, chosen, "boot-slot", slot_name);
	}

	node = err == 0 ? dt_ensure(dt, "/chosen/partitions") : err;
	for (role = 0; node >= 0 && err == 0 && role < ROLE_COUNT; role++) {
		const struct blockdev *partition = layout_partition(role, layout_slotted(role) ? slot : 0);

		if (partition != NULL) {
			partition_format_guid(partition->uuid, uuid);
			err = dt_set_string(dt, node, layout_role_name(role), uuid);
		}
	}

	return node < 0 ? node : err;
}

static int describe_seed(struct dt *dt, int chosen)
{
	uint8_t seed[SEED_SIZE];
	int err;

	if (random_read(seed, sizeof(seed)) != 0) {
		return 0;
	}

	err = dt_set_property(dt, chosen, "boot-seed", seed, sizeof(seed));
	explicit_bzero(seed, sizeof(seed));

	return err;
}

static int describe_boot(struct dt *dt, const struct boot_request *request, const struct boot_state *state)
{
	int chosen = dt_ensure(dt, "/chosen");
	int err = chosen < 0 ? chosen : 0;

	if (err == 0) {
		err = dt_set_string(dt, chosen, "boot-mode", request->mode);
	}

	if (err == 0) {
		err = dt_set_u32(dt, chosen, "boot-verified", state->verified ? 1 : 0);
	}

	if (err == 0) {
		err = dt_set_string(dt, chosen, "boot-policy", security_policy());
	}

	if (err == 0) {
		err = describe_seed(dt, chosen);
	}

	if (err == 0 && request->arguments != NULL) {
		err = dt_set_string(dt, chosen, "boot-args", request->arguments);
	}

	if (err == 0 && request->ramdisk != NULL) {
		int map = dt_ensure(dt, "/chosen/memory-map");

		err = map < 0 ? map : dt_set_range(dt, map, "ramdisk", state->ramdisk.data, state->ramdisk.length);
	}

	if (err == 0) {
		err = describe_partitions(dt, chosen, state);
	}

	return err;
}

static int build_devicetree(const struct boot_request *request, struct boot_state *state)
{
	const struct dt *source = devicetree();
	struct region *region = &state->devicetree;
	int err;

	if (source == NULL) {
		return -ENOENT;
	}

	region->size = ALIGN_UP(dt_used_size(source) + DEVICETREE_SPARE, MEMMAP_PAGE_SIZE);
	err = mem_allocate(region->size, MEMMAP_PAGE_SIZE, owner_devicetree, &region->base);
	if (err == 0) {
		region->data = region->base;
		region->length = region->size;
		err = dt_copy(&state->handoff, source, (void *)(uintptr_t)region->base, (size_t)region->size);
	}

	if (err == 0) {
		err = describe_boot(&state->handoff, request, state);
	}

	return err;
}

static void attach_log(struct boot_state *state)
{
	struct region *log = &state->log;
	int map;

	log->size = CONSOLE_HISTORY_SIZE;
	if (mem_allocate(log->size, MEMMAP_PAGE_SIZE, owner_log, &log->base) != 0) {
		log->size = 0;
		return;
	}

	log->data = log->base;
	log->length = console_history((char *)(uintptr_t)log->base, (size_t)log->size);

	map = dt_ensure(&state->handoff, "/chosen/memory-map");
	if (map < 0 || dt_set_range(&state->handoff, map, "boot-log", log->data, log->length) != 0) {
		mem_release_owner(owner_log);
		log->size = 0;
	}
}

const char *boot_error(void)
{
	return last_error;
}

static void clean(const struct region *region)
{
	if (region->size != 0) {
		arch_dcache_clean_range((uintptr_t)region->base, (size_t)region->size);
	}
}

int boot_start(const struct boot_request *request)
{
	struct boot_state state;
	bool kernel_verified;
	bool ramdisk_verified = true;
	int err;

	memset(&state, 0, sizeof(state));
	last_error[0] = '\0';

	err = load_kernel(request->kernel, &state);
	if (err != 0) {
		return fail("kernel", request->kernel, err);
	}

	if (verify("kernel", request->kernel, &state.kernel, request->kernel_digest, request->verify_required,
		   &kernel_verified) != 0) {
		return abandon();
	}

	describe("kernel", &state.kernel, kernel_verified, request->trusted);

	if (request->ramdisk != NULL) {
		err = load_ramdisk(request->ramdisk, &state.ramdisk);
		if (err != 0) {
			return fail("ramdisk", request->ramdisk, err);
		}

		if (verify("ramdisk", request->ramdisk, &state.ramdisk, request->ramdisk_digest, request->verify_required,
			   &ramdisk_verified) != 0) {
			return abandon();
		}

		describe("ramdisk", &state.ramdisk, ramdisk_verified, request->trusted);
	}

	state.verified = request->trusted && kernel_verified && ramdisk_verified;

	err = build_devicetree(request, &state);
	if (err != 0) {
		return fail("dt", NULL, err);
	}

	printf("jump %#lx el%lu\n", state.kernel.data, handoff_el);
	attach_log(&state);
	console_flush();

	driver_quiesce();

	clean(&state.kernel);
	clean(&state.ramdisk);
	clean(&state.devicetree);
	clean(&state.log);

	arch_handoff((uintptr_t)state.kernel.data, (uintptr_t)state.devicetree.base);
}
