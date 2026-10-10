#!/usr/bin/env python3
"""checkdepth.py -- bound the 2650 subroutine-call depth of a client.

The Signetics 2650's return address stack is EIGHT entries deep and on
chip; there is no RAM stack. A call graph deeper than 8 silently corrupts
returns (MAME wraps the stack), so every client must stay within it. This
walks the assembler source, builds the static call graph from BSTA/BSTR
(subroutine branches) to local labels, and reports the deepest chain.

It is a linter, not an assembler: it treats every `LABEL:` as a node and
every `BSTA,cc TARGET` / `BSTR,cc TARGET` whose TARGET is a known label as
an edge from the enclosing label. Tail calls (BCTA/BCTR) are branches, not
calls, so they do not add depth -- which is exactly why the clients use
them for dispatch. Indirect calls (BSXA and friends) are not used by any
client and are flagged if seen.

  checkdepth.py [--max N] [--warn N] file.asm [file2.asm ...]

Exit nonzero if the deepest chain exceeds --max (default 8) or a cycle
(recursion -- unbounded on this stack) is found.
"""

import re
import sys

CALL_RE = re.compile(r'^\s*BS(?:TA|TR|XA)\s*,\s*\w+\s+([A-Za-z_.$][\w.$]*)',
                     re.IGNORECASE)
INDIRECT_RE = re.compile(r'^\s*BSXA\b', re.IGNORECASE)
LABEL_RE = re.compile(r'^([A-Za-z_.$][\w.$]*)\s*:')


def parse(paths):
    """Return {label: set(callee_labels)} and the ordered label list."""
    edges = {}
    order = []
    cur = None
    indirect = []
    for path in paths:
        with open(path) as f:
            for lineno, line in enumerate(f, 1):
                line = line.split(';', 1)[0]          # strip comments
                m = LABEL_RE.match(line)
                if m:
                    cur = m.group(1)
                    if cur not in edges:
                        edges[cur] = set()
                        order.append(cur)
                    # a label line can also carry an instruction after it
                    line = line[m.end():]
                if INDIRECT_RE.search(line):
                    indirect.append((path, lineno))
                c = CALL_RE.match(line if line.startswith((' ', '\t'))
                                  else ' ' + line)
                if c and cur is not None:
                    edges[cur].add(c.group(1))
    return edges, order, indirect


def deepest(edges):
    """Max call depth over the graph; raise on a cycle."""
    memo = {}
    stack = []

    def visit(node):
        if node in stack:
            raise ValueError("recursion: " + " -> ".join(stack + [node]))
        if node in memo:
            return memo[node]
        stack.append(node)
        best = 0
        best_path = [node]
        for callee in edges.get(node, ()):
            if callee not in edges:          # external / data label: leaf
                continue
            d = visit(callee)
            if d + 1 > best:
                best = d + 1
                best_path = [node] + memo_path[callee]
        stack.pop()
        memo[node] = best
        memo_path[node] = best_path
        return best

    memo_path = {}
    overall = 0
    overall_path = []
    for node in edges:
        d = visit(node)
        if d + 1 > overall:              # +1: the node itself occupies a slot
            overall = d + 1
            overall_path = memo_path[node]
    return overall, overall_path


def main():
    args = sys.argv[1:]
    hard, warn = 8, 6
    files = []
    i = 0
    while i < len(args):
        if args[i] == '--max':
            hard = int(args[i + 1]); i += 2
        elif args[i] == '--warn':
            warn = int(args[i + 1]); i += 2
        else:
            files.append(args[i]); i += 1
    if not files:
        print(__doc__.strip(), file=sys.stderr)
        return 2

    edges, _, indirect = parse(files)
    if indirect:
        for path, ln in indirect:
            print(f"checkdepth: {path}:{ln}: indirect call (BSXA) "
                  f"cannot be bounded statically", file=sys.stderr)
        return 1
    try:
        depth, path = deepest(edges)
    except ValueError as e:
        print(f"checkdepth: {e}", file=sys.stderr)
        return 1

    chain = " -> ".join(path)
    if depth > hard:
        print(f"checkdepth: FAIL depth {depth} > {hard} (RAS is 8): {chain}",
              file=sys.stderr)
        return 1
    flag = "  (WARN: close to the 8-deep limit)" if depth > warn else ""
    print(f"checkdepth: ok, max call depth {depth}: {chain}{flag}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
