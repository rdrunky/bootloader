#include <drivers/ramfb.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/shell.h>

#include "../private.h"

COMMAND(clear, "", "clear the terminal screen")
{
	if (argc != 1) {
		return SHELL_USAGE;
	}

	printf("\x1b[H\x1b[2J");

	return 0;
}

COMMAND(resolution, "[width height [scale] | widthxheight [scale]]", "show or change display resolution")
{
	const char *sep;
	const char *scale_text = NULL;
	char width_text[16];
	size_t width;
	size_t height;
	size_t scale = 0;
	size_t length;
	uint64_t width_value;
	uint64_t height_value;
	uint64_t scale_value;
	int err;

	if (argc > 4) {
		return SHELL_USAGE;
	}

	err = ramfb_get_mode(&width, &height, NULL);
	if (err != 0) {
		printf("display ramfb not active\n");
		return err;
	}

	if (argc == 1) {
		printf("resolution %zux%zu\n", width, height);
		return 0;
	}

	sep = argv[1];
	if (sep[0] == '0' && (sep[1] == 'x' || sep[1] == 'X')) {
		sep += 2;
	}
	sep += strcspn(sep, "xX");
	if (*sep == '\0') {
		sep = NULL;
	}

	if (argc == 2 || (argc == 3 && sep != NULL)) {
		if (sep == NULL) {
			return SHELL_USAGE;
		}

		length = (size_t)(sep - argv[1]);
		if (length >= sizeof(width_text)) {
			return -EINVAL;
		}

		memcpy(width_text, argv[1], length);
		width_text[length] = '\0';
		if (parse_number(width_text, &width_value) != 0 || parse_number(sep + 1, &height_value) != 0) {
			return -EINVAL;
		}

		if (argc == 3) {
			scale_text = argv[2];
		}
	} else {
		if (parse_number(argv[1], &width_value) != 0 || parse_number(argv[2], &height_value) != 0) {
			return -EINVAL;
		}

		if (argc == 4) {
			scale_text = argv[3];
		}
	}

	if (scale_text != NULL) {
		if (parse_number(scale_text, &scale_value) != 0 || scale_value == 0 || scale_value > 8) {
			return -EINVAL;
		}
		scale = (size_t)scale_value;
	}

	width = (size_t)width_value;
	height = (size_t)height_value;
	err = ramfb_set_mode(width, height, scale);
	if (err != 0) {
		printf("resolution %zux%zu failed\n", width, height);
		return err;
	}

	printf("display ramfb %zux%zu ok\n", width, height);

	return 0;
}
