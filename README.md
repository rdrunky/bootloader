# bootloader

An ARM64 bootloader for QEMU's `virt` machine. Boot information is passed to the kernel through a device tree.

Supports VirtIO over MMIO and PCI, MBR/GPT partitions, read-only EXT2/4 and FAT12/16/32 filesystems, and A/B slots with persistent try counters.

Locked mode requires an Ed25519-signed `boot.cfg` and SHA-256 hashes for the kernel and any ramdisk. Developer mode allows unsigned configurations and manual booting. Policy and minimum configuration version come from QEMU's `fw_cfg`; the default policy is developer.

Interrupting autoboot opens a serial shell for manual booting and diagnostics.

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
make        # build binary (build/boot.bin)
make demo   # run in QEMU with autoboot
make phone  # run verified A/B boot in QEMU
make test   # run host unit tests
make smoke  # run QEMU integration scenarios
```

The build generates the development signing key. Demo and test targets prepare disk images, hashes and signatures automatically. `make phone` runs in locked mode. The integration tests boot the included ARM64 payload.

For the Gentoo toolchain above, use `make CROSS=aarch64-unknown-linux-gnu-`.

## Donations

If you want to support me and my project, feel free to donate. Any support would really help me upgrade my PC (currently running on an old i5-6600 with 12GB DDR4 RAM):

- **BTC:** `bc1qzpvtleqhzqldpulh62hzwclvp3yfmf23f0pz6w`
- **LTC:** `LdmacKCVqEf1prn1GtDemySiFbKeGaegzY`
- **ETH:** `0x53dA4619B015E117a4124c656eb2E5182002Ea73`
- **SOL:** `38wAjUhd6ioaLkS9FAw7UcLWDwuneXgxKvRtD86468Mf`

**Goal:** $145 of $900 saved

## License

Copyright (C) 2026 rdrunky

GPL-2.0. See LICENSE.
