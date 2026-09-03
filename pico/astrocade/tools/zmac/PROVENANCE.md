# zmac 1.3 — provenance

Vendored verbatim from the zmac 1.3 source distribution (Russell Marks'
release lineage), minus the MAXAM/RISCOS platform files this tree does not
use. zmac is the Bally Astrocade community's conventional assembler: all
BallyAlley sample code and HVGLIB-style sources are written for it, which is
why the FujiNet clients in `../../testrom/` are too.

## License

Public domain. The README states: "Public domain by Bruce Norskog, John
Providenza and Colin Kelley. Cleaned up somewhat and documented by Russell
Marks." The COPYRIGHT file preserves Russell Marks' own account of the
history: none of the original authors asserted copyright, Colin Kelley
posted it to comp.sources.unix in 1987 as freely distributable and
modifiable, and Marks' own changes are explicitly public domain. Read
COPYRIGHT before assuming more than that.

## Why vendored

- No distro packages it (unlike Macro Assembler AS, there is not even a
  release-tracking mirror to pin), so CI would otherwise depend on a
  personal website staying up.
- `../../build.sh` builds it on demand with plain `make` (needs yacc/bison
  only if zmac.y is touched; the generated zmac.c is included).

## Changes from upstream

None. Any future patch goes here as a description plus its rationale.
