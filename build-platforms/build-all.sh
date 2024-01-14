#!/bin/bash -e

# This script will build the platform target file for all targets found with name platform-<TARGET>
# merging it with BASE.ini to create platformio-test.ini
# which the build.sh script will run a clean build against.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
if [ -f "$SCRIPT_DIR/test.ini" ] ; then
  rm $SCRIPT_DIR/test.ini
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
    ./build.sh -cb -i $SCRIPT_DIR/test.ini
    popd

done

