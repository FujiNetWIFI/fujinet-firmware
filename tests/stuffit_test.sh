#!/usr/bin/env bash
#
# stuffit_test.sh - extract every fork of every archive under SAMPLES_DIR
# with unsit and compare it byte for byte with unar (The Unarchiver).
# Resource forks are compared against the com.apple.ResourceFork xattr unar
# writes in its default fork mode (-k visible adds an AppleDouble header).
#
# Usage: SIT_SAMPLES_DIR=<dir> UNSIT_BIN=<unsit> tests/stuffit_test.sh [dir]
# Needs unar, lsar and jq on PATH (or UNAR_BIN, LSAR_BIN, JQ_BIN).

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SAMPLES_DIR="${1:-${SIT_SAMPLES_DIR:-}}"
UNSIT="${UNSIT_BIN:-$SCRIPT_DIR/../build_stuffit_test_only/unsit}"
UNAR="${UNAR_BIN:-$(command -v unar)}"
LSAR="${LSAR_BIN:-$(command -v lsar)}"
JQ="${JQ_BIN:-$(command -v jq)}"

if [ -z "$SAMPLES_DIR" ]; then
    echo "stuffit_test.sh: set SIT_SAMPLES_DIR (or pass a directory) to the sample archives." >&2
    exit 1
fi

if [ ! -x "$UNSIT" ]; then
    # Fall back to searching common build output locations.
    for cand in "$SCRIPT_DIR"/../build/unsit "$SCRIPT_DIR"/../build*/tests/unsit "$SCRIPT_DIR"/../build*/unsit; do
        if [ -x "$cand" ]; then UNSIT="$cand"; break; fi
    done
fi

if [ ! -x "$UNSIT" ]; then
    echo "stuffit_test.sh: cannot find the unsit binary (looked at $UNSIT and common build dirs)." >&2
    echo "Build it first, e.g.: cc -std=c99 -Wall -Wextra -Ilib -o /tmp/unsit tests/unsit.c lib/stuffit/*.c" >&2
    echo "then: UNSIT_BIN=/tmp/unsit $0" >&2
    exit 1
fi

if [ ! -x "$UNAR" ]; then
    echo "stuffit_test.sh: unar not found at $UNAR (install The Unarchiver CLI, or set UNAR_BIN)." >&2
    exit 1
fi

command -v "$LSAR" >/dev/null 2>&1 || LSAR="$(command -v lsar || true)"
command -v "$JQ" >/dev/null 2>&1 || JQ="$(command -v jq || true)"
if [ -z "$LSAR" ] || [ ! -x "$LSAR" ]; then
    echo "stuffit_test.sh: lsar not found (install The Unarchiver CLI, or set LSAR_BIN)." >&2
    exit 1
fi
if [ -z "$JQ" ] || [ ! -x "$JQ" ]; then
    echo "stuffit_test.sh: jq not found (needed to parse 'lsar -j' output; set JQ_BIN or install jq)." >&2
    exit 1
fi

if [ ! -d "$SAMPLES_DIR" ]; then
    echo "stuffit_test.sh: samples directory not found: $SAMPLES_DIR" >&2
    exit 1
fi

# Recurse into $SAMPLES_DIR (this picks up both loose top-level samples and
# anything under a corpus/ subdirectory), skipping obvious non-archive
# bookkeeping files (manifest, docs, Finder metadata).
samples=()
while IFS= read -r -d '' f; do
    bn="$(basename "$f")"
    case "$bn" in
        .DS_Store|*.txt|*.md) continue ;;
    esac
    samples+=("$f")
done < <(find "$SAMPLES_DIR" -type f -print0)

if [ ${#samples[@]} -eq 0 ]; then
    echo "stuffit_test.sh: no sample archives found in $SAMPLES_DIR" >&2
    exit 1
fi

# A classic Mac HFS filename may legally contain a literal '/' - unar
# (matching Finder convention) writes such a name to POSIX disk with
# that character mapped to ':' instead. lib/stuffit's own e->path is
# documented as "raw bytes as stored" (stuffit.h), so an embedded '/'
# in a single component's real name is indistinguishable, on our side,
# from an actual folder boundary; unsit ends up building what looks
# like one extra path level where unar instead emits one flat
# component with a ':' in it (seen for real in Now_Software.sit's
# ".../Sample/Tutorial Calendar", whose true on-disk name is
# "Sample/Tutorial Calendar" as a single component). This helper tries
# the literal relative path first, then progressively folds trailing
# "/"-separated components together with ':' (matching unar's
# substitution) until it finds unar's actual file - a representation
# quirk, not a decompression difference (content still compared byte
# for byte once the counterpart is found).
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

# Byte offset within the first 64KiB that the real StuffIt magic
# ("SIT!"/"STin"/"STi\d"/"ST\d\d" + "rLau" at +10, or the StuffIt 5
# "StuffIt (c)1997-" banner) is found at - used only for diagnosing an
# UNSUPPORTED wrapper (.sea executable prefix, .bin MacBinary header).
# Purely a textual grep, so it can false-positive on coincidental bytes
# for the 4-byte classic prefixes, but "StuffIt (c)1997-" is unambiguous
# and the classic case also cross-checks "rLau" 10 bytes later.
find_magic_offset() {
    local file="$1"
    local hit
    hit="$(head -c 65536 "$file" 2>/dev/null | grep -abo 'StuffIt (c)1997-' | head -1)"
    if [ -n "$hit" ]; then
        printf 'StuffIt5 banner at offset %s\n' "${hit%%:*}"
        return 0
    fi
    local off
    for off in $(head -c 65536 "$file" 2>/dev/null | grep -abo -e 'SIT!' -e 'STin' | cut -d: -f1); do
        local tag
        tag="$(dd if="$file" bs=1 skip=$((off + 10)) count=4 2>/dev/null)"
        if [ "$tag" = "rLau" ]; then
            printf 'classic SIT! magic at offset %s\n' "$off"
            return 0
        fi
    done
    printf 'no StuffIt magic found in first 65536 bytes\n'
    return 1
}

# Coverage table bookkeeping: keyed by "format<TAB>method" -> counts.
declare -A cov_total cov_pass cov_fail cov_unsupported

record_coverage() {
    local fmt="$1" method="$2" status="$3" # status: pass|fail|unsupported
    local key="${fmt}"$'\t'"${method}"
    cov_total["$key"]=$(( ${cov_total["$key"]:-0} + 1 ))
    case "$status" in
        pass)        cov_pass["$key"]=$(( ${cov_pass["$key"]:-0} + 1 )) ;;
        fail)        cov_fail["$key"]=$(( ${cov_fail["$key"]:-0} + 1 )) ;;
        unsupported) cov_unsupported["$key"]=$(( ${cov_unsupported["$key"]:-0} + 1 )) ;;
    esac
}

total_files_compared=0
total_rsrc_compared=0
total_archives=0
total_unsupported=0
failures=0

for archive in "${samples[@]}"; do
    [ -f "$archive" ] || continue
    rel_disp="${archive#"$SAMPLES_DIR"/}"
    total_archives=$((total_archives + 1))

    # --- classify with lsar -j: archive format name + distinct per-entry
    #     compression method names ---
    lsar_json="$("$LSAR" -j "$archive" 2>/dev/null)"
    fmt="unknown"
    methods="(none)"
    if [ -n "$lsar_json" ] && echo "$lsar_json" | "$JQ" -e . >/dev/null 2>&1; then
        fmt="$(echo "$lsar_json" | "$JQ" -r '.lsarFormatName // "unknown"')"
        m="$(echo "$lsar_json" | "$JQ" -r '[.lsarContents[]? | select(.XADIsDirectory != true) | (.XADCompressionName // "unknown")] | unique | join(",")' 2>/dev/null)"
        [ -n "$m" ] && methods="$m"
    fi

    echo "== $rel_disp == [$fmt] methods: $methods"

    # --- can unsit even open this archive? ---
    open_err="$("$UNSIT" -l "$archive" 2>&1 >/dev/null)"
    open_rc=$?
    if [ $open_rc -ne 0 ] && printf '%s' "$open_err" | grep -q "open failed"; then
        diag="$(find_magic_offset "$archive")"
        echo "  UNSUPPORTED: unsit cannot open this archive ($open_err) - $diag"
        total_unsupported=$((total_unsupported + 1))
        IFS=',' read -r -a marr <<< "$methods"
        for meth in "${marr[@]}"; do
            record_coverage "$fmt" "$meth" unsupported
        done
        continue
    fi

    unsit_dir="$(mktemp -d "${TMPDIR:-/tmp}/stuffit_test_unsit.XXXXXX")"
    unar_dir="$(mktemp -d "${TMPDIR:-/tmp}/stuffit_test_unar.XXXXXX")"

    if ! "$UNSIT" -a "$archive" "$unsit_dir" >"$unsit_dir.log" 2>&1; then
        echo "  unsit -a exited nonzero (see $unsit_dir.log) - continuing, some entries may still have extracted"
    fi

    if ! "$UNAR" -D -f -q -o "$unar_dir" "$archive" >"$unar_dir.log" 2>&1; then
        echo "  FAIL: unar could not extract $archive (see $unar_dir.log)" >&2
        failures=$((failures + 1))
        IFS=',' read -r -a marr <<< "$methods"
        for meth in "${marr[@]}"; do
            record_coverage "$fmt" "$meth" fail
        done
        rm -rf "$unsit_dir" "$unar_dir" "$unsit_dir.log" "$unar_dir.log"
        continue
    fi

    archive_files=0
    archive_rsrc=0
    archive_failures=0

    # --- data forks: every plain (non-".rsrc") file unsit produced ---
    while IFS= read -r -d '' f; do
        rel="${f#"$unsit_dir"/}"
        [ -s "$f" ] || continue   # skip zero-length files, nothing meaningful to compare

        if ! counterpart="$(find_unar_counterpart "$unar_dir" "$rel")"; then
            echo "  FAIL: $rel (data) - unsit extracted it but unar has no matching file" >&2
            archive_failures=$((archive_failures + 1))
            continue
        fi

        if ! cmp -s "$f" "$counterpart"; then
            echo "  FAIL: $rel (data) differs from unar's extraction" >&2
            archive_failures=$((archive_failures + 1))
            continue
        fi

        archive_files=$((archive_files + 1))
    done < <(find "$unsit_dir" -type f -name '*.rsrc' -prune -o -type f -print0)

    # --- resource forks: every ".rsrc" sidecar unsit produced, compared
    #     against unar's counterpart data file's resource-fork xattr ---
    while IFS= read -r -d '' f; do
        rel="${f#"$unsit_dir"/}"
        base_rel="${rel%.rsrc}"
        [ -s "$f" ] || continue   # skip zero-length resource forks

        if ! counterpart="$(find_unar_counterpart "$unar_dir" "$base_rel")"; then
            echo "  FAIL: $base_rel (rsrc) - unar has no counterpart file at all" >&2
            archive_failures=$((archive_failures + 1))
            continue
        fi

        rsrc_tmp="$(mktemp "${TMPDIR:-/tmp}/stuffit_test_rsrc.XXXXXX")"
        if ! xattr -px com.apple.ResourceFork "$counterpart" 2>/dev/null | tr -d ' \n' | xxd -r -p > "$rsrc_tmp" 2>/dev/null || [ ! -s "$rsrc_tmp" ]; then
            echo "  FAIL: $base_rel (rsrc) - unsit found a resource fork but unar's file has no com.apple.ResourceFork xattr" >&2
            archive_failures=$((archive_failures + 1))
            rm -f "$rsrc_tmp"
            continue
        fi

        if ! cmp -s "$f" "$rsrc_tmp"; then
            echo "  FAIL: $base_rel (rsrc) differs from unar's resource-fork xattr" >&2
            archive_failures=$((archive_failures + 1))
            rm -f "$rsrc_tmp"
            continue
        fi

        rm -f "$rsrc_tmp"
        archive_rsrc=$((archive_rsrc + 1))
    done < <(find "$unsit_dir" -type f -name '*.rsrc' -print0)

    status="pass"
    [ "$archive_failures" -gt 0 ] && status="fail"

    echo "  compared $archive_files data fork(s), $archive_rsrc resource fork(s), $archive_failures mismatch(es) -> $(echo "$status" | tr '[:lower:]' '[:upper:]')"
    total_files_compared=$((total_files_compared + archive_files))
    total_rsrc_compared=$((total_rsrc_compared + archive_rsrc))
    failures=$((failures + archive_failures))

    IFS=',' read -r -a marr <<< "$methods"
    for meth in "${marr[@]}"; do
        record_coverage "$fmt" "$meth" "$status"
    done

    rm -rf "$unsit_dir" "$unar_dir" "$unsit_dir.log" "$unar_dir.log"
done

echo
echo "=== coverage table (by archive format / compression method) ==="
printf '%-20s %-16s %6s %6s %6s %6s\n' "format" "method" "total" "pass" "fail" "unsup"
for key in "${!cov_total[@]}"; do
    printf '%s\n' "$key"
done | sort -u | while IFS=$'\t' read -r fmt meth; do
    key="${fmt}"$'\t'"${meth}"
    printf '%-20s %-16s %6s %6s %6s %6s\n' "$fmt" "$meth" \
        "${cov_total[$key]:-0}" "${cov_pass[$key]:-0}" "${cov_fail[$key]:-0}" "${cov_unsupported[$key]:-0}"
done

echo
echo "stuffit_test.sh summary: $total_archives archive(s), $total_unsupported unsupported (skipped), $total_files_compared data fork(s) and $total_rsrc_compared resource fork(s) compared, $failures failure(s)"

if [ "$failures" -ne 0 ]; then
    echo "stuffit_test.sh: FAILED" >&2
    exit 1
fi

echo "stuffit_test.sh: all extracted forks byte-identical to unar"
exit 0
