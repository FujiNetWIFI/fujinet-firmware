"""
version_common.py

Shared git version-detection helpers used by both debug_version.py
(platformio/SCons builds) and build_version_pc.py (cmake builds).

Centralizing this logic means both scripts get the same parsing of
`git describe`, the same macro-file reading, and the same bug fixes
in one place instead of two slightly-different copies.
"""

import re
import subprocess

# Matches a line like: #define NAME VALUE  // optional comment
MACRO_PATTERN = r"^\s*#define\s+(.*?)\s+(.*?)\s*(?://.*|/\*[\s\S]*?\*/)?$"

# `git describe --always HEAD` when a reachable annotated tag exists:
#   v1.2.3-4-gabcdef0  ->  major=1 minor=2 patch=3 commits=4 hash=abcdef0
_TAG_DESCRIBE_RE = re.compile(r"^v([0-9]+)[.]([0-9]+)[.]([0-9]+)-([0-9]+)-g(.*)")

# `git describe --always HEAD` when there is no reachable tag at all,
# it just prints the short commit hash.
_HASH_ONLY_RE = re.compile(r"^([a-z0-9]{7,40})$")


def run_command(cmd):
  return subprocess.check_output(cmd, universal_newlines=True).strip()

def get_modified_files():
  """list of files with uncommitted local modifications"""
  files = run_command(["git", "diff", "--name-only"]).split("\n")
  return [f for f in files if f]

def get_commit_version():
  """raw output of `git describe --always HEAD`"""
  return run_command(["git", "describe", "--always", "HEAD"])

def get_commit_sha(short=False):
  cmd = ["git", "rev-parse", "HEAD"]
  if short:
    cmd.insert(2, "--short")
  try:
    return run_command(cmd)
  except (FileNotFoundError, subprocess.CalledProcessError):
    return ""

def get_head_tags():
  """list of tags (if any) pointing at HEAD"""
  try:
    return run_command(["git", "tag", "--points-at", "HEAD"]).splitlines()
  except (FileNotFoundError, subprocess.CalledProcessError):
    pass
  return []

def parse_describe(describe_str, ver_major=0, ver_minor=0):
  """
  Parse the output of `git describe --always HEAD` into a short
  build-version string such as "v1.2-abcdef0".

  ver_major/ver_minor are used as a fallback major.minor when describe
  only returned a bare commit hash (no reachable tag) -- callers
  typically pass in the major/minor from a known release version.

  Returns a tuple:
      (version_str, major, minor, patch, commits_since, git_hash)
  `major`/`minor`/`patch`/`commits_since`/`git_hash` are None when they
  could not be determined (e.g. describe_str didn't match anything).
  """
  m = _TAG_DESCRIBE_RE.match(describe_str)
  if m:
    major = int(m.group(1))
    minor = int(m.group(2))
    patch = int(m.group(3))
    commits = int(m.group(4))
    git_hash = m.group(5)
    version = f"v{major}.{minor}-{git_hash}"
    return version, major, minor, patch, commits, git_hash

  m = _HASH_ONLY_RE.match(describe_str)
  if m:
    git_hash = m.group(1)
    version = f"v{ver_major}.{ver_minor}-{git_hash}"
    return version, ver_major, ver_minor, None, None, git_hash

  # Nothing matched -- hand back the raw string unchanged.
  return describe_str, None, None, None, None, None

def load_version_macros(path):
  """read #define NAME VALUE macros out of a C header into a dict"""
  macros = {}
  with open(path) as f:
    for line in f:
      m = re.match(MACRO_PATTERN, line)
      if m:
        name = m.group(1)
        if '(' in name:
          continue
        value = m.group(2) or None
        macros[name] = value
  return macros
