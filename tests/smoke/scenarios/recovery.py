import re

from support import COUNTDOWN, PROMPT, check, scenario

MENU = ("1  start system", "2  start recovery", "3  start broken", "4  start tampered", "d  run diagnostics",
        "s  open the shell", "r  restart", "p  power off")


def expect_menu(machine, reason):
    machine.expect(rf"recovery\n{re.escape(reason)}\n")
    for item in MENU:
        machine.expect(re.escape(item))
    machine.expect("choose: ")


@scenario
def recovery_opens_from_the_countdown(context):
    with context.machine(context.kernel(extra=context.drive("autoboot.img"))) as machine:
        machine.expect(COUNTDOWN)
        machine.send("r")
        expect_menu(machine, "requested from the countdown")
        machine.send("2")
        machine.expect(r"jump 0x[0-9a-f]+ el1\n")
        machine.expect(r"payload: boot-mode 'recovery'")
        machine.expect("payload: done")


@scenario
def recovery_returns_after_a_refused_kernel(context):
    with context.machine(context.kernel(extra=context.drive("autoboot.img"))) as machine:
        machine.stop_countdown()
        machine.send("recovery\r")
        expect_menu(machine, "requested from the shell")
        machine.send("4")
        machine.expect("kernel virtio0p1:/payload sha256 mismatch\n")
        expect_menu(machine, "tampered did not start, kernel virtio0p1:/payload sha256 mismatch")
        machine.send("q")
        expect_menu(machine, "choose one of the listed keys")
        machine.send("s")
        machine.expect(re.escape(PROMPT))
        check(machine.run("echo alive").count("alive") == 2, "shell does not answer after recovery")


@scenario
def recovery_runs_diagnostics_and_powers_off(context):
    with context.machine(context.kernel(extra=context.drive("autoboot.img"))) as machine:
        machine.expect(COUNTDOWN)
        machine.send("r")
        expect_menu(machine, "requested from the countdown")
        machine.send("d")
        machine.expect(r"memory test 256 MiB in \d+ ranges?\n")
        machine.expect(r"data lines ok\n")
        machine.expect(r"address lines ok\n")
        machine.expect(r"own address 256 MiB written and read twice in \d+ ms ok\n", timeout=90)
        machine.expect(r"virtio0 96 MiB read in \d+ ms ok\n", timeout=60)
        machine.expect(r"virtio0p1 fat32 'BOOT', \d+ files, \d+ directories, [\d.]+ [KM]iB read ok\n", timeout=60)
        machine.expect(r"virtio0p2 ext4 'system', \d+ files, \d+ directories, [\d.]+ [KM]iB read ok\n", timeout=60)
        machine.expect(r"heap ok\n")
        machine.expect(r"diagnostics passed\n")
        expect_menu(machine, "diagnostics passed")
        machine.send("p")
        machine.wait_exit()


@scenario
def recovery_starts_a_detected_kernel(context):
    with context.machine(context.kernel(extra=context.folder_drive("detect"))) as machine:
        machine.expect(COUNTDOWN)
        machine.send("r")
        machine.expect(r"recovery\nrequested from the countdown\n")
        machine.expect(r"1  start detected")
        machine.expect("choose: ")
        machine.send("1")
        machine.expect(r"jump 0x[0-9a-f]+ el1\n")
        machine.expect(r"payload: boot-mode 'normal'")
        machine.expect("payload: done")
