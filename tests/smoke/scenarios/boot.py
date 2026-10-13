import re
import zlib

from support import PROMPT, RAMDISK, check, scenario

COMMAND = 'boot virtio0p2:/boot/payload -r virtio0p2:/boot/ramdisk -a "console=serial quiet" -m recovery'
RANDOM = ["-device", "virtio-rng-device"]
KERNEL_ALIGN = 2 << 20


def check_handoff(context, machine_type, el, command=COMMAND, cpu="cortex-a57", extra=(), source=None):
    arguments = context.kernel(machine=machine_type, cpu=cpu, extra=[*context.drive("system.img"), *extra])
    with context.machine(arguments) as machine:
        if source is not None:
            machine.expect(rf"random {source} ok\n")
        machine.expect(re.escape(PROMPT))
        machine.send(command + "\r")
        machine.expect(r"kernel [\d.]+ KiB at 0x[0-9a-f]+ loaded\n")
        machine.expect(r"ramdisk 300 KiB at 0x[0-9a-f]+ loaded\n")
        entry = int(machine.expect(rf"jump (0x[0-9a-f]+) el{el}\n").group(1), 16)

        machine.expect(rf"payload: el{el} x0=0x[0-9a-f]+ x1=0 x2=0 x3=0 mmu off")
        check(entry % KERNEL_ALIGN == 0, f"the kernel is not 2 MiB aligned\n{machine.tail()}")
        machine.expect(r"payload: boot-mode 'recovery'")
        machine.expect(r"payload: boot-verified 0\n")
        machine.expect(r"payload: boot-args 'console=serial quiet'")
        if source is not None:
            machine.expect(r"payload: boot-seed 64 bytes crc32 0x[0-9a-f]+\n")
        else:
            machine.expect(r"payload: no boot-seed\n")
        machine.expect(r"payload: boot-log \d+ bytes\n")
        machine.expect(rf"payload: boot-log first 'cpu el{el} ok'\n")
        machine.expect(rf"payload: boot-log last 'jump {entry:#x} el{el}'\n")
        ramdisk = machine.expect(r"payload: ramdisk 0x[0-9a-f]+ (\d+) crc32 (0x[0-9a-f]+)\n")
        check(int(ramdisk.group(1)) == len(RAMDISK), f"ramdisk size differs\n{machine.tail()}")
        check(int(ramdisk.group(2), 16) == zlib.crc32(RAMDISK), f"ramdisk content differs\n{machine.tail()}")
        machine.expect(r"payload: virtio-mmio 0x[0-9a-f]+ device 2 status 0\n")
        machine.expect("payload: done")


@scenario
def boot_hands_off_at_el1(context):
    check_handoff(context, "virt", 1)


@scenario
def boot_hands_off_at_el2(context):
    check_handoff(context, "virt,virtualization=on", 2)


@scenario
def boot_loads_from_fat32(context):
    command = 'boot virtio0p1:/PAYLOAD -r virtio0p1:/ramdisk -a "console=serial quiet" -m recovery'
    check_handoff(context, "virt,virtualization=on", 2, command)


@scenario
def boot_hands_over_a_device_seed(context):
    check_handoff(context, "virt", 1, extra=RANDOM, source="virtio")


@scenario
def boot_hands_over_a_cpu_seed(context):
    check_handoff(context, "virt", 1, cpu="max", source="cpu")


@scenario
def boot_places_the_kernel_at_random(context):
    entries = set()
    for _ in range(3):
        arguments = context.kernel(memory="2G", extra=[*context.drive("system.img"), *RANDOM])
        with context.machine(arguments) as machine:
            machine.expect(re.escape(PROMPT))
            machine.send(COMMAND + "\r")
            entry = int(machine.expect(r"jump (0x[0-9a-f]+) el1\n").group(1), 16)
            machine.expect("payload: done")
            check(entry % KERNEL_ALIGN == 0, f"the kernel is not 2 MiB aligned\n{machine.tail()}")
            entries.add(entry)
    check(len(entries) > 1, f"the kernel always starts at {sorted(entries)}")


@scenario
def boot_refuses_bad_kernels_and_cleans_up(context):
    with context.machine(context.kernel(extra=context.drive("system.img"))) as machine:
        machine.expect(re.escape(PROMPT))
        before = machine.run("mem")
        output = machine.run("boot virtio0p2:/hello.txt")
        check("kernel virtio0p2:/hello.txt exec format error" in output, f"text file booted\n{output}")
        output = machine.run("boot virtio0p2:/missing")
        check("no such file or directory" in output, f"missing kernel not reported\n{output}")
        output = machine.run("boot virtio0p2:/boot/payload -r virtio0p2:/boot")
        check("ramdisk virtio0p2:/boot is a directory" in output, f"directory ramdisk accepted\n{output}")
        check("usage: boot" in machine.run("boot virtio0p2:/boot/payload -m sideways"), "bad mode accepted")
        after = machine.run("mem")
        check("kernel" not in after and "ramdisk" not in after, f"memory was not released\n{after}")
        check(before.count("\n") == after.count("\n"), f"memory map changed\n{before}\n{after}")
