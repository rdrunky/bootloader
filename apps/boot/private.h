#ifndef BOOT_PRIVATE_H
#define BOOT_PRIVATE_H

#include <lib/blockdev.h>
#include <lib/crypto.h>
#include <lib/fs.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CONFIG_ENTRIES_MAX	16
#define CONFIG_NAME_MAX		32
#define CONFIG_TEXT_MAX		256
#define CONFIG_MODE_MAX		16
#define CONFIG_PATH_MAX		32
#define CONFIG_SIZE_MAX		(64 * 1024)
#define TIMEOUT_NEVER		(-1)
#define RECOVERY_REASON_MAX	224
#define SLOT_COUNT		2
#define LAYOUT_NAME_MAX		16

enum role {
	ROLE_SYSTEM,
	ROLE_RECOVERY,
	ROLE_DATA,
	ROLE_STATE,
	ROLE_COUNT,
};

enum signature {
	SIGNATURE_MISSING,
	SIGNATURE_INVALID,
	SIGNATURE_VALID,
};

struct digest {
	bool		present;
	uint8_t		value[SHA256_SIZE];
};

struct boot_entry {
	char		name[CONFIG_NAME_MAX];
	char		kernel[CONFIG_TEXT_MAX];
	char		ramdisk[CONFIG_TEXT_MAX];
	char		arguments[CONFIG_TEXT_MAX];
	char		mode[CONFIG_MODE_MAX];
	struct digest	kernel_digest;
	struct digest	ramdisk_digest;
	unsigned int	line;
};

struct boot_config {
	char			device[BLOCKDEV_NAME_MAX];
	char			path[CONFIG_PATH_MAX];
	char			default_entry[CONFIG_NAME_MAX];
	unsigned int		default_line;
	int			timeout;
	uint32_t		version;
	bool			verify_required;
	bool			detected;
	enum signature		signature;
	size_t			count;
	struct boot_entry	entries[CONFIG_ENTRIES_MAX];
};

struct location {
	struct blockdev	*device;
	struct fs	*fs;
	const char	*path;
};

struct boot_request {
	const char		*kernel;
	const char		*ramdisk;
	const char		*arguments;
	const char		*mode;
	const struct digest	*kernel_digest;
	const struct digest	*ramdisk_digest;
	bool			verify_required;
	bool			trusted;
};

int parse_number(const char *text, uint64_t *value);
int parse_digest(const char *text, size_t length, uint8_t digest[SHA256_SIZE]);
void format_digest(const uint8_t digest[SHA256_SIZE], char *text);

int location_open(const char *spec, struct location *location);
void location_close(struct location *location);

void layout_scan(void);
bool layout_present(void);
bool layout_member(const struct blockdev *device);
struct blockdev *layout_partition(enum role role, unsigned int slot);
unsigned int layout_slot(void);
void layout_select(unsigned int slot);
bool layout_slotted(enum role role);
unsigned int layout_slot_of(const struct blockdev *partition);
void layout_name(enum role role, unsigned int slot, char *name, size_t size);
const char *layout_role_name(enum role role);
char layout_slot_name(unsigned int slot);
struct blockdev *layout_resolve(const char *name);

void slots_load(void);
int slots_pick(void);
void slots_attempt(unsigned int slot);
void slots_fail(unsigned int slot);
int slots_confirm(unsigned int slot);
int slots_activate(unsigned int slot);
void slots_print(void);

void security_init(void);
bool security_locked(void);
uint32_t security_minimum_version(void);
const char *security_policy(void);
const char *signature_name(enum signature signature);
enum signature security_check(struct fs *fs, const char *path, const void *data, size_t length);

int config_parse(const char *text, size_t length, struct boot_config *config, unsigned int *line);
const struct boot_entry *config_find(const struct boot_config *config, const char *name);
bool boot_mode_known(const char *mode);

int boot_start(const struct boot_request *request);
const char *boot_error(void);

void autoboot(void);
const struct boot_config *autoboot_config(void);
int autoboot_entry(const struct boot_config *config, const struct boot_entry *entry);

void recovery_run(const char *reason);
int diagnostics_run(void);

#endif
