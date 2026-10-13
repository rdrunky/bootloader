#ifndef SYS_FORMAT_H
#define SYS_FORMAT_H

#include <stddef.h>
#include <stdint.h>

#define SIZE_TEXT	24

void format_size(char *buffer, size_t length, uint64_t bytes);

#endif
