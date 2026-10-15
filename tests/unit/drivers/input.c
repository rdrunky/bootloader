#include "../../../drivers/virtio/private.h"
#include "harness.h"

#undef VIRTIO_DRIVER
#define VIRTIO_DRIVER(driver) _Static_assert(sizeof(driver) > 0, "virtio driver")

static int input_test_collect(struct virtio_device *dev, struct virtqueue *queue, uint16_t *id, uint32_t *length);
static int input_test_post(struct virtio_device *dev, struct virtqueue *queue, uint16_t id, void *data, uint32_t length);
static void input_test_kick(struct virtio_device *dev, struct virtqueue *queue);

#define virtqueue_collect input_test_collect
#define virtqueue_post input_test_post
#define virtqueue_kick input_test_kick
#include "../../../drivers/virtio/input.c"
#undef virtqueue_collect
#undef virtqueue_post
#undef virtqueue_kick

static unsigned int pending_events;
static unsigned int collected_events;
static unsigned int posted_events;

static int input_test_collect(struct virtio_device *dev, struct virtqueue *queue, uint16_t *id, uint32_t *length)
{
	(void)dev;
	(void)queue;
	if (collected_events == pending_events) {
		return -EAGAIN;
	}

	*id = (uint16_t)collected_events++;
	*length = sizeof(struct virtio_input_event);
	return 0;
}

static int input_test_post(struct virtio_device *dev, struct virtqueue *queue, uint16_t id, void *data, uint32_t length)
{
	(void)dev;
	(void)queue;
	(void)id;
	(void)data;
	(void)length;
	posted_events++;
	return 0;
}

static void input_test_kick(struct virtio_device *dev, struct virtqueue *queue)
{
	(void)dev;
	(void)queue;
}

static void key_event(struct virtio_input *input, uint16_t code, uint32_t value)
{
	struct virtio_input_event event = { .type = EV_KEY, .code = code, .value = value };

	input_process_event(input, &event);
}

TEST(input, keeps_shift_until_both_keys_are_released)
{
	const uint16_t shifts[] = { KEY_LEFTSHIFT, KEY_RIGHTSHIFT };
	unsigned int first;

	for (first = 0; first < 2; first++) {
		struct virtio_input input = { 0 };

		key_event(&input, shifts[0], KEY_PRESSED);
		key_event(&input, shifts[1], KEY_PRESSED);
		key_event(&input, shifts[first], KEY_RELEASED);
		key_event(&input, KEY_A, KEY_PRESSED);
		CHECK_EQ(fifo_get(&input), 'A');
		key_event(&input, shifts[1 - first], KEY_RELEASED);
		key_event(&input, KEY_A, KEY_PRESSED);
		CHECK_EQ(fifo_get(&input), 'a');
		CHECK_EQ(fifo_get(&input), -1);
	}
}

TEST(input, keeps_ctrl_until_both_keys_are_released)
{
	const uint16_t controls[] = { KEY_LEFTCTRL, KEY_RIGHTCTRL };
	unsigned int first;

	for (first = 0; first < 2; first++) {
		struct virtio_input input = { 0 };

		key_event(&input, controls[0], KEY_PRESSED);
		key_event(&input, controls[1], KEY_PRESSED);
		key_event(&input, controls[first], KEY_RELEASED);
		key_event(&input, KEY_C, KEY_PRESSED);
		CHECK_EQ(fifo_get(&input), 3);
		key_event(&input, controls[1 - first], KEY_RELEASED);
		key_event(&input, KEY_C, KEY_PRESSED);
		CHECK_EQ(fifo_get(&input), 'c');
		CHECK_EQ(fifo_get(&input), -1);
	}
}

TEST(input, combines_caps_lock_with_shift)
{
	struct virtio_input input = { 0 };

	key_event(&input, KEY_CAPSLOCK, KEY_PRESSED);
	key_event(&input, KEY_CAPSLOCK, KEY_RELEASED);
	key_event(&input, KEY_CAPSLOCK, KEY_REPEATED);
	key_event(&input, KEY_A, KEY_PRESSED);
	key_event(&input, KEY_1, KEY_PRESSED);
	CHECK_EQ(fifo_get(&input), 'A');
	CHECK_EQ(fifo_get(&input), '1');
	key_event(&input, KEY_RIGHTSHIFT, KEY_PRESSED);
	key_event(&input, KEY_A, KEY_REPEATED);
	key_event(&input, KEY_1, KEY_PRESSED);
	CHECK_EQ(fifo_get(&input), 'a');
	CHECK_EQ(fifo_get(&input), '!');
	key_event(&input, KEY_RIGHTSHIFT, KEY_RELEASED);
	key_event(&input, KEY_CAPSLOCK, KEY_PRESSED);
	key_event(&input, KEY_A, KEY_RELEASED);
	CHECK_EQ(fifo_get(&input), -1);
	key_event(&input, KEY_A, KEY_PRESSED);
	CHECK_EQ(fifo_get(&input), 'a');
}

TEST(input, drains_modifier_releases_even_when_fifo_is_full)
{
	struct event_buffer {
		struct virtio_input_event event;
		uint8_t padding[BUFFER_SIZE - sizeof(struct virtio_input_event)];
	} buffers[] = {
		{ .event = { EV_KEY, KEY_LEFTSHIFT, KEY_PRESSED } },
		{ .event = { EV_KEY, KEY_A, KEY_PRESSED } },
		{ .event = { EV_KEY, KEY_A, KEY_RELEASED } },
		{ .event = { EV_KEY, KEY_LEFTSHIFT, KEY_RELEASED } },
	};
	unsigned int full;

	for (full = 0; full < 2; full++) {
		struct virtio_input input = { .buffers = (uint8_t *)buffers };
		unsigned int i;

		if (full) {
			for (i = 0; i < FIFO_CAPACITY - 1; i++) {
				fifo_put(&input, 'x');
			}
		}

		pending_events = sizeof(buffers) / sizeof(buffers[0]);
		collected_events = 0;
		posted_events = 0;
		CHECK_EQ(input_console_read(&input.console), full ? 'x' : 'A');
		CHECK_EQ(collected_events, pending_events);
		CHECK_EQ(posted_events, pending_events);
		CHECK(!input.left_shift);

		while (fifo_get(&input) >= 0) {
		}
		key_event(&input, KEY_A, KEY_PRESSED);
		CHECK_EQ(fifo_get(&input), 'a');
	}
}

TEST(input, keeps_escape_sequences_whole_when_fifo_fills)
{
	struct virtio_input input = { 0 };
	unsigned int i;

	for (i = 0; i < FIFO_CAPACITY - 3; i++) {
		fifo_put(&input, 'x');
	}

	key_event(&input, KEY_DELETE, KEY_PRESSED);
	for (i = 0; i < FIFO_CAPACITY - 3; i++) {
		CHECK_EQ(fifo_get(&input), 'x');
	}
	CHECK_EQ(fifo_get(&input), -1);

	key_event(&input, KEY_DELETE, KEY_PRESSED);
	CHECK_EQ(fifo_get(&input), '\x1b');
	CHECK_EQ(fifo_get(&input), '[');
	CHECK_EQ(fifo_get(&input), '3');
	CHECK_EQ(fifo_get(&input), '~');
	CHECK_EQ(fifo_get(&input), -1);
}
