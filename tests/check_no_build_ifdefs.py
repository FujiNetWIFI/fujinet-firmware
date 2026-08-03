#!/usr/bin/env python3
"""
Policy check: no platform-specific BUILD_* macros may appear in preprocessor
conditionals (#if / #ifdef / #ifndef / #elif) anywhere under the target
directory (lib/device/fujiDevice by default).

Platform customization must go through subclass overrides, not #ifdefs.
See project conventions for fujiDevice / mixins.

This is NOT a simple grep: it performs real (if simplified) preprocessing
translation-phase work before matching, so it isn't fooled by:
  - BUILD_* mentioned only in a // or /* */ comment
  - BUILD_* mentioned only inside a string or char literal
  - a directive's BUILD_* identifier split across lines with a
    backslash-newline continuation
  - irregular whitespace/tabs around the '#' or the directive keyword

Exit code 0 = no violations found (test passes).
Exit code 1 = violations found, printed to stderr (test fails).
"""

import re
import sys
from pathlib import Path

BUILD_IDENT_RE = re.compile(r"\bBUILD_[A-Za-z0-9_]*\b")
DIRECTIVE_RE = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif)\b")

SOURCE_EXTENSIONS = {".h", ".hpp", ".cpp", ".cc", ".cxx"}


def splice_line_continuations(text: str) -> str:
    """Phase-2-ish: join lines ending in a backslash with the next line,
    so a directive/identifier split across lines is matched as one."""
    return re.sub(r"\\\r?\n", "", text)


def strip_comments_and_strings(text: str) -> str:
    """Phase-3-ish: blank out the contents of comments and string/char
    literals (preserving newlines, so line numbers/line-splits are
    unaffected), so directive matching never sees text that isn't really
    a preprocessor directive."""
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
                out.append(" ")
            i += 1
            continue

        if in_block_comment:
            if c == "*" and nxt == "/":
                in_block_comment = False
                out.append("  ")
                i += 2
            else:
                out.append("\n" if c == "\n" else " ")
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

        # not currently inside comment/string/char
        if c == "/" and nxt == "/":
            in_line_comment = True
            out.append("  ")
            i += 2
            continue
        if c == "/" and nxt == "*":
            in_block_comment = True
            out.append("  ")
            i += 2
            continue
        if c == '"':
            in_string = True
            out.append(c)
            i += 1
            continue
        if c == "'":
            in_char = True
            out.append(c)
            i += 1
            continue

        out.append(c)
        i += 1

    return "".join(out)


def find_violations(path: Path):
    """Return a list of (line_number, line_text, matched_identifiers)."""
    raw = path.read_text(encoding="utf-8", errors="replace")
    spliced = splice_line_continuations(raw)
    cleaned = strip_comments_and_strings(spliced)

    violations = []
    for lineno, line in enumerate(cleaned.splitlines(), start=1):
        if not DIRECTIVE_RE.match(line):
            continue
        matches = BUILD_IDENT_RE.findall(line)
        if matches:
            # Report using the original (uncleaned, but still spliced) line
            # so the person fixing it sees real code, not blanked-out text.
            orig_line = spliced.splitlines()[lineno - 1]
            violations.append((lineno, orig_line.strip(), matches))
    return violations


def main():
    if len(sys.argv) < 2:
        print(f"usage: {sys.argv[0]} <root-dir> [<root-dir> ...]", file=sys.stderr)
        return 2

    roots = [Path(p) for p in sys.argv[1:]]
    any_violation = False

    for root in roots:
        if not root.is_dir():
            print(f"error: {root} is not a directory", file=sys.stderr)
            return 2

        for path in sorted(root.rglob("*")):
            if path.suffix not in SOURCE_EXTENSIONS or not path.is_file():
                continue

            violations = find_violations(path)
            for lineno, line, matches in violations:
                any_violation = True
                idents = ", ".join(sorted(set(matches)))
                print(
                    f"{path}:{lineno}: platform ifdef not allowed here "
                    f"({idents})\n    {line}",
                    file=sys.stderr,
                )

    if any_violation:
        print(
            "\nBUILD_* macros may not appear in #if/#ifdef/#ifndef/#elif "
            "in this directory. Customize via a subclass override instead.",
            file=sys.stderr,
        )
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
