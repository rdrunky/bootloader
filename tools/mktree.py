import argparse
import os
import pathlib
import random
import shutil


def write(root, relative, data):
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def link(root, relative, target):
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    os.symlink(target, path)


def build(root):
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)

    write(root, "hello.txt", b"hello from the test tree\n")
    write(root, "empty", b"")
    write(root, "boot/kernel", random.Random(1).randbytes(1024 * 1024 + 123))
    write(root, "boot/config.txt", b"entry system\n  kernel /boot/kernel\n")
    write(root, "boot/ramdisk", random.Random(3).randbytes(300 * 1024))
    write(root, "deep/a/b/c/d/e/file.txt", b"deep file\n")
    write(root, "big/data.bin", random.Random(2).randbytes(3 * 1024 * 1024 + 7))
    write(root, "docs/a-rather-long-file-name-for-directory-entries.txt", b"long name\n")
    write(root, "docs/MixedCase.Txt", b"mixed case\n")

    for index in range(300):
        name = f"file-{index:03d}.txt"
        write(root, f"many/{name}", name.encode() + b"\n")

    with open(root / "sparse.bin", "wb") as sparse:
        sparse.seek(5 * 1024 * 1024)
        sparse.write(b"!")

    link(root, "link-to-hello", "hello.txt")
    link(root, "absolute-link", "/boot/config.txt")
    link(root, "directory-link", "deep/a/b")
    link(root, "long-link", "./deep/a/b/c/d/e/../e/../e/../e/../e/../e/../e/../e/../e/file.txt")
    link(root, "loop-a", "loop-b")
    link(root, "loop-b", "loop-a")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--payload")
    parser.add_argument("root")
    options = parser.parse_args()

    root = pathlib.Path(options.root)
    build(root)
    if options.payload:
        write(root, "boot/payload", pathlib.Path(options.payload).read_bytes())


if __name__ == "__main__":
    main()
