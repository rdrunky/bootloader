import re

from support import check, scenario

UUIDS = {
    "system-a": "b0070a00-5107-4a00-8000-00000000000a",
    "system-b": "b0070b00-5107-4b00-8000-00000000000b",
    "recovery": "b0070c00-2ec0-4c00-8000-00000000000c",
    "data": "b0070d00-da7a-4d00-8000-00000000000d",
    "state": "b0070e00-57a7-4e00-8000-00000000000e",
}


def check_handoff(machine, slot):
    machine.expect(rf"payload: boot-slot '{slot}'")
    machine.expect("payload: done")
    partitions = dict(re.findall(r"payload: partition (\w+) (\S+)\n", machine.output))
    expected = {
        "system": UUIDS[f"system-{slot}"],
        "recovery": UUIDS["recovery"],
        "data": UUIDS["data"],
        "state": UUIDS["state"],
    }
    check(partitions == expected, f"partitions differ: {partitions}\n{machine.tail()}")
    check(list(partitions) == list(expected), f"partitions are not in layout order\n{machine.tail()}")


@scenario
def phone_boots_from_slot_a(context):
    with context.machine(context.phone()) as machine:
        machine.expect(r"boot\.cfg system-a signed\n")
        machine.expect(r"kernel [\d.]+ KiB at 0x[0-9a-f]+ verified\n")
        machine.expect(r"ramdisk 300 KiB at 0x[0-9a-f]+ verified\n")
        check_handoff(machine, "a")


@scenario
def phone_boots_slot_b_by_role(context):
    with context.machine(context.phone()) as machine:
        machine.stop_countdown()
        machine.send("boot system-b:/boot/kernel -r system-b:/boot/ramdisk\r")
        machine.expect(r"kernel [\d.]+ KiB at 0x[0-9a-f]+ loaded\n")
        check_handoff(machine, "b")


@scenario
def phone_reaches_every_role(context):
    with context.machine(context.phone()) as machine:
        machine.stop_countdown()
        check(re.search(r"\n  kernel +", machine.run("ls system:/boot")), "system role is not reachable")
        check(re.search(r"\n  boot\.cfg +", machine.run("ls system-b:/boot")), "slot b is not reachable")
        check("hello from the test tree" in machine.run("cat data:/hello.txt"), "data role is not reachable")
        check("mode recovery" in machine.run("cat recovery:/boot/boot.cfg"), "recovery role is not reachable")
        disks = machine.run("disks")
        for name in UUIDS:
            check(re.search(rf"virtio0p\d +\d+ MiB  {name} ", disks), f"{name} missing from disks\n{disks}")
