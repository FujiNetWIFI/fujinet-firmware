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
FILENAME="fujinet-$PLATFORM-$VERSION.zip"
WORKINGDIR=`pwd`

# Get tag annotation if available for description
if [ -f "firmware/annotation.txt" ]; then
	DESC=`cat firmware/annotation.txt`
else
    DESC=`cat firmware/release.json | grep "\"description\"" | cut -d "\"" -f 4`
fi

# Get shasum for ZIP file
ZIPSHASUM=`sha256sum firmware/$FILENAME | cut -d ' ' -f 1`

# Create flasher release JSON
cat <<EOF > firmware/releases-$PLATFORM.json
{
    "version": "$VERSION",
    "version_date": "$VERSION_DATE",
    "build_date": "$BUILD_DATE",
    "description": "$DESC",
    "git_commit": "$GIT_SHORT_COMMIT",
    "url": "https://github.com/$REPO_OWNER/fujinet-firmware/releases/download/$VERSION/$FILENAME",
    "sha256": "$ZIPSHASUM"
}
EOF