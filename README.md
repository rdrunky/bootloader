# bootloader

An ARM64 bootloader for QEMU's `virt` machine. Boot information is passed to the kernel through a device tree.

Supports VirtIO over MMIO and PCI, MBR/GPT partitions, read-only EXT2/4 and FAT12/16/32 filesystems, and A/B slots with persistent try counters.

Locked mode requires an Ed25519-signed `boot.cfg` and SHA-256 hashes for the kernel and any ramdisk. Developer mode allows unsigned configurations and manual booting. Policy and minimum configuration version come from QEMU's `fw_cfg`; the default policy is developer.

Interrupting autoboot opens a shell for manual booting and diagnostics.

## Dependencies

- make and a host C compiler
- aarch64-linux-gnu toolchain (gcc, binutils)
- qemu-system-aarch64
- python3 with cryptography
- sfdisk, mkfs.ext4 (for generating test disks)

```sh
# Arch
pacman -S aarch64-linux-gnu-gcc qemu-system-aarch64 python-cryptography e2fsprogs util-linux

# Void
xbps-install -S cross-aarch64-linux-gnu qemu-system-aarch64 python3-cryptography e2fsprogs util-linux

# Gentoo
crossdev -s4 -t aarch64-unknown-linux-gnu
emerge --ask app-emulation/qemu dev-python/cryptography sys-fs/e2fsprogs
```

## Usage

```sh
make        # build the ARM64 binary (build/boot.bin)
make run    # alias for make demo
make demo   # run the autoboot demo in QEMU
make phone  # run the A/B demo in QEMU (locked mode)
make test   # run C unit tests on the host
make smoke  # run integration tests in QEMU
```

For the Gentoo toolchain above, use `make CROSS=aarch64-unknown-linux-gnu-`.

## License

Copyright (C) 2026 rdrunky

GPL-2.0. See LICENSE.
