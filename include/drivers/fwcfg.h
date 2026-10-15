#ifndef DRIVERS_FWCFG_H
#define DRIVERS_FWCFG_H

#include <stdint.h>

#define FWCFG_DMA_CTL_ERROR	0x01
#define FWCFG_DMA_CTL_READ	0x02
#define FWCFG_DMA_CTL_SKIP	0x04
#define FWCFG_DMA_CTL_SELECT	0x08
#define FWCFG_DMA_CTL_WRITE	0x10

int fwcfg_lookup(const char *name, uint16_t *item, uint32_t *size);
int fwcfg_dma_transfer(uint32_t control, uint32_t length, uint64_t address);

#endif
