from support import check, scenario


@scenario
def display_changes_resolution_and_validates_scale(context):
    with context.boot(extra=["-device", "ramfb"]) as machine:
        for mode, expected in [
            ("800x600", "800x600"),
            ("800X600 2", "800x600"),
            ("0x400 768", "1024x768"),
            ("0X400 0x300", "1024x768"),
            ("0b10000000000 0b1100000000", "1024x768"),
            ("0x400X0x300 0b10", "1024x768"),
            ("1024X0x300 2", "1024x768"),
            ("1024 768 2", "1024x768"),
        ]:
            output = machine.run("resolution " + mode)
            check(f"display ramfb {expected} ok" in output, output)
            check(f"resolution {expected}" in machine.run("resolution"), output)
        for scale in ("0", "9"):
            output = machine.run("resolution 800x600 " + scale)
            check("invalid argument" in output, output)
            output = machine.run("resolution")
            check("resolution 1024x768" in output, output)


@scenario
def display_preserves_framebuffer_when_memory_runs_out(context):
    with context.boot(memory="32M", extra=["-device", "ramfb"]) as machine:
        output = machine.run("resolution 4096x2160")
        check("resolution 4096x2160 failed" in output, output)
        output = machine.run("resolution")
        check("resolution 800x600" in output, output)
        output = machine.run("mem")
        check("  framebuffer" in output, output)
        output = machine.run("echo alive")
        check("\nalive\n" in output, output)
