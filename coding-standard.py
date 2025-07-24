#!/usr/bin/env python3
import argparse
import os, sys
import subprocess
import yaml
import re
from pathlib import Path
import mimetypes
from enum import Enum, auto
import termcolor
import shlex

USE_COLOR = sys.stdout.isatty()

CLANG_TYPES = [".c", ".cpp", ".cc", ".cxx", ".h"]
MAKEFILE_TYPES = [r"^[Mm]akefile([.].+)?", r".*[.](mk|make)$"]
# Map of extension groups to their corresponding clang-format override flags
CLANG_FORMAT_OVERRIDES = {
  (".c", ".cpp", ".cc", ".cxx"): {
    'AllowShortBlocksOnASingleLine': "false",
  },
  # Add more extension sets here if needed
}

class FormatError(Enum):
  TrailingWhitespace = auto()
  IllegalTabs = auto()
  IncorrectFormatting = auto()

ErrorStrings = {
  FormatError.TrailingWhitespace: "Trailing Whitespace",
  FormatError.IllegalTabs: "Tab Characters",
  FormatError.IncorrectFormatting: "Incorrect Formatting",
}

TERMCAP_TAB_COLUMNS = 8

def build_argparser():
  parser = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)
  parser.add_argument("file", nargs="*", help="input file")
  parser.add_argument("--against",
                      help="Compare staged files to base branch (e.g., origin/main)")
  parser.add_argument("--fix", action="store_true", help="rewrite improperly formatted files")
  parser.add_argument("--show", action="store_true", help="print reformatted file on stdout")
  return parser

def read_source(path):
  with open(path, "r", encoding="utf-8") as f:
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

  with open(config_path, "r") as f:
    config = yaml.safe_load(f)

  override_options = get_override_options(path.suffix)
  if override_options:
    config.update(override_options)

  inline_style = serialize_flat_style(config)

  cmd = ["clang-format", f"--style={inline_style}", str(path)]
  try:
    result = subprocess.run(cmd, capture_output=True, text=True, check=True)
  except:
    print("Failed to format")
    print(" ".join([shlex.quote(x) for x in cmd]))
    exit(1)

  return result.stdout.splitlines()

def split_tabs(line, allow_leading_tabs):
  prefix = ""
  suffix = line
  if allow_leading_tabs:
    idx = 0
    while idx < len(line) and line[idx] == '\t':
      idx += 1
    prefix = line[:idx]
    suffix = line[idx:]
  return prefix, suffix

def plain_format(path, allow_leading_tabs=False):
  """Remove all trailing whitespace and replace tab characters with
  spaces. Optionally allow leading tabs (such as in Makefiles)

  """
  with open(path, "r") as f:
    lines = f.read().splitlines()

  for idx in range(len(lines)):
    line = lines[idx].rstrip()
    if "\t" in line:
      prefix, suffix = split_tabs(line, allow_leading_tabs)
      while "\t" in suffix:
        column = suffix.index("\t")
        next_column = ((column + 8) % 8) * 8
        suffix = suffix[:column] + " " * (next_column - column) + suffix[column + 1:]
      line = prefix + suffix
    lines[idx] = line
  return lines

def python_format(path):
  # FIXME - do proper Python formatting
  return plain_format(path)

def update_file(path, lines):
  with open(path, "w", encoding="utf-8") as f:
    for line in lines:
      f.write(line + "\n")
  return

def get_staged_files(extension=None):
  try:
    result = subprocess.run(
      ["git", "diff", "--cached", "--name-only", "--diff-filter=ACMRT"],
      check=True,
      stdout=subprocess.PIPE,
      stderr=subprocess.PIPE,
      universal_newlines=True,
    )
    files = result.stdout.splitlines()
  except subprocess.CalledProcessError as e:
    print(f"Error getting staged files: {e.stderr}", file=sys.stderr)
    exit(1)
  return [f for f in files if extension is None or f.endswith(extension)]

def get_changed_files(base_ref):
  result = subprocess.run(
    ['git', 'diff', '--name-only', '--diff-filter=ACMR', f'{base_ref}...HEAD'],
    capture_output=True, text=True, check=True
  )
  return result.stdout.splitlines()

def is_makefile(path):
  for pattern in MAKEFILE_TYPES:
    if re.match(pattern, path):
      return True
  return False

def is_python(path):
  _, ext = os.path.splitext(path)
  if ext == ".py":
    return True

  try:
    with open(path, "r") as file:
      first_line = file.readline()
      if first_line.startswith("#!/usr/bin/env python"):
        return True

  except UnicodeDecodeError as e:
    print(e)

  return False

def is_text(path, blocksize=512):
  try:
    with open(path, 'rb') as f:
      chunk = f.read(blocksize)
  except Exception:
    return False

  if not chunk:
    return True  # empty file = text

  if b'\0' in chunk:
    return False  # null bytes = binary

  # Mostly printable ASCII or common control chars
  text_characters = bytes(range(32, 127)) + b'\n\r\t\f\b'
  nontext = chunk.translate(None, text_characters)
  return len(nontext) / len(chunk) < 0.30

# def is_text(path):
#   mime, _ = mimetypes.guess_type(path)
#   print("MIME", mime, path)
#   if mime and mime.startswith("text/"):
#     return True
#   return False

def classify_errors(original, allow_leading_tabs=False):
  errors = set()

  for line in original:
    if line.rstrip() != line:
      errors.add(FormatError.TrailingWhitespace)
      break

  for line in original:
    trailing_start = len(line.rstrip())
    line = line[:trailing_start]
    prefix, suffix = split_tabs(line, allow_leading_tabs)
    if "\t" in suffix:
      errors.add(FormatError.IllegalTabs)
      break

  if not errors:
    errors.add(FormatError.IncorrectFormatting)

  return errors

def color(text, *args, **kwargs):
  return termcolor.colored(text, *args, **kwargs) if USE_COLOR else text

def visualize_whitespace(line):
  # Identify trailing whitespace
  trailing_start = len(line.rstrip())
  content = line[:trailing_start]
  trailing = line[trailing_start:]

  TAB_COLOR = "on_cyan"
  SPACE_COLOR = "on_red"
  # Replace visible characters
  content = content.replace("\t", color("    ", on_color=TAB_COLOR))
  trailing = (trailing
              .replace(" ", color(" ", on_color=SPACE_COLOR))
              .replace("\t", color("    ", on_color=TAB_COLOR)))
  return content + trailing

def get_git_status():
  result = subprocess.run(
    ['git', 'diff', '--cached', '--name-status'],
    capture_output=True, text=True, check=True
  )
  status = {}
  for line in result.stdout.strip().splitlines():
    kind, filename = line.split(maxsplit=1)
    status[filename] = kind
  return status

def previous_file_contents(path):
  try:
    result = subprocess.run(
      ['git', 'show', f'HEAD:{path}'],
      capture_output=True,
      text=True,
      check=True
    )
  except subprocess.CalledProcessError as e:
    # File is probably new or not tracked in HEAD
    return None

  return result.stdout

def display_errors(path, errs, original, formatted):
  print(f"{path}:")
  print(f"  {', '.join(ErrorStrings[x] for x in errs)}")
  for idx in range(min(len(original), len(formatted))):
    if original[idx] != formatted[idx]:
      print("  First line with error:")
      print(f"  {idx+1}: {visualize_whitespace(original[idx])}")
      break
  return

def check_file(path, display_only=False, fix_in_place=False):
  _, ext = os.path.splitext(path)
  allow_leading_tabs = False
  if ext in CLANG_TYPES:
    prev_formatted = previous = previous_file_contents(path)
    if previous is not None:
      prev_formatted = clang_format(path)
    if previous == prev_formatted:
      formatted = clang_format(path)
    else:
      formatted = plain_format(path)
  elif is_makefile(path):
    allow_leading_tabs = True
    formatted = plain_format(path, allow_leading_tabs)
  elif is_python(path):
    prev_formatted = previous = previous_file_contents(path)
    if previous is not None:
      prev_formatted = python_format(path)
    if previous == prev_formatted:
      formatted = python_format(path)
    else:
      formatted = plain_format(path)
  elif is_text(path):
    formatted = plain_format(path)
  else:
    raise NotImplementedError("Don't know how to check", path)

  if display_only:
    for line in formatted:
      print(line)
    return None

  original = read_source(path)
  if original != formatted:
    if fix_in_place:
      update_file(path, formatted)
    else:
      errs = classify_errors(original, allow_leading_tabs)
      display_errors(path, errs, original, formatted)
      return errs
  return None

def main():
  args = build_argparser().parse_args()

  script_mode = os.path.basename(sys.argv[0])
  if script_mode == "pre-commit":
    to_check = get_staged_files()
    if not to_check:
      sys.exit(0)
  elif args.against:
    to_check = get_changed_files(args.against)
  else:
    to_check = args.file

  err_count = 0
  failed = []
  for path in to_check:
    errs = check_file(path, args.show)
    if errs is not None:
      failed.append(path)
      err_count += 1

  if err_count:
    exit(1)

  return

if __name__ == "__main__":
  exit(main() or 0)
