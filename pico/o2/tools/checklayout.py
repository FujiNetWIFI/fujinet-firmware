#!/usr/bin/env python3
"""Fail a build when two pinned routines overlap in an AS listing.

Every routine in this project is placed with an explicit `org`, because
conditional jumps on the 8048 are page-relative and a routine that drifts
across a 256-byte boundary stops assembling. The cost of that discipline is
that a routine which simply grows silently lands on top of the next one:
p2bin says "overlapping memory allocation!" and nothing about where.
"""
import re
import sys

def check(path):
    owner = {}
    label = "(start)"
    bad = []
    for line in open(path, errors="ignore"):
        m = re.search(r"\b([a-z_][a-z_0-9]*):", line)
        if m and " : " in line:
            label = m.group(1)
        m = re.match(r"^(?:\(\d+\))?\s*\d+/\s*([0-9A-F]{3,4}) : ((?:[0-9A-F]{2} )+)", line)
        if not m:
            continue
        addr = int(m.group(1), 16)
        for i in range(len(m.group(2).split())):
            a = addr + i
            if a in owner and owner[a] != label:
                bad.append((a, owner[a], label))
            owner[a] = label
    if bad:
        first = {}
        for a, old, new in bad:
            first.setdefault((old, new), a)
        print("%s: OVERLAP" % path, file=sys.stderr)
        for (old, new), a in sorted(first.items(), key=lambda kv: kv[1]):
            print("  $%03X: '%s' has grown into '%s'" % (a, old, new), file=sys.stderr)
        return 1
    return 0

sys.exit(max(check(p) for p in sys.argv[1:]))
