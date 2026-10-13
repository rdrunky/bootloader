import re

from support import PROMPT, check, scenario


@scenario
def shell_lists_commands(context):
    with context.boot() as machine:
        output = machine.run("help")
        for name in ("echo", "fault", "heap", "md", "mem", "mw", "poweroff", "reset", "uptime"):
            check(f"  {name} " in output, f"help does not list {name}\n{output}")
        check("  version " not in output, f"help lists version\n{output}")
        check("usage: help [command]" in machine.run("help md extra"), "help accepts too many arguments")


@scenario
def shell_parses_quotes_and_sequences(context):
    with context.boot() as machine:
        check("\na b;c\n" in machine.run('echo "a b;c"'), "quotes are not kept together")
        check("\none\ntwo\n" in machine.run("echo one; echo two"), "sequence did not run both")
        output = machine.run("md && echo skipped")
        check("usage: md" in output and "\nskipped" not in output, f"&& did not stop\n{output}")
        check("unterminated quote" in machine.run('echo "open'), "unterminated quote accepted")
        check("frobnicate: unknown command" in machine.run("frobnicate"), "unknown command not reported")
        check("\na\\b c\n" in machine.run("echo 'a\\b' c"), "single quotes are not literal")


@scenario
def shell_survives_bad_addresses(context):
    with context.boot() as machine:
        image = machine.image_base()
        check("md: 0: bad address" in machine.run("md 0"), "reading address 0 was not caught")
        output = machine.run(f"mw {image:#x} 0")
        check(f"mw: {image:#x}: bad address" in output, f"writing the loader code was not caught\n{output}")
        check(f"{image:016x}:" in machine.run(f"md -w {image:#x} 16"), "loader code is not readable")
        check("md: 0xffff000000000000: bad address" in machine.run("md 0xffff000000000000"),
              "reading a non canonical address was not caught")
        check(machine.run("echo alive").count("alive") == 2, "shell died after faults")


@scenario
def shell_shows_memory_and_heap(context):
    with context.boot() as machine:
        output = machine.run("mem")
        for owner in ("boot", "devicetree", "heap", "free"):
            check(f"  {owner}" in output, f"memory map lacks {owner}\n{output}")
        check("integrity ok" in machine.run("heap"), "heap integrity is not ok")


@scenario
def shell_completes_and_recalls(context):
    with context.boot() as machine:
        machine.send("upt\t\r")
        machine.expect(r"\n\d+\.\d{6} s\n")
        machine.expect(re.escape(PROMPT))
        machine.send("\x1b[A\r")
        machine.expect(r"\n\d+\.\d{6} s\n")
        machine.expect(re.escape(PROMPT))
        machine.send("echo abc\x1b[D\x1b[D\x7fX\r")
        machine.expect("\nXbc\n")


@scenario
def shell_resets_the_machine(context):
    with context.boot() as machine:
        machine.send("reset\r")
        machine.expect("cpu el1 ok\n")
        machine.expect(re.escape(PROMPT))
