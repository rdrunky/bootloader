#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/macros.h>
#include <sys/random.h>

#include "private.h"

#define REQUEST_QUEUE		0
#define BUFFER_SIZE		64
#define BUFFER_ALIGN		64
#define EMPTY_READS_MAX		16

struct virtio_random {
	struct random_source	source;
	struct virtio_device	*virtio;
	struct virtqueue	queue;
	uint8_t			*buffer;
};

static int random_device_read(struct random_source *source, void *buffer, size_t size)
{
	struct virtio_random *entropy = container_of(source, struct virtio_random, source);
	struct virtio_buffer request = { entropy->buffer, BUFFER_SIZE, true };
	uint8_t *bytes = buffer;
	unsigned int empty = 0;

	while (size > 0) {
		uint32_t written = 0;
		size_t take;
		int err = virtqueue_transfer(entropy->virtio, &entropy->queue, &request, 1, &written);

		if (err == 0 && written > BUFFER_SIZE) {
			err = -EIO;
		}

		if (err == 0 && written == 0 && ++empty == EMPTY_READS_MAX) {
			err = -EIO;
		}

		if (err != 0) {
			explicit_bzero(entropy->buffer, BUFFER_SIZE);
			return err;
		}

		take = written < size ? written : size;
		memcpy(bytes, entropy->buffer, take);
		bytes += take;
		size -= take;
	}

	explicit_bzero(entropy->buffer, BUFFER_SIZE);

	return 0;
}

static int random_probe(struct virtio_device *virtio)
{
	struct virtio_random *entropy = calloc(1, sizeof(*entropy));
	int err;

	if (entropy == NULL) {
		return -ENOMEM;
	}

	entropy->virtio = virtio;
	entropy->buffer = aligned_alloc(BUFFER_ALIGN, BUFFER_SIZE);
	err = entropy->buffer != NULL ? virtqueue_setup(virtio, &entropy->queue, REQUEST_QUEUE) : -ENOMEM;
	if (err != 0) {
		free(entropy->buffer);
		free(entropy);
		return err;
	}

	entropy->source.name = "virtio";
	entropy->source.read = random_device_read;
	virtio->context = entropy;

	return 0;
}

static void random_start(struct virtio_device *virtio)
{
	struct virtio_random *entropy = virtio->context;

	random_register(&entropy->source);
}

static const struct virtio_driver random_driver = {
	.device_id	= VIRTIO_DEVICE_RANDOM,
	.probe		= random_probe,
	.start		= random_start,
};

VIRTIO_DRIVER(random_driver);
