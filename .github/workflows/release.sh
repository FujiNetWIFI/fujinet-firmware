#!/bin/bash
# Get all the ducks in a row for FujiNet package release

# Grab boot_app0.bin
wget https://github.com/espressif/arduino-esp32/raw/1.0.6/tools/partitions/boot_app0.bin -O .pio/build/fujinet-v1/boot_app0.bin

# Create sha256sums
sha256sum .pio/build/fujinet-v1/bootloader.bin > .pio/build/fujinet-v1/sha256sums.base
sha256sum .pio/build/fujinet-v1/firmware.bin >> .pio/build/fujinet-v1/sha256sums.base
sha256sum .pio/build/fujinet-v1/partitions.bin >> .pio/build/fujinet-v1/sha256sums.base
sha256sum .pio/build/fujinet-v1/spiffs.bin >> .pio/build/fujinet-v1/sha256sums.base
sha256sum .pio/build/fujinet-v1/boot_app0.bin >> .pio/build/fujinet-v1/sha256sums.base
sed 's#\.pio/build/fujinet-v1/##' .pio/build/fujinet-v1/sha256sums.base > .pio/build/fujinet-v1/sha256sums

# Get variables for JSON data
VERSION=`grep "FN_VERSION_FULL" include/version.h | cut -d '"' -f 2`
VERSION_DATE=`grep "FN_VERSION_DATE" include/version.h | cut -d '"' -f 2`
BUILD_DATE=`date +'%Y-%m-%d %H:%M:%S'`
GIT_COMMIT=`git rev-parse HEAD`
FILENAME="fujinet_$1_$VERSION"

# Create release.json
JSON="{
    \"version\": \"$VERSION\",
    \"version_date\": \"$VERSION_DATE\",
    \"build_date\": \"$BUILD_DATE\",
    \"description\": \"\",
    \"git_commit\": \"$GIT_COMMIT\",
    \"url\": \"https://github.com/FujiNetWIFI/fujinet-platformio/archive/refs/tags/$FILENAME.zip\",
    \"sha256\": \"\"
}"
echo $JSON > .pio/build/fujinet-v1/release.json