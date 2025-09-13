#!/usr/bin/env python3
"""Pre-commit hook and PR checker for maintaining basic coding standards.

This script serves dual purposes:
  1. As a Git pre-commit hook: it runs automatically when committing, and only
     checks files staged for commit. It verifies that:
       - No lines have trailing whitespace
       - Tabs are not used for alignment (except leading tabs in Makefiles)
       - If previous version of C/C++ file was formatted in accordance
         with `.clang-format` then changes must too
  2. As a GitHub Actions PR check: it compares the PR branch against its base
     and verifies all modified files conform to the same standards.

Only **changed or staged files** are checked, to allow incremental
cleanup in legacy code.

To install as a pre-commit hook, run:
    coding-standard.py --addhook

To use as a PR check, add it as a GitHub Action and pass:
    --against ${{ github.base_ref }}

Exit code is nonzero if any violations are found.

"""

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
import tempfile

USE_COLOR = sys.stdout.isatty()

MAKEFILE_TYPES = [r"^[Mm]akefile([.].+)?", r".*[.](mk|make)$"]
# Map of extension groups to their corresponding clang-format override flags

class FormatError(Enum):
  TrailingWhitespace = auto()
  IllegalTabs = auto()
  CodingStandardViolation = auto()

class DisplayType(Enum):
  Reformatted = auto()
  Diff = auto()

ErrorStrings = {
  FormatError.TrailingWhitespace: "Trailing whitespace",
  FormatError.IllegalTabs: "Tab characters",
  FormatError.CodingStandardViolation: "Does not follow project coding standard",
}

TERMCAP_TAB_COLUMNS = 8

def build_argparser():
  parser = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)
  parser.add_argument("file", nargs="*", help="input file")
  parser.add_argument("--against",
                      help="Compare staged files to base branch (e.g., origin/main)")
  parser.add_argument("--addhook", action="store_true",
                      help="setup git pre-commit hook to use this script")

  group = parser.add_mutually_exclusive_group()
  group.add_argument("--fix", action="store_true", help="rewrite improperly formatted files")
  group.add_argument("--show", action="store_true", help="print reformatted file on stdout")
  group.add_argument("--diff", action="store_true", help="diff against reformatted")
  return parser

class ClangFormatter:
  TYPES = [".c", ".cpp", ".cc", ".cxx", ".h"]
  FORMAT_OVERRIDES = {
    (".c", ".cpp", ".cc", ".cxx"): {
      'AllowShortBlocksOnASingleLine': "false",
      'AllowShortFunctionsOnASingleLine': "None",
    },
    tuple(TYPES): {
      'SpacesBeforeTrailingComments': "1",
      'AlignTrailingComments': "false",
      'ReflowComments': "false",
      'AlignConsecutiveMacros': "true",
      'PointerAlignment': "Right",
    }
  }

  @staticmethod
  def formatFile(textFile):
    """Format the file using clang-format with overrides passed inline."""
    configPath = ClangFormatter.findConfig(textFile.path.parent)

    with open(configPath, "r") as f:
      config = yaml.safe_load(f)

    options = ClangFormatter.overrideOptions(textFile.path.suffix)
    if options:
      config.update(options)

    inlineStyle = ClangFormatter.dictToStyle(config)

    cmd = ["clang-format", f"--style={inlineStyle}"]
    text = "\n".join(textFile.contents)
    try:
      result = subprocess.run(cmd, input=text, capture_output=True, text=True, check=True)
    except:
      print("Failed to format")
      print(" ".join([shlex.quote(x) for x in cmd]))
      exit(1)

    return result.stdout.splitlines()

  @staticmethod
  def findConfig(startPath):
    """Search upward for .clang-format file starting from the file's directory."""
    path = startPath.resolve()
    for parent in [path] + list(path.parents):
      configFile = parent / ".clang-format"
      if configFile.is_file():
        return configFile
    raise FileNotFoundError(".clang-format not found")

  @staticmethod
  def overrideOptions(extension):
    all_flags = {}
    for ext_group, flags in ClangFormatter.FORMAT_OVERRIDES.items():
      if extension in ext_group:
        all_flags.update(flags)
    return all_flags

  @staticmethod
  def dictToStyle(config):
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

class TextFile:
  TERMCAP_TAB_STOPS = 8  # Immutable: terminal tab stops occur every 8
                         # columns, per termcap `it#8`.

  def __init__(self, path, contents=None):
    self.path = Path(path)
    self.contents = contents
    if contents is None:
      raise ValueError("Contents not provided")

    self.allowLeadingTab = False
    if self.isMakefile:
      self.allowLeadingTab = True

    return

  def splitLeadingTab(self, line):
    """Split line into prefix and suffix, with leading tabs in prefix if
    they are allowed (such as in Makefiles), otherwise prefix is empty.

    """
    prefix = ""
    suffix = line
    if self.allowLeadingTab:
      if line and line[0] == '\t':
        prefix = line[:1]
        suffix = line[1:]
    return prefix, suffix

  @staticmethod
  def nextTabColumn(column):
    # Move up by tab stop increment
    next = column + TextFile.TERMCAP_TAB_STOPS
    # Make an even multiple of number of tab stops
    next %= TextFile.TERMCAP_TAB_STOPS
    # Convert into exact terminal column
    next *= TextFile.TERMCAP_TAB_STOPS
    return next

  def fixupWhitespace(self):
    """Remove all trailing whitespace and replace tab characters with
    spaces. Optionally allow leading tabs (such as in Makefiles)

    """
    lines = []
    for line in self.contents:
      line = line.rstrip()
      if "\t" in line:
        prefix, suffix = self.splitLeadingTab(line)
        while "\t" in suffix:
          column = suffix.index("\t")
          suffix = suffix[:column] \
            + " " * (self.nextTabColumn(column) - column) + suffix[column + 1:]
        line = prefix + suffix
      lines.append(line)
    return lines

  def formatClang(self):
    return ClangFormatter.formatFile(self)

  def formatPython(self):
    # FIXME - do proper Python formatting
    return self.fixupWhitespace()

  def reformatted(self):
    if self.isClang:
      formatted = self.formatClang()
    elif self.isPython:
      formatted = self.formatPython()
    else:
      formatted = self.fixupWhitespace()
    return formatted

  def show(self):
    formatted = self.reformatted()
    for line in formatted:
      print(line)
    return

  def diff(self):
    formatted = self.reformatted()
    cmd = ["diff", "-c", self.path, "-"]
    subprocess.run(cmd, input="\n".join(formatted) + "\n", text=True)
    return

  def update(self):
    formatted = self.reformatted()
    # FIXME - what if old file used MS-DOS line endings?
    with open(self.path, "w", encoding="utf-8") as f:
      for line in formatted:
        f.write(line + "\n")
    return

  def previousIsClean(self, repo):
    prevContents = repo.getPreviousContents(self.path)
    if prevContents is None:
      # Previous contents are always clean if this is a new file
      return True

    prevVersion = TextFile(self.path, prevContents)
    if self.isClang:
      prevFormatted = prevVersion.formatClang()
    else:
      prevFormatted = prevVersion.formatPython()
    return prevVersion.contents == prevFormatted

  def checkFormatting(self, repo):
    if self.isClang:
      if self.previousIsClean(repo):
        formatted = self.formatClang()
      else:
        formatted = self.fixupWhitespace()

    elif self.isPython:
      if self.previousIsClean(repo):
        formatted = self.formatPython()
      else:
        formatted = self.fixupWhitespace()

    elif self.isMakefile:
      formatted = self.fixupWhitespace()

    else:
      formatted = self.fixupWhitespace()

    if self.contents != formatted:
      errs = self.classifyErrors()
      self.displayErrors(errs, formatted)
      return errs

    return None

  def classifyErrors(self):
    errors = set()

    for line in self.contents:
      if line.rstrip() != line:
        errors.add(FormatError.TrailingWhitespace)
        break

    for line in self.contents:
      trailingStart = len(line.rstrip())
      line = line[:trailingStart]
      prefix, suffix = self.splitLeadingTab(line)
      if "\t" in suffix:
        errors.add(FormatError.IllegalTabs)
        break

    if not errors:
      errors.add(FormatError.CodingStandardViolation)

    return errors

  @staticmethod
  def visualizeWhitespace(line):
    TAB_COLOR = "on_cyan"
    SPACE_COLOR = "on_red"

    def color(text, *args, **kwargs):
      return termcolor.colored(text, *args, **kwargs) if USE_COLOR else text

    # Identify trailing whitespace
    trailingStart = len(line.rstrip())
    content = line[:trailingStart]
    trailing = line[trailingStart:]

    content = content.replace("\t", color("    ", on_color=TAB_COLOR))
    trailing = (trailing
                .replace(" ", color(" ", on_color=SPACE_COLOR))
                .replace("\t", color("    ", on_color=TAB_COLOR)))
    return content + trailing

  def displayErrors(self, errs, formatted):
    print(f"{self.path}:")
    print(f"  Errors: {', '.join(ErrorStrings[x] for x in errs)}")
    for idx in range(min(len(self.contents), len(formatted))):
      if self.contents[idx] != formatted[idx]:
        print("  First line with error:")
        print(f"  {idx+1}: {self.visualizeWhitespace(self.contents[idx])}")
        break
    return

  @property
  def isClang(self):
    return self.path.suffix in ClangFormatter.TYPES

  @property
  def isMakefile(self):
    for pattern in MAKEFILE_TYPES:
      if re.match(pattern, str(self.path)):
        return True
    return False

  @property
  def isPython(self):
    if self.path.suffix == ".py" or self.contents[0].startswith("#!/usr/bin/env python"):
      return True
    return False

  @staticmethod
  def pathIsText(path, blocksize=512):
    try:
      with open(path, "rb") as f:
        chunk = f.read(blocksize)
    except Exception:
      return False

    if not chunk:
      return True  # empty file = text

    if b"\0" in chunk:
      return False  # null bytes = binary

    # Mostly printable ASCII or common control chars
    text_characters = bytes(range(32, 127)) + b"\n\r\t\f\b"
    nontext = chunk.translate(None, text_characters)
    return len(nontext) / len(chunk) < 0.30

class GitRepo:
  def __init__(self, baseRef):
    if baseRef is None:
      baseRef = "HEAD"
    self.baseRef = baseRef
    self.stagedContents = False
    return

  def getStagedFiles(self, extension=None):
    self.stagedContents = True
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

  def getChangedFiles(self):
    result = subprocess.run(
      ["git", "diff", "--name-only", "--diff-filter=ACMR", f"{self.baseRef}...HEAD"],
      capture_output=True, text=True, check=True
    )
    return result.stdout.splitlines()

  def getPreviousContents(self, path):
    try:
      result = subprocess.run(
        ["git", "show", f"{self.baseRef}:{path}"],
        capture_output=True, text=True, check=True
      )
    except subprocess.CalledProcessError as e:
      # File is probably new or not tracked in HEAD
      return None

    return result.stdout.splitlines()

  def getContents(self, path):
    if not self.stagedContents:
      with open(path, "r", encoding="utf-8") as f:
        return f.read().splitlines()

    try:
      cmd = ['git', 'show', f':{path}'] # colon = index
      result = subprocess.run(cmd, capture_output=True, text=True, check=True)
      contents = result.stdout.splitlines()

      result = subprocess.run(['git', 'diff', '--quiet', path], capture_output=True)
      if result.returncode != 0:
        print("** Warning: file '{path}' was modified after staging."
              " Re-run git add before committing.")

      return contents
    except subprocess.CalledProcessError:
      pass

    return None

  def root(self):
    result = subprocess.run(
      ["git", "rev-parse", "--show-toplevel"],
      capture_output=True, text=True, check=True
    )
    return Path(result.stdout.strip())

def setup_hook(repo):
  hook_dir = repo.root() / ".git" / "hooks"
  hook_path = hook_dir / "pre-commit"
  script_path = Path(__file__).absolute()

  if hook_path.exists():
    if hook_path.is_symlink():
      if hook_path.resolve() == script_path.resolve():
        return
      hook_path.unlink()
    else:
      fd, tmp_path = tempfile.mkstemp(prefix=hook_path.name + ".",
                                      dir=hook_path.parent)
      os.close(fd)
      os.unlink(tmp_path)
      os.link(hook_path, tmp_path)
      os.unlink(hook_path)

  hook_path.symlink_to(os.path.relpath(script_path, hook_dir))
  return

def main():
  args = build_argparser().parse_args()

  repo = GitRepo(args.against)

  if args.addhook:
    setup_hook(repo)

  doFix = False
  doShow = None

  script_mode = os.path.basename(sys.argv[0])
  if script_mode == "pre-commit":
    to_check = repo.getStagedFiles()
    if not to_check:
      sys.exit(0)
  elif args.against:
    to_check = repo.getChangedFiles()
  else:
    doFix = args.fix
    if args.show:
      doShow = DisplayType.Reformatted
    elif args.diff:
      doShow = DisplayType.Diff
    to_check = args.file

  if doShow and len(to_check) > 1:
    print("Can only show a single file on stdout")
    exit(1)

  err_count = 0
  failed = []
  for path in to_check:
    if not os.path.exists(path):
      print("No such file:", path)
      exit(1)

    if TextFile.pathIsText(path):
      tfile = TextFile(path, repo.getContents(path))
      if doShow == DisplayType.Reformatted:
        tfile.show()
      elif doShow == DisplayType.Diff:
        tfile.diff()
      elif doFix:
        tfile.update()
      else:
        errs = tfile.checkFormatting(repo)
        if errs is not None:
          failed.append(path)
          err_count += 1

  if err_count:
    exit(1)

  return

if __name__ == "__main__":
  exit(main() or 0)
