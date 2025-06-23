#!/usr/bin/env python3
import argparse
import os
import subprocess
import yaml
from pathlib import Path

# Map of extension groups to their corresponding clang-format override flags
CLANG_FORMAT_OVERRIDES = {
  ('.c', '.cpp', '.cc', '.cxx'): {
    'AllowShortBlocksOnASingleLine': "false",
  },
  # Add more extension sets here if needed
}

def build_argparser():
  parser = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)
  parser.add_argument("file", help="input file")
  parser.add_argument("--fix", action="store_true", help="rewrite improperly formatted files")
  parser.add_argument("--show", action="store_true", help="print reformatted file on stdout")
  return parser

def read_source(path):
  with open(path, 'r', encoding='utf-8') as f:
    return f.read().splitlines()

def get_override_options(extension):
  for ext_group, flags in CLANG_FORMAT_OVERRIDES.items():
    if extension in ext_group:
      return flags
  return []

def find_clang_format_config(start_path):
  """Search upward for .clang-format file starting from the file's directory."""
  path = Path(start_path).resolve()
  for parent in [path] + list(path.parents):
    config_file = parent / ".clang-format"
    if config_file.is_file():
      return config_file
  raise FileNotFoundError(".clang-format not found")

def serialize_flat_style(config):
  """Convert a dict into a compact, single-line clang-format style string."""
  def serialize(obj):
    if isinstance(obj, dict):
      inner = ", ".join(f"{k}: {serialize(v)}" for k, v in obj.items())
      return f"{{{inner}}}"
    elif isinstance(obj, bool):
      return "true" if obj else "false"
    else:
      return str(obj)
  return "{" + ", ".join(f"{k}: {serialize(v)}" for k, v in config.items()) + "}"

def clang_format(path):
  """Format the file using clang-format with overrides passed inline."""
  path = Path(path)
  config_path = find_clang_format_config(path.parent)

  with open(config_path, 'r') as f:
    config = yaml.safe_load(f)

  override_options = get_override_options(path.suffix)
  if override_options:
    config.update(override_options)

  inline_style = serialize_flat_style(config)

  cmd = ["clang-format", f"--style={inline_style}", str(path)]
  result = subprocess.run(cmd, capture_output=True, text=True, check=True)

  return result.stdout.splitlines()

def update_file(path, lines):
  with open(path, 'w', encoding='utf-8') as f:
    for line in lines:
      f.write(line + '\n')
  return

def main():
  args = build_argparser().parse_args()

  path = args.file
  err_count = 0
  formatted = clang_format(path)
  if args.show:
    for line in formatted:
      print(line)
    return
  
  original = read_source(path)
  if original != formatted:
    if args.fix:
      update_file(path, formatted)
    else:
      print(f"{path} is not formatted correctly")
      err_count += 1

  if err_count:
    exit(1)

  return

if __name__ == "__main__":
  exit(main() or 0)
