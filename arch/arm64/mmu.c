#include <arch.h>
#include <arch/arm64/mmu.h>
#include <arch/arm64/sysreg.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>

#define VA_BITS			48
#define PAGE_SHIFT		12
#define PAGE_SIZE		(UL(1) << PAGE_SHIFT)
#define TABLE_ENTRIES		512
#define TABLE_POOL		64
#define LAST_LEVEL		3

extern char __image_start[];
extern char __text_end[];
extern char __rodata_end[];
extern char __stack_guard[];
extern char __stack_bottom[];
extern char __exception_stack_guard[];
extern char __exception_stack_bottom[];
extern char __image_end[];

static uint64_t tables[TABLE_POOL][TABLE_ENTRIES] __aligned(PAGE_SIZE);
static unsigned int tables_used;
static uint64_t *root;
static bool enabled;

static unsigned int level_shift(unsigned int level)
{
	return PAGE_SHIFT + 9 * (LAST_LEVEL - level);
}

static uint64_t *table_alloc(void)
{
	uint64_t *table;

	if (tables_used == TABLE_POOL) {
		return NULL;
	}

	table = tables[tables_used++];
	memset(table, 0, sizeof(tables[0]));

	return table;
}

static bool is_table(uint64_t entry, unsigned int level)
{
	return level < LAST_LEVEL && (entry & (PTE_VALID | PTE_TABLE)) == (PTE_VALID | PTE_TABLE);
}

static uint64_t *table_of(uint64_t entry)
{
	return (uint64_t *)(uintptr_t)(entry & PTE_ADDRESS);
}

static uint64_t attributes(unsigned int flags)
{
	uint64_t entry = PTE_ACCESSED | PTE_UXN;

	if ((flags & ARCH_MAP_DEVICE) != 0) {
		entry |= PTE_ATTR_INDEX(ATTR_DEVICE) | PTE_PXN;
	} else {
		entry |= PTE_ATTR_INDEX(ATTR_NORMAL) | PTE_INNER_SHAREABLE;
	}

	if ((flags & ARCH_MAP_READONLY) != 0) {
		entry |= PTE_READONLY;
	}

	if ((flags & ARCH_MAP_EXECUTE) == 0) {
		entry |= PTE_PXN;
	}

	return entry;
}

static void flush_tlb(void)
{
	__asm__ volatile("dsb ishst\n\ttlbi vmalle1\n\tdsb nsh\n\tisb" ::: "memory");
}

static void store(uint64_t *entry, uint64_t value)
{
	if (enabled && (*entry & PTE_VALID) != 0 && *entry != value) {
		WRITE_ONCE(*entry, 0);
		flush_tlb();
	}

	WRITE_ONCE(*entry, value);
}

static uint64_t *split(uint64_t *entry, unsigned int level)
{
	uint64_t block = *entry;
	uint64_t step = UL(1) << level_shift(level + 1);
	uint64_t *table = table_alloc();
	unsigned int i;

	if (table == NULL) {
		return NULL;
	}

	if ((block & PTE_VALID) != 0) {
		uint64_t base = block & PTE_ADDRESS;
		uint64_t kept = block & ~(PTE_ADDRESS | PTE_VALID | PTE_TABLE);
		uint64_t kind = level + 1 == LAST_LEVEL ? PTE_PAGE : 0;

		for (i = 0; i < TABLE_ENTRIES; i++) {
			table[i] = (base + i * step) | kept | kind | PTE_VALID;
		}
	}

	store(entry, (uint64_t)(uintptr_t)table | PTE_TABLE | PTE_VALID);

	return table;
}

static int map_level(uint64_t *table, unsigned int level, uint64_t address, uint64_t end, uint64_t attrs)
{
	uint64_t span = UL(1) << level_shift(level);

	while (address < end) {
		uint64_t *entry = &table[(address >> level_shift(level)) & (TABLE_ENTRIES - 1)];
		uint64_t boundary = (address | (span - 1)) + 1;
		uint64_t stop = boundary < end ? boundary : end;
		bool whole = (address & (span - 1)) == 0 && stop == boundary;

		if (level == LAST_LEVEL) {
			store(entry, address | attrs | PTE_PAGE | PTE_VALID);
		} else if (level > 0 && whole && !is_table(*entry, level)) {
			store(entry, address | attrs | PTE_VALID);
		} else {
			uint64_t *next = is_table(*entry, level) ? table_of(*entry) : split(entry, level);
			int err;

			if (next == NULL) {
				return -ENOMEM;
			}

			err = map_level(next, level + 1, address, stop, attrs);
			if (err != 0) {
				return err;
			}
		}

		address = stop;
	}

	return 0;
}

int arch_mmu_map(uint64_t base, uint64_t size, unsigned int flags)
{
	int err;

	if (size == 0 || ((base | size) & (PAGE_SIZE - 1)) != 0 || base + size < base ||
	    base + size > (UL(1) << VA_BITS)) {
		return -EINVAL;
	}

	if (root == NULL) {
		root = table_alloc();
		if (root == NULL) {
			return -ENOMEM;
		}
	}

	err = map_level(root, 0, base, base + size, attributes(flags));
	__asm__ volatile("dsb ishst\n\tisb" ::: "memory");

	return err;
}

static int map_section(const char *start, const char *end, unsigned int flags)
{
	if (end <= start) {
		return 0;
	}

	return arch_mmu_map((uintptr_t)start, (uintptr_t)(end - start), flags);
}

int arch_mmu_map_image(void)
{
	int err;

	if ((uintptr_t)__image_start % PAGE_SIZE != 0) {
		return -EINVAL;
	}

	err = map_section(__image_start, __text_end, ARCH_MAP_READONLY | ARCH_MAP_EXECUTE);
	if (err == 0) {
		err = map_section(__text_end, __rodata_end, ARCH_MAP_READONLY);
	}

	if (err == 0) {
		err = map_section(__rodata_end, __stack_guard, 0);
	}

	if (err == 0) {
		err = map_section(__stack_bottom, __exception_stack_guard, 0);
	}

	if (err == 0) {
		err = map_section(__exception_stack_bottom, __image_end, 0);
	}

	return err;
}

int arch_mmu_enable(void)
{
	uint64_t parange = read_sysreg(id_aa64mmfr0_el1) & MMFR0_PARANGE_MASK;
	uint64_t tcr;

	if (root == NULL || enabled) {
		return -EINVAL;
	}

	if (parange > PARANGE_48_BITS) {
		parange = PARANGE_48_BITS;
	}

	tcr = TCR_T0SZ(VA_BITS) | TCR_IRGN0_WBWA | TCR_ORGN0_WBWA | TCR_SH0_INNER | TCR_TG0_4K | TCR_EPD1 |
	      parange << TCR_IPS_SHIFT;

	arch_dcache_invalidate_range(arch_image_start(), arch_image_size());

	write_sysreg(mair_el1, MAIR_VALUE);
	write_sysreg(tcr_el1, tcr);
	write_sysreg(ttbr0_el1, (uint64_t)(uintptr_t)root);
	isb();
	__asm__ volatile("tlbi vmalle1\n\tdsb nsh\n\tic iallu\n\tdsb nsh\n\tisb" ::: "memory");

	write_sysreg(sctlr_el1, read_sysreg(sctlr_el1) | SCTLR_M | SCTLR_C | SCTLR_I | SCTLR_SA | SCTLR_WXN);
	isb();
	enabled = true;

	return 0;
}
