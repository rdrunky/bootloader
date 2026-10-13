import argparse
import pathlib
import subprocess
import sys


def text_symbols(nm, elf):
    listing = subprocess.run([nm, "-n", "--defined-only", elf], check=True, capture_output=True, text=True)
    symbols = []
    for line in listing.stdout.splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[1] in ("t", "T") and not fields[2].startswith(("$", ".L")):
            symbols.append((int(fields[0], 16), fields[2]))
    return symbols


def render(symbols):
    lines = [
        "\t.section .symbols, \"a\"",
        "\t.balign 4",
        "\t.globl __symbol_count",
        "__symbol_count:",
        f"\t.long {len(symbols)}",
        "\t.globl __symbols",
        "__symbols:",
    ]
    offset = 0
    for address, name in symbols:
        lines.append(f"\t.long {address:#x}, {offset}")
        offset += len(name) + 1
    lines += ["\t.globl __symbol_names", "__symbol_names:"]
    lines += [f"\t.asciz \"{name}\"" for _, name in symbols]
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--nm", default="nm")
    parser.add_argument("--elf")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("output")
    options = parser.parse_args()

    text = render(text_symbols(options.nm, options.elf) if options.elf else [])
    path = pathlib.Path(options.output)

    if options.check:
        if path.read_text() != text:
            print(f"{options.elf}: symbol table does not match the final image", file=sys.stderr)
            return 1
        return 0

    path.write_text(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
