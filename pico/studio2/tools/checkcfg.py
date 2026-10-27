#!/usr/bin/env python3
"""checkcfg.py -- the client build-cart.sh bakes must be a claimed ST2 image: "FUJI" at
$07FC, every page one the cart may claim for an app (as tools/mkst2.py checks).

    tools/checkcfg.py build/config.st2
"""
import sys

sys.dont_write_bytecode = True                  # no __pycache__ in the tree

sys.path.insert(0, __import__("os").path.dirname(__file__))
import st2  # noqa: E402

if len(sys.argv) != 2:
    sys.exit("usage: checkcfg.py <client.st2>")

data = open(sys.argv[1], "rb").read()
try:
    img = st2.parse(data)
except ValueError as e:
    sys.exit("%s: %s" % (sys.argv[1], e))
if not img.claim:
    sys.exit("%s: no FUJI claim at $07FC" % sys.argv[1])
bad = [p for p in img.pages if not st2.page_ok(p, app=True)]
if bad:
    sys.exit("%s: pages the cart may not claim: %s" % (sys.argv[1], " ".join("$%02X" % p for p in bad)))
