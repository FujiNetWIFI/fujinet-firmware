#!/usr/bin/env python3
# pyright: reportUndefinedVariable=false
#
# build_pico.py -- builds a board's companion-MCU firmware (RP2040/RP2350/
# whatever its own CMakeLists.txt targets) and renders the result into
# lib/hardware/fn_pico_blob_data.cpp, which PlatformIO picks up via
# src/CMakeLists.txt's lib/hardware/*.cpp glob. See lib/hardware/
# fn_pico_blob.h for the accessor layer consumers use.
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
# (the generated build.ninja has no firmware.elf rule at all). A real
# source file in an already-globbed directory sidesteps both.
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
import hashlib
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
    print(f"build_pico.py: {msg}")


def fail(board: str, key: Optional[str], msg: str):
    key_part = f" [ini key: {key}]" if key else ""
    raise PicoBuildError(f"build_pico.py: board '{board}': {msg}{key_part}")


def run(cmd: List[str], cwd: str, board: str, key: Optional[str],
        extra_env: Optional[Dict[str, str]] = None, dry_run: bool = False,
        capture: bool = False) -> Optional[str]:
    printable = " ".join(shlex.quote(c) for c in cmd)
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

    result = subprocess.run(cmd, cwd=cwd, env=build_env)
    if result.returncode != 0:
        fail(board, key, f"'{printable}' exited {result.returncode}")
    return None


# ---------------------------------------------------------------------------
# Fixed paths
# ---------------------------------------------------------------------------

# Under lib/hardware/ to land in src/CMakeLists.txt's *.cpp glob, and ONE
# fixed path for every board -- a per-board filename would reopen the
# contamination hole the always-generate rule closes. Gitignored.
# fujinet_pc.cmake lists lib/hardware sources explicitly rather than
# globbing, so this never reaches the FujiNet-PC build.
GENERATED_CPP = os.path.join("lib", "hardware", "fn_pico_blob_data.cpp")

CLEAN_TARGETS = {"clean", "cleanall"}
# Don't link firmware.elf, so they don't need a companion build -- `pio run
# -t clean` used to run the entire pico build just to discard the result.
NO_LINK_TARGETS = {"buildfs", "uploadfs", "erase", "envdump", "idedata", "monitor"}

CMAKE_MODES = ("cmake-ninja", "cmake-make")
BUILD_MODES = ("cmake-ninja", "cmake-make", "make", "command")

BLOB_NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]*$")

TRUE_WORDS = {"yes", "true", "1", "on"}
FALSE_WORDS = {"no", "false", "0", "off"}


# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------

class PicoConfig:
    """Everything resolved from one board's [fujinet] pico_* ini keys."""

    def __init__(self, board, ini_path, src, build_mode, build_dir, build_type,
                 pico_board, cmake_args, make_args, command_lines, toolchain,
                 sdk_path, sdk_required, artifacts, repo, repo_ref, repo_dir):
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
        self.toolchain = toolchain
        self.sdk_path = sdk_path
        self.sdk_required = sdk_required
        self.artifacts = artifacts  # dict: name -> path (relative to build dir)
        self.repo = repo
        self.repo_ref = repo_ref
        self.repo_dir = repo_dir

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
        if name in result:
            fail(board, "pico_artifacts", f"duplicate artifact name '{name}'")
        result[name] = path
    return result


def read_config(ini_path: str, board: str) -> Optional[PicoConfig]:
    """Reads [fujinet] from ini_path and returns a PicoConfig, or None if
    this ini simply doesn't describe a pico build for `board` (missing
    file, missing [fujinet] section, no pico_src, or a build_board that
    names a different board -- see the "board guard" note below). A real
    configuration *problem* (bad pico_build value, missing pico_repo_ref,
    etc.) is a hard fail via fail(), never a silent None."""
    parser = configparser.ConfigParser(inline_comment_prefixes=(";", "#"))
    if not parser.read(ini_path):
        return None
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
        fail(board, key, f"unrecognised boolean value '{raw}' -- expected "
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

    pico_sdk_path = section.get("pico_sdk_path", "").strip()
    if not pico_sdk_path:
        pico_sdk_path = os.environ.get("PICO_SDK_PATH", "").strip() or "/usr/share/pico-sdk"
    pico_sdk_path = os.path.expanduser(os.path.expandvars(pico_sdk_path))

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
        "build_dir": os.path.join(pico_src, pico_build_dir),
        "sdk": pico_sdk_path,
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
        toolchain = ["arm-none-eabi-gcc"] if pico_build in CMAKE_MODES else []

    cmake_args = parse_multiline_args("pico_cmake_args")
    make_args = parse_multiline_args("pico_make_args")

    command_lines: List[List[str]] = []
    for line in expand("pico_command").splitlines():
        line = line.strip()
        if not line or line.startswith((";", "#")):
            continue
        command_lines.append(shlex.split(line))
    if pico_build == "command" and not command_lines:
        fail(board, "pico_command",
             "pico_build = command requires at least one pico_command line")

    artifacts = parse_artifacts(expand("pico_artifacts"), board)

    return PicoConfig(
        board=board, ini_path=ini_path, src=pico_src, build_mode=pico_build,
        build_dir=pico_build_dir, build_type=pico_build_type,
        pico_board=pico_board, cmake_args=cmake_args, make_args=make_args,
        command_lines=command_lines, toolchain=toolchain,
        sdk_path=pico_sdk_path, sdk_required=pico_sdk_required,
        artifacts=artifacts, repo=pico_repo, repo_ref=pico_repo_ref,
        repo_dir=pico_repo_dir,
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


def ensure_source(cfg: PicoConfig, force_external: bool = False, dry_run: bool = False) -> None:
    if cfg.repo:
        _ensure_remote_source(cfg, force_external=force_external, dry_run=dry_run)

    if not os.path.isdir(cfg.src):
        fail(cfg.board, "pico_src", f"directory does not exist: {cfg.src}")
    if cfg.build_mode in CMAKE_MODES and not os.path.isfile(os.path.join(cfg.src, "CMakeLists.txt")):
        fail(cfg.board, "pico_src",
             f"no CMakeLists.txt found in {cfg.src} (pico_build={cfg.build_mode})")
    if cfg.build_mode == "make" and not os.path.isfile(os.path.join(cfg.src, "Makefile")):
        fail(cfg.board, "pico_src", f"no Makefile found in {cfg.src} (pico_build=make)")


# ---------------------------------------------------------------------------
# Preflight + build
# ---------------------------------------------------------------------------

def preflight(cfg: PicoConfig) -> None:
    tools: List[str] = list(cfg.toolchain)
    if cfg.build_mode == "cmake-ninja":
        tools += ["cmake", "ninja"]
    elif cfg.build_mode == "cmake-make":
        tools += ["cmake", "make"]
    elif cfg.build_mode == "make":
        tools += ["make"]

    missing = [t for t in dict.fromkeys(tools) if shutil.which(t) is None]
    if missing:
        fail(cfg.board, "pico_toolchain",
             f"required tool(s) not found on PATH: {', '.join(missing)}")

    if cfg.sdk_required and not os.path.isdir(cfg.sdk_path):
        fail(cfg.board, "pico_sdk_path",
             f"PICO_SDK_PATH does not exist: {cfg.sdk_path} (set the "
             f"PICO_SDK_PATH env var, or the pico_sdk_path ini key, to "
             f"override the default)")


def build(cfg: PicoConfig, reconfigure: bool = False, dry_run: bool = False) -> None:
    build_dir_abs = cfg.build_dir_abs
    if not dry_run:
        os.makedirs(build_dir_abs, exist_ok=True)

    extra_env = {"PICO_SDK_PATH": cfg.sdk_path} if cfg.sdk_required else None

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
            # Not just an optimisation: Minty's CMakeLists.txt does
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


def render(board: str, artifacts: Optional[List[Tuple[str, bytes, str]]]) -> str:
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
            "    { nullptr, nullptr, 0 },\n"
            "};\n"
            "const size_t fn_pico_blob_count = 0;\n"
            "}\n"
        )

    out = [f"// AUTO-GENERATED by build_pico.py for board '{board}' -- do not edit, do not commit.\n",
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
    out.append("const fn_pico_blob fn_pico_blobs[] = {\n")
    for name, _data, _path in artifacts:
        ident = sanitize_c_ident(name)
        out.append(f'    {{ "{name}", fn_pico_blob_{ident}, sizeof(fn_pico_blob_{ident}) }},\n')
    out.append("};\n")
    out.append(f"const size_t fn_pico_blob_count = {len(artifacts)};\n")
    out.append("}\n")
    return "".join(out)


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


def generate(board: str, ini_path: str, artifacts: Optional[List[Tuple[str, bytes, str]]]) -> None:
    content = render(board, artifacts)
    write_if_changed(GENERATED_CPP, content)
    if artifacts:
        log(f"board '{board}': embedded {len(artifacts)} artifact(s) "
            f"(from {ini_path}) into {GENERATED_CPP}")
    else:
        log(f"board '{board}': wrote stub {GENERATED_CPP} "
            f"(no companion-MCU artifacts, ini={ini_path})")


def _remove_generated() -> None:
    if os.path.isfile(GENERATED_CPP):
        os.remove(GENERATED_CPP)
        log(f"removed {GENERATED_CPP} (clean/cleanall target -- it's a build artifact)")
    else:
        log(f"{GENERATED_CPP} not present, nothing to clean")


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
               force_external: bool, no_generate: bool, skip_pico: bool) -> int:
    tclass = _target_class(targets)

    if tclass == "clean":
        _remove_generated()
        return 0

    if skip_pico:
        cfg = None

    if cfg is None:
        if not no_generate:
            generate(board, ini_path, None)
        return 0

    if tclass == "no-build":
        # Doesn't link firmware.elf, so don't kick off a companion build --
        # just reflect whatever's already built (or a stub, if nothing is).
        artifacts = collect(cfg, required=False)
        if not no_generate:
            generate(board, ini_path, artifacts)
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
        generate(board, ini_path, artifacts)
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
    # Any PicoBuildError raised below propagates straight out of this
    # "pre:" extra_script's exec, which is exactly how build_pico_intv.py's
    # fail() aborted the build before it -- SCons treats an uncaught
    # exception during extra_script execution as a fatal build error.
    _dispatch(board, resolved_ini, cfg, targets, dry_run=False,
              reconfigure=False, force_external=False, no_generate=False,
              skip_pico=skip_pico)


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
    print(f"  pico_build       = {cfg.build_mode}")
    print(f"  pico_build_dir   = {cfg.build_dir}  (-> {cfg.build_dir_abs})")
    print(f"  pico_build_type  = {cfg.build_type}")
    print(f"  pico_board       = {cfg.pico_board or '(unset)'}")
    print(f"  pico_cmake_args  = {cfg.cmake_args}")
    print(f"  pico_make_args   = {cfg.make_args}")
    print(f"  pico_command     = {cfg.command_lines}")
    print(f"  pico_toolchain   = {cfg.toolchain}")
    print(f"  pico_sdk_path    = {cfg.sdk_path}  (required: {cfg.sdk_required})")
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
    cfg, resolved_ini = resolve_config(ini_path, board)

    if args.print_config:
        # Must exit before any ensure_source()/preflight()/build() call --
        # printing the config should never require a toolchain, network
        # access, or a writable tree.
        _print_config(board, resolved_ini, cfg)
        return 0

    skip_pico = _skip_pico_requested(args.skip_pico)
    targets = args.pio_target or []

    try:
        return _dispatch(board, resolved_ini, cfg, targets,
                          dry_run=args.dry_run, reconfigure=args.reconfigure,
                          force_external=args.force_external,
                          no_generate=args.no_generate, skip_pico=skip_pico)
    except PicoBuildError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1


if env is not None:
    _scons_entry(env)
elif __name__ == "__main__":
    sys.exit(main())
