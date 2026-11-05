#!/usr/bin/env python3
"""
Policy check: preprocessor conditionals (#if / #ifdef / #ifndef / #elif) may
not reference an externally-defined identifier unless that identifier is
explicitly allowed for the file being checked. This is a default-deny
allow-list, not a name-pattern denylist: a prior version matched only
BUILD_* names, which is gameable by simply not using that name (e.g.
#ifdef COCO_HS_UART). Here, every identifier is a violation unless it is
allowed by one of:

  1. GLOBAL_ALLOW below -- identifiers allowed everywhere this script is
     used, because they are genuinely universal, not project- or
     platform-specific (e.g. __cplusplus).

  2. The include-guard idiom, recognized structurally
     (#ifndef FOO_H / #define FOO_H), so header guards never need listing.

  3. A per-directory allow-list in the config file (--config, default
     ifdef_check_config.json next to this script). This is for things
     that are legitimately fine in *some* directories but not others --
     e.g. lib/hal legitimately needs to know whether it's compiling for
     ESP32 vs. the host (Linux/Mac/Windows) test build, but
     lib/device/fujiDevice should never need to know that at all. The
     config maps a directory (relative to --root) to the extra
     identifiers allowed for every file under it; the longest matching
     directory prefix wins, so subdirectories inherit their parent's
     list unless they override it. Each entry may also carry
     "allow_regex": patterns that must match an identifier in full
     (re.fullmatch), e.g. "FUJI_PLATFORM_[A-Z0-9]+". Keep patterns
     narrow: a broad prefix wildcard re-opens the invent-a-new-name
     loophole this check exists to close.

  3b. A per-file allow-list in the same config file's "files" section,
      keyed by exact path (relative to --root). Use this instead of the
      inline comment (#4) when you'd rather not hand-edit the file
      itself -- e.g. it's vendored or generated -- or when you want every
      exception in the tree visible in one place instead of scattered
      across files.

  4. An inline suppression comment for a one-off exception scoped to a
     single line, in a single file:
         #ifdef SOME_RARE_THING  // ifdef-check-allow: SOME_RARE_THING
     or, on its own line immediately above the directive:
         // ifdef-check-allow: SOME_RARE_THING
         #ifdef SOME_RARE_THING
     This is preferred over growing the config file for a case that only
     applies to one line: the exception lives right next to the code it
     exempts, shows up in the same diff/review as the code, and vanishes
     automatically if that code is later deleted.

This is NOT a simple grep: it performs real (if simplified) preprocessing
translation-phase work before matching, so it isn't fooled by an identifier
mentioned only in a comment or string, a directive split across lines with
a backslash-newline continuation, or irregular whitespace around '#'.

Exit code 0 = no violations found (test passes).
Exit code 1 = violations found, printed to stderr (test fails).
Exit code 2 = usage / config error.
"""

import argparse
import json
import re
import sys
from pathlib import Path

DIRECTIVE_RE = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif)\b\s*(.*)$")
DEFINE_RE = re.compile(r"^\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)\b")
SUPPRESS_RE = re.compile(r"ifdef-check-allow:\s*([A-Za-z0-9_,\s]+)")

# Identifiers allowed in every directory this script is ever pointed at,
# because they are genuinely universal -- not project- or
# platform-specific. Keep this list very short; anything narrower than
# "true for every C++ build everywhere" belongs in the per-directory
# config instead.
GLOBAL_ALLOW = {
    # Standard idiom for C/C++ interop headers (extern "C" wrapping).
    "__cplusplus",
}

# Words that can appear in a conditional expression but are not macro
# identifiers being tested (C++ alternative operator spellings, "defined").
NON_IDENTIFIER_KEYWORDS = {
    "defined", "and", "or", "not", "xor", "bitand", "bitor", "compl",
}

IDENTIFIER_RE = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")
INTEGER_LITERAL_RE = re.compile(r"^(0[xX][0-9a-fA-F]+|[0-9]+)$")

SOURCE_EXTENSIONS = {".h", ".hpp", ".cpp", ".cc", ".cxx"}

DEFAULT_CONFIG_NAME = "ifdef_check_config.json"


def config_error(msg: str):
    """Config problems exit 2, distinct from exit 1 (violations found)."""
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(2)


def load_config(config_path: Path):
    """Return {} if there is no config file at all (script still works
    with just GLOBAL_ALLOW + include guards); raise if one exists but is
    malformed, since a broken config should fail loudly, not silently
    allow everything or nothing."""
    if not config_path.is_file():
        return {}
    try:
        data = json.loads(config_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as e:
        config_error(f"could not parse {config_path}: {e}")
    data.setdefault("directories", {})
    data.setdefault("files", {})
    return data


class AllowedIdentifiers(set):
    """A set of literal identifiers plus a list of compiled regexes.
    `name in allowed` is true for an exact literal match, or when a regex
    matches the *entire* name (re.fullmatch), so a pattern like
    FUJI_PLATFORM_[A-Z0-9]+ can't accidentally match a longer name that
    merely starts the same way."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.patterns = []

    def add_patterns(self, patterns, origin):
        for pat in patterns:
            try:
                self.patterns.append(re.compile(pat))
            except re.error as e:
                config_error(f"bad allow_regex {pat!r} in {origin}: {e}")

    def __contains__(self, name):
        return super().__contains__(name) or any(
            p.fullmatch(name) for p in self.patterns)


def allowed_identifiers_for(rel_path: Path, config: dict) -> AllowedIdentifiers:
    """Union of three layers, all additive (nothing a broader layer allows
    is ever taken away by a narrower one):
      - global_allow / GLOBAL_ALLOW: applies everywhere
      - directories: longest-matching-directory-prefix (a file under
        lib/hal/esp32/ inherits lib/hal's allow-list unless
        lib/hal/esp32 has its own, more specific entry)
      - files: exact match on this one file's path, for an exception
        that's specific to a single file rather than a whole directory
        (e.g. a vendored/generated file, or a case you'd rather record
        centrally than annotate inline in that file).
    Each layer may have "allow" (literal names) and "allow_regex"
    (full-match patterns)."""
    allowed = AllowedIdentifiers(GLOBAL_ALLOW)
    allowed.update(config.get("global_allow", []))
    allowed.add_patterns(config.get("global_allow_regex", []), "global_allow_regex")

    best_match_len = -1
    best_entry = {}
    best_key = None
    for dir_key, entry in config.get("directories", {}).items():
        if dir_key.startswith("_"):
            continue  # convention for a comment key, e.g. "_comment"
        dir_parts = Path(dir_key).parts
        if rel_path.parts[: len(dir_parts)] == dir_parts:
            if len(dir_parts) > best_match_len:
                best_match_len = len(dir_parts)
                best_entry = entry
                best_key = dir_key
    allowed.update(best_entry.get("allow", []))
    allowed.add_patterns(best_entry.get("allow_regex", []), f"directories[{best_key!r}]")

    rel_posix = rel_path.as_posix()
    for file_key, entry in config.get("files", {}).items():
        if file_key.startswith("_"):
            continue  # convention for a comment key, e.g. "_comment"
        if Path(file_key).as_posix() == rel_posix:
            allowed.update(entry.get("allow", []))
            allowed.add_patterns(entry.get("allow_regex", []), f"files[{file_key!r}]")
            break

    return allowed


def splice_line_continuations(text: str) -> str:
    return re.sub(r"\\\r?\n", "", text)


def strip_comments_and_strings(text: str, keep_comments: bool = False) -> str:
    """Blank out code (or, if keep_comments, blank out everything BUT
    comments) while preserving line breaks and string/char literal
    boundaries, so line numbers never shift."""
    out = []
    i = 0
    n = len(text)
    in_line_comment = False
    in_block_comment = False
    in_string = False
    in_char = False

    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""

        if in_line_comment:
            if c == "\n":
                in_line_comment = False
                out.append(c)
            else:
                out.append(c if keep_comments else " ")
            i += 1
            continue

        if in_block_comment:
            if c == "*" and nxt == "/":
                in_block_comment = False
                out.append("  " if not keep_comments else "**")
                i += 2
            else:
                out.append("\n" if c == "\n" else (c if keep_comments else " "))
                i += 1
            continue

        if in_string:
            if c == "\\" and i + 1 < n:
                out.append("  ")
                i += 2
                continue
            if c == '"':
                in_string = False
            out.append(" " if c != "\n" else "\n")
            i += 1
            continue

        if in_char:
            if c == "\\" and i + 1 < n:
                out.append("  ")
                i += 2
                continue
            if c == "'":
                in_char = False
            out.append(" " if c != "\n" else "\n")
            i += 1
            continue

        if c == "/" and nxt == "/":
            in_line_comment = True
            out.append("  " if not keep_comments else "//")
            i += 2
            continue
        if c == "/" and nxt == "*":
            in_block_comment = True
            out.append("  " if not keep_comments else "/*")
            i += 2
            continue
        if c == '"':
            in_string = True
            out.append(c if keep_comments else c)
            i += 1
            continue
        if c == "'":
            in_char = True
            out.append(c)
            i += 1
            continue

        if c == "\n":
            out.append("\n")
        else:
            out.append(" " if keep_comments else c)
        i += 1

    return "".join(out)


def suppressed_identifiers(comment_text: str) -> set:
    """Parse identifiers named in an 'ifdef-check-allow: A, B' marker
    found within a line's comment text (or empty set if none)."""
    m = SUPPRESS_RE.search(comment_text)
    if not m:
        return set()
    return {tok.strip() for tok in m.group(1).split(",") if tok.strip()}


def suspect_identifiers(expr: str, allowed: set):
    found = set()
    for tok in IDENTIFIER_RE.findall(expr):
        if tok in NON_IDENTIFIER_KEYWORDS:
            continue
        if tok in allowed:
            continue
        if INTEGER_LITERAL_RE.match(tok):
            continue
        found.add(tok)
    return found


def find_violations(path: Path, allowed: set):
    raw = path.read_text(encoding="utf-8", errors="replace")
    spliced = splice_line_continuations(raw)
    code_only = strip_comments_and_strings(spliced, keep_comments=False)
    comments_only = strip_comments_and_strings(spliced, keep_comments=True)

    code_lines = code_only.splitlines()
    comment_lines = comments_only.splitlines()
    spliced_lines = spliced.splitlines()

    violations = []

    for lineno, line in enumerate(code_lines, start=1):
        m = DIRECTIVE_RE.match(line)
        if not m:
            continue

        kind, expr = m.group(1), m.group(2)

        # Recognize the standard include-guard idiom structurally.
        if kind == "ifndef":
            guard_ident = expr.strip()
            if IDENTIFIER_RE.fullmatch(guard_ident):
                j = lineno
                while j < len(code_lines) and not code_lines[j].strip():
                    j += 1
                if j < len(code_lines):
                    dm = DEFINE_RE.match(code_lines[j])
                    if dm and dm.group(1) == guard_ident:
                        continue

        suspects = suspect_identifiers(expr, allowed)
        if not suspects:
            continue

        # Inline suppression: same line, or a comment-only line directly above.
        suppressed = suppressed_identifiers(comment_lines[lineno - 1])
        if lineno - 2 >= 0 and not code_lines[lineno - 2].strip():
            suppressed |= suppressed_identifiers(comment_lines[lineno - 2])

        suspects -= suppressed
        if suspects:
            orig_line = spliced_lines[lineno - 1]
            violations.append((lineno, orig_line.strip(), suspects))

    return violations


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("roots", nargs="+", help="directories to scan")
    parser.add_argument(
        "--root",
        default=None,
        help="project root that config directory/file keys are relative to "
             "(default: the parent of the directory containing the config "
             "file, i.e. the repo root when the config lives in tests/)",
    )
    parser.add_argument(
        "--config",
        default=None,
        help=f"path to config JSON (default: {DEFAULT_CONFIG_NAME} next to this script)",
    )
    args = parser.parse_args()

    if args.config:
        config_path = Path(args.config)
        if not config_path.is_file():
            config_error(f"config file not found: {config_path}")
    else:
        config_path = Path(__file__).resolve().parent / DEFAULT_CONFIG_NAME
        if not config_path.is_file():
            print(f"warning: no config at {config_path}; using built-in "
                  f"defaults only", file=sys.stderr)
    config = load_config(config_path)

    if args.root:
        project_root = Path(args.root).resolve()
    else:
        project_root = config_path.resolve().parent.parent
    if not project_root.is_dir():
        config_error(f"--root {project_root} is not a directory")

    # Catch typos / stale entries: a key that points at nothing can never
    # take effect, which otherwise looks exactly like "the config is ignored".
    for section in ("directories", "files"):
        for key in config.get(section, {}):
            if key.startswith("_"):
                continue
            if not (project_root / key).exists():
                print(f"warning: {section} entry {key!r} does not exist under "
                      f"{project_root}; it will have no effect (wrong --root?)",
                      file=sys.stderr)

    any_violation = False

    for root_arg in args.roots:
        root = Path(root_arg)
        if not root.is_dir():
            print(f"error: {root} is not a directory", file=sys.stderr)
            return 2

        for path in sorted(root.rglob("*")):
            if path.suffix not in SOURCE_EXTENSIONS or not path.is_file():
                continue

            try:
                rel_path = path.resolve().relative_to(project_root)
            except ValueError:
                # Silently falling back to the absolute path made every
                # directory/file entry in the config quietly stop matching.
                config_error(f"{path} is not under project root "
                             f"{project_root}; pass the correct --root so "
                             f"config paths can be matched")

            allowed = allowed_identifiers_for(rel_path, config)

            for lineno, line, suspects in find_violations(path, allowed):
                any_violation = True
                idents = ", ".join(sorted(suspects))
                print(
                    f"{path}:{lineno}: platform ifdef not allowed here "
                    f"({idents})\n    {line}",
                    file=sys.stderr,
                )

    if any_violation:
        print(
            "\nPreprocessor conditionals referencing externally-defined "
            "identifiers are not allowed unless explicitly permitted "
            "(see GLOBAL_ALLOW, the config's per-directory allow-list, or "
            "an inline '// ifdef-check-allow: NAME' comment). Customize "
            "via a subclass override instead.",
            file=sys.stderr,
        )
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
