import os
import random
import re
import select
import subprocess
import time

PROMPT = "boot> "
COUNTDOWN = "r for recovery, any other key for the shell"
LOCKED = ["-fw_cfg", "name=opt/boot/policy,string=locked"]
MIB = 1024 * 1024
PHONE_PARTITIONS = {"system-a": (1, 65), "system-b": (65, 129), "recovery": (129, 161), "state": (193, 194)}
RAMDISK = random.Random(3).randbytes(300 * 1024)
ESCAPE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")
SCENARIOS = []


class Failure(Exception):
    pass


def scenario(function):
    SCENARIOS.append(function)
    return function


def check(condition, message):
    if not condition:
        raise Failure(message)


def tamper(image, partition, old=b"args console=serial", new=b"args console=silent"):
    start, end = (offset * MIB for offset in PHONE_PARTITIONS[partition])
    with open(image, "r+b") as disk:
        disk.seek(start)
        offset = disk.read(end - start).find(old)
        check(offset != -1, f"{old!r} was not found in {partition}")
        disk.seek(start + offset)
        disk.write(new)


def clean(raw):
    start = raw.rfind("\x1b")
    if start != -1 and not ESCAPE.match(raw, start):
        raw = raw[:start]
    return ESCAPE.sub("", raw.replace("\r", ""))


class Machine:
    def __init__(self, qemu, arguments):
        self.process = subprocess.Popen(
            [qemu, "-display", "none", "-monitor", "none", "-serial", "stdio", *arguments],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        self.raw = ""
        self.output = ""
        self.cursor = 0

    def __enter__(self):
        return self

    def __exit__(self, *details):
        self.close()

    def close(self):
        if self.process.poll() is None:
            self.process.kill()
        self.process.wait()

    def read(self, timeout):
        ready, _, _ = select.select([self.process.stdout], [], [], timeout)
        if not ready:
            return True
        data = os.read(self.process.stdout.fileno(), 65536)
        if not data:
            return False
        self.raw += data.decode("utf-8", "replace")
        self.output = clean(self.raw)
        return True

    def expect(self, pattern, timeout=10):
        regex = re.compile(pattern)
        deadline = time.monotonic() + timeout
        while True:
            match = regex.search(self.output, self.cursor)
            if match:
                self.cursor = match.end()
                return match
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not self.read(min(remaining, 0.25)):
                raise Failure(f"expected {pattern!r}\n{self.tail()}")

    def send(self, text):
        self.process.stdin.write(text.encode())
        self.process.stdin.flush()

    def run(self, line, timeout=10):
        start = self.cursor
        self.send(line + "\r")
        self.expect(re.escape(PROMPT), timeout)
        return self.output[start:self.cursor]

    def image_base(self):
        output = self.run("mem")
        return int(re.search(r"([0-9a-f]{16})-[0-9a-f]{16} +[\d.]+ \w+  boot\n", output).group(1), 16)

    def stop_countdown(self):
        self.expect(COUNTDOWN)
        self.send("x")
        self.expect(re.escape(PROMPT))

    def wait_exit(self, timeout=10):
        try:
            self.process.wait(timeout)
        except subprocess.TimeoutExpired:
            raise Failure(f"machine did not exit\n{self.tail()}")

    def tail(self, lines=40):
        return "\n".join(self.output.splitlines()[-lines:])


class Context:
    def __init__(self, image, qemu, data=""):
        self.image = image
        self.qemu = qemu
        self.data = data

    def machine(self, arguments):
        return Machine(self.qemu, arguments)

    def kernel(self, machine="virt", cpu="cortex-a57", memory="1G", extra=()):
        return ["-machine", machine, "-cpu", cpu, "-m", memory, "-kernel", self.image, *extra]

    def drive(self, image, name="disk", device="virtio-blk-device"):
        return [
            "-drive", f"file={self.data}/{image},format=raw,if=none,id={name},snapshot=on",
            "-device", f"{device},drive={name}",
        ]

    def phone(self, image=None, extra=(), snapshot=True):
        drive = f"file={image or self.data + '/phone.img'},format=raw,if=virtio" + (",snapshot=on" if snapshot else "")
        return self.kernel(cpu="cortex-a72", memory="2G", extra=[
            "-smp", "4", "-device", "virtio-rng-pci", "-drive", drive, *extra,
        ])

    def phone_copy(self, name):
        target = f"{self.data}/{name}.img"
        subprocess.run(["cp", "--sparse=always", f"{self.data}/phone.img", target], check=True)
        return target

    def folder_drive(self, folder, name="folder"):
        return [
            "-drive", f"file=fat:{self.data}/{folder},format=raw,if=none,id={name},readonly=on",
            "-device", f"virtio-blk-device,drive={name}",
        ]

    def loaded(self, address, machine="virt", cpu="cortex-a57", memory="1G"):
        return [
            "-machine", machine, "-cpu", cpu, "-m", memory,
            "-device", f"loader,file={self.image},addr={address:#x}",
            "-device", f"loader,addr={address:#x},cpu-num=0",
        ]

    def boot(self, **options):
        machine = self.machine(self.kernel(**options))
        machine.expect(re.escape(PROMPT))
        return machine
