import os
import re

from support import LOCKED, MIB, PHONE_PARTITIONS, check, scenario, tamper

RECORD_SECTOR = 512
RECORD_SEQUENCE = 12


def tries_text(tries):
    return f"{tries} {'try' if tries == 1 else 'tries'}"


def boot_slot(context, image, slot, tries):
    with context.machine(context.phone(image, snapshot=False)) as machine:
        machine.expect(rf"slot {slot} {tries_text(tries)} left\n")
        machine.expect("payload: done")


def newest_record(image):
    with open(image, "rb") as disk:
        disk.seek(PHONE_PARTITIONS["state"][0] * MIB)
        records = disk.read(2 * RECORD_SECTOR)
    sequences = [int.from_bytes(records[copy * RECORD_SECTOR + RECORD_SEQUENCE:][:4], "little") for copy in (0, 1)]
    return sequences.index(max(sequences))


@scenario
def slots_count_tries_and_fall_back(context):
    image = context.phone_copy("phone-tries")
    try:
        for slot, tries in (("a", 2), ("a", 1), ("a", 0), ("b", 2)):
            with context.machine(context.phone(image, snapshot=False)) as machine:
                if slot == "b":
                    machine.expect(r"slot a out of tries\n")
                machine.expect(rf"slot {slot} {tries_text(tries)} left\n")
                machine.expect(rf"payload: boot-slot '{slot}'")
                machine.expect("payload: done")
    finally:
        os.remove(image)


@scenario
def slots_survive_a_torn_record_write(context):
    image = context.phone_copy("phone-torn")
    try:
        boot_slot(context, image, "a", 2)
        boot_slot(context, image, "a", 1)
        with open(image, "r+b") as disk:
            disk.seek(PHONE_PARTITIONS["state"][0] * MIB + newest_record(image) * RECORD_SECTOR + 17)
            disk.write(b"\x07")
        boot_slot(context, image, "a", 1)
    finally:
        os.remove(image)


@scenario
def slot_command_confirms_and_switches(context):
    image = context.phone_copy("phone-switch")
    try:
        with context.machine(context.phone(image, snapshot=False)) as machine:
            machine.stop_countdown()
            output = machine.run("slot")
            check("  a  priority 15, 3 tries, unconfirmed, next\n" in output, f"slot a\n{output}")
            check("  b  priority 14, 3 tries, unconfirmed\n" in output, f"slot b\n{output}")
            output = machine.run("slot confirm")
            check("  a  priority 15, 3 tries, confirmed, next\n" in output, f"slot a was not confirmed\n{output}")
            machine.send("reset\r")
            machine.expect(r"slot a confirmed\n")
            machine.expect(r"payload: boot-slot 'a'")

        with context.machine(context.phone(image, snapshot=False)) as machine:
            machine.stop_countdown()
            output = machine.run("slot b")
            check("  b  priority 15, 3 tries, unconfirmed, next\n" in output, f"slot b is not next\n{output}")
            check("  a  priority 14, 3 tries, confirmed\n" in output, f"slot a kept its priority\n{output}")
            check("usage: slot" in machine.run("slot c"), "unknown slot accepted")
            machine.send("reset\r")
            machine.expect(r"slot b 2 tries left\n")
            machine.expect(r"payload: boot-slot 'b'")
    finally:
        os.remove(image)


@scenario
def slots_fall_back_when_a_kernel_is_corrupt(context):
    image = context.phone_copy("phone-kernel")
    try:
        tamper(image, "system-a", b"payload: done", b"payload: dOne")
        with context.machine(context.phone(image, LOCKED)) as machine:
            machine.expect(r"boot\.cfg system-a signed\n")
            machine.expect(r"slot a 2 tries left\n")
            machine.expect(r"kernel system-a:/boot/kernel sha256 mismatch\n")
            machine.expect(r"slot a unbootable\n")
            machine.expect(r"boot\.cfg system-b signed\n")
            machine.expect(r"slot b 2 tries left\n")
            machine.expect(r"payload: boot-slot 'b'")
            machine.expect("payload: done")
    finally:
        os.remove(image)


@scenario
def locked_phone_uses_recovery_when_both_slots_fail(context):
    image = context.phone_copy("phone-broken")
    try:
        tamper(image, "system-a")
        tamper(image, "system-b")
        with context.machine(context.phone(image, LOCKED)) as machine:
            machine.expect(r"boot\.cfg system-a bad signature, refused\n")
            machine.expect(r"slot a unbootable\n")
            machine.expect(r"boot\.cfg system-b bad signature, refused\n")
            machine.expect(r"slot b unbootable\n")
            machine.expect(r"no bootable slot\n")
            machine.expect(r"boot\.cfg recovery signed\n")
            machine.expect(r"payload: boot-mode 'recovery'")
            machine.expect(r"payload: boot-verified 1\n")
            check(not re.search(r"slot \w \d+ tr", machine.output), f"a slot was tried\n{machine.tail()}")
    finally:
        os.remove(image)
