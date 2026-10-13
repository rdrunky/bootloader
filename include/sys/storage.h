#ifndef SYS_STORAGE_H
#define SYS_STORAGE_H

#include <lib/blockdev.h>

void storage_attach(struct blockdev *disk);

#endif
