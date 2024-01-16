#!/bin/bash -e

# This script will build the platform target file for all targets found with name platform-<TARGET>
# merging it with BASE.ini to create platformio-test.ini
# which the build.sh script will run a clean build against.

# search_file is the name of the completed firmware that proves we built that platform
# file_name is the resulting file that will hold the results of the build-all
SEARCH_FILE="firmware.bin"
FILE_NAME="build-results.txt"


# Clean up any old ini files as well.
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
if [ -f "$SCRIPT_DIR/test.ini" ] ; then
  rm $SCRIPT_DIR/test.ini
fi


echo ' ---------------------------------------- '
echo ' |                                      | '
echo ' | Starting build-all script.....       | '
echo " |  SCRIPT_DIR=$SCRIPT_DIR"
echo " |  output will be saved in $FILE_NAME"
echo ' |  in the root of this repo.'
echo ' |                                      | '
echo '  ---------------------------------------'
echo ''
echo ''

printf "Below this begins each platform build in sequence...."
printf "\n\n\n"

# We will create a record of what is built
# but first, clean out the old one....
if [ -f "$FILE_NAME" ]; then
    rm "$FILE_NAME"
    printf 'Found an old results file, deleted it.\n\n'
fi


# reads an ini [section] given the name from file, writes to output file.
function get_section {
    local target_name=$1
    local in_file=$2
    local out_file=$3
    awk -v TARGET=$target_name '
    {
        if ($0 ~ /^\[.*/) { 
          gsub(/^\[|\]$/, "", $0)
          SECTION=$0
        } else if (($0 != "") && (SECTION==TARGET)) { 
          print $0 
        }
    }' $in_file > $out_file
}


# Setup a results file so we can see how we did.
NOW=$(date +"%Y-%m-%d %H:%M:%S")
printf "Start Time: $NOW - ----- Starting Build the World for Fujinet\n\n\n" >> "$FILE_NAME"

# loop over every platformio-XXX.ini file, and use it to create a test platformio file

for piofile in $(ls -1 $SCRIPT_DIR/platformio-*.ini) ; do
    echo "Testing $(basename $piofile)"

    SECTION_FUJINET_FILE=$SCRIPT_DIR/section_fujinet.txt
    get_section fujinet $piofile $SECTION_FUJINET_FILE

    build_board=$(grep build_board $SECTION_FUJINET_FILE | cut -d= -f2 | tr -d ' ')

    SECTION_ENV_FILE=$SCRIPT_DIR/section_env.txt
    get_section "env:$build_board" $piofile $SECTION_ENV_FILE
    sed "1i[env:$build_board]" $SECTION_ENV_FILE > ${SECTION_ENV_FILE}.tmp
    mv ${SECTION_ENV_FILE}.tmp ${SECTION_ENV_FILE}

    # now change the BASE.ini to include these sections
    awk 'NR == FNR { a[FNR] = $0; n++; next} /##BUILD_PROPERTIES##/ { for(i=1; i<=n; i++){print a[i]}; next}1' $SECTION_FUJINET_FILE $SCRIPT_DIR/BASE.ini > $SCRIPT_DIR/part1.ini
    awk 'NR == FNR { a[FNR] = $0; n++; next} /##ENV_SECTION##/ { for(i=1; i<=n; i++){print a[i]}; next}1' $SECTION_ENV_FILE $SCRIPT_DIR/part1.ini > $SCRIPT_DIR/test.ini
    rm $SCRIPT_DIR/part1.ini

    pushd ${SCRIPT_DIR}/..

    # Breaking this up into 3 parts.
     # 1. - call build for the platform
     # 2. - echo a line in results file, find firmware.bin
     # 3. - now call build but just to clean

    ./build.sh -b -i $SCRIPT_DIR/test.ini

    # first determine if there is a firmware bin which means a good build
    NOW=$(date +"%Y-%m-%d %H:%M:%S")

    # Extract the substring after 'platformio-' and before '.ini'
    extracted_string=$(echo "$piofile" | sed -e 's|.*platformio-\(.*\)\.ini|\1|')
    printf "$NOW - Built $extracted_string\n" >> "$FILE_NAME"

    FOUND=$(find . -name "$SEARCH_FILE" -print -quit)
    if [ -n "$FOUND" ]; then
      printf "File '$SEARCH_FILE' found" >> "$FILE_NAME"
    else
      printf "File '$SEARCH_FILE' not found - this platfrom has issues \n" >> "$FILE_NAME"
    fi
    echo "------------------------------------------------" >> "$FILE_NAME"

    ./build.sh -c -i $SCRIPT_DIR/test.ini

    popd


done
