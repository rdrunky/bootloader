import argparse
import concurrent.futures
import importlib
import os
import pathlib
import sys
import time

import support


def load_scenarios():
    folder = pathlib.Path(__file__).parent / "scenarios"
    for path in sorted(folder.glob("*.py")):
        importlib.import_module(f"scenarios.{path.stem}")
    return support.SCENARIOS


def run_one(function, context):
    start = time.monotonic()
    try:
        function(context)
        return None, time.monotonic() - start
    except support.Failure as failure:
        return str(failure), time.monotonic() - start


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", required=True)
    parser.add_argument("--qemu", default="qemu-system-aarch64")
    parser.add_argument("--data", default="")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    parser.add_argument("filter", nargs="?", default="")
    options = parser.parse_args()

    context = support.Context(options.image, options.qemu, options.data)
    scenarios = [function for function in load_scenarios() if options.filter in function.__name__]

    with concurrent.futures.ThreadPoolExecutor(options.jobs) as pool:
        results = list(pool.map(lambda function: run_one(function, context), scenarios))

    failed = 0
    for function, (failure, seconds) in zip(scenarios, results):
        if failure is None:
            print(f"ok   {function.__name__} ({seconds:.1f} s)")
        else:
            failed += 1
            print(f"FAIL {function.__name__} ({seconds:.1f} s)")
            print("     " + failure.replace("\n", "\n     "))

    print(f"{len(scenarios) - failed} passed, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.path.insert(0, str(pathlib.Path(__file__).parent))
    sys.exit(main())
