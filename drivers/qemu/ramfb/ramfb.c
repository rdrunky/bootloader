#include <drivers/fwcfg.h>
#include <drivers/ramfb.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/console.h>
#include <sys/mem.h>

#include "font.h"

#define RAMFB_FILE		"etc/ramfb"
#define RAMFB_RES_FILE		"opt/boot/resolution"
#define RAMFB_FOURCC_XRGB	0x34325258
#define RAMFB_DEFAULT_WIDTH	800
#define RAMFB_DEFAULT_HEIGHT	600
#define RAMFB_MIN_WIDTH		320
#define RAMFB_MIN_HEIGHT	200
#define RAMFB_MAX_WIDTH		4096
#define RAMFB_MAX_HEIGHT	2160
#define RAMFB_BPP		4

#define COLOR_BG		0x00000000
#define COLOR_FG		0x00ffffff
#define MARGIN_X		8
#define MARGIN_Y		8

struct ramfb_cfg {
	uint64_t	addr;
	uint32_t	fourcc;
	uint32_t	flags;
	uint32_t	width;
	uint32_t	height;
	uint32_t	stride;
} __attribute__((packed));

enum ansi_state {
	ANSI_STATE_NONE,
	ANSI_STATE_ESC,
	ANSI_STATE_CSI,
};

struct ramfb {
	struct console	console;
	uint64_t	base;
	uint16_t	item;
	size_t		width;
	size_t		height;
	size_t		stride;
	size_t		scale;
	size_t		char_width;
	size_t		char_height;
	size_t		cols;
	size_t		rows;
	size_t		col;
	size_t		row;
	uint32_t	fg;
	uint32_t	bg;
	enum ansi_state	state;
	unsigned int	arg;
};

static struct ramfb fb;

static inline void put_pixel(size_t x, size_t y, uint32_t color)
{
	if (x < fb.width && y < fb.height) {
		((volatile uint32_t *)fb.base)[y * fb.width + x] = color;
	}
}

static void fill_rect(size_t x, size_t y, size_t w, size_t h, uint32_t color)
{
	size_t i;
	size_t j;

	if (x >= fb.width || y >= fb.height) {
		return;
	}

	if (x + w > fb.width) {
		w = fb.width - x;
	}

	if (y + h > fb.height) {
		h = fb.height - y;
	}

	if (color == 0) {
		if (x == 0 && w == fb.width) {
			memset((uint8_t *)fb.base + y * fb.stride, 0, h * fb.stride);
			return;
		}

		for (j = y; j < y + h; j++) {
			memset((uint8_t *)fb.base + j * fb.stride + x * sizeof(uint32_t), 0,
			       w * sizeof(uint32_t));
		}
		return;
	}

	for (j = y; j < y + h; j++) {
		for (i = x; i < x + w; i++) {
			((volatile uint32_t *)fb.base)[j * fb.width + i] = color;
		}
	}
}

static void draw_glyph(size_t col, size_t row, uint8_t c)
{
	size_t px = MARGIN_X + col * fb.char_width;
	size_t py = MARGIN_Y + row * fb.char_height;
	const uint8_t *glyph;
	size_t r;
	size_t b;

	if (c > 127) {
		c = '?';
	}

	glyph = font8x8[c];
	for (r = 0; r < FONT_HEIGHT; r++) {
		uint8_t bits = glyph[r];

		for (b = 0; b < FONT_WIDTH; b++) {
			uint32_t color = (bits & (0x80 >> b)) != 0 ? fb.fg : fb.bg;

			if (fb.scale == 1) {
				put_pixel(px + b, py + r, color);
			} else {
				fill_rect(px + b * fb.scale, py + r * fb.scale, fb.scale, fb.scale, color);
			}
		}
	}
}

static void scroll(void)
{
	uint32_t *pixels = (uint32_t *)fb.base;
	size_t move_rows = fb.rows - 1;
	size_t row_pixels = fb.width * fb.char_height;

	memmove(pixels + MARGIN_Y * fb.width,
		pixels + (MARGIN_Y + fb.char_height) * fb.width,
		move_rows * row_pixels * sizeof(uint32_t));

	fill_rect(MARGIN_X, MARGIN_Y + move_rows * fb.char_height,
		  fb.width - MARGIN_X * 2, fb.char_height, fb.bg);
}

static void ramfb_csi(char c)
{
	unsigned int arg = fb.arg;
	size_t count = arg != 0 ? arg : 1;

	switch (c) {
	case 'C':
		fb.col = fb.col + count < fb.cols ? fb.col + count : fb.cols - 1;
		break;
	case 'D':
		fb.col = fb.col > count ? fb.col - count : 0;
		break;
	case 'H':
		fb.col = 0;
		fb.row = 0;
		break;
	case 'J':
		if (arg == 2) {
			fill_rect(0, 0, fb.width, fb.height, fb.bg);
			fb.col = 0;
			fb.row = 0;
		}
		break;
	case 'K':
		if (arg == 0 && fb.col < fb.cols && fb.row < fb.rows) {
			fill_rect(MARGIN_X + fb.col * fb.char_width, MARGIN_Y + fb.row * fb.char_height,
				  (fb.cols - fb.col) * fb.char_width, fb.char_height, fb.bg);
		} else if (arg == 2 && fb.row < fb.rows) {
			fill_rect(MARGIN_X, MARGIN_Y + fb.row * fb.char_height,
				  fb.cols * fb.char_width, fb.char_height, fb.bg);
		}
		break;
	default:
		break;
	}
}

static void ramfb_putc(char c)
{
	if (fb.state == ANSI_STATE_ESC) {
		if (c == '[') {
			fb.state = ANSI_STATE_CSI;
			fb.arg = 0;
			return;
		}
		fb.state = ANSI_STATE_NONE;
	} else if (fb.state == ANSI_STATE_CSI) {
		if (c >= '0' && c <= '9') {
			fb.arg = fb.arg * 10 + (unsigned int)(c - '0');
			return;
		}
		if (c == ';' || c == '?') {
			return;
		}
		ramfb_csi(c);
		fb.state = ANSI_STATE_NONE;
		return;
	}

	if (c == '\x1b') {
		fb.state = ANSI_STATE_ESC;
		return;
	}

	if (c == '\r') {
		fb.col = 0;
		return;
	}

	if (c == '\n') {
		fb.col = 0;
		if (fb.row + 1 < fb.rows) {
			fb.row++;
		} else {
			scroll();
		}
		return;
	}

	if (c == '\b') {
		if (fb.col > 0) {
			fb.col--;
			draw_glyph(fb.col, fb.row, ' ');
		}
		return;
	}

	if (c == '\t') {
		size_t tab = 8 - (fb.col % 8);

		while (tab-- > 0 && fb.col < fb.cols) {
			draw_glyph(fb.col++, fb.row, ' ');
		}
		return;
	}

	if ((uint8_t)c < ' ') {
		return;
	}

	if (fb.col >= fb.cols) {
		fb.col = 0;
		if (fb.row + 1 < fb.rows) {
			fb.row++;
		} else {
			scroll();
		}
	}

	draw_glyph(fb.col++, fb.row, (uint8_t)c);
}

static void ramfb_console_write(struct console *console, const char *data, size_t len)
{
	(void)console;

	while (len-- > 0) {
		ramfb_putc(*data++);
	}
}

static int ramfb_console_read(struct console *console)
{
	(void)console;
	return -1;
}

static void ramfb_console_flush(struct console *console)
{
	(void)console;
}

static void parse_resolution(const char *str, size_t *width, size_t *height)
{
	unsigned int w = 0;
	unsigned int h = 0;

	while (*str >= '0' && *str <= '9') {
		unsigned int digit = (unsigned int)(*str++ - '0');

		if (w > (RAMFB_MAX_WIDTH - digit) / 10) {
			return;
		}
		w = w * 10 + digit;
	}

	if (*str != 'x' && *str != 'X') {
		return;
	}
	str++;

	while (*str >= '0' && *str <= '9') {
		unsigned int digit = (unsigned int)(*str++ - '0');

		if (h > (RAMFB_MAX_HEIGHT - digit) / 10) {
			return;
		}
		h = h * 10 + digit;
	}

	if (*str == '\0' && w >= RAMFB_MIN_WIDTH && w <= RAMFB_MAX_WIDTH &&
	    h >= RAMFB_MIN_HEIGHT && h <= RAMFB_MAX_HEIGHT) {
		*width = (size_t)w;
		*height = (size_t)h;
	}
}

int ramfb_set_mode(size_t width, size_t height, size_t scale)
{
	struct ramfb_cfg cfg;
	uint64_t base;
	size_t stride;
	size_t size;
	int err;

	if (fb.item == 0) {
		return -ENODEV;
	}

	if (width < RAMFB_MIN_WIDTH || width > RAMFB_MAX_WIDTH ||
	    height < RAMFB_MIN_HEIGHT || height > RAMFB_MAX_HEIGHT) {
		return -EINVAL;
	}

	if (scale == 0) {
		scale = height >= 900 ? 2 : 1;
	}
	if (scale > 8) {
		return -EINVAL;
	}

	stride = width * RAMFB_BPP;
	size = height * stride;

	err = mem_allocate(size, MEMMAP_PAGE_SIZE, "framebuffer", &base);
	if (err != 0) {
		return err;
	}

	cfg.addr = __builtin_bswap64(base);
	cfg.fourcc = __builtin_bswap32(RAMFB_FOURCC_XRGB);
	cfg.flags = 0;
	cfg.width = __builtin_bswap32((uint32_t)width);
	cfg.height = __builtin_bswap32((uint32_t)height);
	cfg.stride = __builtin_bswap32((uint32_t)stride);

	err = fwcfg_dma_transfer(((uint32_t)fb.item << 16) | FWCFG_DMA_CTL_SELECT | FWCFG_DMA_CTL_WRITE,
				 sizeof(cfg), (uint64_t)&cfg);
	if (err != 0) {
		mem_release(base, size, "framebuffer");
		return err;
	}

	if (fb.base != 0) {
		mem_release(fb.base, fb.height * fb.stride, "framebuffer");
	}

	fb.base = base;
	fb.width = width;
	fb.height = height;
	fb.stride = stride;
	fb.scale = scale;
	fb.char_width = FONT_WIDTH * scale;
	fb.char_height = FONT_HEIGHT * scale;
	fb.cols = (width - MARGIN_X * 2) / fb.char_width;
	fb.rows = (height - MARGIN_Y * 2) / fb.char_height;
	fb.col = 0;
	fb.row = 0;
	fb.state = ANSI_STATE_NONE;
	fb.arg = 0;

	fill_rect(0, 0, width, height, fb.bg);

	if (fb.console.write != NULL) {
		console_replay(&fb.console);
	}

	return 0;
}

int ramfb_get_mode(size_t *width, size_t *height, size_t *scale)
{
	if (fb.base == 0) {
		return -ENODEV;
	}

	if (width != NULL) {
		*width = fb.width;
	}
	if (height != NULL) {
		*height = fb.height;
	}
	if (scale != NULL) {
		*scale = fb.scale;
	}

	return 0;
}

int ramfb_init(void)
{
	uint16_t item;
	uint32_t file_size;
	uint16_t res_item;
	uint32_t res_size;
	size_t width = RAMFB_DEFAULT_WIDTH;
	size_t height = RAMFB_DEFAULT_HEIGHT;
	int err;

	err = fwcfg_lookup(RAMFB_FILE, &item, &file_size);
	if (err != 0) {
		return err;
	}

	fb.item = item;
	fb.fg = COLOR_FG;
	fb.bg = COLOR_BG;
	fb.state = ANSI_STATE_NONE;
	fb.arg = 0;

	if (fwcfg_lookup(RAMFB_RES_FILE, &res_item, &res_size) == 0 && res_size > 0) {
		char buf[32];
		size_t len = res_size < sizeof(buf) - 1 ? res_size : sizeof(buf) - 1;

		if (fwcfg_dma_transfer(((uint32_t)res_item << 16) | FWCFG_DMA_CTL_SELECT | FWCFG_DMA_CTL_READ,
				       (uint32_t)len, (uint64_t)buf) == 0) {
			buf[len] = '\0';
			parse_resolution(buf, &width, &height);
		}
	}

	err = ramfb_set_mode(width, height, 0);
	if (err != 0) {
		return err;
	}

	fb.console.name = "ramfb";
	fb.console.write = ramfb_console_write;
	fb.console.read = ramfb_console_read;
	fb.console.flush = ramfb_console_flush;

	printf("display ramfb %zux%zu ok\n", fb.width, fb.height);
	console_register(&fb.console);

	return 0;
}
