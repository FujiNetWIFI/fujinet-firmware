#!/usr/bin/env bash
# Keep PlatformIO's packages and platforms in the runner host's Docker volume, mounted at
# /ci-cache, so a job does not download them again. Does nothing if the volume is absent.
#
#   pio-cache.sh restore <target>   copy the target's cached packages into PlatformIO's core dir
#   pio-cache.sh save <target>      add any package the job downloaded to the cache
#
# Pull-request jobs mount the volume read-only, so save is a no-op there.
set -euo pipefail

MODE=${1:?usage: pio-cache.sh restore|save <target>}
TARGET=${2:?usage: pio-cache.sh restore|save <target>}
CACHE=/ci-cache
CORE=${PLATFORMIO_CORE_DIR:-$HOME/.platformio}
SUBDIRS=(packages platforms)

[ -d "$CACHE" ] || { echo "no host cache mounted"; exit 0; }

cd "$(dirname "$0")/../.."
# the files that pin the platform and package versions; a change starts a fresh cache
KEY=$(cat platformio-ini-files/platformio.common.ini build-platforms/*.ini | sha256sum | cut -c1-16)
DIR=$CACHE/platformio-$KEY/$TARGET

case $MODE in
restore)
    if [ ! -d "$DIR" ]; then
        echo "nothing cached for $TARGET yet"
        exit 0
    fi
    for sub in "${SUBDIRS[@]}"; do
        mkdir -p "$CORE/$sub"
        # the glob skips the .tmp-* directories a save in progress is still filling
        for pkg in "$DIR/$sub"/*; do
            [ -e "$pkg" ] && cp -a "$pkg" "$CORE/$sub/"
        done
    done
    echo "restored $TARGET from $DIR"
    ;;
save)
    if ! touch "$CACHE/.writable" 2>/dev/null; then
        echo "host cache is read-only"
        exit 0
    fi
    (
        flock 9
        for sub in "${SUBDIRS[@]}"; do
            mkdir -p "$DIR/$sub"
            rm -rf "$DIR/$sub"/.tmp-*
            for pkg in "$CORE/$sub"/*/; do
                [ -d "$pkg" ] || continue
                name=$(basename "$pkg")
                [ -e "$DIR/$sub/$name" ] && continue
                # copy beside the destination, then rename, so a cancelled job leaves no half package
                cp -a "$pkg" "$DIR/$sub/.tmp-$name"
                mv "$DIR/$sub/.tmp-$name" "$DIR/$sub/$name"
                echo "cached $sub/$name"
            done
        done
        touch "$(dirname "$DIR")"
        # keep the two newest pin sets
        ls -dt "$CACHE"/platformio-*/ | tail -n +3 | xargs -r rm -rf
    ) 9>"$CACHE/.lock"
    ;;
*)
    echo "usage: pio-cache.sh restore|save <target>" >&2
    exit 2
    ;;
esac
