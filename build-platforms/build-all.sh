#!/bin/bash -e

# This script will build the platform target file for all targets found with name platform-<TARGET>
# which the build.sh script will run a clean build against.

# FIRMWARE_OUTPUT_FILE is the name of the completed firmware that proves we built that platform
# RESULTS_OUTPUT_FILE is the resulting file that will hold the results of the build-all
FIRMWARE_OUTPUT_FILE="firmware.bin"
RESULTS_OUTPUT_FILE="build-results.txt"

# Clean up any old ini files as well.
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
if [ -f "$SCRIPT_DIR/test.ini" ] ; then
  rm $SCRIPT_DIR/test.ini
fi

LOCAL_INI="$SCRIPT_DIR/local.ini"

echo ' ---------------------------------------- '
echo ' |                                      | '
echo ' | Starting build-all script.....       | '
echo " |  SCRIPT_DIR=$SCRIPT_DIR"
echo " |  output will be saved in $RESULTS_OUTPUT_FILE"
echo ' |  in the root of this repo.'
echo ' |                                      | '
echo '  ---------------------------------------'
echo ''
echo ''

printf "Below this begins each platform build in sequence...."
printf "\n\n\n"

# We will create a record of what is built
# but first, clean out the old one....
if [ -f "$RESULTS_OUTPUT_FILE" ]; then
    rm "$RESULTS_OUTPUT_FILE"
    printf 'Found an old results file, deleted it.\n\n'
fi

# Setup a results file so we can see how we did.
NOW=$(date +"%Y-%m-%d %H:%M:%S")
printf "Start Time: $NOW - ----- Starting Build the World for Fujinet\n\n\n" >> "$RESULTS_OUTPUT_FILE"

# loop over every platformio-XXX.ini file, and use it to create a test platformio file

for piofile in $(ls -1 $SCRIPT_DIR/platformio-*.ini) ; do
    BASE_NAME=$(basename $piofile)
    BOARD_NAME=$(echo ${BASE_NAME//.ini} | cut -d\- -f2-)
    echo "Testing $(basename $piofile)"

    pushd ${SCRIPT_DIR}/..

    # Breaking this up into 3 parts.
     # 1. - call build for the platform
     # 2. - echo a line in results file, find firmware.bin
     # 3. - now call build but just to clean

    ./build.sh -y -s ${BOARD_NAME} -l $LOCAL_INI -i $SCRIPT_DIR/test.ini -b

    # first determine if there is a firmware bin which means a good build
    NOW=$(date +"%Y-%m-%d %H:%M:%S")

    # Extract the substring after 'platformio-' and before '.ini'
    printf "$NOW - Built $BOARD_NAME\n" >> "$RESULTS_OUTPUT_FILE"

    FOUND=$(find . -name "$FIRMWARE_OUTPUT_FILE" -print -quit)
    if [ -n "$FOUND" ]; then
      printf "File '$FIRMWARE_OUTPUT_FILE' found" >> "$RESULTS_OUTPUT_FILE"
    else
      printf "File '$FIRMWARE_OUTPUT_FILE' not found - this platfrom has issues \n" >> "$RESULTS_OUTPUT_FILE"
    fi
    echo "------------------------------------------------" >> "$RESULTS_OUTPUT_FILE"

    # clean up after ourselves
    ./build.sh -c -l $LOCAL_INI -i $SCRIPT_DIR/test.ini

    popd


done
