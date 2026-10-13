import argparse
import pathlib
import subprocess

MIB = 1024 * 1024
SECTOR = 512
ALIGNMENT = MIB

ROLE_TYPES = {
    "system": "B0070001-2A2A-4C4F-9A3E-0123456789AB",
    "recovery": "B0070002-2A2A-4C4F-9A3E-0123456789AB",
    "data": "B0070003-2A2A-4C4F-9A3E-0123456789AB",
    "state": "B0070004-2A2A-4C4F-9A3E-0123456789AB",
}


class Partition:
    def __init__(self, fields):
        self.name, self.role, size, self.filesystem, self.source, self.uuid = fields
        self.size = int(size) * MIB
        self.start = 0


def read_layout(path):
    partitions = []
    for line in pathlib.Path(path).read_text().splitlines():
        if line.strip():
            partitions.append(Partition(line.split()))
    return partitions


def place(partitions):
    offset = ALIGNMENT
    for partition in partitions:
        partition.start = offset
        offset += partition.size
    return offset + ALIGNMENT


def partition_table(partitions):
    lines = ["label: gpt", f"first-lba: {ALIGNMENT // SECTOR}"]
    for partition in partitions:
        lines.append(
            f"start={partition.start // SECTOR}, size={partition.size // SECTOR}, "
            f"type={ROLE_TYPES[partition.role]}, uuid={partition.uuid}, name=\"{partition.name}\""
        )
    return "\n".join(lines) + "\n"


def format_partition(image, partition, root):
    if partition.filesystem == "raw":
        return
    command = ["mkfs.ext4", "-q", "-F", "-E", f"offset={partition.start}", "-L", partition.name]
    if partition.source != "-":
        command += ["-d", str(root / partition.source)]
    subprocess.run(command + [str(image), f"{partition.size // 1024}k"], check=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("layout")
    parser.add_argument("root")
    parser.add_argument("output")
    options = parser.parse_args()

    partitions = read_layout(options.layout)
    image = pathlib.Path(options.output)
    image.unlink(missing_ok=True)
    with open(image, "wb") as disk:
        disk.truncate(place(partitions))

    subprocess.run(["sfdisk", "--quiet", str(image)], input=partition_table(partitions), text=True, check=True,
                   stdout=subprocess.DEVNULL)

    for partition in partitions:
        format_partition(image, partition, pathlib.Path(options.root))


if __name__ == "__main__":
    main()
