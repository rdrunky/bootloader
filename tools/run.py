import argparse
import os
import pathlib
import shlex
import signal
import sys
import termios
import tty


def detect_resolution():
    for drm in pathlib.Path("/sys/class/drm").glob("*"):
        status = drm / "status"
        modes = drm / "modes"
        if not status.is_file() or not modes.is_file():
            continue
        try:
            if status.read_text().strip() != "connected":
                continue
            lines = modes.read_text().splitlines()
            if lines:
                return lines[0].strip()
        except OSError:
            pass
    return ""


def select_mode():
    if not sys.stdin.isatty():
        return "console"

    options = ["Graphical", "Console"]
    index = 0
    lines = len(options) + 4

    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)

    def terminate(signum, _frame):
        sys.exit(128 + signum)

    old_handler = signal.signal(signal.SIGTERM, terminate)
    try:
        tty.setraw(fd)
        sys.stdout.write("\x1b[?25l")
        sys.stdout.flush()

        def draw(first=False):
            if not first:
                sys.stdout.write(f"\x1b[{lines}A\r")
            out = ["Select mode:\r\n\r\n"]
            for i, name in enumerate(options):
                cursor = ">" if i == index else " "
                out.append(f"  {cursor} {name}\x1b[K\r\n")
            out.append("\r\nUse arrow keys to choose, Enter to select.\x1b[K\r\n")
            sys.stdout.write("".join(out))
            sys.stdout.flush()

        draw(first=True)

        buf = b""
        while True:
            chunk = os.read(fd, 32)
            if not chunk:
                break
            buf += chunk

            while buf:
                key = buf[:1]
                if key == b"\x1b":
                    if len(buf) < 2:
                        break
                    if buf[1:2] in (b"[", b"O"):
                        if len(buf) < 3:
                            break
                        key = buf[:3]
                    else:
                        buf = buf[1:]
                        continue
                buf = buf[len(key):]

                if key in (b"\x1b[A", b"\x1bOA", b"k", b"K"):
                    index = (index - 1) % len(options)
                    draw()
                elif key in (b"\x1b[B", b"\x1bOB", b"j", b"J"):
                    index = (index + 1) % len(options)
                    draw()
                elif key in (b"\r", b"\n"):
                    return "graphic" if index == 0 else "console"
                elif key in (b"\x03", b"q", b"Q"):
                    sys.exit(0)

        return "graphic" if index == 0 else "console"
    finally:
        signal.signal(signal.SIGTERM, old_handler)
        termios.tcsetattr(fd, termios.TCSADRAIN, old)
        sys.stdout.write(f"\x1b[{lines}A\r\x1b[J\x1b[?25h")
        sys.stdout.flush()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", default="qemu-system-aarch64")
    parser.add_argument("--kernel", required=True)
    parser.add_argument("--disk", default="")
    parser.add_argument("--drive", default="")
    parser.add_argument("--qemu-args", default="")
    parser.add_argument("--res", default="")
    parser.add_argument("--mode", choices=["auto", "graphic", "console"], default="auto")
    options = parser.parse_args()

    mode = options.mode
    if mode == "auto":
        mode = select_mode()

    command = [options.qemu]
    if options.qemu_args:
        command.extend(shlex.split(options.qemu_args))

    if options.drive:
        drive = shlex.split(options.drive)
    elif options.disk:
        drive = [
            "-drive",
            f"file={options.disk},format=raw,if=none,id=disk,snapshot=on",
            "-device", "virtio-blk-device,drive=disk",
        ]
    else:
        drive = []

    if mode == "graphic":
        command.extend([
            "-kernel", options.kernel,
            "-device", "ramfb",
            "-device", "virtio-keyboard-pci",
        ])
        resolution = options.res or detect_resolution()
        if resolution:
            command.extend(["-fw_cfg", f"name=opt/boot/resolution,string={resolution}"])
        command.extend(drive)
        command.extend(["-serial", "stdio"])
    else:
        command.extend([
            "-nographic",
            "-kernel", options.kernel,
        ])
        command.extend(drive)

    os.execvp(command[0], command)


if __name__ == "__main__":
    main()
