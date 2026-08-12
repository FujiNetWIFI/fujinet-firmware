# pyright: reportUndefinedVariable=false
#
# "post:" extra_script for the fujiversal-intv board: after a normal build
# produces the usual three separate flash pieces (bootloader.bin,
# partitions.bin, firmware.bin -- the last one already carrying the embedded
# Minty/fujicard firmware, see build_pico_intv.py), merges them into ONE
# file via `esptool.py merge_bin` so `esptool.py write_flash 0x0 <merged
# file>` is the only command needed to get a fully working ESP32-S3 image
# onto a chip.
#
# Registered via env.AddPostAction() (the same pattern build_firmwarezip.py
# already uses), not as top-level script code: a "post:" extra_script's
# top-level code still runs at SCons DAG-*construction* time, before
# firmware.bin actually exists on disk -- AddPostAction is what makes this
# genuinely run after that specific target is built.
#
# Deliberately does NOT include the LittleFS `storage` partition (webui/
# config data) -- that's unrelated to the RP2040/RP2350 hand-off this exists
# for, and stays on the existing, orthogonal `-t uploadfs` path for
# first-time setup (see build.sh -f).
#
# flash_mode/flash_size/flash_freq are read from ESP-IDF's own
# flasher_args.json (regenerated every build) rather than hardcoded, so this
# always matches whatever a normal `pio run -t upload` would actually use.
#
# Hard-fails if esptool.py or any of the three input files are missing, or
# if the merge itself errors -- a build that claims success but silently
# skipped producing the single-file artifact the user asked for is worse
# than one that stops and says so.

import glob
import json
import os
import subprocess
import sys

Import("env")


def find_esptool():
    candidates = glob.glob(os.path.expanduser(
        "~/.platformio/packages/tool-esptoolpy*/esptool.py"))
    if not candidates:
        raise Exception("build_merge_intv.py: esptool.py not found under "
                         "~/.platformio/packages/tool-esptoolpy*")
    return candidates[0]


def merge_intv(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    bootloader = os.path.join(build_dir, "bootloader.bin")
    partitions = os.path.join(build_dir, "partitions.bin")
    firmware = os.path.join(build_dir, "firmware.bin")
    flasher_args_path = os.path.join(build_dir, "flasher_args.json")
    merged_out = os.path.join(build_dir, "fujiversal-intv-merged.bin")

    for f in (bootloader, partitions, firmware, flasher_args_path):
        if not os.path.isfile(f):
            raise Exception(f"build_merge_intv.py: expected build output missing: {f}")

    with open(flasher_args_path) as fh:
        flash_settings = json.load(fh)["flash_settings"]

    esptool_py = find_esptool()

    cmd = [
        sys.executable, esptool_py,
        "--chip", "esp32s3",
        "merge_bin",
        "-o", merged_out,
        "--flash_mode", flash_settings["flash_mode"],
        "--flash_size", flash_settings["flash_size"],
        "--flash_freq", flash_settings["flash_freq"],
        "0x0000", bootloader,
        "0x8000", partitions,
        "0x10000", firmware,
    ]

    print(f"build_merge_intv.py: running: {' '.join(cmd)}")
    result = subprocess.run(cmd)
    if result.returncode != 0:
        raise Exception(f"build_merge_intv.py: esptool.py merge_bin exited {result.returncode}")

    if not os.path.isfile(merged_out):
        raise Exception(f"build_merge_intv.py: expected output missing after merge_bin: {merged_out}")

    print(f"build_merge_intv.py: OK -- {merged_out} ({os.path.getsize(merged_out)} bytes)")
    print(f"  Flash with: esptool.py --chip esp32s3 write_flash 0x0 {merged_out}")


if env["PIOENV"] == "fujiversal-intv":
    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_intv)
