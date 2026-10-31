"""Check (default) or format maintained main-firmware C sources."""

import argparse
from importlib.metadata import PackageNotFoundError, version
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "software/front/US_Firmware.X"
EXCLUDED = {"fonts.h", "logo.h", "isr_old.c"}
FORMAT_VERSION = "21.1.8"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true", help="report formatting differences (default)")
    mode.add_argument("--fix", action="store_true", help="format files in place")
    args = parser.parse_args()

    try:
        installed = version("clang-format")
    except PackageNotFoundError:
        installed = None
    if installed != FORMAT_VERSION:
        print("Install the pinned formatter: python -m pip install -r tools/requirements-format.txt",
              file=sys.stderr)
        return 2

    # Deliberately nonrecursive: vendor USB sources and generated output are excluded.
    files = sorted(path for path in SOURCE.iterdir()
                   if path.suffix in {".c", ".h"}
                   and path.name not in EXCLUDED
                   and not (path.name.startswith("font") and path.name[4:5].isdigit()))
    if not files:
        print("No firmware sources found.", file=sys.stderr)
        return 2

    # Invoke the package entry point without requiring the pip scripts directory on PATH.
    command = [sys.executable, "-c", "from clang_format import clang_format; clang_format()",
               "--style=file"]
    command += ["-i"] if args.fix else ["--dry-run", "--Werror", "--ferror-limit=1"]
    print(f"{'Formatting' if args.fix else 'Checking'} {len(files)} firmware files.", flush=True)
    return subprocess.run(command + [str(path) for path in files], cwd=ROOT).returncode


if __name__ == "__main__":
    sys.exit(main())
