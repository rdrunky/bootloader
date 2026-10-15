#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/macros.h>

#include "../../../drivers/qemu/ramfb/ramfb.c"
#include "../../../sys/console.c"
#include "harness.h"

#define POOL_SIZE	(4 * 1024 * 1024)

static uint8_t pool[POOL_SIZE] __attribute__((aligned(MEMMAP_PAGE_SIZE)));
static struct memmap map;
static int dma_error;

int mem_allocate(uint64_t size, uint64_t align, const char *owner, uint64_t *base)
{
	return memmap_alloc(&map, size, align, owner, base);
}

int mem_release(uint64_t base, uint64_t size, const char *owner)
{
	return memmap_release_owned(&map, base, size, owner);
}

int fwcfg_dma_transfer(uint32_t control, uint32_t length, uint64_t address)
{
	if ((control & FWCFG_DMA_CTL_READ) != 0) {
		memset((void *)(uintptr_t)address, 0, length);
	}
	return dma_error;
}

static int setup(void)
{
	memset(&fb, 0, sizeof(fb));
	consoles = NULL;
	history_length = 0;
	fb.item = 1;
	fb.fg = COLOR_FG;
	dma_error = 0;
	memmap_init(&map);
	memmap_add(&map, (uintptr_t)pool, sizeof(pool));
	return ramfb_set_mode(800, 600, 0);
}

TEST(ramfb, allocation_failure_preserves_the_active_buffer)
{
	uint64_t other;
	uint64_t base;
	size_t width;
	size_t height;
	size_t i;

	CHECK_EQ(setup(), 0);
	base = fb.base;
	CHECK_EQ(ramfb_set_mode(4096, 2160, 0), -ENOMEM);
	CHECK_EQ(ramfb_get_mode(&width, &height, NULL), 0);
	CHECK_EQ(width, 800);
	CHECK_EQ(height, 600);
	CHECK_EQ(fb.base, base);
	CHECK_STR(memmap_find(&map, base)->owner, "framebuffer");
	CHECK_EQ(mem_allocate(1024 * 1024, MEMMAP_PAGE_SIZE, "kernel", &other), 0);
	memset((void *)(uintptr_t)other, 0xaa, 1024 * 1024);
	ramfb_console_write(&fb.console, "A", 1);
	for (i = 0; i < 1024 * 1024; i++) {
		CHECK_EQ(((uint8_t *)(uintptr_t)other)[i], 0xaa);
	}
}

TEST(ramfb, dma_failure_preserves_the_mode_and_releases_the_new_buffer)
{
	uint64_t base;
	uint64_t available;

	CHECK_EQ(setup(), 0);
	base = fb.base;
	available = memmap_available(&map);
	dma_error = -EIO;
	CHECK_EQ(ramfb_set_mode(640, 480, 2), -EIO);
	CHECK_EQ(fb.base, base);
	CHECK_EQ(fb.width, 800);
	CHECK_EQ(fb.height, 600);
	CHECK_EQ(fb.scale, 1);
	CHECK_EQ(memmap_available(&map), available);
	CHECK_STR(memmap_find(&map, base)->owner, "framebuffer");
}

TEST(ramfb, successful_changes_release_previous_buffers)
{
	uint64_t previous;
	size_t i;

	CHECK_EQ(setup(), 0);
	for (i = 0; i < 8; i++) {
		previous = fb.base;
		CHECK_EQ(ramfb_set_mode(640, 480, 2), 0);
		CHECK_EQ(fb.width, 640);
		CHECK_EQ(fb.height, 480);
		CHECK_EQ(fb.scale, 2);
		CHECK(memmap_find(&map, previous)->owner == NULL);
		CHECK_STR(memmap_find(&map, fb.base)->owner, "framebuffer");
		CHECK_EQ(memmap_available(&map), POOL_SIZE - ALIGN_UP(640 * 480 * 4, MEMMAP_PAGE_SIZE));
	}
}

TEST(ramfb, invalid_scale_does_not_change_the_mode)
{
	uint64_t available;

	CHECK_EQ(setup(), 0);
	available = memmap_available(&map);
	CHECK_EQ(ramfb_set_mode(800, 600, 9), -EINVAL);
	CHECK_EQ(ramfb_set_mode(800, 600, SIZE_MAX), -EINVAL);
	CHECK_EQ(fb.scale, 1);
	CHECK_EQ(memmap_available(&map), available);
}

TEST(ramfb, resolution_parser_checks_bounds_and_format)
{
	static const struct {
		const char *text;
		size_t width;
		size_t height;
	} cases[] = {
		{ "320x200", 320, 200 },
		{ "4096X2160", 4096, 2160 },
		{ "1024x768", 1024, 768 },
		{ "4294968319x768", 800, 600 },
		{ "1023x4294968064", 800, 600 },
		{ "4294968319x4294968064", 800, 600 },
		{ "4097x600", 800, 600 },
		{ "800x2161", 800, 600 },
		{ "319x600", 800, 600 },
		{ "800x199", 800, 600 },
		{ "1024 768", 800, 600 },
		{ "1024foo768", 800, 600 },
		{ "1024x768junk", 800, 600 },
		{ "1024xx768", 800, 600 },
		{ "1024x", 800, 600 },
		{ "x768", 800, 600 },
	};
	size_t i;

	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		size_t width = 800;
		size_t height = 600;

		parse_resolution(cases[i].text, &width, &height);
		CHECK_EQ(width, cases[i].width);
		CHECK_EQ(height, cases[i].height);
	}
}

TEST(ramfb, resize_preserves_more_than_one_kilobyte_of_visible_history)
{
	char line[40];
	uint64_t previous;
	size_t i;

	CHECK_EQ(setup(), 0);
	fb.console.write = ramfb_console_write;
	consoles = &fb.console;
	for (i = 0; i < 60; i++) {
		memset(line, 'A' + (int)(i % 26), sizeof(line) - 1);
		line[sizeof(line) - 1] = '\n';
		console_write(line, sizeof(line));
	}
	CHECK_EQ(history_length, 2400);
	CHECK_EQ(fb.row, 60);
	previous = fb.base;
	CHECK_EQ(ramfb_set_mode(800, 600, 1), 0);
	CHECK_EQ(fb.row, 60);
	CHECK(memcmp((void *)(uintptr_t)previous, (void *)(uintptr_t)fb.base, fb.height * fb.stride) == 0);
}
