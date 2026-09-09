#!/bin/bash
set -e

REPO_ROOT="$(pwd)"
VERIFY_DIR="/tmp/webui-verify-$$"
mkdir -p "$VERIFY_DIR"

# Extract all unique boards from platformio-*.ini files
declare -A BOARDS
declare -A PLATFORMS

for ini_file in build-platforms/platformio-*.ini; do
    board=$(basename "$ini_file" | sed 's/platformio-//; s/.ini//')
    platform=$(grep "^build_platform" "$ini_file" | sed 's/.*= //')
    BOARDS[$board]="$platform"
done

echo "Found ${#BOARDS[@]} boards"
echo "Working directory: $VERIFY_DIR"
echo ""

# Function to build all boards and save outputs
build_and_capture() {
    local branch_name="$1"
    local output_dir="$VERIFY_DIR/$branch_name"
    mkdir -p "$output_dir"

    echo "=== Building on $branch_name ==="

    local count=0
    for board in "${!BOARDS[@]}"; do
        platform="${BOARDS[$board]}"
        ((count++))

        # Run build_webui.py via environment variables
        export FUJINET_BUILD_BOARD="$board"
        export FUJINET_BUILD_PLATFORM="$platform"
        export BUILD_DATA_DIR="$output_dir/$board"

        python3 build_webui.py >/dev/null 2>&1

        echo "  [$count/${#BOARDS[@]}] $board ($platform): ✓"
    done
}

# Save current branch
current_branch=$(git rev-parse --abbrev-ref HEAD)
echo "Current branch: $current_branch"
echo ""

# Build on master
git checkout master >/dev/null 2>&1
build_and_capture "master"

# Build on consolidate-webui
git checkout consolidate-webui >/dev/null 2>&1
build_and_capture "consolidate-webui"

# Now diff
echo ""
echo "=== DIFF ANALYSIS ==="

# Find all files that differ
diff_count=0
boards_with_diffs=()

for board in "${!BOARDS[@]}"; do
    if [ ! -d "$VERIFY_DIR/master/$board" ] || [ ! -d "$VERIFY_DIR/consolidate-webui/$board" ]; then
        continue
    fi

    diff_output=$(diff -r "$VERIFY_DIR/master/$board" "$VERIFY_DIR/consolidate-webui/$board" 2>/dev/null || true)
    if [ -n "$diff_output" ]; then
        boards_with_diffs+=("$board")
        ((diff_count++))
    fi
done

if [ $diff_count -eq 0 ]; then
    echo "✓ All ${#BOARDS[@]} boards produce identical webui output"
else
    echo "Found $diff_count boards with differences:"
    for board in "${boards_with_diffs[@]}"; do
        platform="${BOARDS[$board]}"
        echo ""
        echo "  $board ($platform):"
        diff -r "$VERIFY_DIR/master/$board" "$VERIFY_DIR/consolidate-webui/$board" 2>/dev/null | head -20 || true
    done
fi

echo ""
echo "Full results saved to: $VERIFY_DIR"
echo "Verification complete. Current branch: $(git rev-parse --abbrev-ref HEAD)"
