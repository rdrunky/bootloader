#include <arch.h>
#include <stdio.h>
#include <sys/devicetree.h>
#include <sys/time.h>

#define FALLBACK_FREQUENCY	62500000ULL

static uint64_t frequency;

static uint64_t devicetree_frequency(void)
{
	const struct dt *dt = devicetree();
	uint32_t hz;

	if (dt == NULL) {
		return 0;
	}

	if (dt_read_u32(dt, dt_find_compatible(dt, -1, "arm,armv8-timer"), "clock-frequency", &hz) != 0) {
		return 0;
	}

	return hz;
}

void time_init(void)
{
	frequency = arch_timer_frequency();

	if (frequency == 0) {
		frequency = devicetree_frequency();
	}

	if (frequency == 0) {
		frequency = FALLBACK_FREQUENCY;
		printf("timer frequency unset, assuming %llu Hz\n", FALLBACK_FREQUENCY);
	}
}

uint64_t time_us(void)
{
	uint64_t ticks = arch_timer_count();

	return ticks / frequency * USEC_PER_SEC + ticks % frequency * USEC_PER_SEC / frequency;
}
