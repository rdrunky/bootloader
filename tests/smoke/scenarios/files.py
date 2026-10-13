import random
import re

from support import PROMPT, check, scenario


def system_disk(context):
    return context.kernel(extra=context.drive("system.img"))


@scenario
def files_show_the_filesystem(context):
    with context.machine(system_disk(context)) as machine:
        machine.expect(re.escape(PROMPT))
        output = machine.run("disks")
        check(re.search(r"virtio0p1 +40 MiB  boot .* fat32 'BOOT'", output), f"no fat32 in disks\n{output}")
        check(re.search(r"virtio0p2 +48 MiB  system .* ext4 'system'", output), f"no ext4 in disks\n{output}")
        output = machine.run("ls virtio0p1:/")
        for name in ("config.txt", "kernel", "payload", "ramdisk"):
            check(re.search(rf"\n  {name} ", output), f"fat listing lacks {name}\n{output}")
        check("kernel /boot/kernel" in machine.run("cat virtio0p1:/CONFIG.TXT"), "fat names are case sensitive")

        output = machine.run("ls virtio0p2:/")
        for name in ("boot", "hello.txt", "link-to-hello", "many", "sparse.bin"):
            check(re.search(rf"\n  {re.escape(name)} ", output), f"ls lacks {name}\n{output}")

        output = machine.run("ls virtio0p2:/boot")
        check(re.search(r"kernel +1.0 MiB  file", output), f"kernel missing\n{output}")
        check("hello from the test tree" in machine.run("cat virtio0p2:/link-to-hello"), "cat through a link failed")
        check("no such file or directory" in machine.run("ls virtio0p2:/missing"), "missing path not reported")
        check("no such device" in machine.run("ls nothing0:/"), "missing device not reported")
        check("too many levels of symbolic links" in machine.run("cat virtio0p2:/loop-a"), "link loop not reported")


@scenario
def files_load_into_free_memory_only(context):
    with context.machine(system_disk(context)) as machine:
        machine.expect(re.escape(PROMPT))
        output = machine.run("load virtio0p2:/boot/kernel")
        match = re.search(r"loaded 1048699 bytes at (0x[0-9a-f]+)", output)
        check(match, f"load failed\n{output}")

        expected = random.Random(1).randbytes(16)
        dump = machine.run(f"md -b {match.group(1)} 16")
        check(" ".join(f"{byte:02x}" for byte in expected) in dump, f"loaded bytes differ\n{dump}")

        image = machine.image_base()
        output = machine.run(f"load virtio0p2:/hello.txt {image:#x}")
        check("no room at" in output, f"loading over the loader was allowed\n{output}")
        output = machine.run(f"load virtio0p2:/hello.txt {match.group(1)}")
        check("loaded 25 bytes" in output, f"reloading into the same place failed\n{output}")
        check("integrity ok" in machine.run("heap"), "heap damaged by loading")
