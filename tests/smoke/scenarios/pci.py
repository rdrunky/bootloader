import re
import zlib

from support import RAMDISK, check, scenario


def pci_disk(context, machine="virt"):
    return context.kernel(machine=machine, extra=[
        "-drive", f"file={context.data}/autoboot.img,format=raw,if=virtio,snapshot=on",
    ])


def bridged_disks(context):
    return context.kernel(extra=[
        "-nic", "none",
        "-device", "pcie-root-port,id=empty,bus=pcie.0,chassis=1",
        "-device", "pcie-root-port,id=occupied,bus=pcie.0,chassis=2",
        *context.drive("autoboot.img", "system", "virtio-blk-pci,bus=occupied"),
        *context.drive("ext4.img", "data", "virtio-blk-pci,bus=pcie.0"),
    ])


@scenario
def pci_disk_boots_the_default_entry(context):
    with context.machine(pci_disk(context)) as machine:
        machine.expect(r"pci 3 devices ok\n")
        machine.expect(r"disk virtio0 96 MiB ok\n")
        machine.expect(r"boot\.cfg virtio0p1 signed\n")
        machine.expect(r"kernel [\d.]+ KiB at 0x[0-9a-f]+ verified\n")
        machine.expect(r"ramdisk 300 KiB at 0x[0-9a-f]+ verified\n")
        machine.expect(r"payload: boot-verified 1\n")
        ramdisk = machine.expect(r"payload: ramdisk 0x[0-9a-f]+ (\d+) crc32 (0x[0-9a-f]+)\n")
        check(int(ramdisk.group(2), 16) == zlib.crc32(RAMDISK), f"ramdisk differs\n{machine.tail()}")
        machine.expect(r"payload: pci 00:01\.0 1af4:1000 command 0x3\n")
        machine.expect(r"payload: pci 00:02\.0 1af4:1001 command 0x3\n")
        machine.expect("payload: done")


@scenario
def pci_lists_devices_with_resources(context):
    with context.machine(pci_disk(context)) as machine:
        machine.stop_countdown()
        output = machine.run("pci")
        check(re.search(r"00:00\.0  1b36:0008  bridge +-\n", output), f"host bridge missing\n{output}")
        check(re.search(r"1af4:1000  network +virtio-pci\n", output), f"network device not bound\n{output}")
        check(re.search(r"1af4:1001  storage +virtio-pci\n", output), f"disk not bound\n{output}")
        check(re.search(r"bar4  memory  0x1[0-9a-f]{7} +16 KiB, 64-bit, prefetchable", output), f"bar4\n{output}")
        check(re.search(r"bar0  io +0x3eff[0-9a-f]{4} +", output), f"io bar\n{output}")


@scenario
def pci_works_with_the_low_configuration_window(context):
    with context.machine(pci_disk(context, "virt,highmem=off")) as machine:
        machine.stop_countdown()
        disks = machine.run("disks")
        check(re.search(r"virtio0p1 +40 MiB  boot .* fat32 'BOOT'", disks), f"disk over low ecam\n{disks}")


@scenario
def pci_reaches_disks_behind_a_bridge(context):
    with context.machine(bridged_disks(context)) as machine:
        machine.stop_countdown()
        output = machine.run("pci")
        check(re.search(r"02:00\.0  1af4:1042  storage +virtio-pci\n", output), f"bridged disk\n{output}")
        check(re.search(r"00:03\.0  1af4:1001  storage +virtio-pci\n", output), f"root disk\n{output}")
        disks = machine.run("disks")
        check(re.search(r"virtio0p2 +48 MiB  system .* ext4 'system'", disks), f"bridged partitions\n{disks}")
        check(re.search(r"virtio1 +32 MiB  ext4 'system'", disks), f"second disk\n{disks}")
        check("hello" in machine.run("cat virtio1:/hello.txt").lower(), "cannot read the second disk")
