#!/bin/bash
# Get all the ducks in a row for FujiNet package release QUACK!

# Get arguments into named variables
PLATFORM=$1
VERSION=$2
REPO_OWNER=$3
VERSION_DATE=`grep "FN_VERSION_DATE" include/version.h | cut -d '"' -f 2`
BUILD_DATE=`date +'%Y-%m-%d %H:%M:%S'`
GIT_COMMIT=`git rev-parse HEAD`
GIT_SHORT_COMMIT=`git rev-parse --short HEAD`
FILENAME="fujinet-$PLATFORM-$VERSION"
WORKINGDIR=`pwd`
if [ "$PLATFORM" == "APPLE" ]; then
    BUILDPATH="$WORKINGDIR/.pio/build/fujiapple-rev0"
elif [ "$PLATFORM" == "ADAM" ]; then
    BUILDPATH="$WORKINGDIR/.pio/build/fujinet-adam-v1"
elif [ "$PLATFORM" == "ATARI" ]; then
    BUILDPATH="$WORKINGDIR/.pio/build/fujinet-atari-v1"
elif [ "$PLATFORM" == "IEC" ]; then
    BUILDPATH="$WORKINGDIR/.pio/build/fujinet-iec"
else
    BUILDPATH="$WORKINGDIR/.pio/build/fujinet-v1"
fi

if [ -f "annotation.txt" ]; then
	NOTE=`cat annotation.txt`
	GIT_LOG="\"${NOTE}\""
else
	# Split git log into array for JSON
	UNO=1
	while read -r line
	do
		if [ $UNO -eq 1 ]; then
			GIT_LOG="["$'\n'"          \"$line\""
		else
			GIT_LOG="          ${GIT_LOG},"$'\n'"          \"$line\""
		fi
		UNO=0
	done < change.log
	GIT_LOG="${GIT_LOG}"$'\n'"    ]"
fi

# Create release JSON
if [ "$PLATFORM" == "ATARI" ]; then
# For 16MB Flash
cat <<EOF > $BUILDPATH/release.json
{
	"version": "$VERSION",
	"version_date": "$VERSION_DATE",
	"build_date": "$BUILD_DATE",
	"description": $GIT_LOG,
	"git_commit": "$GIT_SHORT_COMMIT",
	"files":
	[
		{
			"filename": "bootloader.bin",
			"offset": "0x1000"
		},
		{
			"filename": "partitions.bin",
			"offset": "0x8000"
		},
		{
			"filename": "firmware.bin",
			"offset": "0x10000"
		},
		{
			"filename": "spiffs.bin",
			"offset": "0x910000"
		}
	]
}
EOF
else
# For 8MB Flash
cat <<EOF > $BUILDPATH/release.json
{
	"version": "$VERSION",
	"version_date": "$VERSION_DATE",
	"build_date": "$BUILD_DATE",
	"description": $GIT_LOG,
	"git_commit": "$GIT_SHORT_COMMIT",
	"files":
	[
		{
			"filename": "bootloader.bin",
			"offset": "0x1000"
		},
		{
			"filename": "partitions.bin",
			"offset": "0x8000"
		},
		{
			"filename": "firmware.bin",
			"offset": "0x10000"
		},
		{
			"filename": "spiffs.bin",
			"offset": "0x600000"
		}
	]
}
EOF
fi

# Create ZIP file
zip -qq -j "$FILENAME.zip" $BUILDPATH/bootloader.bin $BUILDPATH/firmware.bin $BUILDPATH/partitions.bin $BUILDPATH/spiffs.bin $BUILDPATH/release.json change.log

# Get shasum for ZIP file
ZIPSHASUM=`sha256sum $FILENAME.zip | cut -d ' ' -f 1`

# Create flasher release JSON
cat <<EOF > releases-$PLATFORM.json
{
    "version": "$VERSION",
    "version_date": "$VERSION_DATE",
    "build_date": "$BUILD_DATE",
    "description": $GIT_LOG,
    "git_commit": "$GIT_SHORT_COMMIT",
    "url": "https://github.com/$REPO_OWNER/fujinet-platformio/releases/download/$VERSION/$FILENAME.zip",
    "sha256": "$ZIPSHASUM"
}
EOF