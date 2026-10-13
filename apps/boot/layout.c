#include <lib/partition.h>
#include <stdio.h>
#include <string.h>

#include "private.h"

#define SLOT_SEPARATOR	'-'

struct layout {
	struct blockdev	*partitions[ROLE_COUNT][SLOT_COUNT];
	unsigned int	slot;
	bool		present;
};

static const char *const role_types[ROLE_COUNT] = {
	[ROLE_SYSTEM]	= "b0070001-2a2a-4c4f-9a3e-0123456789ab",
	[ROLE_RECOVERY]	= "b0070002-2a2a-4c4f-9a3e-0123456789ab",
	[ROLE_DATA]	= "b0070003-2a2a-4c4f-9a3e-0123456789ab",
	[ROLE_STATE]	= "b0070004-2a2a-4c4f-9a3e-0123456789ab",
};

static const char *const role_names[ROLE_COUNT] = {
	[ROLE_SYSTEM]	= "system",
	[ROLE_RECOVERY]	= "recovery",
	[ROLE_DATA]	= "data",
	[ROLE_STATE]	= "state",
};

static const char slot_names[SLOT_COUNT] = { 'a', 'b' };

static struct layout layout;

static int role_of(const struct blockdev *partition)
{
	char type[PARTITION_GUID_TEXT];
	int role;

	if (partition->parent == NULL || partition->mbr_type != 0) {
		return -1;
	}

	partition_format_guid(partition->type, type);
	for (role = 0; role < ROLE_COUNT; role++) {
		if (strcmp(type, role_types[role]) == 0) {
			return role;
		}
	}

	return -1;
}

static unsigned int slot_of(const struct blockdev *partition)
{
	size_t length = strnlen(partition->label, sizeof(partition->label));
	unsigned int slot;

	for (slot = 0; slot < SLOT_COUNT && length >= 2; slot++) {
		if (partition->label[length - 2] == SLOT_SEPARATOR && partition->label[length - 1] == slot_names[slot]) {
			return slot;
		}
	}

	return 0;
}

void layout_scan(void)
{
	struct blockdev *partition;

	memset(&layout, 0, sizeof(layout));

	for (partition = blockdev_list(); partition != NULL; partition = partition->next) {
		int role = role_of(partition);
		unsigned int slot;

		if (role < 0) {
			continue;
		}

		slot = slot_of(partition);
		if (layout.partitions[role][slot] == NULL) {
			layout.partitions[role][slot] = partition;
			layout.present = true;
		}
	}
}

bool layout_present(void)
{
	return layout.present;
}

bool layout_member(const struct blockdev *device)
{
	unsigned int role;
	unsigned int slot;

	for (role = 0; role < ROLE_COUNT; role++) {
		for (slot = 0; slot < SLOT_COUNT; slot++) {
			if (layout.partitions[role][slot] == device) {
				return true;
			}
		}
	}

	return false;
}

struct blockdev *layout_partition(enum role role, unsigned int slot)
{
	return slot < SLOT_COUNT ? layout.partitions[role][slot] : NULL;
}

unsigned int layout_slot(void)
{
	return layout.slot;
}

void layout_select(unsigned int slot)
{
	if (slot < SLOT_COUNT) {
		layout.slot = slot;
	}
}

bool layout_slotted(enum role role)
{
	return layout.partitions[role][SLOT_COUNT - 1] != NULL;
}

unsigned int layout_slot_of(const struct blockdev *partition)
{
	unsigned int slot;

	for (slot = 0; slot < SLOT_COUNT; slot++) {
		if (partition != NULL && layout.partitions[ROLE_SYSTEM][slot] == partition) {
			return slot;
		}
	}

	return layout.slot;
}

const char *layout_role_name(enum role role)
{
	return role_names[role];
}

char layout_slot_name(unsigned int slot)
{
	return slot_names[slot % SLOT_COUNT];
}

void layout_name(enum role role, unsigned int slot, char *name, size_t size)
{
	if (layout_slotted(role)) {
		snprintf(name, size, "%s%c%c", role_names[role], SLOT_SEPARATOR, slot_names[slot]);
	} else {
		snprintf(name, size, "%s", role_names[role]);
	}
}

struct blockdev *layout_resolve(const char *name)
{
	size_t length = strlen(name);
	unsigned int role;
	unsigned int slot;

	for (role = 0; role < ROLE_COUNT; role++) {
		size_t base = strlen(role_names[role]);

		if (strncmp(name, role_names[role], base) != 0) {
			continue;
		}

		if (length == base) {
			return layout.partitions[role][layout_slotted(role) ? layout.slot : 0];
		}

		for (slot = 0; slot < SLOT_COUNT; slot++) {
			if (length == base + 2 && name[base] == SLOT_SEPARATOR && name[base + 1] == slot_names[slot]) {
				return layout.partitions[role][slot];
			}
		}
	}

	return NULL;
}
