import re

from support import PROMPT, check, scenario


def boot_and_check(context, arguments, entered, handoff):
    with context.machine([*arguments, *context.folder_drive("detect")]) as machine:
        machine.expect(f"cpu el{entered} ok\n")
        machine.expect("mmu ok\n")
        machine.stop_countdown()
        check(machine.run("echo alive").count("alive") == 2, "shell does not answer")
        machine.send("boot virtio0p1:/kernel\r")
        machine.expect(rf"jump 0x[0-9a-f]+ el{handoff}\n")
        machine.expect(rf"payload: el{handoff} ")
        machine.expect("payload: done")

    with context.machine(arguments) as machine:
        machine.expect(re.escape(PROMPT))
        if "power psci ok\n" in machine.output:
            machine.send("poweroff\r")
            machine.wait_exit()


@scenario
def entry_el1(context):
    boot_and_check(context, context.kernel(), 1, 1)


@scenario
def entry_el2(context):
    boot_and_check(context, context.kernel(machine="virt,virtualization=on"), 2, 2)


@scenario
def entry_el3_with_el2(context):
    arguments = context.loaded(0x40a12000, machine="virt,secure=on,virtualization=on")
    boot_and_check(context, arguments, 3, 2)


@scenario
def entry_el3_without_el2(context):
    arguments = context.loaded(0x47000000, machine="virt,secure=on", cpu="cortex-a53", memory="512M")
    boot_and_check(context, arguments, 3, 1)


@scenario
def entry_unaligned_load_address(context):
    arguments = context.loaded(0x5f3ff000, cpu="cortex-a72", memory="2G")
    boot_and_check(context, arguments, 1, 1)


@scenario
def entry_max_cpu_four_cores_gicv3(context):
    arguments = context.kernel(machine="virt,virtualization=on,gic-version=3", cpu="max", memory="4G",
                               extra=("-smp", "4"))
    boot_and_check(context, arguments, 2, 2)


@scenario
def entry_small_memory(context):
    boot_and_check(context, context.kernel(cpu="cortex-a53", memory="128M"), 1, 1)
