#!/bin/bash
# Get all the ducks in a row for FujiNet package release QUACK!

# Get arguments into named variables
PLATFORM=$1
VERSION=$2
VERSION_DATE=`grep "FN_VERSION_DATE" include/version.h | cut -d '"' -f 2`
BUILD_DATE=`date +'%Y-%m-%d %H:%M:%S'`
GIT_COMMIT=`git rev-parse HEAD`
GIT_SHORT_COMMIT=`git rev-parse --short HEAD`
GIT_LOG=`cat commit.log`
FILENAME="fujinet-$PLATFORM-$VERSION"
WORKINGDIR=`pwd`
if [ "$PLATFORM" == "APPLE" ]; then
    BUILDPATH="/home/runner/work/fujinet-platformio/fujinet-platformio/.pio/build/fujiapple-rev0"
elif [ "$PLATFORM" == "ADAM" ]; then
    BUILDPATH="/home/runner/work/fujinet-platformio/fujinet-platformio/.pio/build/fujinet-adam-v1"
else
    BUILDPATH="/home/runner/work/fujinet-platformio/fujinet-platformio/.pio/build/fujinet-v1"
fi

echo "Working Dir: $WORKINGDIR"

# Create release JSON
JSON="{
	\"version\": \"$VERSION\",
	\"version_date\": \"$VERSION_DATE\",
	\"build_date\": \"$BUILD_DATE\",
	\"description\": \"$GIT_LOG\",
	\"git_commit\": \"$GIT_SHORT_COMMIT\",
	\"files\":
	[
		{
			\"filename\": \"bootloader.bin\",
			\"offset\": \"0x1000\"
		},
		{
			\"filename\": \"partitions.bin\",
			\"offset\": \"0x8000\"
		},
		{
			\"filename\": \"firmware.bin\",
			\"offset\": \"0x10000\"
		},
		{
			\"filename\": \"spiffs.bin\",
			\"offset\": \"0x910000\"
		}
	]
}"
echo $JSON > $BUILDPATH/release.json

# Create ZIP file for assets
zip -qq -j "$FILENAME.zip" $BUILDPATH/bootloader.bin $BUILDPATH/firmware.bin $BUILDPATH/partitions.bin $BUILDPATH/spiffs.bin $BUILDPATH/release.json

# Get shasum for ZIP file
ZIPSHASUM=`sha256sum $FILENAME.zip | cut -d ' ' -f 1`

# Create flasher release JSON
JSON="{
    \"version\": \"$VERSION\",
    \"version_date\": \"$VERSION_DATE\",
    \"build_date\": \"$BUILD_DATE\",
    \"description\": \"$GIT_LOG\",
    \"git_commit\": \"$GIT_COMMIT\",
    \"url\": \"https://github.com/FujiNetWIFI/fujinet-platformio/releases/download/$VERSION/$FILENAME.zip\",
    \"sha256\": \"$ZIPSHASUM\"
}"
echo $JSON > releases.json
