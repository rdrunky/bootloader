#include <errno.h>
#include <lib/cksum.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/endian.h>

#include "private.h"

#define RECORD_MAGIC		"b007slot"
#define RECORD_MAGIC_SIZE	8
#define RECORD_FORMAT		1
#define RECORD_COPIES		2
#define RECORD_FORMAT_OFFSET	8
#define RECORD_SEQUENCE_OFFSET	12
#define RECORD_SLOTS_OFFSET	16
#define RECORD_SLOT_SIZE	4
#define RECORD_CRC_OFFSET	60
#define SLOT_PRIORITY		0
#define SLOT_TRIES		1
#define SLOT_CONFIRMED		2
#define PRIORITY_MAX		15
#define TRIES_DEFAULT		3
#define TRIES_MAX		7

struct slot_state {
	uint8_t		priority;
	uint8_t		tries;
	bool		confirmed;
};

struct slot_table {
	struct slot_state	slot[SLOT_COUNT];
	uint32_t		sequence;
};

static struct slot_table slots;

static void set_defaults(struct slot_table *table)
{
	unsigned int slot;

	memset(table, 0, sizeof(*table));
	for (slot = 0; slot < SLOT_COUNT; slot++) {
		table->slot[slot].priority = (uint8_t)(PRIORITY_MAX - slot);
		table->slot[slot].tries = TRIES_DEFAULT;
	}
}

static bool decode(const uint8_t *record, struct slot_table *table)
{
	unsigned int slot;

	if (memcmp(record, RECORD_MAGIC, RECORD_MAGIC_SIZE) != 0 ||
	    get_le32(record + RECORD_FORMAT_OFFSET) != RECORD_FORMAT ||
	    get_le32(record + RECORD_CRC_OFFSET) != crc32(record, RECORD_CRC_OFFSET)) {
		return false;
	}

	table->sequence = get_le32(record + RECORD_SEQUENCE_OFFSET);
	for (slot = 0; slot < SLOT_COUNT; slot++) {
		const uint8_t *entry = record + RECORD_SLOTS_OFFSET + slot * RECORD_SLOT_SIZE;

		if (entry[SLOT_PRIORITY] > PRIORITY_MAX || entry[SLOT_TRIES] > TRIES_MAX || entry[SLOT_CONFIRMED] > 1) {
			return false;
		}

		table->slot[slot].priority = entry[SLOT_PRIORITY];
		table->slot[slot].tries = entry[SLOT_TRIES];
		table->slot[slot].confirmed = entry[SLOT_CONFIRMED] != 0;
	}

	return true;
}

static void encode(const struct slot_table *table, uint8_t *record)
{
	unsigned int slot;

	memcpy(record, RECORD_MAGIC, RECORD_MAGIC_SIZE);
	put_le32(record + RECORD_FORMAT_OFFSET, RECORD_FORMAT);
	put_le32(record + RECORD_SEQUENCE_OFFSET, table->sequence);
	for (slot = 0; slot < SLOT_COUNT; slot++) {
		uint8_t *entry = record + RECORD_SLOTS_OFFSET + slot * RECORD_SLOT_SIZE;

		entry[SLOT_PRIORITY] = table->slot[slot].priority;
		entry[SLOT_TRIES] = table->slot[slot].tries;
		entry[SLOT_CONFIRMED] = table->slot[slot].confirmed ? 1 : 0;
	}

	put_le32(record + RECORD_CRC_OFFSET, crc32(record, RECORD_CRC_OFFSET));
}

static bool newer(uint32_t sequence, uint32_t than)
{
	return (int32_t)(sequence - than) > 0;
}

static int save(void)
{
	struct blockdev *partition = layout_partition(ROLE_STATE, 0);
	uint8_t *block;
	int err;

	if (partition == NULL) {
		return -ENODEV;
	}

	block = calloc(1, partition->block_size);
	if (block == NULL) {
		return -ENOMEM;
	}

	slots.sequence++;
	encode(&slots, block);
	err = blockdev_write(partition, slots.sequence % RECORD_COPIES, 1, block);
	free(block);

	return err;
}

static void save_logged(void)
{
	int err = save();

	if (err != 0) {
		printf("state %s\n", strerror(-err));
	}
}

void slots_load(void)
{
	struct blockdev *partition = layout_partition(ROLE_STATE, 0);
	struct slot_table copy;
	bool found = false;
	uint8_t *block;
	unsigned int i;

	set_defaults(&slots);

	if (!layout_slotted(ROLE_SYSTEM) || partition == NULL) {
		return;
	}

	block = malloc(partition->block_size);
	if (block == NULL) {
		return;
	}

	for (i = 0; i < RECORD_COPIES; i++) {
		if (blockdev_read(partition, i, 1, block) != 0 || !decode(block, &copy)) {
			continue;
		}

		if (!found || newer(copy.sequence, slots.sequence)) {
			slots = copy;
			found = true;
		}
	}

	free(block);

	for (i = 0; i < SLOT_COUNT; i++) {
		if (slots.slot[i].priority > 0 && !slots.slot[i].confirmed && slots.slot[i].tries == 0) {
			printf("slot %c out of tries\n", layout_slot_name(i));
		}
	}
}

int slots_pick(void)
{
	int best = -1;
	unsigned int slot;

	for (slot = 0; slot < SLOT_COUNT; slot++) {
		const struct slot_state *candidate = &slots.slot[slot];

		if (candidate->priority == 0 || (!candidate->confirmed && candidate->tries == 0)) {
			continue;
		}

		if (best < 0 || candidate->priority > slots.slot[best].priority) {
			best = (int)slot;
		}
	}

	return best;
}

void slots_attempt(unsigned int slot)
{
	struct slot_state *entry = &slots.slot[slot];

	if (entry->confirmed) {
		printf("slot %c confirmed\n", layout_slot_name(slot));
		return;
	}

	if (entry->tries > 0) {
		entry->tries--;
	}

	printf("slot %c %u %s left\n", layout_slot_name(slot), entry->tries, entry->tries == 1 ? "try" : "tries");
	save_logged();
}

void slots_fail(unsigned int slot)
{
	slots.slot[slot].priority = 0;
	printf("slot %c unbootable\n", layout_slot_name(slot));
	save_logged();
}

int slots_confirm(unsigned int slot)
{
	slots.slot[slot].confirmed = true;
	slots.slot[slot].tries = TRIES_DEFAULT;

	return save();
}

int slots_activate(unsigned int slot)
{
	unsigned int other;

	for (other = 0; other < SLOT_COUNT; other++) {
		if (other != slot && slots.slot[other].priority >= PRIORITY_MAX) {
			slots.slot[other].priority = PRIORITY_MAX - 1;
		}
	}

	slots.slot[slot].priority = PRIORITY_MAX;
	slots.slot[slot].tries = TRIES_DEFAULT;
	slots.slot[slot].confirmed = false;

	return save();
}

void slots_print(void)
{
	int picked = slots_pick();
	unsigned int slot;

	for (slot = 0; slot < SLOT_COUNT; slot++) {
		const struct slot_state *entry = &slots.slot[slot];

		printf("  %c  priority %u, %u %s, %s%s\n", layout_slot_name(slot), entry->priority, entry->tries,
		       entry->tries == 1 ? "try" : "tries", entry->confirmed ? "confirmed" : "unconfirmed",
		       picked == (int)slot ? ", next" : "");
	}
}
