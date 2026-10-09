#!/usr/bin/env python3
#
# build_prereqs.py -- build.sh's check of every prerequisite it cannot set up by
# itself: a host C/C++ compiler, PlatformIO's version, the Python modules in
# python_modules.txt and, for a board with a companion MCU, what build_pico.py
# needs. It lists everything missing in one report, asks once before installing
# it, and exits 0 only when the build can go ahead.
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
MODULES_SH = os.path.join(ROOT, "install_python_modules.sh")


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
                      f'{sys.executable} -m pip install "{requirement}"', install, pip=True)


def _compiler_install_command() -> str:
    """The package-manager command that installs a host C/C++ compiler here, or ""."""
    try:
        with open("/etc/os-release") as f:
            info = dict(line.rstrip("\n").split("=", 1) for line in f if "=" in line)
    except OSError:
        return ""
    ids = f" {info.get('ID', '')} {info.get('ID_LIKE', '')} ".replace('"', "")
    for names, command in ((("debian", "ubuntu"),
                            "apt-get update && DEBIAN_FRONTEND=noninteractive "
                            "apt-get install -y build-essential"),
                           (("fedora", "rhel", "centos"), "dnf install -y gcc gcc-c++ make"),
                           (("arch",), "pacman -S --noconfirm base-devel"),
                           (("suse", "opensuse"), "zypper install -y gcc gcc-c++ make"),
                           (("alpine",), "apk add build-base")):
        if any(f" {n} " in ids for n in names):
            return command
    return ""


def _compiler_version(cxx: str):
    """("GCC" | "AppleClang" | "Clang", version) for a C++ compiler, or None."""
    out = bp._output([cxx, "--version"])
    version = re.search(r"\d+\.\d+(\.\d+)?", out)
    if not version:
        return None
    family = ("AppleClang" if "Apple clang" in out else
              "Clang" if "clang" in out.lower() else "GCC")
    return family, version.group(0)


def compiler_problems() -> list:
    """The host C/C++ compiler, which the pico-sdk builds pioasm and picotool
    with and the PC build compiles everything with."""
    family = ("AppleClang" if sys.platform == "darwin" else
              "Clang" if bp._is_windows() else "GCC")
    cc = next((c for c in ("cc", "gcc", "clang") if bp.shutil.which(c)), None)
    cxx = next((c for c in ("c++", "g++", "clang++") if bp.shutil.which(c)), None)
    found = _compiler_version(cxx) if cc and cxx else None
    if found:
        family = found[0]
    need = _common(f"host_{family.lower()}_min")
    if found and (not need or _version(found[1]) >= _version(need)):
        return []
    detail = f"{found[0]} {found[1]} found" if found else "not found"
    name = "C/C++ compiler"
    message = f"{detail} (need {family} {need} or newer)" if need else detail
    if sys.platform == "darwin":
        # Opens Apple's installer dialog, so it is left to the person.
        return [bp.Problem(None, name, message, "xcode-select --install")]
    command = _compiler_install_command()
    if not command or bp._is_windows():
        return [bp.Problem(None, name, message, command or "install a C/C++ compiler")]
    if os.geteuid() == 0:
        runner = []
    elif bp.shutil.which("sudo"):
        # Without a terminal sudo -n fails at once rather than waiting for a password.
        runner = ["sudo"] if sys.stdin.isatty() else ["sudo", "-n"]
    else:
        return [bp.Problem(None, name, message, f"(as root) {command}")]

    def install():
        bp.run(runner + ["sh", "-c", command], cwd=ROOT, board="build", key=None, quiet=True)

    return [bp.Problem(None, name, message,
                       f"{'sudo ' if runner else ''}sh -c '{command}'", install)]


def _common(key: str) -> str:
    """A [fujinet] value from platformio.common.ini, where the minimum versions live."""
    parser = configparser.ConfigParser(inline_comment_prefixes=(";", "#"), interpolation=None)
    parser.read(COMMON_INI)
    return parser.get("fujinet", key, fallback="").strip()


def python_problems(pc_build: bool) -> list:
    """PlatformIO's version (not for -p builds) and the python_modules.txt minimums."""
    wanted = []
    if not pc_build:
        pio_min = _common("platformio_min")
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
        message = f"{detail} (need {need} or newer)" if need else detail
        if package == "platformio":
            found.append(_pip_problem(name, f"{package}>={need}", message))
        else:
            found.append(_module_problem(name, message))
    return found


def _module_problem(name: str, message: str):
    """install_python_modules.sh installs every module, so all of them share one
    command, which install() runs once."""
    # Found through PATH: on Windows a bare "bash" resolves to System32's WSL
    # launcher before MSYS2's.
    bash = bp.shutil.which("bash") or "bash"
    script = MODULES_SH.replace(os.sep, "/")

    def install():
        bp.run([bash, script], cwd=ROOT, board="build", key=None,
               extra_env={"PYTHON": sys.executable}, quiet=True)

    return bp.Problem(None, name, message, f"PYTHON={sys.executable} bash {MODULES_SH}",
                      install, pip=True)


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
        found = compiler_problems() + python_problems(args.pc)
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
