import hashlib
import re
import zlib

from support import COUNTDOWN, PROMPT, RAMDISK, check, scenario


@scenario
def autoboot_starts_the_default_entry_verified(context):
    arguments = context.kernel(machine="virt,virtualization=on", extra=context.drive("autoboot.img"))
    with context.machine(arguments) as machine:
        machine.expect(r"boot\.cfg virtio0p1 signed\n")
        machine.expect(rf"starting system in 2 s, {COUNTDOWN}\n")
        machine.expect(r"kernel [\d.]+ KiB at 0x[0-9a-f]+ verified\n")
        machine.expect(r"ramdisk 300 KiB at 0x[0-9a-f]+ verified\n")
        machine.expect(r"jump 0x[0-9a-f]+ el2\n")
        machine.expect(r"payload: el2 ")
        machine.expect(r"payload: boot-mode 'normal'")
        machine.expect(r"payload: boot-verified 1\n")
        machine.expect(r"payload: boot-args 'console=serial quiet'")
        ramdisk = machine.expect(r"payload: ramdisk 0x[0-9a-f]+ (\d+) crc32 (0x[0-9a-f]+)\n")
        check(int(ramdisk.group(2), 16) == zlib.crc32(RAMDISK), f"ramdisk differs\n{machine.tail()}")
        machine.expect("payload: done")


@scenario
def autoboot_stops_for_a_key_and_offers_entries(context):
    with context.machine(context.kernel(extra=context.drive("autoboot.img"))) as machine:
        machine.stop_countdown()
        output = machine.run("entries")
        check(re.search(r"\* system +normal +/payload", output), f"default entry not marked\n{output}")
        check(re.search(r"  recovery +recovery +virtio0p2:/boot/payload", output), f"recovery missing\n{output}")
        machine.send("boot recovery\r")
        machine.expect(r"kernel [\d.]+ KiB at 0x[0-9a-f]+ loaded\n")
        machine.expect(r"jump 0x[0-9a-f]+ el1\n")
        machine.expect(r"payload: boot-mode 'recovery'")
        machine.expect(r"payload: boot-verified 0\n")
        machine.expect(r"payload: boot-args 'recovery console=serial'")
        machine.expect(r"payload: no ramdisk")


@scenario
def autoboot_reports_broken_entries(context):
    with context.machine(context.kernel(extra=context.drive("autoboot.img"))) as machine:
        machine.stop_countdown()
        output = machine.run("boot broken")
        check("kernel virtio0p1:/missing no such file or directory" in output, f"broken entry\n{output}")
        check("boot: no entry 'nothing'" in machine.run("boot nothing"), "unknown entry not reported")
        check(machine.run("echo alive").count("alive") == 2, "shell died")


@scenario
def autoboot_refuses_a_tampered_kernel(context):
    with open(f"{context.data}/payload/payload.bin", "rb") as source:
        digest = hashlib.sha256(source.read()).hexdigest()

    with context.machine(context.kernel(extra=context.drive("autoboot.img"))) as machine:
        machine.stop_countdown()
        before = machine.run("mem")
        output = machine.run("boot tampered")
        check("kernel virtio0p1:/payload sha256 mismatch" in output, f"tampered kernel ran\n{output}")
        check(f"sha256 {digest} found" in output, f"actual digest not shown\n{output}")
        check("\npayload: " not in output, f"payload started\n{output}")
        check(machine.run("mem").count("\n") == before.count("\n"), "memory was not released")
        check(f"{digest}  virtio0p1:/payload" in machine.run("sha256 virtio0p1:/payload"), "sha256 command differs")


@scenario
def autoboot_detects_a_kernel_without_configuration(context):
    with context.machine(context.kernel(extra=context.folder_drive("detect"))) as machine:
        machine.expect(r"kernel virtio0p1:/kernel detected\n")
        machine.expect(rf"starting detected in 3 s, {COUNTDOWN}\n")
        machine.expect(r"payload: el1 ")
        machine.expect(r"payload: boot-mode 'normal'")
        machine.expect("payload: done")


@scenario
def autoboot_waits_in_the_shell_without_anything(context):
    with context.machine(context.kernel()) as machine:
        machine.expect("nothing to boot\n")
        machine.expect(re.escape(PROMPT))
        check("no boot.cfg was found" in machine.run("boot"), "boot without a configuration")
