from support import scenario

FAULTS = {
    "null": r"data abort: write, translation fault",
    "stack": r"stack overflow",
    "write": r"data abort: write, permission fault",
    "exec": r"instruction abort: permission fault",
    "smash": r"panic: stack smashing detected",
    "overflow": r"panic: free\(0x[0-9a-f]+\): heap buffer overflow",
    "double": r"panic: free\(0x[0-9a-f]+\): double free",
    "brk": r"brk #1000",
    "undefined": r"undefined instruction",
}


def fault_scenario(kind, pattern):
    def run(context):
        with context.boot() as machine:
            machine.send(f"fault {kind}\r")
            machine.expect(pattern)
            machine.expect("backtrace:")
            machine.expect("halted")

    run.__name__ = f"fault_{kind}"
    return run


for kind, pattern in FAULTS.items():
    scenario(fault_scenario(kind, pattern))
