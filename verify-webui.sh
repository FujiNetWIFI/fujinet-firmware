#!/bin/bash
set -e

REPO_ROOT="$(pwd)"
VERIFY_DIR="/tmp/webui-verify-$$"
mkdir -p "$VERIFY_DIR"

# Extract all unique boards from platformio-*.ini files
# Board name is in filename: platformio-BOARDNAME.ini
declare -A BOARDS
declare -A PLATFORMS

for ini_file in build-platforms/platformio-*.ini; do
    board=$(basename "$ini_file" | sed 's/platformio-//; s/.ini//')
    platform=$(grep "^build_platform" "$ini_file" | sed 's/.*= //')
    BOARDS[$board]="$platform"
done

echo "Found ${#BOARDS[@]} boards"
echo "Working directory: $VERIFY_DIR"

# Function to build all boards and save outputs
build_and_capture() {
    local branch_name="$1"
    local output_dir="$VERIFY_DIR/$branch_name"
    mkdir -p "$output_dir"

    echo ""
    echo "=== Building on $branch_name ==="

    for board in "${!BOARDS[@]}"; do
        platform="${BOARDS[$board]}"

        # Run build_webui.py
        build_output=$(python3 build_webui.py "$board" "$platform" 2>&1)

        # Capture generated files
        board_dir="$output_dir/$board"
        mkdir -p "$board_dir"

        if [ -d "build/data/www" ]; then
            cp -r build/data/www/* "$board_dir/" 2>/dev/null || true
        fi

        echo "  $board ($platform): generated"
    done
}

# Save current branch
current_branch=$(git rev-parse --abbrev-ref HEAD)
echo "Current branch: $current_branch"

# Build on master
git checkout master 2>/dev/null
build_and_capture "master"

# Build on consolidate-webui
git checkout consolidate-webui 2>/dev/null
build_and_capture "consolidate-webui"

# Now diff
echo ""
echo "=== DIFF RESULTS ==="

# Find all files that differ
diff_count=0
for board in "${!BOARDS[@]}"; do
    if [ ! -d "$VERIFY_DIR/master/$board" ] || [ ! -d "$VERIFY_DIR/consolidate-webui/$board" ]; then
        continue
    fi

    diff_output=$(diff -r "$VERIFY_DIR/master/$board" "$VERIFY_DIR/consolidate-webui/$board" 2>/dev/null || true)
    if [ -n "$diff_output" ]; then
        echo ""
        echo "--- $board DIFFERS ---"
        echo "$diff_output" | head -50
        ((diff_count++))
    fi
done

if [ $diff_count -eq 0 ]; then
    echo "All boards produce identical output ✓"
else
    echo ""
    echo "Found $diff_count boards with differences (see above)"
fi

echo ""
echo "Results saved to: $VERIFY_DIR"
