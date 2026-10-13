import os
import re

from support import LOCKED, PROMPT, check, scenario, tamper

DEVELOPER_COMMANDS = ("md", "mw", "load", "fault")


def rollback(value):
    return ["-fw_cfg", f"name=opt/boot/rollback,string={value}"]


@scenario
def locked_phone_accepts_the_current_version(context):
    with context.machine(context.phone(extra=[*LOCKED, *rollback(1)])) as machine:
        machine.expect(r"(?m)^rollback 1$")
        machine.expect(r"boot\.cfg system-a signed\n")
        machine.expect(r"slot a 2 tries left\n")
        machine.expect("payload: done")


@scenario
def locked_phone_refuses_older_versions(context):
    with context.machine(context.phone(extra=[*LOCKED, *rollback(2)])) as machine:
        machine.expect(r"boot\.cfg system-a version 1 below 2, refused\n")
        machine.expect(r"slot a unbootable\n")
        machine.expect(r"boot\.cfg system-b version 1 below 2, refused\n")
        machine.expect(r"slot b unbootable\n")
        machine.expect(r"no bootable slot\n")
        machine.expect(r"boot\.cfg recovery version 1 below 2, refused\n")
        machine.expect("nothing to boot\n")
        check("virtio0p" not in machine.output, f"layout partitions were scanned again\n{machine.tail()}")


@scenario
def rollback_fuse_fails_closed(context):
    with context.machine(context.phone(extra=[*LOCKED, *rollback("soon")])) as machine:
        machine.expect("rollback fuse unreadable\n")
        machine.expect("nothing to boot\n")

    with context.machine(context.phone(extra=rollback(2))) as machine:
        machine.expect(r"boot\.cfg system-a version 1 below 2, allowed\n")
        machine.expect("payload: done")


@scenario
def locked_phone_boots_verified(context):
    with context.machine(context.phone(extra=LOCKED)) as machine:
        machine.expect(r"(?m)^locked$")
        machine.expect(r"boot\.cfg system-a signed\n")
        machine.expect(r"kernel [\d.]+ KiB at 0x[0-9a-f]+ verified\n")
        machine.expect(r"ramdisk 300 KiB at 0x[0-9a-f]+ verified\n")
        machine.expect(r"jump 0x[0-9a-f]+ el1\n")
        machine.expect(r"payload: boot-verified 1\n")
        machine.expect(r"payload: boot-policy 'locked'")
        machine.expect(r"payload: boot-slot 'a'")
        machine.expect("payload: done")


@scenario
def locked_phone_falls_back_when_slot_a_is_tampered(context):
    image = context.phone_copy("phone-tampered")
    try:
        tamper(image, "system-a")
        with context.machine(context.phone(image, LOCKED)) as machine:
            machine.expect(r"boot\.cfg system-a bad signature, refused\n")
            machine.expect(r"slot a unbootable\n")
            machine.expect(r"boot\.cfg system-b signed\n")
            machine.expect(r"payload: boot-verified 1\n")
            machine.expect(r"payload: boot-slot 'b'")
    finally:
        os.remove(image)


@scenario
def locked_refuses_an_unsigned_config(context):
    with context.machine(context.kernel(extra=[*LOCKED, *context.folder_drive("unsigned")])) as machine:
        machine.expect(r"boot\.cfg virtio0p1 not signed, refused\n")
        machine.expect("nothing to boot\n")
        machine.expect(re.escape(PROMPT))


@scenario
def locked_refuses_a_tampered_config(context):
    with context.machine(context.kernel(extra=[*LOCKED, *context.folder_drive("tampered")])) as machine:
        machine.expect(r"boot\.cfg virtio0p1 bad signature, refused\n")
        machine.expect("nothing to boot\n")


@scenario
def locked_does_not_detect_kernels(context):
    with context.machine(context.kernel(extra=[*LOCKED, *context.folder_drive("detect")])) as machine:
        machine.expect("nothing to boot\n")
        check("detected" not in machine.output, f"a kernel was detected\n{machine.tail()}")


@scenario
def locked_requires_a_digest_for_every_file(context):
    with context.machine(context.kernel(extra=[*LOCKED, *context.drive("autoboot.img")])) as machine:
        machine.stop_countdown()
        output = machine.run("boot recovery")
        check("kernel virtio0p2:/boot/payload sha256 required" in output, f"recovery started\n{output}")
        check("\npayload: " not in output, f"payload started\n{output}")


@scenario
def locked_shell_keeps_only_safe_commands(context):
    with context.machine(context.phone(extra=LOCKED)) as machine:
        machine.stop_countdown()
        listing = machine.run("help")
        for name in DEVELOPER_COMMANDS:
            check(not re.search(rf"\n  {name} ", listing), f"{name} is listed\n{listing}")
            output = machine.run(name)
            check(f"{name}: only available on a developer machine" in output, f"{name} ran\n{output}")
        check("help: no command 'mw'" in machine.run("help mw"), "mw is described")
        output = machine.run("boot system-b:/boot/kernel")
        check("a locked machine starts only entries of a signed boot.cfg" in output, f"kernel by path\n{output}")
        check("system-a:/boot/boot.cfg, signed" in machine.run("entries"), "entries do not show the signature")
        check("hello from the test tree" in machine.run("cat data:/hello.txt"), "files are not readable")


@scenario
def developer_boots_an_unsigned_config_unverified(context):
    with context.machine(context.kernel(extra=context.folder_drive("unsigned"))) as machine:
        machine.expect(r"(?m)^developer$")
        machine.expect(r"boot\.cfg virtio0p1 not signed\n")
        machine.expect(r"kernel [\d.]+ KiB at 0x[0-9a-f]+ sha256 ok\n")
        machine.expect(r"ramdisk 300 KiB at 0x[0-9a-f]+ sha256 ok\n")
        machine.expect(r"jump 0x[0-9a-f]+ el1\n")
        machine.expect(r"payload: boot-verified 0\n")
        machine.expect(r"payload: boot-policy 'developer'")


@scenario
def developer_warns_about_a_tampered_config(context):
    with context.machine(context.kernel(extra=context.folder_drive("tampered"))) as machine:
        machine.expect(r"boot\.cfg virtio0p1 bad signature\n")
        machine.expect(r"kernel [\d.]+ KiB at 0x[0-9a-f]+ sha256 ok\n")
        machine.expect(r"payload: boot-mode 'recovery'")
        machine.expect(r"payload: boot-verified 0\n")


@scenario
def policy_fuse_decides_the_mode(context):
    developer = ["-fw_cfg", "name=opt/boot/policy,string=developer"]
    with context.machine(context.kernel(extra=developer)) as machine:
        machine.expect(r"(?m)^developer$")
        machine.expect(re.escape(PROMPT))
        check("unreadable" not in machine.output, f"developer fuse was not accepted\n{machine.tail()}")

    unknown = ["-fw_cfg", "name=opt/boot/policy,string=open"]
    with context.machine(context.kernel(extra=unknown)) as machine:
        machine.expect("policy fuse unreadable\n")
        machine.expect(r"(?m)^locked$")
