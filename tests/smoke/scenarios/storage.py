import re

from support import PROMPT, check, scenario


@scenario
def storage_lists_gpt_and_mbr_partitions(context):
    arguments = context.kernel(extra=context.drive("gpt.img", "gpt") + context.drive("mbr.img", "mbr"))
    with context.machine(arguments) as machine:
        machine.expect("disk virtio0 64 MiB ok\n")
        machine.expect("disk virtio1 64 MiB ok\n")
        machine.expect(re.escape(PROMPT))
        output = machine.run("disks")
        for pattern in (
            r"p1 +8 MiB  boot +c12a7328-f81f-11d2-ba4b-00a0c93ec93b",
            r"p2 +16 MiB  system +b0070001-2a2a-4c4f-9a3e-0123456789ab",
            r"p3 +38 MiB  data",
            r"p1 +8 MiB  - +mbr 0x0c",
            r"p5 +4 MiB  - +mbr 0x0c",
            r"p6 +4 MiB  - +mbr 0x07",
        ):
            check(re.search(pattern, output), f"disks lacks {pattern}\n{output}")


@scenario
def storage_recovers_from_damaged_gpt(context):
    arguments = context.kernel(extra=context.drive("damaged.img"))
    with context.machine(arguments) as machine:
        machine.expect("disk virtio0 64 MiB ok\n")
        machine.expect("virtio0 primary gpt damaged, backup used\n")
        machine.expect(re.escape(PROMPT))
        output = machine.run("disks")
        check(re.search(r"virtio0p3 +38 MiB  data", output), f"backup partitions missing\n{output}")
