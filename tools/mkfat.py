import argparse
import pathlib
import struct

SECTOR = 512
ENTRY = 32
ATTRIBUTE_DIRECTORY = 0x10
ATTRIBUTE_VOLUME = 0x08
ATTRIBUTE_LONG_NAME = 0x0F
LOWER_BASE = 0x08
LOWER_EXTENSION = 0x10
LONG_NAME_UNITS = 13
SHORT_CHARACTERS = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!#$%&'()-@^_`{}~")
JUMP = b"\xeb\x58\x90"
OEM_NAME = b"BOOT2.0 "
BOOT_SIGNATURE = b"\x55\xaa"
MEDIA_FIXED = 0xF8
DRIVE_NUMBER = 0x80
EXTENDED_SIGNATURE = 0x29
VOLUME_SERIAL = 0x20261005
FSINFO_LEAD = 0x41615252
FSINFO_STRUCTURE = 0x61417272
FSINFO_UNKNOWN = 0xFFFFFFFF

LAYOUTS = {
    "fat12": {"cluster": 8, "reserved": 1, "root": 512, "bits": 12},
    "fat16": {"cluster": 4, "reserved": 1, "root": 512, "bits": 16},
    "fat32": {"cluster": 1, "reserved": 32, "root": 0, "bits": 32},
}


def checksum(short):
    total = 0
    for byte in short:
        total = (((total & 1) << 7) + (total >> 1) + byte) & 0xFF
    return total


def short_part(text, length):
    return "".join(c if c in SHORT_CHARACTERS else "_" for c in text.upper())[:length]


def split_name(name):
    if "." in name[1:]:
        base, extension = name.rsplit(".", 1)
        return base, extension
    return name, ""


def plain_short(name):
    base, extension = split_name(name)
    if not 1 <= len(base) <= 8 or len(extension) > 3:
        return None
    for part in (base, extension):
        if part != part.lower() and part != part.upper():
            return None
        if any(c.upper() not in SHORT_CHARACTERS for c in part):
            return None
    flags = (LOWER_BASE if base != base.upper() else 0) | (LOWER_EXTENSION if extension != extension.upper() else 0)
    return (base.upper().ljust(8) + extension.upper().ljust(3)).encode(), flags


def alias(name, taken):
    base, extension = split_name(name)
    base = short_part(base, 8) or "_"
    extension = short_part(extension, 3)
    for number in range(1, 1000000):
        suffix = f"~{number}"
        candidate = (base[: 8 - len(suffix)] + suffix).ljust(8) + extension.ljust(3)
        if candidate not in taken:
            return candidate.encode()
    raise ValueError(f"no short name left for {name}")


def long_entries(name, short):
    units = list(name.encode("utf-16-le"))
    units = [units[i] | units[i + 1] << 8 for i in range(0, len(units), 2)] + [0]
    while len(units) % LONG_NAME_UNITS:
        units.append(0xFFFF)
    count = len(units) // LONG_NAME_UNITS
    entries = []
    for index in range(count, 0, -1):
        part = units[(index - 1) * LONG_NAME_UNITS: index * LONG_NAME_UNITS]
        order = index | (0x40 if index == count else 0)
        entries.append(
            struct.pack("<B5HBBB6HH2H", order, *part[0:5], ATTRIBUTE_LONG_NAME, 0, checksum(short), *part[5:11], 0,
                        *part[11:13])
        )
    return entries


def short_entry(short, attribute, flags, cluster, size):
    return struct.pack("<11sBBBHHHHHHHI", short, attribute, flags, 0, 0, 0, 0, cluster >> 16, 0, 0,
                       cluster & 0xFFFF, size)


class Node:
    def __init__(self, path, name):
        self.path = path
        self.name = name
        self.directory = path.is_dir()
        self.children = []
        self.clusters = []
        if self.directory:
            for child in sorted(path.iterdir()):
                if not child.is_symlink():
                    self.children.append(Node(child, child.name))

    def data(self):
        return b"" if self.directory else self.path.read_bytes()


class Volume:
    def __init__(self, kind, size, label, fragment):
        layout = LAYOUTS[kind]
        self.kind = kind
        self.bits = layout["bits"]
        self.cluster_sectors = layout["cluster"]
        self.cluster_size = self.cluster_sectors * SECTOR
        self.reserved = layout["reserved"]
        self.root_entries = layout["root"]
        self.total = size // SECTOR
        self.label = label.upper().ljust(11)[:11].encode()
        self.fragment = fragment
        self.root_sectors = self.root_entries * ENTRY // SECTOR
        self.fat_sectors = 1
        while True:
            data = self.total - self.reserved - 2 * self.fat_sectors - self.root_sectors
            self.cluster_count = data // self.cluster_sectors
            needed = ((self.cluster_count + 2) * self.bits + 7) // 8
            if needed <= self.fat_sectors * SECTOR:
                break
            self.fat_sectors += 1
        self.first_data = self.reserved + 2 * self.fat_sectors + self.root_sectors
        self.fat = [0] * (self.cluster_count + 2)
        self.fat[0] = 0x0FFFFFF8 & ((1 << self.bits) - 1)
        self.fat[1] = 0x0FFFFFFF & ((1 << self.bits) - 1)
        self.next_free = 2
        self.end_of_chain = 0x0FFFFFFF & ((1 << self.bits) - 1)
        self.image = bytearray(size)

    def allocate(self, count):
        clusters = []
        step = 2 if self.fragment and count > 1 else 1
        candidate = self.next_free
        while len(clusters) < count:
            if candidate >= self.cluster_count + 2:
                if step == 2:
                    step, candidate = 1, 2
                    continue
                raise ValueError("volume is full")
            if self.fat[candidate] == 0 and candidate not in clusters:
                clusters.append(candidate)
                self.fat[candidate] = self.end_of_chain
            candidate += step
        for current, following in zip(clusters, clusters[1:]):
            self.fat[current] = following
        if step == 1:
            self.next_free = clusters[-1] + 1
        return clusters

    def entries_for(self, node, parent_cluster):
        entries = []
        if node is not self.root:
            entries.append(short_entry(b".          ", ATTRIBUTE_DIRECTORY, 0, node.clusters[0], 0))
            entries.append(short_entry(b"..         ", ATTRIBUTE_DIRECTORY, 0, parent_cluster, 0))
        else:
            entries.append(short_entry(self.label, ATTRIBUTE_VOLUME, 0, 0, 0))
        taken = set()
        for child in node.children:
            plain = plain_short(child.name)
            if plain is not None and plain[0].decode() not in taken:
                short, flags = plain
            else:
                short, flags = alias(child.name, taken), 0
                entries.extend(long_entries(child.name, short))
            taken.add(short.decode())
            first = child.clusters[0] if child.clusters else 0
            size = 0 if child.directory else child.path.stat().st_size
            entries.append(short_entry(short, ATTRIBUTE_DIRECTORY if child.directory else 0, flags, first, size))
        return entries

    def count_entries(self, node):
        count = 2
        for child in node.children:
            count += 1
            if plain_short(child.name) is None:
                count += (len(child.name) + LONG_NAME_UNITS) // LONG_NAME_UNITS
        return count

    def plan(self, node):
        if node.directory:
            if node is not self.root or self.bits == 32:
                size = self.count_entries(node) * ENTRY
                node.clusters = self.allocate(max(1, -(-size // self.cluster_size)))
            for child in node.children:
                self.plan(child)
        else:
            size = node.path.stat().st_size
            if size:
                node.clusters = self.allocate(-(-size // self.cluster_size))

    def store(self, clusters, data):
        for index, cluster in enumerate(clusters):
            chunk = data[index * self.cluster_size:(index + 1) * self.cluster_size]
            offset = (self.first_data + (cluster - 2) * self.cluster_sectors) * SECTOR
            self.image[offset:offset + len(chunk)] = chunk

    def write_tree(self, node, parent_cluster):
        if node.directory:
            directory = b"".join(self.entries_for(node, parent_cluster))
            if node is self.root and self.bits != 32:
                if len(directory) > self.root_entries * ENTRY:
                    raise ValueError("root directory is full")
                offset = (self.reserved + 2 * self.fat_sectors) * SECTOR
                self.image[offset:offset + len(directory)] = directory
            else:
                self.store(node.clusters, directory)
            own = node.clusters[0] if node.clusters and node is not self.root else 0
            for child in node.children:
                self.write_tree(child, own)
        elif node.clusters:
            self.store(node.clusters, node.data())

    def boot_sector(self):
        sector = bytearray(SECTOR)
        small_total = self.total if self.total < 0x10000 and self.bits != 32 else 0
        sector[0:3] = JUMP
        sector[3:11] = OEM_NAME
        struct.pack_into("<HBHBHHBHHHII", sector, 11, SECTOR, self.cluster_sectors, self.reserved, 2,
                         self.root_entries, small_total, MEDIA_FIXED, 0 if self.bits == 32 else self.fat_sectors, 32,
                         64, 0, 0 if small_total else self.total)
        if self.bits == 32:
            struct.pack_into("<IHHIHH", sector, 36, self.fat_sectors, 0, 0, self.root.clusters[0], 1, 6)
            struct.pack_into("<BBBI11s8s", sector, 64, DRIVE_NUMBER, 0, EXTENDED_SIGNATURE, VOLUME_SERIAL,
                             self.label, b"FAT32   ")
        else:
            struct.pack_into("<BBBI11s8s", sector, 36, DRIVE_NUMBER, 0, EXTENDED_SIGNATURE, VOLUME_SERIAL,
                             self.label, b"FAT12   " if self.bits == 12 else b"FAT16   ")
        sector[510:512] = BOOT_SIGNATURE
        return sector

    def fat_bytes(self):
        if self.bits == 32:
            return b"".join(struct.pack("<I", value) for value in self.fat)
        if self.bits == 16:
            return b"".join(struct.pack("<H", value) for value in self.fat)
        data = bytearray((len(self.fat) * 3 + 1) // 2)
        for cluster, value in enumerate(self.fat):
            offset = cluster + cluster // 2
            if cluster % 2:
                data[offset] = (data[offset] & 0x0F) | (value << 4 & 0xF0)
                data[offset + 1] = value >> 4 & 0xFF
            else:
                data[offset] = value & 0xFF
                data[offset + 1] = (data[offset + 1] & 0xF0) | (value >> 8 & 0x0F)
        return bytes(data)

    def build(self, source):
        self.root = Node(source, "")
        self.plan(self.root)
        self.write_tree(self.root, 0)
        boot = self.boot_sector()
        self.image[0:SECTOR] = boot
        if self.bits == 32:
            fsinfo = bytearray(SECTOR)
            struct.pack_into("<I", fsinfo, 0, FSINFO_LEAD)
            struct.pack_into("<III", fsinfo, 484, FSINFO_STRUCTURE, FSINFO_UNKNOWN, FSINFO_UNKNOWN)
            fsinfo[510:512] = BOOT_SIGNATURE
            self.image[SECTOR:2 * SECTOR] = fsinfo
            self.image[6 * SECTOR:7 * SECTOR] = boot
        fat = self.fat_bytes()
        for copy in range(2):
            offset = (self.reserved + copy * self.fat_sectors) * SECTOR
            self.image[offset:offset + len(fat)] = fat
        return bytes(self.image)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--kind", choices=sorted(LAYOUTS), default="fat32")
    parser.add_argument("--size", type=int, required=True)
    parser.add_argument("--label", default="BOOT")
    parser.add_argument("--fragment", action="store_true")
    parser.add_argument("source")
    parser.add_argument("output")
    options = parser.parse_args()

    volume = Volume(options.kind, options.size * 1024 * 1024, options.label, options.fragment)
    pathlib.Path(options.output).write_bytes(volume.build(pathlib.Path(options.source)))


if __name__ == "__main__":
    main()
