#ifndef PARTITION_PRIVATE_H
#define PARTITION_PRIVATE_H

#include <lib/partition.h>

int mbr_scan(struct blockdev *disk, struct partition_table *table, bool *protective);
int gpt_scan(struct blockdev *disk, struct partition_table *table);
struct partition *partition_add(struct partition_table *table);

#endif
