#!/bin/bash
set -e

VERIFY_DIR="/tmp/webui-verify-full-$$"
mkdir -p "$VERIFY_DIR"
mkdir -p "$VERIFY_DIR/master"
mkdir -p "$VERIFY_DIR/consolidate-webui"

# Extract boards from platformio-*.ini files
echo "Extracting boards from platformio-*.ini files..."
declare -a BOARDS
declare -a PLATFORMS

for ini_file in build-platforms/platformio-*.ini; do
    board=$(basename "$ini_file" | sed 's/platformio-//; s/.ini//')
    platform=$(grep "^build_platform" "$ini_file" | sed 's/.*= //')
    BOARDS+=("$board")
    PLATFORMS+=("$platform")
done

total=${#BOARDS[@]}
echo "Found $total boards"
echo "Verification directory: $VERIFY_DIR"
echo ""

# Function to build all boards and save outputs
build_and_capture() {
    local branch_name="$1"
    local output_base="$VERIFY_DIR/$branch_name"

    echo "=== Building on $branch_name (started at $(date)) ==="

    for i in "${!BOARDS[@]}"; do
        board="${BOARDS[$i]}"
        platform="${PLATFORMS[$i]}"
        count=$((i + 1))

        export FUJINET_BUILD_BOARD="$board"
        export FUJINET_BUILD_PLATFORM="$platform"
        export BUILD_DATA_DIR="$output_base/$board"

        python3 build_webui.py >/dev/null 2>&1

        printf "  [%2d/%d] %-45s (%s) ✓\n" "$count" "$total" "$board" "$platform"
    done

    echo "Completed $branch_name at $(date)"
}

# Save current branch and stash any uncommitted changes
current_branch=$(git rev-parse --abbrev-ref HEAD)
echo "Current branch: $current_branch"

# Stash uncommitted changes
git stash push -u -m "verification stash" >/dev/null 2>&1 || true

echo ""

# Build on master
git checkout master >/dev/null 2>&1
build_and_capture "master"
echo ""

# Build on consolidate-webui
git checkout consolidate-webui >/dev/null 2>&1
build_and_capture "consolidate-webui"
echo ""

# Restore original branch and stashed changes
git checkout "$current_branch" >/dev/null 2>&1
git stash pop >/dev/null 2>&1 || true

# Analyze differences
echo "=== DIFF ANALYSIS ==="
echo "Comparing outputs..."
echo ""

diff_count=0
boards_with_diffs=()

for i in "${!BOARDS[@]}"; do
    board="${BOARDS[$i]}"
    master_dir="$VERIFY_DIR/master/$board"
    consolidated_dir="$VERIFY_DIR/consolidate-webui/$board"

    if [ ! -d "$master_dir" ] || [ ! -d "$consolidated_dir" ]; then
        echo "ERROR: Missing directory for $board"
        continue
    fi

    diff_output=$(diff -r "$master_dir" "$consolidated_dir" 2>/dev/null || true)
    if [ -n "$diff_output" ]; then
        boards_with_diffs+=("$board")
        ((diff_count++))
    fi
done

if [ $diff_count -eq 0 ]; then
    echo "✓ PASS: All $total boards produce identical webui output"
else
    echo "Found $diff_count boards with differences:"
    echo ""
    for board in "${boards_with_diffs[@]}"; do
        platform=$(grep -h "^build_platform" build-platforms/platformio-${board}.ini 2>/dev/null | sed 's/.*= //')
        echo "--- $board ($platform) ---"
        diff -r "$VERIFY_DIR/master/$board" "$VERIFY_DIR/consolidate-webui/$board" 2>/dev/null | head -30 || true
        echo ""
    done
fi

echo "=== SUMMARY ==="
echo "Total boards: $total"
echo "Boards with differences: $diff_count"
echo "Results directory: $VERIFY_DIR"
echo "Current branch: $(git rev-parse --abbrev-ref HEAD)"
