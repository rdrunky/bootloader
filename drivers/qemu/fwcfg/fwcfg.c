#include <drivers/fwcfg.h>
#include <drivers/ramfb.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/driver.h>
#include <sys/endian.h>
#include <sys/fuse.h>
#include <sys/mem.h>

#define REGISTERS_SIZE		0x18
#define REGISTER_DATA		0x00
#define REGISTER_SELECTOR	0x08
#define REGISTER_DMA		0x10
#define SELECT_SIGNATURE	0x0000
#define SELECT_FILE_DIRECTORY	0x0019
#define SIGNATURE		"QEMU"
#define SIGNATURE_SIZE		4
#define FILE_NAME_SIZE		56
#define FILES_MAX		1024
#define FUSE_DIRECTORY		"opt/boot/"

struct file_entry {
	uint8_t		size[4];
	uint8_t		select[2];
	uint8_t		reserved[2];
	char		name[FILE_NAME_SIZE];
};

static uintptr_t registers;

static void fwcfg_select(uint16_t item)
{
	*(volatile uint16_t *)(registers + REGISTER_SELECTOR) = __builtin_bswap16(item);
}

static void fwcfg_read(void *buffer, size_t size)
{
	uint8_t *bytes = buffer;
	size_t i;

	for (i = 0; i < size; i++) {
		bytes[i] = *(volatile uint8_t *)(registers + REGISTER_DATA);
	}
}

int fwcfg_lookup(const char *name, uint16_t *item, uint32_t *size)
{
	uint8_t count_bytes[4];
	uint32_t count;
	uint32_t i;

	if (registers == 0) {
		return -ENODEV;
	}

	fwcfg_select(SELECT_FILE_DIRECTORY);
	fwcfg_read(count_bytes, sizeof(count_bytes));
	count = get_be32(count_bytes);
	if (count > FILES_MAX) {
		return -EIO;
	}

	for (i = 0; i < count; i++) {
		struct file_entry entry;

		fwcfg_read(&entry, sizeof(entry));
		if (strncmp(entry.name, name, sizeof(entry.name)) == 0) {
			*item = get_be16(entry.select);
			*size = get_be32(entry.size);
			return 0;
		}
	}

	return -ENOENT;
}

int fwcfg_dma_transfer(uint32_t control, uint32_t length, uint64_t address)
{
	static struct {
		volatile uint32_t	control;
		uint32_t		length;
		uint64_t		address;
	} __attribute__((packed, aligned(8))) dma;

	if (registers == 0) {
		return -ENODEV;
	}

	dma.control = __builtin_bswap32(control);
	dma.length = __builtin_bswap32(length);
	dma.address = __builtin_bswap64(address);

	__asm__ volatile("dmb sy" ::: "memory");
	*(volatile uint64_t *)(registers + REGISTER_DMA) = __builtin_bswap64((uint64_t)&dma);
	__asm__ volatile("dmb sy" ::: "memory");

	while ((__builtin_bswap32(dma.control) & ~FWCFG_DMA_CTL_ERROR) != 0) {
	}

	return (__builtin_bswap32(dma.control) & FWCFG_DMA_CTL_ERROR) != 0 ? -EIO : 0;
}

static int fwcfg_fuse_read(const char *name, void *buffer, size_t size, size_t *length)
{
	char path[FILE_NAME_SIZE];
	uint16_t item;
	uint32_t file_size;
	int err;

	if (snprintf(path, sizeof(path), "%s%s", FUSE_DIRECTORY, name) >= (int)sizeof(path)) {
		return -ENAMETOOLONG;
	}

	err = fwcfg_lookup(path, &item, &file_size);
	if (err != 0) {
		return err;
	}

	if (file_size > size) {
		return -EFBIG;
	}

	fwcfg_select(item);
	fwcfg_read(buffer, file_size);
	*length = file_size;

	return 0;
}

static const struct fuse_ops fwcfg_fuses = {
	.read	= fwcfg_fuse_read,
};

static int fwcfg_probe(const struct dt *dt, int node)
{
	uint8_t signature[SIGNATURE_SIZE];
	uint64_t base;
	uint64_t size;
	int err = dt_reg(dt, node, 0, &base, &size);

	if (err == 0 && size < REGISTERS_SIZE) {
		err = -EINVAL;
	}

	if (err == 0) {
		err = mem_map_device(base, REGISTERS_SIZE);
	}

	if (err != 0) {
		return err;
	}

	registers = (uintptr_t)base;
	fwcfg_select(SELECT_SIGNATURE);
	fwcfg_read(signature, sizeof(signature));
	if (memcmp(signature, SIGNATURE, sizeof(signature)) != 0) {
		return -ENODEV;
	}

	fuse_register(&fwcfg_fuses);
	ramfb_init();

	return 0;
}

static const char *const fwcfg_compatible[] = { "qemu,fw-cfg-mmio", NULL };

static const struct driver fwcfg_driver = {
	.name		= "fw-cfg",
	.compatible	= fwcfg_compatible,
	.probe		= fwcfg_probe,
};

DRIVER(fwcfg_driver);
