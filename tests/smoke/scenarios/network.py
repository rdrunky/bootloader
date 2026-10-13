import re

from support import PROMPT, check, scenario

MMIO_NIC = ["-nic", "none", "-netdev", "user,id=user", "-device", "virtio-net-device,netdev=user"]


@scenario
def network_configures_with_dhcp_and_pings_the_gateway(context):
    with context.boot() as machine:
        output = machine.run("net")
        check(re.search(r"net0  52:54:00:12:34:56  link up\n", output), f"interface missing\n{output}")
        check("address  none, run dhcp" in output, f"interface configured too early\n{output}")
        output = machine.run("dhcp", timeout=20)
        check("address  10.0.2.15/24, dhcp" in output, f"no dhcp address\n{output}")
        check("gateway  10.0.2.2\n" in output, f"no gateway\n{output}")
        check("dns      10.0.2.3\n" in output, f"no dns\n{output}")
        check(re.search(r"lease    86400 s from 10\.0\.2\.2, \d+ s left", output), f"no lease\n{output}")
        output = machine.run("ping 10.0.2.2 2", timeout=20)
        check(output.count("reply from 10.0.2.2") == 2, f"ping lost replies\n{output}")
        check("10.0.2.2: 2 sent, 2 received, rtt " in output, f"no ping summary\n{output}")


@scenario
def network_works_over_mmio_with_a_static_address(context):
    with context.boot(extra=MMIO_NIC) as machine:
        output = machine.run("net net0 10.0.2.20/24 10.0.2.2")
        check("address  10.0.2.20/24, static" in output, f"static address not applied\n{output}")
        output = machine.run("ping 10.0.2.2 1", timeout=20)
        check("10.0.2.2: 1 sent, 1 received" in output, f"no reply over mmio\n{output}")
        output = machine.run("ping 10.0.2.99 1", timeout=20)
        check("ping: 10.0.2.99: host is unreachable" in output, f"unreachable host\n{output}")
        check(output.count("host is unreachable") == 1, f"error printed twice\n{output}")


@scenario
def network_explains_what_is_missing(context):
    with context.boot() as machine:
        output = machine.run("ping 10.0.2.2")
        check("ping: no configured interface reaches 10.0.2.2, run dhcp first" in output, f"no hint\n{output}")
        check("usage" in machine.run("net net0 10.0.2.20").lower(), "prefix should be required")
        check("net: no interface 'net7'" in machine.run("net net7"), "unknown interface")

    with context.boot(extra=["-nic", "none"]) as machine:
        check("no network interfaces" in machine.run("net"), "net without interfaces")
        check("no network interfaces" in machine.run("dhcp"), "dhcp without interfaces")


@scenario
def network_device_is_quiet_at_handoff(context):
    arguments = context.kernel(extra=[*MMIO_NIC, *context.drive("system.img")])
    with context.machine(arguments) as machine:
        machine.expect(re.escape(PROMPT))
        check("address  10.0.2.15/24" in machine.run("dhcp", timeout=20), "dhcp failed")
        machine.send("boot virtio0p2:/boot/payload\r")
        machine.expect(r"payload: virtio-mmio 0x[0-9a-f]+ device 1 status 0\n")
        machine.expect("payload: done")
