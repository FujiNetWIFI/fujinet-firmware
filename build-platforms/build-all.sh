#!/bin/bash -e

LAST_FAILED=.last-failed

# This script will build the platform target file for all targets found with name platform-<TARGET>
# which the build.sh script will run a clean build against.

print_with_border() {
    local input="$1"
    local max_length=0

    while IFS= read -r line; do
        (( ${#line} > max_length )) && max_length=${#line}
    done <<< "$input"

    echo "+-$(
        for ((i=0; i<max_length; i++)); do
            printf "-"
        done
    )-+"

    while IFS= read -r line; do
        printf "| %-${max_length}s |\n" "$line"
    done <<< "$input"

    echo "+-$(
        for ((i=0; i<max_length; i++)); do
            printf "-"
        done
    )-+"
}

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

# prevent realpath from erroring on some systems if file doesn't exist
if [ ! -f "${RESULTS_OUTPUT_FILE}" ]; then
  touch "${RESULTS_OUTPUT_FILE}"
fi

OUTPUT_STRING="Starting build-all script.

Output will be saved in:
$(realpath $RESULTS_OUTPUT_FILE)"

print_with_border "$OUTPUT_STRING"
echo ""

if [ -f "$RESULTS_OUTPUT_FILE" ]; then
    rm "$RESULTS_OUTPUT_FILE"
fi

# Setup a results file so we can see how we did.
NOW=$(date +"%Y-%m-%d %H:%M:%S")
printf "Start Time: $NOW\nRunning Builds\n\n" >> "$RESULTS_OUTPUT_FILE"

# loop over every platformio-XXX.ini file, and use it to create a test platformio file
mapfile -t piofiles < <(find "$SCRIPT_DIR" -name 'platformio-*.ini' -print)
BOARDS=()
for piofile in "${piofiles[@]}"; do
    BASE_NAME=$(basename $piofile)
    BOARD_NAME=$(echo ${BASE_NAME//.ini} | cut -d\- -f2-)
    BOARDS+=("${BOARD_NAME}")
done

LAST_FAILED="${SCRIPT_DIR}/.last-failed"
if [[ -f "${LAST_FAILED}" ]]; then
    last_fail=$(awk '{print $1}' "${LAST_FAILED}")
    for idx in "${!BOARDS[@]}"; do
	if [[ "${BOARDS[$idx]}" == "$last_fail" ]]; then
	    RESUME_INDEX=$idx
	    break
	fi
    done

    piofiles=("${piofiles[@]:RESUME_INDEX}" "${piofiles[@]:1:RESUME_INDEX}")
fi

FAILED=""
TMPLOG=/tmp/results.log.$$
for piofile in "${piofiles[@]}"; do
    BASE_NAME=$(basename $piofile)
    BOARD_NAME=$(echo ${BASE_NAME//.ini} | cut -d\- -f2-)
    echo "Testing $(basename $piofile), please wait..."

    pushd ${SCRIPT_DIR}/.. > /dev/null

    # Breaking this up into 3 parts.
     # 1. - call build for the platform
     # 2. - echo a line in results file, find firmware.bin
     # 3. - now call build but just to clean

    if ! ./build.sh -y -s ${BOARD_NAME} -l $LOCAL_INI -i $SCRIPT_DIR/test.ini -b > ${TMPLOG} 2>&1 ; then
	echo ${BOARD_NAME} failed
	cat ${TMPLOG}
	echo ${BOARD_NAME} failed
	FAILED="${BOARD_NAME} ${FAILED}"
	break
    fi

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
    echo "" >> "$RESULTS_OUTPUT_FILE"

    # clean up after ourselves
    ./build.sh -c -l $LOCAL_INI -i $SCRIPT_DIR/test.ini > /dev/null 2>&1

    popd > /dev/null
done

rm -f ${TMPLOG}
OUTPUT_STRING="Results

$(cat $RESULTS_OUTPUT_FILE)"
print_with_border "$OUTPUT_STRING"

if [ -n "${FAILED}" ] ; then
    echo "${FAILED}" > "${LAST_FAILED}"
    exit 1
fi
