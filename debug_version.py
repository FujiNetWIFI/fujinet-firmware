#!/usr/bin/env python3
#
# Created by FozzTexx

import argparse
import subprocess
import os, sys
import re
import shlex
from datetime import datetime
from pathlib import Path

VERSION_H = "include/version.h"
PLATFORMS = "build-platforms"
MACRO_PATTERN = r"^\s*#define\s+(.*?)\s+(.*?)\s*(?://.*|/\*[\s\S]*?\*/)?$"

def build_argparser():
  parser = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)
  parser.add_argument("--update_version_h", action="store_true",
                      help="update include/version.h with current commit information")
  return parser

def run_command(cmd):
  return subprocess.check_output(cmd, universal_newlines=True).strip()

def get_repo_base():
  return run_command(["git", "rev-parse", "--show-toplevel"])

def is_fujinet_repo(base):
  if os.path.exists(os.path.join(base, VERSION_H)) \
     and os.path.isdir(os.path.join(base, PLATFORMS)):
    return True
  return False

def get_commit_version():
  return run_command(["git", "describe", "--always", "HEAD"])

def get_modified_files():
  files = run_command(["git", "diff", "--name-only"]).split("\n")
  files = [f for f in files if f]
  return files

def load_version_macros(path):
  txt = [line for line in open(path)]
  macros = {}
  for line in txt:
    m = re.match(MACRO_PATTERN, line)
    if m:
      name = m.group(1)
      if '(' in name:
        continue
      value = m.group(2)
      if not value:
        value = None
      macros[name] = value
  return macros

def main():
  base = get_repo_base()
  if not is_fujinet_repo(base):
    print("Not in FujiNet firmware repo")
    return 0

  args = build_argparser().parse_args()
  # FIXME - don't allow update_version_h unless on the actual tag?

  version_h_path = os.path.join(base, VERSION_H)
  version = get_commit_version()
  modified = get_modified_files()
  commit_id_long = run_command(["git", "rev-parse", "HEAD"])

  mtime = int(run_command(["git", "show", "--no-patch", "--format=%ct", "HEAD"]))
  for path in modified:
    if os.path.exists(path):
      mt = os.path.getmtime(path)
      if not mtime or mt > mtime:
        mtime = mt

  macros = {
    'FN_VERSION_BUILD': commit_id_long,
  }
  cur_macros = load_version_macros(version_h_path)
  ver_major = int(cur_macros['FN_VERSION_MAJOR'])
  ver_minor = int(cur_macros['FN_VERSION_MINOR'])

  m = re.match(r"^v([0-9]+)[.]([0-9]+)[.]([0-9]+)-([0-9]+)-g(.*)", version)
  if m:
    ver_major = macros['FN_VERSION_MAJOR'] = int(m.group(1))
    ver_minor = macros['FN_VERSION_MINOR'] = int(m.group(2))
    version = f"v{m.group(1)}.{m.group(2)}-{m.group(5)}"
  else:
    m = re.match(r"^([a-z0-9]{8})$", version)
    if m:
      version = f"v{ver_major}.{ver_minor}-{version}"

  if modified:
    version += "*"
  macros['FN_VERSION_FULL'] = version
  macros['FN_VERSION_DATE'] = datetime.fromtimestamp(mtime).strftime("%Y-%m-%d %H:%M:%S")

  new_macros = cur_macros.copy()
  new_macros.update(macros)

  if new_macros != cur_macros:
    for key in macros:
      if isinstance(macros[key], str):
        macros[key] = f'"{macros[key]}"'

    cur_macros.update(macros)
    macro_defs = []
    for key in cur_macros:
      value = cur_macros[key]
      mdef = f"-D{key}"
      if value is not None:
        mdef += f"={value}"
      macro_defs.append(shlex.quote(mdef))

    # FIXME - if args.update_version_h then update, don't print
    macro_defs = " ".join(macro_defs)
    print(macro_defs)
    #Path(version_h_path).touch()

  return

if __name__ == "__main__":
  exit(main() or 0)
elif __name__ == "SCons.Script":
  print("Running as build script")
