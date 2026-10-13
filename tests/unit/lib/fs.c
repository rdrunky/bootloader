#include <errno.h>
#include <lib/blockdev.h>
#include <lib/fs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"

#define SECTOR		512
#define FUZZ_ROUNDS	1500
#define FUZZ_SPAN	(1024 * 1024)
#define WALK_DEPTH	4

struct image {
	struct blockdev	device;
	uint8_t		*data;
	size_t		size;
};

struct names {
	unsigned int	count;
	bool		seen_kernel;
	bool		seen_link;
};

static bool load_image(const char *path, struct image *image)
{
	image->data = test_load(path, &image->size);

	return image->data != NULL &&
	       blockdev_init_memory(&image->device, "image", image->data, image->size, SECTOR, true) == 0;
}

static uint8_t *tree_file(const char *relative, size_t *size)
{
	char path[512];

	snprintf(path, sizeof(path), "%s/%s", TEST_TREE, relative);

	return test_load(path, size);
}

static bool same_as_tree(struct fs *fs, const char *path, const char *relative)
{
	size_t expected_size;
	uint8_t *expected = tree_file(relative, &expected_size);
	uint8_t *actual = malloc(expected_size + 16);
	size_t done = 0;
	bool same;

	same = expected != NULL && actual != NULL &&
	       fs_read(fs, path, 0, actual, expected_size + 16, &done) == 0 &&
	       done == expected_size && memcmp(actual, expected, expected_size) == 0;

	free(actual);
	free(expected);

	return same;
}

static int count_names(void *context, const struct fs_entry *entry)
{
	struct names *names = context;

	names->count++;
	names->seen_kernel |= strcmp(entry->name, "kernel") == 0 && entry->kind == FS_KIND_FILE;
	names->seen_link |= strcmp(entry->name, "link-to-hello") == 0 && entry->kind == FS_KIND_SYMLINK;

	return 0;
}

static void check_image(const char *path, const char *type, const char *label)
{
	struct image image;
	struct fs_stat stat;
	struct names names;
	uint8_t window[100];
	size_t kernel_size;
	uint8_t *kernel;
	size_t done;
	struct fs *fs;

	CHECK(load_image(path, &image));
	CHECK_EQ(fs_mount(&image.device, &fs), 0);
	CHECK_STR(fs_type(fs), type);
	CHECK_STR(fs_label(fs), label);

	CHECK(same_as_tree(fs, "/hello.txt", "hello.txt"));
	CHECK(same_as_tree(fs, "/boot/kernel", "boot/kernel"));
	CHECK(same_as_tree(fs, "/big/data.bin", "big/data.bin"));
	CHECK(same_as_tree(fs, "/many/file-299.txt", "many/file-299.txt"));
	CHECK(same_as_tree(fs, "/link-to-hello", "hello.txt"));
	CHECK(same_as_tree(fs, "/absolute-link", "boot/config.txt"));
	CHECK(same_as_tree(fs, "/directory-link/c/d/e/file.txt", "deep/a/b/c/d/e/file.txt"));
	CHECK(same_as_tree(fs, "/long-link", "deep/a/b/c/d/e/file.txt"));
	CHECK(same_as_tree(fs, "//deep/./a/b/../b/c/d/e/file.txt", "deep/a/b/c/d/e/file.txt"));

	kernel = tree_file("boot/kernel", &kernel_size);
	CHECK(kernel != NULL);
	CHECK_EQ(fs_read(fs, "/boot/kernel", 4095, window, sizeof(window), &done), 0);
	CHECK_EQ(done, sizeof(window));
	CHECK(memcmp(window, kernel + 4095, sizeof(window)) == 0);
	CHECK_EQ(fs_read(fs, "/boot/kernel", kernel_size - 10, window, sizeof(window), &done), 0);
	CHECK_EQ(done, 10);
	CHECK_EQ(fs_read(fs, "/boot/kernel", kernel_size + 10, window, sizeof(window), &done), 0);
	CHECK_EQ(done, 0);
	free(kernel);

	CHECK_EQ(fs_read(fs, "/sparse.bin", 4 * 1024 * 1024, window, sizeof(window), &done), 0);
	CHECK_EQ(done, sizeof(window));
	CHECK_EQ(window[0] | window[50] | window[99], 0);
	CHECK_EQ(fs_read(fs, "/sparse.bin", 5 * 1024 * 1024, window, sizeof(window), &done), 0);
	CHECK_EQ(done, 1);
	CHECK_EQ(window[0], '!');

	CHECK_EQ(fs_stat(fs, "/boot", &stat), 0);
	CHECK_EQ(stat.kind, FS_KIND_DIRECTORY);
	CHECK_EQ(fs_stat(fs, "/empty", &stat), 0);
	CHECK_EQ(stat.size, 0);
	CHECK_EQ(fs_stat(fs, "/missing", &stat), -ENOENT);
	CHECK_EQ(fs_stat(fs, "/hello.txt/inside", &stat), -ENOTDIR);
	CHECK_EQ(fs_stat(fs, "/loop-a", &stat), -ELOOP);
	CHECK_EQ(fs_stat(fs, "relative", &stat), -EINVAL);
	CHECK_EQ(fs_read(fs, "/boot", 0, window, sizeof(window), &done), -EISDIR);

	memset(&names, 0, sizeof(names));
	CHECK_EQ(fs_list(fs, "/many", count_names, &names), 0);
	CHECK_EQ(names.count, 300);
	memset(&names, 0, sizeof(names));
	CHECK_EQ(fs_list(fs, "/boot", count_names, &names), 0);
	CHECK(names.seen_kernel);
	memset(&names, 0, sizeof(names));
	CHECK_EQ(fs_list(fs, "/", count_names, &names), 0);
	CHECK(names.seen_link);
	CHECK_EQ(fs_list(fs, "/hello.txt", count_names, &names), -ENOTDIR);

	fs_unmount(fs);
	free(image.data);
}

TEST(fs, reads_ext4_with_extents)
{
	check_image(TEST_EXT4, "ext4", "system");
}

TEST(fs, reads_ext4_with_small_blocks_and_meta_groups)
{
	check_image(TEST_EXT4_SMALL, "ext4", "small");
}

TEST(fs, reads_ext2_with_indirect_blocks)
{
	check_image(TEST_EXT2, "ext2", "legacy");
}

static void check_fat_image(const char *path, const char *type)
{
	struct image image;
	struct fs_stat stat;
	struct names names;
	uint8_t window[1500];
	size_t kernel_size;
	uint8_t *kernel;
	size_t done;
	struct fs *fs;

	CHECK(load_image(path, &image));
	CHECK_EQ(fs_mount(&image.device, &fs), 0);
	CHECK_STR(fs_type(fs), type);
	CHECK_STR(fs_label(fs), "TESTVOL");

	CHECK(same_as_tree(fs, "/hello.txt", "hello.txt"));
	CHECK(same_as_tree(fs, "/boot/kernel", "boot/kernel"));
	CHECK(same_as_tree(fs, "/big/data.bin", "big/data.bin"));
	CHECK(same_as_tree(fs, "/sparse.bin", "sparse.bin"));
	CHECK(same_as_tree(fs, "/many/file-299.txt", "many/file-299.txt"));
	CHECK(same_as_tree(fs, "/docs/a-rather-long-file-name-for-directory-entries.txt",
			   "docs/a-rather-long-file-name-for-directory-entries.txt"));
	CHECK(same_as_tree(fs, "/docs/MixedCase.Txt", "docs/MixedCase.Txt"));
	CHECK(same_as_tree(fs, "/BOOT/KERNEL", "boot/kernel"));
	CHECK(same_as_tree(fs, "/Docs/mixedcase.TXT", "docs/MixedCase.Txt"));
	CHECK(same_as_tree(fs, "/boot/../deep/./a/b/c/d/e/file.txt", "deep/a/b/c/d/e/file.txt"));
	CHECK(same_as_tree(fs, "/../hello.txt", "hello.txt"));

	kernel = tree_file("boot/kernel", &kernel_size);
	CHECK(kernel != NULL);
	CHECK_EQ(fs_read(fs, "/boot/kernel", 511, window, sizeof(window), &done), 0);
	CHECK_EQ(done, sizeof(window));
	CHECK(memcmp(window, kernel + 511, sizeof(window)) == 0);
	CHECK_EQ(fs_read(fs, "/boot/kernel", 700000, window, sizeof(window), &done), 0);
	CHECK(memcmp(window, kernel + 700000, sizeof(window)) == 0);
	CHECK_EQ(fs_read(fs, "/boot/kernel", kernel_size - 3, window, sizeof(window), &done), 0);
	CHECK_EQ(done, 3);
	free(kernel);

	CHECK_EQ(fs_stat(fs, "/deep/a", &stat), 0);
	CHECK_EQ(stat.kind, FS_KIND_DIRECTORY);
	CHECK_EQ(fs_stat(fs, "/empty", &stat), 0);
	CHECK_EQ(stat.size, 0);
	CHECK_EQ(fs_stat(fs, "/missing", &stat), -ENOENT);
	CHECK_EQ(fs_stat(fs, "/hello.txt/inside", &stat), -ENOTDIR);
	CHECK_EQ(fs_read(fs, "/boot", 0, window, sizeof(window), &done), -EISDIR);

	memset(&names, 0, sizeof(names));
	CHECK_EQ(fs_list(fs, "/many", count_names, &names), 0);
	CHECK_EQ(names.count, 300);
	memset(&names, 0, sizeof(names));
	CHECK_EQ(fs_list(fs, "/boot", count_names, &names), 0);
	CHECK(names.seen_kernel);
	CHECK_EQ(fs_list(fs, "/hello.txt", count_names, &names), -ENOTDIR);

	fs_unmount(fs);
	free(image.data);
}

TEST(fs, reads_fat12)
{
	check_fat_image(TEST_FAT12, "fat12");
}

TEST(fs, reads_fat16)
{
	check_fat_image(TEST_FAT16, "fat16");
}

TEST(fs, reads_fragmented_fat32)
{
	check_fat_image(TEST_FAT32, "fat32");
}

TEST(fs, refuses_unknown_data)
{
	struct blockdev device;
	uint8_t *zeros = calloc(1, 64 * 1024);
	struct fs *fs;

	CHECK(zeros != NULL);
	CHECK_EQ(blockdev_init_memory(&device, "zeros", zeros, 64 * 1024, SECTOR, true), 0);
	CHECK_EQ(fs_mount(&device, &fs), -ENOENT);
	CHECK_EQ(blockdev_init_memory(&device, "tiny", zeros, 1024, SECTOR, true), 0);
	CHECK_EQ(fs_mount(&device, &fs), -ENOENT);
	free(zeros);
}

static uint64_t random_state = 0x6a09e667f3bcc908ULL;

static int walk_entry(void *context, const struct fs_entry *entry);

struct walker {
	struct fs	*fs;
	char		path[FS_PATH_MAX];
	unsigned int	depth;
	unsigned int	visited;
};

static void walk(struct walker *walker)
{
	if (walker->depth < WALK_DEPTH && walker->visited < 2000) {
		fs_list(walker->fs, walker->path, walk_entry, walker);
	}
}

static int walk_entry(void *context, const struct fs_entry *entry)
{
	struct walker *walker = context;
	size_t length = strlen(walker->path);
	uint8_t sample[64];
	size_t done;

	if (++walker->visited > 2000 || length + strlen(entry->name) + 2 >= sizeof(walker->path)) {
		return 1;
	}

	snprintf(walker->path + length, sizeof(walker->path) - length, "%s%s", length > 1 ? "/" : "",
		 entry->name);
	fs_read(walker->fs, walker->path, entry->size / 2, sample, sizeof(sample), &done);

	if (entry->kind == FS_KIND_DIRECTORY) {
		walker->depth++;
		walk(walker);
		walker->depth--;
	}

	walker->path[length] = '\0';

	return 0;
}

static void fuzz_image(const char *path)
{
	struct image original;
	struct blockdev device;
	uint8_t *work;
	unsigned int round;

	CHECK(load_image(path, &original));
	CHECK(original.size > FUZZ_SPAN);
	work = test_guarded(original.size);
	CHECK(work != NULL);
	memcpy(work, original.data, original.size);
	CHECK_EQ(blockdev_init_memory(&device, "fuzz", work, original.size, SECTOR, true), 0);

	for (round = 0; round < FUZZ_ROUNDS; round++) {
		struct walker walker = { .path = "/" };
		unsigned int count = 1 + test_random(&random_state) % 8;
		struct fs *fs;

		memcpy(work, original.data, FUZZ_SPAN);
		while (count-- > 0) {
			work[test_random(&random_state) % FUZZ_SPAN] = (uint8_t)test_random(&random_state);
		}

		if (fs_mount(&device, &fs) == 0) {
			walker.fs = fs;
			walk(&walker);
			fs_unmount(fs);
		}
	}

	test_release(work, original.size);
	free(original.data);
}

TEST(fs, survives_fuzzed_ext4)
{
	fuzz_image(TEST_EXT4);
}

TEST(fs, survives_fuzzed_ext2)
{
	fuzz_image(TEST_EXT2);
}

TEST(fs, survives_fuzzed_fat16)
{
	fuzz_image(TEST_FAT16);
}

TEST(fs, survives_fuzzed_fat32)
{
	fuzz_image(TEST_FAT32);
}
