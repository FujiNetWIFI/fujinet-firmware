#!/usr/bin/env python3
# pyright: reportUndefinedVariable=false
#
# build_pico.py -- builds a board's companion-MCU firmware (RP2040/RP2350/
# whatever its own CMakeLists.txt targets) and renders the result into
# fn_pico_blob_data.cpp in the env's build dir, which src/CMakeLists.txt
# adds to its sources. See lib/hardware/fn_pico_blob.h for the accessor
# layer consumers use.
#
# Runs both as a "pre:" extra_script (shared [env] list, so it sees every
# board) and as a CLI: ./build_pico.py <board>, or build.sh -P. Per-board
# config comes from [fujinet] pico_* keys, documented in
# platformio-ini-files/platformio.common.ini.
#
# Why not EMBED_FILES or objcopy? (load-bearing -- read before switching
# this to a "proper" ESP-IDF mechanism.) Both were tried and abandoned:
# EMBED_FILES computes a doubled output path (.pio/build/<env>/.pio/build/
# <env>/...) for files outside a component's source tree, and objcopy -I
# binary + target_link_libraries() compiles but never reaches firmware.elf
# -- PlatformIO uses CMake only to *discover* the source graph, then does
# its own SCons compile and link that never sees CMakeLists.txt link calls
# (the generated build.ninja has no firmware.elf rule at all). A plain
# generated source in the component's SRCS sidesteps both.
#
# The generated .cpp is written for EVERY board -- a stub with
# fn_pico_blob_count == 0 when there's nothing to bundle. That is
# deliberate: the previous design generated it only for fujiversal-intv and
# needed a second script to delete it for other boards, which silently
# failed once and linked Minty's image into fujiversal-rs232. Always
# generating makes the file a pure function of (board, ini, artifact bytes)
# so no such path exists. fn_pico_blob.h's header covers why weak symbols
# can't solve this instead.
#
# Any failure aborts the whole ESP32 build rather than falling back to a
# stale .bin -- a mismatched payload is far worse to debug than a build
# that stops. fail() names the board and the offending ini key, since the
# config now lives in the ini rather than in this file.
#
# SCons execs this with no __file__ (cwd is the project root), so CLI mode
# chdirs here explicitly to match. No Return(): it's SCons-only, raises,
# and would make this module unimportable.

import argparse
import configparser
import glob
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple

try:
    Import("env")
except NameError:
    env = None


class PicoBuildError(Exception):
    """Raised by fail() below. In SCons mode this simply propagates out of
    the "pre:" extra_script exec and aborts the build. In CLI mode main()
    catches it, prints it, and returns 1."""


def log(msg: str) -> None:
    print(f"build_pico.py: {msg}", flush=True)


def fail(board: str, key: Optional[str], msg: str):
    key_part = f" [ini key: {key}]" if key else ""
    raise PicoBuildError(f"build_pico.py: board '{board}': {msg}{key_part}")


def run(cmd: List[str], cwd: str, board: str, key: Optional[str],
        extra_env: Optional[Dict[str, str]] = None, dry_run: bool = False,
        capture: bool = False, quiet: bool = False) -> Optional[str]:
    printable = " ".join(shlex.quote(c) for c in cmd)
    if not quiet:
        log(f"[{board}] running: {printable}  (in {cwd})")
    if dry_run:
        log(f"[{board}] (--dry-run: not executed)")
        return "" if capture else None

    build_env = os.environ.copy()
    if extra_env:
        build_env.update(extra_env)

    if capture:
        result = subprocess.run(cmd, cwd=cwd, env=build_env,
                                 stdout=subprocess.PIPE, text=True)
        if result.returncode != 0:
            fail(board, key, f"'{printable}' exited {result.returncode}")
        return result.stdout.strip()

    if quiet:
        # Its output is only shown when it fails.
        result = subprocess.run(cmd, cwd=cwd, env=build_env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)
        if result.returncode != 0:
            print(result.stdout, file=sys.stderr)
    else:
        result = subprocess.run(cmd, cwd=cwd, env=build_env)
    if result.returncode != 0:
        fail(board, key, f"'{printable}' exited {result.returncode}")
    return None


# ---------------------------------------------------------------------------
# Fixed paths
# ---------------------------------------------------------------------------

# In the env's build dir: a copy left in the source tree survives a checkout
# and breaks building older commits. src/CMakeLists.txt lists this name.
GENERATED_CPP = "fn_pico_blob_data.cpp"
# Where older builds wrote it; the lib/hardware/*.cpp glob would compile it too.
LEGACY_GENERATED_CPP = os.path.join("lib", "hardware", GENERATED_CPP)

CLEAN_TARGETS = {"clean", "cleanall"}
# Don't link firmware.elf, so they don't need a companion build -- `pio run
# -t clean` used to run the entire pico build just to discard the result.
NO_LINK_TARGETS = {"buildfs", "uploadfs", "erase", "envdump", "idedata", "monitor"}

CMAKE_MODES = ("cmake-ninja", "cmake-make")
BUILD_MODES = ("cmake-ninja", "cmake-make", "make", "command")

BLOB_NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]*$")
# A blob name doubles as its NVS key on the ESP32 side (namespace "picofw",
# one entry per blob holding the sha256 last successfully flashed), and NVS
# keys are capped at 15 characters plus a NUL. Enforced here so the failure
# lands on whoever edits the ini, not at runtime on a device where the
# nvs_set_str() would just return ESP_ERR_NVS_KEY_TOO_LONG and the updater
# would reflash the companion on every single boot.
BLOB_NAME_MAX = 15

# Erase granularity of every RP-series flash part; also the write chunk the
# ESP32 side uses. Only needed here to bounds-check against pico_flash_limit
# the same way PicobootClient::flashImage() does at runtime.
PICO_FLASH_SECTOR_SIZE = 4096

# Companion chips the ESP32-side updater knows how to reboot. Kept in sync
# with FN_PICO_CHIP_* in lib/hardware/fn_pico_blob.h: RP2040 takes PC_REBOOT,
# RP2350/RP2354 take PC_REBOOT2, and they enumerate in BOOTSEL under
# different USB PIDs -- so a wrong value here is not cosmetic.
# RP2354 is an RP2350 die with stacked flash: same bootrom, same BOOTSEL USB
# PID, same PC_REBOOT2 -- it is spelled "rp2350" here, not given its own value.
PICO_CHIPS = {"rp2040": "FN_PICO_CHIP_RP2040", "rp2350": "FN_PICO_CHIP_RP2350"}

# The ESP32-side build flag that compiles in the updater which consumes these
# blobs. A board carrying a blob without it would embed a few hundred KB of
# rodata that nothing reads, and the companion would silently never be
# flashed -- so the two are required to agree. See _require_picoboot_define().
PICOBOOT_DEFINE = "CONFIG_USB_PICOBOOT_HOST_ENABLED"

# Written next to the ESP32 build outputs for build_firmwarezip.py to fold
# into release.json's optional "companion" array, which is what tells the
# FujiNet-Flasher to mention the second flashing step. Written for EVERY
# board (an empty list when there is nothing to embed), same invariant as
# GENERATED_CPP.
BLOB_SIDECAR_JSON = "fn_pico_blobs.json"

# Searched in order when PICO_SDK_PATH is unset, after the --install location.
PICO_SDK_SEARCH = ("~/.pico-sdk/sdk/*", "~/pico/pico-sdk", "/usr/share/pico-sdk",
                   "/usr/local/share/pico-sdk", "/opt/pico-sdk")
# --install clones pico_sdk_version here, one directory per version; relative to
# the project root, which is the cwd in both modes.
PICO_SDK_INSTALL_ROOT = os.path.join("build", "pico-sdk")
PICO_SDK_REPO = "https://github.com/raspberrypi/pico-sdk.git"

# Read under every ini, so a bare board ini still gets the shared pico_* versions.
COMMON_INI = os.path.join("platformio-ini-files", "platformio.common.ini")

TRUE_WORDS = {"yes", "true", "1", "on"}
FALSE_WORDS = {"no", "false", "0", "off"}


# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------

class PicoConfig:
    """Everything resolved from one board's [fujinet] pico_* ini keys."""

    def __init__(self, board, ini_path, src, build_mode, build_dir, build_type,
                 pico_board, cmake_args, make_args, command_lines, prebuild_lines,
                 toolchain, toolchain_package, toolchain_min, cmake_version,
                 ninja_version, sdk_path, sdk_required, sdk_version, sdk_explicit,
                 artifacts, repo, repo_ref, repo_dir, chip, flash_base, flash_limit):
        self.board = board
        self.ini_path = ini_path
        self.src = src
        self.build_mode = build_mode
        self.build_dir = build_dir
        self.build_type = build_type
        self.pico_board = pico_board
        self.cmake_args = cmake_args
        self.make_args = make_args
        self.command_lines = command_lines
        self.prebuild_lines = prebuild_lines
        self.toolchain = toolchain
        self.toolchain_package = toolchain_package  # PlatformIO spec, "" = none
        self.toolchain_min = toolchain_min          # oldest gcc accepted, "" = any
        self.cmake_version = cmake_version          # oldest cmake accepted, "" = any
        self.ninja_version = ninja_version          # oldest ninja accepted, "" = any
        self.sdk_path = sdk_path
        self.sdk_required = sdk_required
        self.sdk_version = sdk_version              # exact SDK version, "" = any
        self.sdk_explicit = sdk_explicit            # from PICO_SDK_PATH/pico_sdk_path, not searched
        self.artifacts = artifacts  # dict: name -> path (relative to build dir)
        self.repo = repo
        self.repo_ref = repo_ref
        self.repo_dir = repo_dir
        self.chip = chip                # "rp2040" | "rp2350"
        self.flash_base = flash_base    # int, XIP address the image is written at
        self.flash_limit = flash_limit  # int, 0 = no limit; erase/write never reaches it

    @property
    def build_dir_abs(self) -> str:
        return os.path.join(self.src, self.build_dir)


class _Placeholders(dict):
    """Defaulting dict for str.format_map(): an unpopulated placeholder
    (a typo, or e.g. {pico_board} on a board that doesn't set it) expands
    to "" rather than raising KeyError."""

    def __missing__(self, key):
        return ""


def _repo_name(url: str) -> str:
    name = url.rstrip("/").split("/")[-1]
    if name.endswith(".git"):
        name = name[:-4]
    return name or "external"


def sanitize_c_ident(name: str) -> str:
    """Blob names are validated against BLOB_NAME_RE (so they're safe as C
    string literals), but that charset allows '.' and '-', which are not
    legal in a C/C++ identifier. This derives the identifier used for the
    generated `fn_pico_blob_<ident>` byte array from the blob name."""
    ident = re.sub(r"[^A-Za-z0-9_]", "_", name)
    if not ident or not (ident[0].isalpha() or ident[0] == "_"):
        ident = "_" + ident
    return ident


def parse_artifacts(raw: str, board: str) -> "Dict[str, str]":
    """Parses an already placeholder-expanded pico_artifacts value: one
    entry per non-blank/non-comment line, either `name = path` or a bare
    `path` (name then defaults to the path's file stem, with any character
    outside the blob-name charset replaced by '_')."""
    result: Dict[str, str] = {}
    for line in raw.splitlines():
        line = line.strip()
        if not line or line.startswith((";", "#")):
            continue
        if "=" in line:
            name, path = line.split("=", 1)
            name = name.strip()
            path = path.strip()
        else:
            path = line
            stem = Path(path).stem
            name = re.sub(r"[^A-Za-z0-9_.-]", "_", stem)
        if not path:
            fail(board, "pico_artifacts", f"empty path on line '{line}'")
        if not BLOB_NAME_RE.match(name):
            fail(board, "pico_artifacts",
                 f"invalid artifact name '{name}' (from line '{line}') -- "
                 f"must match {BLOB_NAME_RE.pattern}")
        if len(name) > BLOB_NAME_MAX:
            fail(board, "pico_artifacts",
                 f"artifact name '{name}' is {len(name)} characters -- the "
                 f"limit is {BLOB_NAME_MAX}, because the ESP32 side uses it "
                 f"verbatim as an NVS key to remember which image it last "
                 f"flashed, and NVS keys are capped at {BLOB_NAME_MAX} chars")
        if name in result:
            fail(board, "pico_artifacts", f"duplicate artifact name '{name}'")
        result[name] = path
    return result


def _version_key(path: str) -> List[int]:
    return [int(p) for p in re.findall(r"\d+", os.path.basename(path))]


def _parse_version(text: str) -> Tuple[int, ...]:
    return tuple(int(p) for p in re.findall(r"\d+", text))


def sdk_version(path: str) -> Optional[str]:
    """The version an SDK reports in pico_sdk_version.cmake, or None."""
    try:
        with open(os.path.join(path, "pico_sdk_version.cmake")) as f:
            text = f.read()
    except OSError:
        return None
    parts = [re.search(rf"PICO_SDK_VERSION_{p}\s+(\d+)", text)
             for p in ("MAJOR", "MINOR", "REVISION")]
    if not all(parts):
        return None
    return ".".join(m.group(1) for m in parts)


def sdk_install_dir(version: str) -> str:
    # Absolute: the companion build runs with cwd=pico_src.
    return os.path.abspath(os.path.join(PICO_SDK_INSTALL_ROOT, version))


def _find_pico_sdk(version: str) -> str:
    """The first SDK at version, trying the --install location and then
    PICO_SDK_SEARCH (newest first within a glob). Failing that, the first SDK
    of any version, so preflight() can name the mismatch; "" if none."""
    candidates = [sdk_install_dir(version)] if version else []
    for pattern in PICO_SDK_SEARCH:
        candidates += sorted(glob.glob(os.path.expanduser(pattern)),
                             key=_version_key, reverse=True)
    sdks = [p for p in candidates if os.path.isfile(os.path.join(p, "pico_sdk_init.cmake"))]
    for path in sdks:
        if not version or sdk_version(path) == version:
            log(f"PICO_SDK_PATH not set -- using {path}")
            return path
    return sdks[0] if sdks else ""


def read_config(ini_path: str, board: str) -> Optional[PicoConfig]:
    """Reads [fujinet] from ini_path and returns a PicoConfig, or None if
    this ini simply doesn't describe a pico build for `board` (missing
    file, missing [fujinet] section, no pico_src, or a build_board that
    names a different board -- see the "board guard" note below). A real
    configuration *problem* (bad pico_build value, missing pico_repo_ref,
    etc.) is a hard fail via fail(), never a silent None."""
    parser = configparser.ConfigParser(inline_comment_prefixes=(";", "#"))
    if not os.path.isfile(ini_path):
        return None
    parser.read([COMMON_INI, ini_path])
    if not parser.has_section("fujinet"):
        return None
    section = parser["fujinet"]

    # The merged ini has one global [fujinet] describing one board; if it
    # names a different board, treat it as "no pico config" rather than
    # building the wrong thing.
    build_board = section.get("build_board", "").strip()
    pico_src = section.get("pico_src", "").strip()
    if not pico_src:
        return None
    if not build_board:
        fail(board, "build_board",
             "[fujinet] pico_src is set but build_board is missing -- the "
             "board guard needs it to avoid leaking one board's companion "
             "firmware into another board's build")
    if build_board != board:
        return None

    def get_bool(key: str, default: bool) -> bool:
        raw = section.get(key, "").strip()
        if raw == "":
            return default
        low = raw.lower()
        if low in TRUE_WORDS:
            return True
        if low in FALSE_WORDS:
            return False
        fail(board, key, f"unrecognized boolean value '{raw}' -- expected "
             f"one of {sorted(TRUE_WORDS | FALSE_WORDS)}")
        raise AssertionError("unreachable")  # fail() always raises

    pico_repo = section.get("pico_repo", "").strip() or None
    pico_repo_ref = section.get("pico_repo_ref", "").strip() or None
    if pico_repo and not pico_repo_ref:
        fail(board, "pico_repo_ref",
             "pico_repo is set but pico_repo_ref (a pinned SHA, never a "
             "branch) is required")
    pico_repo_dir = section.get("pico_repo_dir", "").strip() or None
    if pico_repo and not pico_repo_dir:
        pico_repo_dir = os.path.join("pico", "_external", _repo_name(pico_repo))

    pico_build = section.get("pico_build", "cmake-ninja").strip() or "cmake-ninja"
    if pico_build not in BUILD_MODES:
        fail(board, "pico_build",
             f"unsupported value '{pico_build}' -- expected one of {BUILD_MODES}")

    pico_build_dir = section.get("pico_build_dir", "build").strip() or "build"
    pico_build_type = section.get("pico_build_type", "Release").strip() or "Release"
    pico_board = section.get("pico_board", "").strip() or None

    # Which chip the artifact is for. No default: the ESP32 side reboots
    # RP2040 and RP2350/RP2354 with different PICOBOOT commands and matches
    # a different BOOTSEL USB PID, and guessing wrong fails on hardware
    # rather than here. RP2354 is an RP2350 die -- spell it "rp2350".
    pico_chip = section.get("pico_chip", "").strip().lower()
    if not pico_chip:
        fail(board, "pico_chip",
             "[fujinet] pico_src is set but pico_chip is missing -- give the "
             f"companion chip, one of {sorted(PICO_CHIPS)} (RP2354 counts as "
             f"rp2350)")
    if pico_chip not in PICO_CHIPS:
        fail(board, "pico_chip",
             f"unsupported chip '{pico_chip}' -- expected one of "
             f"{sorted(PICO_CHIPS)} (RP2354 counts as rp2350)")

    def get_addr(key: str, default: int) -> int:
        raw = section.get(key, "").strip()
        if raw == "":
            return default
        try:
            return int(raw, 0)
        except ValueError:
            fail(board, key, f"'{raw}' is not an integer (use 0x... for hex)")
            raise AssertionError("unreachable")

    # Where the image is written. 0x10000000 is the XIP base on every
    # RP-series part, so the default is right unless a board deliberately
    # writes somewhere else (a second slot, say).
    pico_flash_base = get_addr("pico_flash_base", 0x10000000)
    # A hard ceiling the erase/write must stay below. The Intellivision cart
    # is why this exists: its LittleFS lives at flash offset 0x100000 and
    # holds user ROMs and saves, so an oversized image must fail the build
    # here rather than eat the filesystem on the first boot after an update.
    pico_flash_limit = get_addr("pico_flash_limit", 0)
    if pico_flash_limit and pico_flash_limit <= pico_flash_base:
        fail(board, "pico_flash_limit",
             f"0x{pico_flash_limit:08x} is not above pico_flash_base "
             f"0x{pico_flash_base:08x}")

    pico_sdk_version = section.get("pico_sdk_version", "").strip()
    pico_sdk_path = (section.get("pico_sdk_path", "").strip()
                     or os.environ.get("PICO_SDK_PATH", "").strip())
    pico_sdk_explicit = bool(pico_sdk_path)
    if pico_sdk_path:
        pico_sdk_path = os.path.expanduser(os.path.expandvars(pico_sdk_path))
    else:
        pico_sdk_path = _find_pico_sdk(pico_sdk_version)

    # cmake-* modes need PICO_SDK_PATH by construction (pico_sdk_init.cmake);
    # make/command modes might not (e.g. a Makefile that vendors everything
    # it needs), so they default to not requiring it, but pico_sdk_required
    # lets a board's ini override either default explicitly.
    pico_sdk_required = get_bool("pico_sdk_required", pico_build in CMAKE_MODES)

    placeholders = _Placeholders({
        "board": board,
        "pico_board": pico_board or "",
        "build_type": pico_build_type,
        "src": pico_src,
        # Repo-relative, NOT relative to the cwd a command runs in --
        # pico_prebuild/pico_command lines run with cwd=pico_src, so use
        # {repo} for anything outside the companion source tree.
        "build_dir": os.path.join(pico_src, pico_build_dir),
        "sdk": pico_sdk_path,
        # Absolute project root. Both modes run with cwd = project root at
        # this point (SCons by construction, CLI via main()'s chdir).
        "repo": os.getcwd(),
    })

    def expand(key: str) -> str:
        raw = section.get(key, "")
        try:
            return raw.format_map(placeholders)
        except (KeyError, IndexError, ValueError) as e:
            fail(board, key, f"placeholder expansion failed: {e}")
            raise AssertionError("unreachable")

    def parse_multiline_args(key: str) -> List[str]:
        args: List[str] = []
        for line in expand(key).splitlines():
            line = line.strip()
            if not line or line.startswith((";", "#")):
                continue
            args.extend(shlex.split(line))
        return args

    pico_toolchain_raw = section.get("pico_toolchain", "").strip()
    if pico_toolchain_raw:
        toolchain = pico_toolchain_raw.split()
    else:
        # Only cmake-* modes get an implicit default -- make/command modes
        # are free-form and shouldn't be forced to depend on an ARM
        # cross-compiler that a given companion project might not even use.
        toolchain = (["arm-none-eabi-gcc", "arm-none-eabi-g++", "arm-none-eabi-objcopy"]
                     if pico_build in CMAKE_MODES else [])

    cmake_args = parse_multiline_args("pico_cmake_args")
    make_args = parse_multiline_args("pico_make_args")

    def parse_command_lines(key: str) -> List[List[str]]:
        lines: List[List[str]] = []
        for line in expand(key).splitlines():
            line = line.strip()
            if not line or line.startswith((";", "#")):
                continue
            lines.append(shlex.split(line))
        return lines

    command_lines = parse_command_lines("pico_command")
    if pico_build == "command" and not command_lines:
        fail(board, "pico_command",
             "pico_build = command requires at least one pico_command line")

    # Runs before the configure/build step in EVERY mode, with cwd=pico_src,
    # after the build dir has been created. Exists for generated sources the
    # companion build treats as inputs -- the fujiversal tree includes its
    # cartridge ROM as build/<BOARD>/rom.h, which upstream's Makefile
    # produces with xxd and this repo produces with pico/tools/rom2h.py.
    prebuild_lines = parse_command_lines("pico_prebuild")

    artifacts = parse_artifacts(expand("pico_artifacts"), board)

    return PicoConfig(
        board=board, ini_path=ini_path, src=pico_src, build_mode=pico_build,
        build_dir=pico_build_dir, build_type=pico_build_type,
        pico_board=pico_board, cmake_args=cmake_args, make_args=make_args,
        command_lines=command_lines, prebuild_lines=prebuild_lines,
        toolchain=toolchain,
        toolchain_package=section.get("pico_toolchain_package", "").strip(),
        toolchain_min=section.get("pico_toolchain_min", "").strip(),
        cmake_version=section.get("pico_cmake_version", "").strip(),
        ninja_version=section.get("pico_ninja_version", "").strip(),
        sdk_path=pico_sdk_path, sdk_required=pico_sdk_required,
        sdk_version=pico_sdk_version, sdk_explicit=pico_sdk_explicit,
        artifacts=artifacts, repo=pico_repo, repo_ref=pico_repo_ref,
        repo_dir=pico_repo_dir, chip=pico_chip, flash_base=pico_flash_base,
        flash_limit=pico_flash_limit,
    )


def resolve_config(ini_path: str, board: str) -> Tuple[Optional[PicoConfig], str]:
    """Applies the "if the resolved ini has no pico_src, fall back to
    build-platforms/platformio-<board>.ini" rule that applies in BOTH SCons
    and CLI mode -- this is what makes `./build_pico.py fujiversal-intv`
    work on a clean checkout with no platformio-generated.ini present, and
    what makes the shared "pre:" script a no-op the first time a brand new
    board (with no pico_src at all) is ever built."""
    cfg = read_config(ini_path, board)
    if cfg is not None:
        return cfg, ini_path
    fallback = os.path.join("build-platforms", f"platformio-{board}.ini")
    if os.path.abspath(fallback) == os.path.abspath(ini_path):
        return None, ini_path
    if not os.path.isfile(fallback):
        return None, ini_path
    cfg = read_config(fallback, board)
    if cfg is not None:
        return cfg, fallback
    return None, ini_path


# ---------------------------------------------------------------------------
# Source acquisition (local dir, or a pinned-ref shallow clone)
# ---------------------------------------------------------------------------

def _clone_dirty(repo_dir: str) -> bool:
    if not os.path.isdir(os.path.join(repo_dir, ".git")):
        return False
    result = subprocess.run(["git", "status", "--porcelain"], cwd=repo_dir,
                             stdout=subprocess.PIPE, text=True)
    return bool(result.stdout.strip())


def _read_stamp(stamp_path: str) -> Tuple[Optional[str], Optional[str]]:
    if not os.path.isfile(stamp_path):
        return None, None
    ref = sha = None
    with open(stamp_path) as f:
        for line in f:
            line = line.strip()
            if line.startswith("ref="):
                ref = line[len("ref="):]
            elif line.startswith("sha="):
                sha = line[len("sha="):]
    return ref, sha


def _write_stamp(stamp_path: str, ref: str, sha: str) -> None:
    with open(stamp_path, "w") as f:
        f.write(f"ref={ref}\nsha={sha}\n")


def _ensure_remote_source(cfg: PicoConfig, force_external: bool, dry_run: bool) -> None:
    repo_dir = cfg.repo_dir
    stamp_path = os.path.join(repo_dir, ".fujinet-pico-ref")
    stored_ref, stored_sha = _read_stamp(stamp_path)
    have_clone = os.path.isdir(os.path.join(repo_dir, ".git"))

    up_to_date = have_clone and stored_ref == cfg.repo_ref and stored_sha
    if up_to_date:
        head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=repo_dir,
                               stdout=subprocess.PIPE, text=True)
        up_to_date = head.returncode == 0 and head.stdout.strip() == stored_sha

    if up_to_date:
        # NEVER a network round-trip on an unchanged build: this is the
        # whole point of the stamp file.
        log(f"[{cfg.board}] {repo_dir} already at pico_repo_ref="
            f"{cfg.repo_ref}, skipping fetch")
        return

    if have_clone and _clone_dirty(repo_dir) and not force_external:
        fail(cfg.board, "pico_repo",
             f"{repo_dir} has local modifications -- refusing to discard "
             f"them (pass --force-external to override, or clean/remove "
             f"{repo_dir} yourself)")

    if dry_run:
        log(f"[{cfg.board}] (--dry-run) would fetch pico_repo={cfg.repo} at "
            f"pico_repo_ref={cfg.repo_ref} into {repo_dir}")
        return

    # On any ref change, the old build dir is for the old ref's source --
    # delete it before (re)cloning so a stale build.ninja/Makefile sentinel
    # can never cause a half-updated build to be reused.
    build_dir_abs = cfg.build_dir_abs
    if os.path.isdir(build_dir_abs):
        log(f"[{cfg.board}] pico_repo_ref changed -- removing stale build "
            f"dir {build_dir_abs}")
        shutil.rmtree(build_dir_abs)

    os.makedirs(repo_dir, exist_ok=True)
    if not have_clone:
        run(["git", "init"], cwd=repo_dir, board=cfg.board, key="pico_repo")
        run(["git", "remote", "add", "origin", cfg.repo], cwd=repo_dir,
            board=cfg.board, key="pico_repo")
    else:
        run(["git", "remote", "set-url", "origin", cfg.repo], cwd=repo_dir,
            board=cfg.board, key="pico_repo")

    log(f"[{cfg.board}] fetching pico_repo_ref={cfg.repo_ref} (shallow)")
    shallow = subprocess.run(["git", "fetch", "--depth", "1", "origin", cfg.repo_ref],
                              cwd=repo_dir, env=os.environ.copy())
    if shallow.returncode != 0:
        # Some git servers (notably plain `git daemon`/older self-hosted
        # setups without `uploadpack.allowAnySHA1InWant`) refuse to serve an
        # arbitrary SHA shallowly. Fall back to a full, non-shallow fetch.
        log(f"[{cfg.board}] shallow fetch of an arbitrary ref/SHA failed "
            f"(server may not support allowAnySHA1InWant) -- retrying "
            f"non-shallow")
        run(["git", "fetch", "origin", cfg.repo_ref], cwd=repo_dir,
            board=cfg.board, key="pico_repo_ref")

    run(["git", "checkout", "--force", "FETCH_HEAD"], cwd=repo_dir,
        board=cfg.board, key="pico_repo_ref")

    resolved = run(["git", "rev-parse", "HEAD"], cwd=repo_dir, board=cfg.board,
                    key="pico_repo_ref", capture=True)
    _write_stamp(stamp_path, cfg.repo_ref, resolved)
    log(f"[{cfg.board}] pico_repo={cfg.repo} pico_repo_ref={cfg.repo_ref} "
        f"-> {resolved} cloned into {repo_dir}")


def _submodule_paths() -> List[str]:
    """Paths listed in .gitmodules, normalized. Parsed directly rather than
    shelled out to `git config -f`, so this works from a tarball export with
    no git available."""
    paths: List[str] = []
    if not os.path.isfile(".gitmodules"):
        return paths
    parser = configparser.ConfigParser()
    try:
        # .gitmodules is INI-shaped but its section names are quoted
        # ([submodule "pico/fujiversal"]), which configparser handles fine.
        parser.read(".gitmodules")
    except configparser.Error:
        return paths
    for sect in parser.sections():
        path = parser[sect].get("path", "").strip()
        if path:
            paths.append(os.path.normpath(path))
    return paths


def _check_submodule_initialized(cfg: PicoConfig) -> None:
    """An uninitialized submodule is an empty directory, so the CMakeLists.txt
    check below would report 'no CMakeLists.txt found', which is true but
    sends the reader looking for a broken ini key instead of a one-line fix."""
    src = os.path.normpath(cfg.src)
    for sub in _submodule_paths():
        if src == sub or src.startswith(sub + os.sep):
            # No .git: a tarball export, nothing to fetch from.
            if (not os.path.isdir(sub) or not os.listdir(sub)) and os.path.exists(".git"):
                log(f"[{cfg.board}] {sub} not checked out -- running "
                    f"git submodule update --init --recursive {sub}")
                try:
                    subprocess.run(["git", "submodule", "--quiet", "update", "--init",
                                    "--recursive", sub])
                except OSError:
                    pass
            if not os.path.isdir(sub) or not os.listdir(sub):
                fail(cfg.board, "pico_src",
                     f"{sub} is a git submodule that has not been checked "
                     f"out -- run: git submodule update --init {sub}")
            return


def ensure_source(cfg: PicoConfig, force_external: bool = False, dry_run: bool = False) -> None:
    if cfg.repo:
        _ensure_remote_source(cfg, force_external=force_external, dry_run=dry_run)
    else:
        _check_submodule_initialized(cfg)

    if not os.path.isdir(cfg.src):
        fail(cfg.board, "pico_src", f"directory does not exist: {cfg.src}")
    if cfg.build_mode in CMAKE_MODES and not os.path.isfile(os.path.join(cfg.src, "CMakeLists.txt")):
        fail(cfg.board, "pico_src",
             f"no CMakeLists.txt found in {cfg.src} (pico_build={cfg.build_mode})")
    if cfg.build_mode == "make" and not os.path.isfile(os.path.join(cfg.src, "Makefile")):
        fail(cfg.board, "pico_src", f"no Makefile found in {cfg.src} (pico_build=make)")


def source_revision(cfg: PicoConfig) -> str:
    """Best-effort identity of the companion source tree, recorded in the
    generated .cpp's comment header and the sidecar JSON so a firmware image
    can be traced back to the commit its companion blob came from."""
    # os.path.exists, not isdir: in a submodule checkout .git is a FILE
    # holding a gitdir: pointer, so isdir() would report every submodule as
    # in-tree and lose the very revision worth recording.
    if not os.path.exists(os.path.join(cfg.src, ".git")):
        return "in-tree"
    result = subprocess.run(["git", "-C", cfg.src, "rev-parse", "HEAD"],
                             stdout=subprocess.PIPE,
                             stderr=subprocess.DEVNULL, text=True)
    if result.returncode != 0:
        return "unknown"
    return result.stdout.strip() or "unknown"


# ---------------------------------------------------------------------------
# Preflight + build
# ---------------------------------------------------------------------------

def _pio_core_dir() -> str:
    return os.environ.get("PLATFORMIO_CORE_DIR") or os.path.expanduser("~/.platformio")


def _split_spec(spec: str) -> Tuple[str, str]:
    """("toolchain-gccarmnoneeabi", "1.140201.0") from
    "platformio/toolchain-gccarmnoneeabi@1.140201.0"."""
    name, _, version = spec.partition("@")
    return name.split("/")[-1], version


def _package_bin(spec: str) -> Optional[str]:
    """Executable dir of the installed PlatformIO package matching spec."""
    if not spec:
        return None
    name, version = _split_spec(spec)
    for pkg in sorted(glob.glob(os.path.join(_pio_core_dir(), "packages", name + "*"))):
        try:
            with open(os.path.join(pkg, "package.json")) as f:
                meta = json.load(f)
        except (OSError, ValueError):
            continue
        if meta.get("name") == name and (not version or meta.get("version") == version):
            bin_dir = os.path.join(pkg, "bin")
            return bin_dir if os.path.isdir(bin_dir) else pkg
    return None


def _in_venv() -> bool:
    return sys.prefix != sys.base_prefix


def tool_path(cfg: PicoConfig) -> str:
    """PATH with the installed toolchain package, then the venv's pip-installed
    cmake and ninja, ahead of the system's."""
    dirs = [_package_bin(cfg.toolchain_package)]
    if _in_venv():
        dirs.append(os.path.dirname(sys.executable))
    return os.pathsep.join([d for d in dirs if d] + [os.environ.get("PATH", "")])


def _is_windows() -> bool:
    return os.name == "nt" or sys.platform.startswith(("msys", "cygwin"))


def _output(cmd: List[str]) -> str:
    try:
        return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                              text=True).stdout.strip()
    except OSError:
        return ""


def _cmake_minimum(src: str) -> str:
    try:
        with open(os.path.join(src, "CMakeLists.txt")) as f:
            m = re.search(r"cmake_minimum_required\s*\(\s*VERSION\s+([\d.]+)", f.read(), re.I)
    except OSError:
        return ""
    return m.group(1) if m else ""


class Problem:
    """A missing or unsuitable prerequisite. command is the fix for a person
    to run; install, when set, is what --install runs for it."""

    def __init__(self, key: str, name: str, message: str, command: str = "", install=None,
                 pip: bool = False):
        self.key = key
        self.pip = pip          # a pip install into the build venv, allowed on Windows too
        self.name = name        # what is missing, e.g. "cmake"
        self.message = message
        self.command = command
        self.install = install


def _pip_problem(cfg: PicoConfig, key: str, package: str, version: str,
                 message: str) -> Problem:
    spec = f"{package}=={version}"
    command = f"{sys.executable} -m pip install {spec}"
    if not version or not _in_venv():
        # Outside a venv pip would write into the system Python.
        return Problem(key, package, message, command if version else "")

    def install():
        run([sys.executable, "-m", "pip", "install", spec], cwd=".", board=cfg.board, key=key,
            quiet=True)

    return Problem(key, package, message, command, install, pip=True)


def _package_problem(cfg: PicoConfig, key: str, name: str, spec: str,
                     message: str) -> Problem:
    if not spec:
        return Problem(key, name, message)

    def install():
        pio = shutil.which("pio")
        cmd = [pio] if pio else [sys.executable, "-m", "platformio"]
        run(cmd + ["pkg", "install", "-g", "-t", spec], cwd=".", board=cfg.board, key=key,
            quiet=True)

    return Problem(key, name, message, f"pio pkg install -g -t {spec}", install)


def _sdk_problem(cfg: PicoConfig, message: str) -> Problem:
    name = f"pico-sdk {cfg.sdk_version}".strip()
    if not cfg.sdk_version:
        return Problem("pico_sdk_path", name, message)
    dest = sdk_install_dir(cfg.sdk_version)
    command = (f"git clone --depth 1 --branch {cfg.sdk_version} {PICO_SDK_REPO} {dest} && "
               f"git -C {dest} submodule update --init --depth 1 lib/tinyusb")
    if cfg.sdk_explicit:
        # The search never overrides a path that was set, so point it at the clone.
        if sdk_version(dest) == cfg.sdk_version:
            command = ""
        return Problem("pico_sdk_path", name, message,
                       " && ".join(filter(None, [command, f"export PICO_SDK_PATH={dest}"])))

    def install():
        partial = dest + ".partial"
        shutil.rmtree(partial, ignore_errors=True)
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        run(["git", "-c", "advice.detachedHead=false", "clone", "--depth", "1", "--branch",
             cfg.sdk_version, PICO_SDK_REPO, partial], cwd=".", board=cfg.board,
            key="pico_sdk_version", quiet=True)
        run(["git", "-C", partial, "submodule", "update", "--init", "--depth", "1",
             "lib/tinyusb"], cwd=".", board=cfg.board, key="pico_sdk_version", quiet=True)
        shutil.rmtree(dest, ignore_errors=True)
        os.replace(partial, dest)

    return Problem("pico_sdk_version", name, message, command, install)


def _sdk_problems(cfg: PicoConfig) -> List[Problem]:
    path = cfg.sdk_path
    if not path:
        return [_sdk_problem(cfg, "not found (PICO_SDK_PATH is not set)")]
    if not os.path.isfile(os.path.join(path, "pico_sdk_init.cmake")):
        return [_sdk_problem(cfg, f"PICO_SDK_PATH {path} is not a pico-sdk (no "
                                  f"pico_sdk_init.cmake)")]
    found = sdk_version(path)
    if cfg.sdk_version and found != cfg.sdk_version:
        return [_sdk_problem(cfg, f"PICO_SDK_PATH {path} is "
                                  f"{found or 'an unknown version'}")]
    # Without it the SDK configure only warns; the compile then fails on tusb.h.
    tinyusb = os.path.join(path, "lib", "tinyusb")
    if not os.path.isdir(tinyusb) or not os.listdir(tinyusb):
        return [Problem("pico_sdk_path", "pico-sdk tinyusb submodule", f"{tinyusb} is empty",
                        f"git -C {path} submodule update --init lib/tinyusb")]
    return []


def problems(cfg: PicoConfig) -> List[Problem]:
    """Every missing or unsuitable prerequisite, each with its fix."""
    tools: List[str] = list(cfg.toolchain)
    if cfg.build_mode == "cmake-ninja":
        tools += ["cmake", "ninja"]
    elif cfg.build_mode == "cmake-make":
        tools += ["cmake", "make"]
    elif cfg.build_mode == "make":
        tools += ["make"]
    tools = list(dict.fromkeys(tools))
    path = tool_path(cfg)
    found = {t: shutil.which(t, path=path) for t in tools}
    result: List[Problem] = []

    arm = [t for t in tools if t.startswith("arm-none-eabi-")]
    missing_arm = [t for t in arm if not found[t]]
    if missing_arm:
        result.append(_package_problem(cfg, "pico_toolchain_package", "ARM compiler",
                                       cfg.toolchain_package,
                                       "not found" if missing_arm == arm
                                       else f"{', '.join(missing_arm)} not found"))
    elif "arm-none-eabi-gcc" in arm:
        gcc = found["arm-none-eabi-gcc"]
        version = _output([gcc, "-dumpversion"])
        libs = [("arm-none-eabi-gcc", "libc.a"), ("arm-none-eabi-g++", "libstdc++.a")]
        no_lib = [lib for compiler, lib in libs if found.get(compiler)
                  and not os.path.isabs(_output([found[compiler], f"-print-file-name={lib}"]))]
        if cfg.toolchain_min and _parse_version(version) < _parse_version(cfg.toolchain_min):
            result.append(_package_problem(cfg, "pico_toolchain_min", "ARM compiler",
                                           cfg.toolchain_package,
                                           f"{gcc} is {version}; need "
                                           f"{cfg.toolchain_min} or newer"))
        elif no_lib:
            result.append(_package_problem(cfg, "pico_toolchain_package", "ARM compiler",
                                           cfg.toolchain_package,
                                           f"{gcc} has no {' or '.join(no_lib)}"))

    cmake_need = max([v for v in (cfg.cmake_version, _cmake_minimum(cfg.src)) if v],
                     key=_parse_version, default="")
    for tool, need, key in (("cmake", cmake_need, "pico_cmake_version"),
                            ("ninja", cfg.ninja_version, "pico_ninja_version")):
        if tool not in tools:
            continue
        version = getattr(cfg, f"{tool}_version")
        if not found[tool]:
            result.append(_pip_problem(cfg, key, tool, version,
                                       f"not found (need {need} or newer)" if need
                                       else "not found"))
            continue
        have = re.search(r"\d+\.\d+(\.\d+)?", _output([found[tool], "--version"]))
        if need and have and _parse_version(have.group(0)) < _parse_version(need):
            result.append(_pip_problem(cfg, key, tool, version,
                                       f"{found[tool]} is {have.group(0)}; need "
                                       f"{need} or newer"))
    other = [t for t in tools if not found[t] and t not in arm and t not in ("cmake", "ninja")]
    if other:
        result.append(Problem("pico_toolchain", ", ".join(other), "not found"))

    if cfg.sdk_required:
        result += _sdk_problems(cfg)
    return result


RULE = "=" * 72


def join_names(names: List[str]) -> str:
    return names[0] if len(names) == 1 else ", ".join(names[:-1]) + " and " + names[-1]


def installable(found: List[Problem]) -> List[Problem]:
    """What install() may run here: on Windows only pip installs into the venv."""
    return [p for p in found if p.install and (p.pip or not _is_windows())]


def format_problems(title: str, found: List[Problem], commands: bool = True) -> str:
    """A boxed list of what is missing, set apart from the build output around
    it. commands adds the ones the build could run; those a person must run
    are always shown."""
    auto = installable(found)
    lines = ["", RULE, title] + [f"  - {p.name}: {p.message}" for p in found]
    manual = list(dict.fromkeys(p.command for p in found if p.command and p not in auto))
    if manual:
        lines += ["", "You need to run:"] + [f"  {c}" for c in manual]
    lines.append(RULE)
    if commands and auto:
        lines.append(install_commands(auto))
    return "\n".join(lines)


def install_commands(auto: List[Problem]) -> str:
    return "\n".join(["To install them yourself, run:"]
                     + [f"  {c}" for c in dict.fromkeys(p.command for p in auto)])


def install(found: List[Problem]) -> None:
    """Runs installable(found), once per command."""
    done = set()
    for p in installable(found):
        if p.command not in done:
            done.add(p.command)
            print(f"Installing {p.name}...", flush=True)
            p.install()
    print("Done.", flush=True)


def _title(cfg: PicoConfig) -> str:
    return f"Missing build prerequisites for {cfg.board}'s companion-MCU firmware:"


def preflight(cfg: PicoConfig) -> None:
    found = problems(cfg)
    if found:
        raise PicoBuildError(f"board '{cfg.board}': prerequisites missing"
                             + format_problems(_title(cfg), found))


def build(cfg: PicoConfig, reconfigure: bool = False, dry_run: bool = False) -> None:
    build_dir_abs = cfg.build_dir_abs
    if not dry_run:
        os.makedirs(build_dir_abs, exist_ok=True)

    extra_env = {"PATH": tool_path(cfg)}
    if cfg.sdk_required:
        extra_env["PICO_SDK_PATH"] = cfg.sdk_path

    # Generated inputs the companion build expects to already exist (e.g. a
    # ROM rendered as a C header). After the build dir exists, since that is
    # usually where they are written; before configure, since a cmake glob
    # would otherwise miss them on a first build.
    for line_tokens in cfg.prebuild_lines:
        run(line_tokens, cwd=cfg.src, board=cfg.board, key="pico_prebuild",
            extra_env=extra_env, dry_run=dry_run)

    if cfg.build_mode == "cmake-ninja":
        marker = os.path.join(build_dir_abs, "build.ninja")
        if reconfigure and os.path.isfile(marker) and not dry_run:
            os.remove(marker)
        if reconfigure or not os.path.isfile(marker):
            cmd = ["cmake", "-B", cfg.build_dir, "-G", "Ninja",
                   f"-DCMAKE_BUILD_TYPE={cfg.build_type}"]
            if cfg.pico_board:
                cmd.append(f"-DPICO_BOARD={cfg.pico_board}")
            cmd += cfg.cmake_args
            run(cmd, cwd=cfg.src, board=cfg.board, key="pico_cmake_args",
                extra_env=extra_env, dry_run=dry_run)
        else:
            # Not just an optimization: Minty's CMakeLists.txt does
            # FetchContent at configure time, so reconfiguring would put a
            # network fetch inside every ESP32 build. --reconfigure forces it.
            log(f"[{cfg.board}] {marker} already exists -- skipping cmake "
                f"configure (pass --reconfigure to force one)")
        run(["ninja", "-C", cfg.build_dir], cwd=cfg.src, board=cfg.board,
            key="pico_build", extra_env=extra_env, dry_run=dry_run)

    elif cfg.build_mode == "cmake-make":
        marker = os.path.join(build_dir_abs, "Makefile")
        if reconfigure and os.path.isfile(marker) and not dry_run:
            os.remove(marker)
        if reconfigure or not os.path.isfile(marker):
            cmd = ["cmake", "-B", cfg.build_dir, "-G", "Unix Makefiles",
                   f"-DCMAKE_BUILD_TYPE={cfg.build_type}"]
            if cfg.pico_board:
                cmd.append(f"-DPICO_BOARD={cfg.pico_board}")
            cmd += cfg.cmake_args
            run(cmd, cwd=cfg.src, board=cfg.board, key="pico_cmake_args",
                extra_env=extra_env, dry_run=dry_run)
        else:
            log(f"[{cfg.board}] {marker} already exists -- skipping cmake "
                f"configure (pass --reconfigure to force one)")
        run(["cmake", "--build", cfg.build_dir], cwd=cfg.src, board=cfg.board,
            key="pico_build", extra_env=extra_env, dry_run=dry_run)

    elif cfg.build_mode == "make":
        cmd = ["make", "-C", cfg.src] + cfg.make_args
        run(cmd, cwd=".", board=cfg.board, key="pico_make_args",
            extra_env=extra_env, dry_run=dry_run)

    elif cfg.build_mode == "command":
        for line_tokens in cfg.command_lines:
            run(line_tokens, cwd=cfg.src, board=cfg.board, key="pico_command",
                extra_env=extra_env, dry_run=dry_run)

    else:  # pragma: no cover -- read_config() already validated pico_build
        fail(cfg.board, "pico_build", f"unhandled build mode '{cfg.build_mode}'")


# ---------------------------------------------------------------------------
# Collect built artifacts, render + write the generated .cpp
# ---------------------------------------------------------------------------

def collect(cfg: PicoConfig, required: bool) -> Optional[List[Tuple[str, bytes, str]]]:
    """Reads each pico_artifacts file into memory. When `required` is True
    (the full build path), a missing file is a hard fail -- the build
    reported success but didn't produce what the ini says it should have.
    When `required` is False (a target that skips the pico build, e.g.
    buildfs), a missing file just means "nothing built yet"; returns None
    so the caller writes the stub instead of failing a target that was
    never supposed to trigger a companion build in the first place."""
    result: List[Tuple[str, bytes, str]] = []
    build_dir_abs = cfg.build_dir_abs
    for name, rel_path in cfg.artifacts.items():
        path = os.path.join(build_dir_abs, rel_path)
        if not os.path.isfile(path):
            if required:
                fail(cfg.board, "pico_artifacts",
                     f"expected artifact '{name}' missing after build: {path}")
            return None
        with open(path, "rb") as f:
            data = f.read()
        result.append((name, data, path))
    return result


_BYTES_PER_LINE = 20


def check_flash_bounds(cfg: PicoConfig, artifacts: List[Tuple[str, bytes, str]]) -> None:
    """Refuse to embed an image that the ESP32 side could not write without
    running past pico_flash_limit. The runtime checks this too, but failing
    the build is the only place it can be fixed, and a runtime failure would
    otherwise only show up as a device that never finishes flashing."""
    if not cfg.flash_limit:
        return
    for name, data, path in artifacts:
        sectors = (len(data) + PICO_FLASH_SECTOR_SIZE - 1) // PICO_FLASH_SECTOR_SIZE
        end = cfg.flash_base + sectors * PICO_FLASH_SECTOR_SIZE
        if end > cfg.flash_limit:
            fail(cfg.board, "pico_flash_limit",
                 f"artifact '{name}' ({len(data)} bytes, {sectors} sectors "
                 f"from 0x{cfg.flash_base:08x}) would be written up to "
                 f"0x{end:08x}, past the limit 0x{cfg.flash_limit:08x} -- "
                 f"{path}")


def render(board: str, cfg: Optional[PicoConfig],
           artifacts: Optional[List[Tuple[str, bytes, str]]]) -> str:
    if not artifacts:
        return (
            f"// AUTO-GENERATED by build_pico.py for board '{board}' -- do not edit, do not commit.\n"
            "// No companion-MCU artifacts for this board ([fujinet] pico_src\n"
            "// unset, zero pico_artifacts, or --skip-pico/FUJINET_SKIP_PICO).\n"
            "// Written for EVERY board so switching boards can never leak a\n"
            "// stale blob from a previous build -- see this script's header.\n"
            '#include "fn_pico_blob.h"\n'
            'extern "C" {\n'
            "// ISO C++ forbids a zero-size array, so this carries one dummy\n"
            "// entry; fn_pico_blob_count stays 0 and no consumer should ever\n"
            "// index into it.\n"
            "const fn_pico_blob fn_pico_blobs[] = {\n"
            "    { nullptr, nullptr, 0, nullptr, 0, 0, FN_PICO_CHIP_UNKNOWN },\n"
            "};\n"
            "const size_t fn_pico_blob_count = 0;\n"
            "}\n"
        )

    assert cfg is not None  # artifacts only ever come from a real config
    rev = source_revision(cfg)
    out = [f"// AUTO-GENERATED by build_pico.py for board '{board}' -- do not edit, do not commit.\n",
           f"// Companion source: {cfg.src} @ {rev}\n",
           "// Sources:\n"]
    for name, data, path in artifacts:
        sha = hashlib.sha256(data).hexdigest()
        out.append(f"//   {name} <- {path} ({len(data)} bytes, sha256 {sha})\n")
    out.append('#include "fn_pico_blob.h"\n')
    out.append('extern "C" {\n')
    for name, data, _path in artifacts:
        ident = sanitize_c_ident(name)
        # const (flash .rodata, not the scarce ESP32-S3 DRAM) and
        # aligned(4) (lets a future consumer DMA/memcpy without a bounce
        # buffer) are both load-bearing -- don't drop either.
        out.append(f"static const uint8_t fn_pico_blob_{ident}[] __attribute__((aligned(4))) = {{\n")
        for i in range(0, len(data), _BYTES_PER_LINE):
            row = data[i:i + _BYTES_PER_LINE]
            out.append("    " + ",".join(f"0x{b:02x}" for b in row) + ",\n")
        out.append("};\n")
        # The sha256 is a build-time constant rather than something the
        # ESP32 hashes at boot: it is compared against an NVS record on
        # every boot to decide whether the companion already runs this
        # image, and hashing ~113 KB of rodata each time to learn something
        # the build already knows would just be a slower boot.
        sha = hashlib.sha256(data).hexdigest()
        out.append(f'static const char fn_pico_sha_{ident}[] = "{sha}";\n')
    out.append("const fn_pico_blob fn_pico_blobs[] = {\n")
    for name, _data, _path in artifacts:
        ident = sanitize_c_ident(name)
        out.append(
            f'    {{ "{name}", fn_pico_blob_{ident}, sizeof(fn_pico_blob_{ident}),\n'
            f"      fn_pico_sha_{ident}, 0x{cfg.flash_base:08x}u, "
            f"0x{cfg.flash_limit:08x}u, {PICO_CHIPS[cfg.chip]} }},\n")
    out.append("};\n")
    out.append(f"const size_t fn_pico_blob_count = {len(artifacts)};\n")
    out.append("}\n")
    return "".join(out)


def render_sidecar(cfg: Optional[PicoConfig],
                   artifacts: Optional[List[Tuple[str, bytes, str]]]) -> str:
    """The same facts as the generated .cpp, as JSON, for build_firmwarezip.py
    to copy into release.json. Always valid JSON, always a list."""
    entries = []
    if artifacts and cfg is not None:
        rev = source_revision(cfg)
        for name, data, _path in artifacts:
            entries.append({
                "name": name,
                "chip": cfg.chip,
                "size": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
                "flash_base": f"0x{cfg.flash_base:08x}",
                "flash_limit": f"0x{cfg.flash_limit:08x}",
                "source": cfg.src,
                "source_rev": rev,
            })
    return json.dumps(entries, indent=4) + "\n"


def write_if_changed(path: str, content: str) -> bool:
    old = None
    if os.path.isfile(path):
        with open(path, "r") as f:
            old = f.read()
    if old == content:
        log(f"{path} already up to date, not rewriting")
        return False
    dirname = os.path.dirname(path)
    if dirname:
        os.makedirs(dirname, exist_ok=True)
    with open(path, "w") as f:
        f.write(content)
    log(f"wrote {path} ({len(content)} bytes)")
    return True


def generate(board: str, ini_path: str, cfg: Optional[PicoConfig],
             artifacts: Optional[List[Tuple[str, bytes, str]]],
             out_dir: str) -> None:
    if artifacts and cfg is not None:
        check_flash_bounds(cfg, artifacts)

    cpp_path = os.path.join(out_dir, GENERATED_CPP)
    write_if_changed(cpp_path, render(board, cfg, artifacts))

    # Written unconditionally (an empty list when there is nothing to
    # embed) so build_firmwarezip.py can tell "this board has no companion"
    # apart from "build_pico.py never ran".
    write_if_changed(os.path.join(out_dir, BLOB_SIDECAR_JSON),
                      render_sidecar(cfg, artifacts))

    if artifacts:
        log(f"board '{board}': embedded {len(artifacts)} artifact(s) "
            f"(from {ini_path}) into {cpp_path}")
    else:
        log(f"board '{board}': wrote stub {cpp_path} "
            f"(no companion-MCU artifacts, ini={ini_path})")


def _remove_generated(out_dir: str) -> None:
    path = os.path.join(out_dir, GENERATED_CPP)
    if os.path.isfile(path):
        os.remove(path)
        log(f"removed {path} (clean/cleanall target -- it's a build artifact)")


def _check_no_legacy_cpp(board: str) -> None:
    """Fails instead of deleting it: cleaning up source dirs is left to the user."""
    if os.path.exists(LEGACY_GENERATED_CPP):
        fail(board, None,
             f"{LEGACY_GENERATED_CPP} is left over from an older build and "
             f"would be compiled alongside this one's -- delete it: "
             f"rm {LEGACY_GENERATED_CPP}")


# ---------------------------------------------------------------------------
# Target-guarded dispatch (shared by SCons and CLI mode)
# ---------------------------------------------------------------------------

def _target_class(targets: List[str]) -> str:
    tset = set(targets)
    if tset & CLEAN_TARGETS:
        return "clean"
    if tset & NO_LINK_TARGETS:
        return "no-build"
    return "full"


def _require_picoboot_define(cfg: PicoConfig, scons_env=None) -> None:
    """A board that embeds a companion image must also compile in the code
    that flashes it. Checked here because the two live in different ini
    sections -- [fujinet] pico_* and [env:<board>] build_flags -- and
    nothing else would notice them disagreeing: the image would be embedded,
    the updater would not exist, and the companion would silently never be
    flashed while the firmware still grew by the size of the image."""
    # The ini text is the reliable source in both modes: in SCons mode
    # PROJECT_CONFIG points at the merged ini, and in CLI mode this is the
    # same ini the pico config was read from.
    parser = configparser.ConfigParser(inline_comment_prefixes=(";", "#"))
    try:
        parser.read(cfg.ini_path)
        flags = parser.get(f"env:{cfg.board}", "build_flags", fallback="")
    except configparser.Error:
        flags = ""
    if PICOBOOT_DEFINE in flags:
        return

    # Fall back to what PlatformIO actually resolved, which also covers a
    # board that inherits the flag from somewhere other than its own section.
    if scons_env is not None:
        try:
            resolved = scons_env.GetProjectOption("build_flags") or []
            if isinstance(resolved, str):
                resolved = [resolved]
            if any(PICOBOOT_DEFINE in str(f) for f in resolved):
                return
        except Exception:
            pass

    fail(cfg.board, "build_flags",
         f"[fujinet] pico_src embeds a companion image for this board, but "
         f"-D {PICOBOOT_DEFINE}=1 is not in [env:{cfg.board}] build_flags. "
         f"Without it the ESP32 firmware carries the image but has no code "
         f"to flash it. Add the define, or drop the pico_* keys.")


def _skip_pico_requested(cli_flag: bool) -> bool:
    env_flag = os.environ.get("FUJINET_SKIP_PICO", "").strip().lower() in TRUE_WORDS
    if not (cli_flag or env_flag):
        return False
    log("=" * 72)
    log("FUJINET_SKIP_PICO / --skip-pico is set -- the companion-MCU firmware")
    log("will NOT be built. The ESP image will embed the stub blob table")
    log("(fn_pico_blob_count == 0). This exists for CI boards without an ARM")
    log("toolchain (build-platforms/build-all.sh walks every board ini, and")
    log("that becomes structural as more boards gain pico_* configs) -- it")
    log("should never be set for a build you intend to actually ship.")
    log("=" * 72)
    return True


def _dispatch(board: str, ini_path: str, cfg: Optional[PicoConfig],
               targets: List[str], *, dry_run: bool, reconfigure: bool,
               force_external: bool, no_generate: bool, skip_pico: bool,
               out_dir: str, scons_env=None) -> int:
    tclass = _target_class(targets)

    _check_no_legacy_cpp(board)

    if tclass == "clean":
        _remove_generated(out_dir)
        return 0

    # Checked before --skip-pico blanks the config: a misconfigured board
    # should fail the same way on a CI runner without an ARM toolchain as
    # it does on a developer's machine.
    if cfg is not None:
        _require_picoboot_define(cfg, scons_env)

    if skip_pico:
        cfg = None

    if cfg is None:
        if not no_generate:
            generate(board, ini_path, None, None, out_dir)
        return 0

    if tclass == "no-build":
        # Doesn't link firmware.elf, so don't kick off a companion build --
        # just reflect whatever's already built (or a stub, if nothing is).
        artifacts = collect(cfg, required=False)
        if not no_generate:
            generate(board, ini_path, cfg, artifacts, out_dir)
        return 0

    # tclass == "full": the only path that actually needs a real build.
    ensure_source(cfg, force_external=force_external, dry_run=dry_run)
    preflight(cfg)
    build(cfg, reconfigure=reconfigure, dry_run=dry_run)
    if dry_run:
        log(f"[{board}] --dry-run: stopping before collecting artifacts / "
            f"writing {GENERATED_CPP}")
        return 0
    artifacts = collect(cfg, required=True)
    if not no_generate:
        generate(board, ini_path, cfg, artifacts, out_dir)
    return 0


# ---------------------------------------------------------------------------
# SCons entry point
# ---------------------------------------------------------------------------

def _scons_entry(env) -> None:
    board = env["PIOENV"]
    # PROJECT_CONFIG is set by pio when passed -i (build.sh uses the same
    # env var name for consistency, same pattern as build_webui.py).
    ini_path = env["PROJECT_CONFIG"] if env["PROJECT_CONFIG"] else \
        (os.environ.get("PROJECT_CONFIG") or "platformio.ini")
    try:
        targets = [str(t) for t in COMMAND_LINE_TARGETS]
    except NameError:
        targets = []

    skip_pico = _skip_pico_requested(False)
    cfg, resolved_ini = resolve_config(ini_path, board)
    # $BUILD_DIR: src/CMakeLists.txt compiles the .cpp from there, and
    # build_firmwarezip.py reads the sidecar next to the images it bundles.
    out_dir = env.subst("$BUILD_DIR")
    # Any PicoBuildError raised below propagates straight out of this
    # "pre:" extra_script's exec, which is exactly how build_pico_intv.py's
    # fail() aborted the build before it -- SCons treats an uncaught
    # exception during extra_script execution as a fatal build error.
    _dispatch(board, resolved_ini, cfg, targets, dry_run=False,
              reconfigure=False, force_external=False, no_generate=False,
              skip_pico=skip_pico, out_dir=out_dir, scons_env=env)


# ---------------------------------------------------------------------------
# CLI entry point
# ---------------------------------------------------------------------------

def _resolve_ini_cli(args, board: str) -> str:
    if args.ini:
        return args.ini
    if os.path.isfile("platformio-generated.ini"):
        return "platformio-generated.ini"
    return os.path.join("build-platforms", f"platformio-{board}.ini")


def _print_config(board: str, ini_path: str, cfg: Optional[PicoConfig]) -> None:
    print(f"build_pico.py: resolved config for board '{board}' (ini: {ini_path})")
    if cfg is None:
        print("  no [fujinet] pico_src configured for this board -- nothing "
              "to build (a stub fn_pico_blob_data.cpp would be written)")
        return
    print(f"  pico_src         = {cfg.src}")
    print(f"  pico_chip        = {cfg.chip}  ({PICO_CHIPS[cfg.chip]})")
    print(f"  pico_build       = {cfg.build_mode}")
    print(f"  pico_build_dir   = {cfg.build_dir}  (-> {cfg.build_dir_abs})")
    print(f"  pico_build_type  = {cfg.build_type}")
    print(f"  pico_board       = {cfg.pico_board or '(unset)'}")
    print(f"  pico_flash_base  = 0x{cfg.flash_base:08x}")
    print(f"  pico_flash_limit = 0x{cfg.flash_limit:08x}"
          f"{'  (no limit)' if not cfg.flash_limit else ''}")
    print(f"  pico_prebuild    = {cfg.prebuild_lines}")
    print(f"  pico_cmake_args  = {cfg.cmake_args}")
    print(f"  pico_make_args   = {cfg.make_args}")
    print(f"  pico_command     = {cfg.command_lines}")
    print(f"  pico_toolchain   = {cfg.toolchain}")
    print(f"  pico_toolchain_package = {cfg.toolchain_package or '(none)'}  "
          f"(min: {cfg.toolchain_min or 'any'})")
    print(f"  pico_cmake_version     = {cfg.cmake_version or 'any'}")
    print(f"  pico_ninja_version     = {cfg.ninja_version or 'any'}")
    print(f"  pico_sdk_version = {cfg.sdk_version or 'any'}")
    print(f"  pico_sdk_path    = {cfg.sdk_path or '(not found)'}  (required: {cfg.sdk_required})")
    if cfg.repo:
        print(f"  pico_repo        = {cfg.repo}")
        print(f"  pico_repo_ref    = {cfg.repo_ref}")
        print(f"  pico_repo_dir    = {cfg.repo_dir}")
    print("  pico_artifacts:")
    if not cfg.artifacts:
        print("    (none)")
    for name, path in cfg.artifacts.items():
        print(f"    {name} <- {path}")


def _build_arg_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="build_pico.py",
        description="Build a board's companion-MCU (RP2040/RP2350/etc.) "
                     "firmware per its [fujinet] pico_* ini keys, and "
                     f"(re)generate {GENERATED_CPP} for the ESP32 build to "
                     "embed. Also runnable as a PlatformIO 'pre:' extra_script.")
    # The board may be given positionally or via --target/-t; the latter
    # reads naturally next to build.sh's own -e/-t flags. Exactly one of the
    # two forms must be used (checked in main()).
    p.add_argument("board", nargs="?", default=None,
                    help="PlatformIO env / [fujinet] build_board to "
                    "build for, e.g. fujiversal-intv")
    p.add_argument("--target", "-t", default=None,
                    help="same as the positional board argument")
    p.add_argument("--pio-target", action="append", default=None,
                    help="Simulate a PlatformIO target (clean, cleanall, "
                         "buildfs, uploadfs, erase, envdump, idedata, "
                         "monitor, upload, size, program, ...). May be "
                         "given multiple times. Omit for the default full "
                         "build path.")
    p.add_argument("--ini", default=None,
                    help="ini file to read [fujinet] from (default: "
                         "platformio-generated.ini if present, else "
                         "build-platforms/platformio-<board>.ini)")
    p.add_argument("--print-config", action="store_true",
                    help="print the resolved pico config and exit -- does "
                         "not touch the filesystem or build anything")
    p.add_argument("--check", action="store_true",
                    help="run the source and prerequisite checks a build "
                         "would (fetching a missing submodule), print a fix "
                         "for each problem, then exit: 0 ok, 1 problems, 2 "
                         "problems --install can fix at least some of")
    p.add_argument("--install", action="store_true",
                    help="as --check, but first install what can be installed "
                         "without root (the toolchain package, cmake and ninja "
                         "into the venv, the pico-sdk)")
    p.add_argument("--dry-run", action="store_true",
                    help="print the commands that would run, without "
                         "running them (still clones/updates a pico_repo "
                         "checkout's metadata check, but performs no writes)")
    p.add_argument("--reconfigure", action="store_true",
                    help="force a fresh cmake configure even if the build "
                         "dir already has one")
    p.add_argument("--force-external", action="store_true",
                    help="allow discarding local modifications in a "
                         "pico_repo clone")
    p.add_argument("--skip-pico", action="store_true",
                    help="skip the companion build entirely and write the "
                         "stub (same as FUJINET_SKIP_PICO=1)")
    p.add_argument("--no-generate", action="store_true",
                    help=f"don't write {GENERATED_CPP}")
    return p


def main(argv=None) -> int:
    parser = _build_arg_parser()
    args = parser.parse_args(argv)

    if args.board and args.target:
        parser.error("give the board once, either positionally or via "
                     "--target/-t, not both")
    board = args.board or args.target
    if not board:
        parser.error("a board is required (positionally or via --target/-t)")

    # No __file__-less SCons exec in CLI mode, so we chdir to the project
    # root (this script's own directory) explicitly, mirroring what "pre:"
    # mode gets automatically.
    os.chdir(os.path.dirname(os.path.abspath(__file__)))

    # PlatformIO validates PIOENV in SCons mode, but a CLI typo would
    # otherwise resolve to "no pico config" and exit 0, indistinguishable
    # from a legitimate non-pico board. build-platforms/ is the canonical
    # board list (create-platformio-ini.py checks it too).
    board_ini = os.path.join("build-platforms", f"platformio-{board}.ini")
    if not os.path.isfile(board_ini):
        print(f"error: build_pico.py: no such board '{board}' -- "
              f"{board_ini} does not exist", file=sys.stderr)
        return 1

    ini_path = _resolve_ini_cli(args, board)

    # resolve_config() is inside the handler too: a bad pico_chip / malformed
    # pico_flash_limit / missing pico_repo_ref is an ordinary misconfiguration
    # and deserves the same one-line message as a failed build, not a
    # traceback -- and it must read that way under --print-config as well.
    try:
        cfg, resolved_ini = resolve_config(ini_path, board)

        if args.print_config:
            # Must exit before any ensure_source()/preflight()/build() call
            # -- printing the config should never require a toolchain,
            # network access, or a writable tree.
            _print_config(board, resolved_ini, cfg)
            return 0

        if args.check or args.install:
            skip = os.environ.get("FUJINET_SKIP_PICO", "").strip().lower() in TRUE_WORDS
            if cfg is None or skip:
                return 0
            # A pico_repo source is cloned by the build, not by a check.
            if not cfg.repo:
                ensure_source(cfg)
            found = problems(cfg)
            if found and args.install:
                install(found)
                cfg, resolved_ini = resolve_config(ini_path, board)
                found = problems(cfg)
            if not found:
                return 0
            print(format_problems(_title(cfg), found), file=sys.stderr)
            return 2 if installable(found) and args.check else 1

        skip_pico = _skip_pico_requested(args.skip_pico)
        targets = args.pio_target or []

        # Mirror $BUILD_DIR, so a CLI run leaves the same artifacts a
        # `pio run` would.
        out_dir = os.path.join(".pio", "build", board)

        return _dispatch(board, resolved_ini, cfg, targets,
                          dry_run=args.dry_run, reconfigure=args.reconfigure,
                          force_external=args.force_external,
                          no_generate=args.no_generate, skip_pico=skip_pico,
                          out_dir=out_dir)
    except PicoBuildError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1


if env is not None:
    _scons_entry(env)
elif __name__ == "__main__":
    sys.exit(main())
