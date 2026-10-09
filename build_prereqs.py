#!/usr/bin/env python3
#
# build_prereqs.py -- build.sh's check of every prerequisite it cannot set up by
# itself: PlatformIO's version, the Python modules in python_modules.txt and,
# for a board with a companion MCU, what build_pico.py needs. It lists
# everything missing in one report, asks once before installing it, and exits
# 0 only when the build can go ahead.
#
# Runs in the build venv, so pip installs land there. Minimum versions come
# from platformio-ini-files/platformio.common.ini and python_modules.txt.

import argparse
import configparser
import importlib.util
import os
import re
import sys
from importlib import metadata

ROOT = os.path.dirname(os.path.abspath(__file__))
COMMON_INI = os.path.join(ROOT, "platformio-ini-files", "platformio.common.ini")
MODULES_TXT = os.path.join(ROOT, "python_modules.txt")


def _load_build_pico():
    spec = importlib.util.spec_from_file_location("build_pico",
                                                  os.path.join(ROOT, "build_pico.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


bp = _load_build_pico()


def _version(text: str):
    return tuple(int(x) for x in re.findall(r"\d+", text))


def _installed(name: str):
    try:
        return metadata.version(name)
    except metadata.PackageNotFoundError:
        return None


def _pip_problem(name: str, requirement: str, message: str):
    def install():
        bp.run([sys.executable, "-m", "pip", "install", "-q", requirement], cwd=ROOT,
               board="build", key=None, quiet=True)

    return bp.Problem(None, name, message,
                      f'{sys.executable} -m pip install "{requirement}"', install)


def python_problems(pc_build: bool) -> list:
    """PlatformIO's version (not for -p builds) and the python_modules.txt minimums."""
    wanted = []
    if not pc_build:
        parser = configparser.ConfigParser(inline_comment_prefixes=(";", "#"),
                                           interpolation=None)
        parser.read(COMMON_INI)
        pio_min = parser.get("fujinet", "platformio_min", fallback="").strip()
        if pio_min:
            wanted.append(("PlatformIO", "platformio", pio_min))
    with open(MODULES_TXT) as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#") and "|" in line:
                package, _, need = line.split("|", 1)[1].partition(">=")
                wanted.append((package, package, need))

    found = []
    for name, package, need in wanted:
        have = _installed(package)
        if have and (not need or _version(have) >= _version(need)):
            continue
        detail = f"{have} found" if have else "not found"
        found.append(_pip_problem(name, f"{package}>={need}" if need else package,
                                  f"{detail} (need {need} or newer)" if need else detail))
    return found


def companion_problems(board: str, ini: str) -> list:
    if os.environ.get("FUJINET_SKIP_PICO", "").strip().lower() in bp.TRUE_WORDS:
        return []
    cfg, _ = bp.resolve_config(ini, board)
    if cfg is None:
        return []
    # A pico_repo source is cloned by the build, not by a check.
    if not cfg.repo:
        bp.ensure_source(cfg)
    return bp.problems(cfg)


def ask(found: list, yes: bool) -> bool:
    question = f"Install {bp.join_names(list(dict.fromkeys(p.name for p in found)))} now? [y/N] "
    if yes:
        print(question + "y (-y)", flush=True)
        return True
    if not sys.stdin.isatty():
        return False
    return input(question).strip().lower().startswith("y")


def main() -> int:
    p = argparse.ArgumentParser(description="Check build.sh's prerequisites and offer to "
                                            "install what is missing.")
    p.add_argument("--board", help="board being built, for its companion-MCU checks")
    p.add_argument("--ini", help="ini to read the board's [fujinet] pico_* keys from")
    p.add_argument("--pc", action="store_true", help="a -p host build: modules only")
    p.add_argument("--yes", action="store_true", help="install without asking (build.sh -y)")
    args = p.parse_args()
    os.chdir(ROOT)

    def check() -> list:
        found = python_problems(args.pc)
        if args.board and not args.pc:
            found += companion_problems(args.board, args.ini or "platformio-generated.ini")
        return found

    title = "Missing build prerequisites:"
    try:
        found = check()
        if not found:
            return 0
        print(bp.format_problems(title, found, commands=False), flush=True)
        auto = bp.installable(found)
        if not auto:
            return 1
        if not ask(auto, args.yes):
            print(bp.install_commands(auto))
            return 1
        bp.install(found)
        found = check()
        if found:
            print(bp.format_problems(title, found), file=sys.stderr)
            return 1
        return 0
    except bp.PicoBuildError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
