"""Build and run production-PID arithmetic tests with GCC/Clang or a MIPS compiler."""

import argparse
from pathlib import Path
import shlex
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="gcc", help="C compiler executable")
    parser.add_argument("--runner", help="emulator command, e.g. 'qemu-mipsel'")
    parser.add_argument("--sanitize", action="store_true", help="enable native undefined-behavior checks")
    parser.add_argument("--opt", choices=["0", "2"], default="2", help="optimization level")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="unisolder-pid-") as directory:
        executable = Path(directory) / "test_pid"
        command = [args.cc, "-std=c99", "-O" + args.opt, "-Wall", "-Wextra",
                   "-I" + str(ROOT / "tests/firmware/include"),
                   "-I" + str(ROOT / "software/front/US_Firmware.X"),
                   str(ROOT / "tests/firmware/test_pid.c"), "-o", str(executable)]
        if args.sanitize:
            command += ["-fsanitize=undefined", "-fno-sanitize-recover=all"]
        if args.runner:
            command += ["-static"]
        subprocess.run(command, cwd=ROOT, check=True)
        runner = shlex.split(args.runner) if args.runner else []
        subprocess.run(runner + [str(executable)], cwd=ROOT, check=True)


if __name__ == "__main__":
    main()
