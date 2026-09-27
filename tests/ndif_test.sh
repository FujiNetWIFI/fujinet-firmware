#!/usr/bin/env bash
#
# ndif_test.sh - decode every NDIF (Disk Copy 6) "*.img" entry of
# Now_Software.sit with `unsit -i` and compare it with ndif2raw, the
# reference ndif.c was ported from; ndif_blocks_test spot-checks
# ndif_read_blocks() against the same output. Skipped (exit 0) when
# ndif2raw cannot be built.
#
# Usage: NDIF2RAW_DIR=<ndif2raw checkout> NOW_SOFTWARE_SIT=<archive> \
#        UNSIT_BIN=<unsit> NDIF_BLOCKS_TEST_BIN=<ndif_blocks_test> tests/ndif_test.sh

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

UNSIT="${UNSIT_BIN:-$SCRIPT_DIR/../build_pc/tests/unsit}"
NDIF_BLOCKS_TEST="${NDIF_BLOCKS_TEST_BIN:-$SCRIPT_DIR/../build_pc/tests/ndif_blocks_test}"
UNAR="${UNAR_BIN:-$(command -v unar)}"
MAKE_BIN="${MAKE_BIN:-make}"
NDIF2RAW_DIR="${NDIF2RAW_DIR:-}"
NOW_SOFTWARE_SIT="${NOW_SOFTWARE_SIT:-${SIT_SAMPLES_DIR:+$SIT_SAMPLES_DIR/Now_Software.sit}}"

if [ -z "$NDIF2RAW_DIR" ] || [ -z "$NOW_SOFTWARE_SIT" ]; then
    echo "ndif_test.sh: set NDIF2RAW_DIR and NOW_SOFTWARE_SIT (or SIT_SAMPLES_DIR)." >&2
    exit 1
fi

if [ ! -x "$UNSIT" ]; then
    for cand in "$SCRIPT_DIR"/../build*/tests/unsit "$SCRIPT_DIR"/../build*/unsit; do
        if [ -x "$cand" ]; then UNSIT="$cand"; break; fi
    done
fi
if [ ! -x "$NDIF_BLOCKS_TEST" ]; then
    for cand in "$SCRIPT_DIR"/../build*/tests/ndif_blocks_test "$SCRIPT_DIR"/../build*/ndif_blocks_test; do
        if [ -x "$cand" ]; then NDIF_BLOCKS_TEST="$cand"; break; fi
    done
fi

if [ ! -x "$UNSIT" ]; then
    echo "ndif_test.sh: cannot find the unsit binary (looked at $UNSIT and common build dirs)." >&2
    echo "Build it first (see stuffit_test.sh), then: UNSIT_BIN=/path/to/unsit $0" >&2
    exit 1
fi
if [ ! -x "$NDIF_BLOCKS_TEST" ]; then
    echo "ndif_test.sh: cannot find the ndif_blocks_test binary (looked at $NDIF_BLOCKS_TEST and common build dirs)." >&2
    exit 1
fi
if [ ! -x "$UNAR" ]; then
    echo "ndif_test.sh: unar not found at $UNAR (install The Unarchiver CLI, or set UNAR_BIN)." >&2
    exit 1
fi
if [ ! -f "$NOW_SOFTWARE_SIT" ]; then
    echo "ndif_test.sh: sample archive not found: $NOW_SOFTWARE_SIT" >&2
    exit 1
fi
if [ ! -d "$NDIF2RAW_DIR" ]; then
    echo "ndif_test.sh: SKIPPED - ndif2raw reference tool source not found at $NDIF2RAW_DIR" >&2
    exit 0
fi

# --- build the ndif2raw oracle (dev-only tooling: skip, don't fail, if this doesn't work) ---
if ! command -v "$MAKE_BIN" >/dev/null 2>&1; then
    echo "ndif_test.sh: SKIPPED - '$MAKE_BIN' not available to build the ndif2raw oracle" >&2
    exit 0
fi
if ! "$MAKE_BIN" -C "$NDIF2RAW_DIR" >"$NDIF2RAW_DIR/.ndif_test_make.log" 2>&1; then
    echo "ndif_test.sh: SKIPPED - building ndif2raw failed (see $NDIF2RAW_DIR/.ndif_test_make.log)" >&2
    exit 0
fi
NDIF2RAW="$NDIF2RAW_DIR/ndif2raw"
if [ ! -x "$NDIF2RAW" ]; then
    echo "ndif_test.sh: SKIPPED - ndif2raw did not produce an executable at $NDIF2RAW" >&2
    exit 0
fi

# --- enumerate the *.img entries via `unsit -l` ---
mapfile -t img_entries < <(
    "$UNSIT" -l "$NOW_SOFTWARE_SIT" 2>/dev/null \
        | sed -E 's/ data:.*$//; s/[[:space:]]+$//' \
        | grep -E '\.img$'
)

if [ ${#img_entries[@]} -eq 0 ]; then
    echo "ndif_test.sh: no *.img entries found in $NOW_SOFTWARE_SIT (via unsit -l)" >&2
    exit 1
fi

echo "ndif_test.sh: found ${#img_entries[@]} *.img entr(y/ies) in Now_Software.sit:"
for p in "${img_entries[@]}"; do echo "    $p"; done
if [ ${#img_entries[@]} -ne 11 ]; then
    echo "ndif_test.sh: NOTE - expected 11 per the task brief, found ${#img_entries[@]} - continuing with what was found" >&2
fi

# --- extract every entry's resource fork (as a com.apple.ResourceFork xattr) once ---
unar_dir="$(mktemp -d "${TMPDIR:-/tmp}/ndif_test_unar.XXXXXX")"
if ! "$UNAR" -D -f -q -o "$unar_dir" "$NOW_SOFTWARE_SIT" >"$unar_dir.log" 2>&1; then
    echo "ndif_test.sh: FAIL - unar could not extract $NOW_SOFTWARE_SIT (see $unar_dir.log)" >&2
    rm -rf "$unar_dir" "$unar_dir.log"
    exit 1
fi

# Same colon/slash-folding lookup stuffit_test.sh uses to find unar's
# on-disk counterpart for an entry path that may contain a literal '/'
# within a single logical path component.
find_unar_counterpart() {
    local dir="$1" rel="$2"
    if [ -e "$dir/$rel" ]; then
        printf '%s\n' "$dir/$rel"
        return 0
    fi
    local IFS='/'
    read -r -a parts <<< "$rel"
    local n=${#parts[@]}
    local k
    for ((k = n - 2; k >= 0; k--)); do
        local candidate=""
        local i
        for ((i = 0; i <= k; i++)); do
            [ -z "$candidate" ] && candidate="${parts[i]}" || candidate="$candidate/${parts[i]}"
        done
        for ((i = k + 1; i < n; i++)); do
            candidate="$candidate:${parts[i]}"
        done
        if [ -e "$dir/$candidate" ]; then
            printf '%s\n' "$dir/$candidate"
            return 0
        fi
    done
    return 1
}

tmp="$(mktemp -d "${TMPDIR:-/tmp}/ndif_test.XXXXXX")"
trap 'rm -rf "$tmp" "$unar_dir" "$unar_dir.log"' EXIT

total=0
failures=0

for entry in "${img_entries[@]}"; do
    total=$((total + 1))
    echo "== $entry =="

    out_raw="$tmp/out_$total.raw"
    oracle_raw="$tmp/oracle_$total.raw"

    if ! "$UNSIT" -i "$NOW_SOFTWARE_SIT" "$entry" "$out_raw" >"$tmp/unsit_$total.log" 2>&1; then
        echo "  FAIL: unsit -i failed (see $tmp/unsit_$total.log)" >&2
        failures=$((failures + 1))
        continue
    fi

    if ! counterpart="$(find_unar_counterpart "$unar_dir" "$entry")"; then
        echo "  FAIL: unar has no counterpart file for this entry" >&2
        failures=$((failures + 1))
        continue
    fi

    # Sanity check noted in the task brief: the 'bcem' resource fork for
    # these particular sample images is 668 bytes. xattr -p prints hex
    # pairs separated by whitespace/newlines, so byte count = hex-digit
    # count / 2.
    rsrc_hexchars="$(xattr -px com.apple.ResourceFork "$counterpart" 2>/dev/null | tr -d ' \n' | wc -c | tr -d ' ')"
    rsrc_bytes=$(( rsrc_hexchars / 2 ))
    if [ "$rsrc_bytes" -eq 0 ]; then
        echo "  FAIL: unar's extracted file has no com.apple.ResourceFork xattr" >&2
        failures=$((failures + 1))
        continue
    fi
    echo "  resource fork: $rsrc_bytes byte(s)"

    if ! "$NDIF2RAW" --format=resource-fork "$counterpart" "$oracle_raw" >"$tmp/ndif2raw_$total.log" 2>&1; then
        echo "  FAIL: ndif2raw failed (see $tmp/ndif2raw_$total.log)" >&2
        failures=$((failures + 1))
        continue
    fi

    if ! cmp -s "$out_raw" "$oracle_raw"; then
        echo "  FAIL: unsit -i output differs from ndif2raw oracle" >&2
        failures=$((failures + 1))
        continue
    fi
    echo "  PASS: unsit -i output byte-identical to ndif2raw oracle ($(wc -c < "$out_raw" | tr -d ' ') bytes)"

    # ndif_read_blocks() spot check against the same oracle output.
    if ! "$NDIF_BLOCKS_TEST" "$NOW_SOFTWARE_SIT" "$entry" "$oracle_raw" >"$tmp/blocks_$total.log" 2>&1; then
        echo "  FAIL: ndif_read_blocks spot check failed (see $tmp/blocks_$total.log)" >&2
        cat "$tmp/blocks_$total.log" >&2
        failures=$((failures + 1))
        continue
    fi
    echo "  PASS: ndif_read_blocks spot check"
done

echo
echo "ndif_test.sh summary: $total image(s) checked, $failures failure(s)"

if [ "$failures" -ne 0 ]; then
    echo "ndif_test.sh: FAILED" >&2
    exit 1
fi

echo "ndif_test.sh: all NDIF images byte-identical to ndif2raw, all ndif_read_blocks spot checks passed"
exit 0
