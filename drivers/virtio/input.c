#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/console.h>
#include <sys/macros.h>

#include "private.h"

#define EVENT_QUEUE		0
#define BUFFER_SIZE		64
#define BUFFER_ALIGN		64
#define FIFO_CAPACITY		256

#define EV_KEY			0x01

#define KEY_RELEASED		0
#define KEY_PRESSED		1
#define KEY_REPEATED		2

#define KEY_ESC			1
#define KEY_1			2
#define KEY_2			3
#define KEY_3			4
#define KEY_4			5
#define KEY_5			6
#define KEY_6			7
#define KEY_7			8
#define KEY_8			9
#define KEY_9			10
#define KEY_0			11
#define KEY_MINUS		12
#define KEY_EQUAL		13
#define KEY_BACKSPACE		14
#define KEY_TAB			15
#define KEY_Q			16
#define KEY_W			17
#define KEY_E			18
#define KEY_R			19
#define KEY_T			20
#define KEY_Y			21
#define KEY_U			22
#define KEY_I			23
#define KEY_O			24
#define KEY_P			25
#define KEY_LEFTBRACE		26
#define KEY_RIGHTBRACE		27
#define KEY_ENTER		28
#define KEY_LEFTCTRL		29
#define KEY_A			30
#define KEY_S			31
#define KEY_D			32
#define KEY_F			33
#define KEY_G			34
#define KEY_H			35
#define KEY_J			36
#define KEY_K			37
#define KEY_L			38
#define KEY_SEMICOLON		39
#define KEY_APOSTROPHE		40
#define KEY_GRAVE		41
#define KEY_LEFTSHIFT		42
#define KEY_BACKSLASH		43
#define KEY_Z			44
#define KEY_X			45
#define KEY_C			46
#define KEY_V			47
#define KEY_B			48
#define KEY_N			49
#define KEY_M			50
#define KEY_COMMA		51
#define KEY_DOT			52
#define KEY_SLASH		53
#define KEY_RIGHTSHIFT		54
#define KEY_KPASTERISK		55
#define KEY_SPACE		57
#define KEY_CAPSLOCK		58

#define KEY_KP7			71
#define KEY_KP8			72
#define KEY_KP9			73
#define KEY_KPMINUS		74
#define KEY_KP4			75
#define KEY_KP5			76
#define KEY_KP6			77
#define KEY_KPPLUS		78
#define KEY_KP1			79
#define KEY_KP2			80
#define KEY_KP3			81
#define KEY_KP0			82
#define KEY_KPDOT		83
#define KEY_KPENTER		96
#define KEY_RIGHTCTRL		97
#define KEY_KPSLASH		98
#define KEY_HOME		102
#define KEY_UP			103
#define KEY_LEFT		105
#define KEY_RIGHT		106
#define KEY_END			107
#define KEY_DOWN		108
#define KEY_DELETE		111

struct virtio_input_event {
	uint16_t	type;
	uint16_t	code;
	uint32_t	value;
};

struct virtio_input {
	struct console		console;
	struct virtio_device	*virtio;
	struct virtqueue	queue;
	uint8_t			*buffers;
	uint8_t			fifo[FIFO_CAPACITY];
	unsigned int		fifo_head;
	unsigned int		fifo_tail;
	bool			left_shift;
	bool			right_shift;
	bool			left_ctrl;
	bool			right_ctrl;
	bool			caps_lock;
};

static const char keymap_normal[58] = {
	[KEY_ESC] = '\x1b',
	[KEY_1] = '1', [KEY_2] = '2', [KEY_3] = '3', [KEY_4] = '4', [KEY_5] = '5',
	[KEY_6] = '6', [KEY_7] = '7', [KEY_8] = '8', [KEY_9] = '9', [KEY_0] = '0',
	[KEY_MINUS] = '-', [KEY_EQUAL] = '=', [KEY_BACKSPACE] = '\x7f', [KEY_TAB] = '\t',
	[KEY_Q] = 'q', [KEY_W] = 'w', [KEY_E] = 'e', [KEY_R] = 'r', [KEY_T] = 't',
	[KEY_Y] = 'y', [KEY_U] = 'u', [KEY_I] = 'i', [KEY_O] = 'o', [KEY_P] = 'p',
	[KEY_LEFTBRACE] = '[', [KEY_RIGHTBRACE] = ']', [KEY_ENTER] = '\r',
	[KEY_A] = 'a', [KEY_S] = 's', [KEY_D] = 'd', [KEY_F] = 'f', [KEY_G] = 'g',
	[KEY_H] = 'h', [KEY_J] = 'j', [KEY_K] = 'k', [KEY_L] = 'l',
	[KEY_SEMICOLON] = ';', [KEY_APOSTROPHE] = '\'', [KEY_GRAVE] = '`',
	[KEY_BACKSLASH] = '\\',
	[KEY_Z] = 'z', [KEY_X] = 'x', [KEY_C] = 'c', [KEY_V] = 'v', [KEY_B] = 'b',
	[KEY_N] = 'n', [KEY_M] = 'm',
	[KEY_COMMA] = ',', [KEY_DOT] = '.', [KEY_SLASH] = '/',
	[KEY_SPACE] = ' ',
};

static const char keymap_shifted[58] = {
	[KEY_ESC] = '\x1b',
	[KEY_1] = '!', [KEY_2] = '@', [KEY_3] = '#', [KEY_4] = '$', [KEY_5] = '%',
	[KEY_6] = '^', [KEY_7] = '&', [KEY_8] = '*', [KEY_9] = '(', [KEY_0] = ')',
	[KEY_MINUS] = '_', [KEY_EQUAL] = '+', [KEY_BACKSPACE] = '\x7f', [KEY_TAB] = '\t',
	[KEY_LEFTBRACE] = '{', [KEY_RIGHTBRACE] = '}', [KEY_ENTER] = '\r',
	[KEY_SEMICOLON] = ':', [KEY_APOSTROPHE] = '"', [KEY_GRAVE] = '~',
	[KEY_BACKSLASH] = '|',
	[KEY_COMMA] = '<', [KEY_DOT] = '>', [KEY_SLASH] = '?',
	[KEY_SPACE] = ' ',
};

static void fifo_put(struct virtio_input *input, uint8_t byte)
{
	unsigned int next = (input->fifo_head + 1) % sizeof(input->fifo);

	if (next != input->fifo_tail) {
		input->fifo[input->fifo_head] = byte;
		input->fifo_head = next;
	}
}

static void fifo_puts(struct virtio_input *input, const char *str)
{
	size_t length = strlen(str);
	size_t available = (input->fifo_tail + sizeof(input->fifo) - input->fifo_head - 1) % sizeof(input->fifo);

	if (length > available) {
		return;
	}

	while (*str != '\0') {
		fifo_put(input, (uint8_t)*str++);
	}
}

static int fifo_get(struct virtio_input *input)
{
	uint8_t byte;

	if (input->fifo_head == input->fifo_tail) {
		return -1;
	}

	byte = input->fifo[input->fifo_tail];
	input->fifo_tail = (input->fifo_tail + 1) % sizeof(input->fifo);

	return (int)byte;
}

static void input_process_event(struct virtio_input *input, const struct virtio_input_event *ev)
{
	uint16_t code;
	uint32_t value;
	bool shift;

	if (ev->type != EV_KEY) {
		return;
	}

	code = ev->code;
	value = ev->value;

	if (value != KEY_RELEASED && value != KEY_PRESSED && value != KEY_REPEATED) {
		return;
	}

	switch (code) {
	case KEY_LEFTSHIFT:
		input->left_shift = value != KEY_RELEASED;
		return;
	case KEY_RIGHTSHIFT:
		input->right_shift = value != KEY_RELEASED;
		return;
	case KEY_LEFTCTRL:
		input->left_ctrl = value != KEY_RELEASED;
		return;
	case KEY_RIGHTCTRL:
		input->right_ctrl = value != KEY_RELEASED;
		return;
	case KEY_CAPSLOCK:
		if (value == KEY_PRESSED) {
			input->caps_lock = !input->caps_lock;
		}
		return;
	default:
		break;
	}

	if (value == KEY_RELEASED) {
		return;
	}

	switch (code) {
	case KEY_UP:
		fifo_puts(input, "\x1b[A");
		return;
	case KEY_DOWN:
		fifo_puts(input, "\x1b[B");
		return;
	case KEY_RIGHT:
		fifo_puts(input, "\x1b[C");
		return;
	case KEY_LEFT:
		fifo_puts(input, "\x1b[D");
		return;
	case KEY_HOME:
		fifo_puts(input, "\x1b[H");
		return;
	case KEY_END:
		fifo_puts(input, "\x1b[F");
		return;
	case KEY_DELETE:
		fifo_puts(input, "\x1b[3~");
		return;
	case KEY_KP0:
		fifo_put(input, '0');
		return;
	case KEY_KP1:
		fifo_put(input, '1');
		return;
	case KEY_KP2:
		fifo_put(input, '2');
		return;
	case KEY_KP3:
		fifo_put(input, '3');
		return;
	case KEY_KP4:
		fifo_put(input, '4');
		return;
	case KEY_KP5:
		fifo_put(input, '5');
		return;
	case KEY_KP6:
		fifo_put(input, '6');
		return;
	case KEY_KP7:
		fifo_put(input, '7');
		return;
	case KEY_KP8:
		fifo_put(input, '8');
		return;
	case KEY_KP9:
		fifo_put(input, '9');
		return;
	case KEY_KPDOT:
		fifo_put(input, '.');
		return;
	case KEY_KPENTER:
		fifo_put(input, '\r');
		return;
	case KEY_KPPLUS:
		fifo_put(input, '+');
		return;
	case KEY_KPMINUS:
		fifo_put(input, '-');
		return;
	case KEY_KPASTERISK:
		fifo_put(input, '*');
		return;
	case KEY_KPSLASH:
		fifo_put(input, '/');
		return;
	default:
		break;
	}

	if (code < sizeof(keymap_normal)) {
		char c = keymap_normal[code];

		shift = input->left_shift || input->right_shift;
		if (c >= 'a' && c <= 'z') {
			if (input->caps_lock ^ shift) {
				c = (char)(c - 'a' + 'A');
			}
		} else if (shift) {
			c = keymap_shifted[code];
		}

		if ((input->left_ctrl || input->right_ctrl) && c != '\0') {
			c = (char)(c & 0x1f);
		}

		if (c != '\0') {
			fifo_put(input, (uint8_t)c);
		}
	}
}

static void input_console_write(struct console *console, const char *data, size_t len)
{
	(void)console;
	(void)data;
	(void)len;
}

static int input_console_read(struct console *console)
{
	struct virtio_input *input = container_of(console, struct virtio_input, console);
	uint16_t id;
	uint32_t written;

	while (virtqueue_collect(input->virtio, &input->queue, &id, &written) == 0) {
		if (written >= sizeof(struct virtio_input_event)) {
			const struct virtio_input_event *ev = (void *)(input->buffers + id * BUFFER_SIZE);

			input_process_event(input, ev);
		}

		virtqueue_post(input->virtio, &input->queue, id, input->buffers + id * BUFFER_SIZE, BUFFER_SIZE);
		virtqueue_kick(input->virtio, &input->queue);
	}

	return fifo_get(input);
}

static void input_console_flush(struct console *console)
{
	(void)console;
}

static int input_probe(struct virtio_device *virtio)
{
	struct virtio_input *input = calloc(1, sizeof(*input));
	int err;

	if (input == NULL) {
		return -ENOMEM;
	}

	input->virtio = virtio;
	input->buffers = aligned_alloc(BUFFER_ALIGN, VIRTQ_SIZE_MAX * BUFFER_SIZE);
	err = input->buffers != NULL ? virtqueue_setup(virtio, &input->queue, EVENT_QUEUE) : -ENOMEM;
	if (err != 0) {
		free(input->buffers);
		free(input);
		return err;
	}

	input->console.name = "keyboard";
	input->console.write = input_console_write;
	input->console.read = input_console_read;
	input->console.flush = input_console_flush;
	virtio->context = input;

	return 0;
}

static void input_start(struct virtio_device *virtio)
{
	struct virtio_input *input = virtio->context;
	uint16_t id;

	for (id = 0; id < input->queue.size; id++) {
		virtqueue_post(virtio, &input->queue, id, input->buffers + id * BUFFER_SIZE, BUFFER_SIZE);
	}

	virtqueue_kick(virtio, &input->queue);
	console_register(&input->console);
}

static const struct virtio_driver input_driver = {
	.device_id	= VIRTIO_DEVICE_INPUT,
	.probe		= input_probe,
	.start		= input_start,
};

VIRTIO_DRIVER(input_driver);
