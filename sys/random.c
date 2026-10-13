#include <arch.h>
#include <compiler.h>
#include <errno.h>
#include <lib/crypto.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/random.h>

#define CPU_ATTEMPTS	16

static struct random_source *sources;
static struct random_source cpu_source;

static int read_cpu(struct random_source *source __unused, void *buffer, size_t size)
{
	uint8_t *bytes = buffer;
	unsigned int failures = 0;
	uint64_t value = 0;

	while (size > 0) {
		size_t take = size < sizeof(value) ? size : sizeof(value);
		int err = arch_random(&value);

		if (err == -EAGAIN && ++failures < CPU_ATTEMPTS) {
			continue;
		}

		if (err != 0) {
			return err;
		}

		memcpy(bytes, &value, take);
		bytes += take;
		size -= take;
	}

	explicit_bzero(&value, sizeof(value));

	return 0;
}

void random_init(void)
{
	uint64_t probe;

	if (arch_random(&probe) == -ENOTSUP) {
		return;
	}

	cpu_source.name = "cpu";
	cpu_source.read = read_cpu;
	random_register(&cpu_source);
}

void random_register(struct random_source *source)
{
	struct random_source **tail = &sources;

	while (*tail != NULL) {
		tail = &(*tail)->next;
	}

	source->next = NULL;
	*tail = source;

	printf("random %s ok\n", source->name);
}

int random_read(void *buffer, size_t size)
{
	uint8_t block[RANDOM_READ_MAX];
	uint8_t digest[SHA512_SIZE];
	struct random_source *source;
	struct sha512 context;
	unsigned int contributed = 0;

	if (size > sizeof(digest)) {
		return -EINVAL;
	}

	sha512_init(&context);
	for (source = sources; source != NULL; source = source->next) {
		if (source->read(source, block, sizeof(block)) == 0) {
			sha512_update(&context, block, sizeof(block));
			contributed++;
		}
	}

	sha512_final(&context, digest);
	if (contributed > 0) {
		memcpy(buffer, digest, size);
	}

	explicit_bzero(block, sizeof(block));
	explicit_bzero(digest, sizeof(digest));

	return contributed > 0 ? 0 : -ENODEV;
}
