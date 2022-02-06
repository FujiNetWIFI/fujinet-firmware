#!/bin/bash
# Get all the ducks in a row for FujiNet package release

# Get arguments into named variables
PLATFORM=$1
VERSION=$2
FILENAME="fujinet-$PLATFORM-$VERSION"
VERSION_DATE=`grep "FN_VERSION_DATE" include/version.h | cut -d '"' -f 2`
BUILD_DATE=`date +'%Y-%m-%d %H:%M:%S'`
GIT_COMMIT=`git rev-parse HEAD`
GIT_SHORT_COMMIT=`git rev-parse --short HEAD`

# Grab boot_app0.bin
wget https://github.com/espressif/arduino-esp32/raw/1.0.6/tools/partitions/boot_app0.bin -O .pio/build/fujinet-v1/boot_app0.bin

# Create sha256sums
sha256sum .pio/build/fujinet-v1/bootloader.bin > .pio/build/fujinet-v1/sha256sums.base
sha256sum .pio/build/fujinet-v1/firmware.bin >> .pio/build/fujinet-v1/sha256sums.base
sha256sum .pio/build/fujinet-v1/partitions.bin >> .pio/build/fujinet-v1/sha256sums.base
sha256sum .pio/build/fujinet-v1/spiffs.bin >> .pio/build/fujinet-v1/sha256sums.base
sha256sum .pio/build/fujinet-v1/boot_app0.bin >> .pio/build/fujinet-v1/sha256sums.base
sed 's#\.pio/build/fujinet-v1/##' .pio/build/fujinet-v1/sha256sums.base > .pio/build/fujinet-v1/sha256sums

# Create ZIP file for assets
zip -qq -j "$FILENAME.zip" .pio/build/fujinet-v1/sha256sums .pio/build/fujinet-v1/bootloader.bin .pio/build/fujinet-v1/firmware.bin .pio/build/fujinet-v1/partitions.bin .pio/build/fujinet-v1/spiffs.bin .pio/build/fujinet-v1/boot_app0.bin

# Get shasum for ZIP file
ZIPSHASUM=`sha256sum $FILENAME.zip | cut -d ' ' -f 1`

# Create release.json
JSON="{
    \"version\": \"$VERSION\",
    \"version_date\": \"$VERSION_DATE\",
    \"build_date\": \"$BUILD_DATE\",
    \"description\": \"\",
    \"git_commit\": \"$GIT_COMMIT\",
    \"url\": \"https://github.com/FujiNetWIFI/fujinet-platformio/releases/download/$GIT_SHORT_COMMIT/$FILENAME.zip\",
    \"sha256\": \"$ZIPSHASUM\"
}"
echo $JSON > .pio/build/fujinet-v1/release.json
