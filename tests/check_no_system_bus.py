#!/usr/bin/env python3

import sys
from pathlib import Path


SOURCE_EXTENSIONS = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
}


def check_directory(directory):
    directory = Path(directory)
    found = False

    for path in directory.rglob("*"):
        if not path.is_file() or path.suffix.lower() not in SOURCE_EXTENSIONS:
            continue

        try:
            lines = path.read_text(encoding="utf-8").splitlines()
        except UnicodeDecodeError:
            print(f"ERROR: Could not read {path} as UTF-8")
            found = True
            continue

        for line_number, line in enumerate(lines, 1):
            if "SYSTEM_BUS" in line:
                print(f"{path}:{line_number}: SYSTEM_BUS is not allowed")
                print(f"    {line}")
                found = True

    return found


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <directory> [directory ...]")
        return 2

    found = False

    for directory in sys.argv[1:]:
        if not Path(directory).is_dir():
            print(f"ERROR: Not a directory: {directory}")
            found = True
            continue

        if check_directory(directory):
            found = True

    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
