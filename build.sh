#!/usr/bin/env bash

# an interface to running pio builds
# args can be combined, e.g. '-cbufm' and in any order.
# SEE build-sh.md FOR ADDITIONAL IMPORTANT INFORMATION ABOUT
# CONFIGURATION INI FILE USAGE

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
PIO_VENV_ROOT="${HOME}/.platformio/penv"

BUILD_ALL=0
RUN_BUILD=0
ENV_NAME=""
DO_CLEAN=0
SHOW_GRAPH=0
SHOW_MONITOR=0
SHOW_BOARDS=0
TARGET_NAME=""
PC_TARGET=""
DEBUG_PC_BUILD=0
UPLOAD_IMAGE=0
UPLOAD_FS=0
DEV_MODE=0
ZIP_MODE=0
AUTOCLEAN=1
SETUP_NEW_BOARD=""
ANSWER_YES=0
CMAKE_GENERATOR=""
INI_FILE="${SCRIPT_DIR}/platformio-generated.ini"
LOCAL_INI_VALUES_FILE="${SCRIPT_DIR}/platformio.local.ini"

# Function to check if the specified Python version is 3
check_python_version() {
  local python_bin=$1

  if ! command -v "${python_bin}" &> /dev/null; then
    return 1
  fi

  # Extract the major version number
  local major_version="$(${python_bin} --version 2>&1 | cut -d' ' -f2 | cut -d'.' -f1)"

  # Verify if it's Python 3
  if [ "${major_version}" -eq 3 ]; then
    return 0
  else
    return 1
  fi
}

# Check if "python" exists first since that's what PlatformIO names it
PYTHON=python
if ! check_python_version "${PYTHON}" ; then
  PYTHON=python3
  if ! check_python_version "${PYTHON}" ; then
    echo "Python 3 is not installed"
    exit 1
  fi
fi

function display_board_names {
  while IFS= read -r piofile; do
    BOARD_NAME=$(echo $(basename $piofile) | sed 's#^platformio-##;s#.ini$##')
    echo "$BOARD_NAME"
  done < <(find "$SCRIPT_DIR/build-platforms" -name 'platformio-*.ini' -print | sort)
}

function show_help {
  echo "Usage: $(basename $0) [options] -- [additional args]"
  echo ""
  echo "fujinet-firmware (pio) options:"
  echo "   -c       # run clean before build"
  echo "   -b       # run build"
  echo "   -u       # upload firmware"
  echo "   -f       # upload filesystem (webUI etc)"
  echo "   -m       # run monitor after build"
  echo "   -d       # add dev flag to build"
  echo "   -e ENV   # use specific environment"
  echo "   -t TGT   # run target task (default of none means do build, but -b must be specified"
  echo "   -n       # do not autoclean"
  echo ""
  echo "one-off firmware options"
  echo "   -a       # build ALL target platforms to test changes work on all platforms"
  echo "   -z       # build flashable zip"
  echo ""
  echo "fujinet-firmware board setup options:"
  echo "   -s NAME  # Setup a new board from name, writes a new file 'platformio.local.ini'"
  echo "   -i FILE  # use FILE as INI instead of platformio-generated.ini"
  echo "   -l FILE  # use FILE to use instead of 'platform.local.ini'"
  echo ""
  echo "fujinet-pc (cmake) options:"
  echo "   -c       # run clean before build"
  echo "   -p TGT   # perform PC build instead of ESP, for given target (e.g. APPLE|ATARI)"
  echo "   -g       # enable debug in generated fujinet-pc exe"
  echo "   -G GEN   # Use GEN as the Generator for cmake (e.g. -G \"Unix Makefiles\" )"  
  echo ""
  echo "other options:"  
  echo "   -y       # answers any questions with Y automatically, for unattended builds"
  echo "   -h       # this help"
  echo "   -V       # Override default Python virtual environment location (e.g. \"-V ~/.platformio/penv\")"
  echo ""
  echo "Additional Args can be accepted to pass values onto sub processes where supported."
  echo "  e.g. ./build.sh -p APPLE -- -DFOO=BAR"
  echo ""
  echo "Simple firmware builds:"
  echo "    ./build.sh -cb        # for CLEAN + BUILD of current target in platformio-local.ini"
  echo "    ./build.sh -m         # View FujiNet Monitor"
  echo "    ./build.sh -cbum      # Clean/Build/Upload to FN/Monitor"
  echo "    ./build.sh -f         # Upload filesystem"
  echo ""
  echo "Supported boards:"
  echo ""
  display_board_names
  exit 1
}

if [ $# -eq 0 ] ; then
  show_help
fi

while getopts "abcde:fgG:hi:l:mnp:s:St:uyzV:N" flag
do
  case "$flag" in
    a) BUILD_ALL=1 ;;
    b) RUN_BUILD=1 ;;
    c) DO_CLEAN=1 ;;
    d) DEV_MODE=1 ;;
    e) ENV_NAME=${OPTARG} ;;
    f) UPLOAD_FS=1 ;;
    g) DEBUG_PC_BUILD=1 ;;
    i) INI_FILE=${OPTARG} ;;
    l) LOCAL_INI_VALUES_FILE=${OPTARG} ;;
    m) SHOW_MONITOR=1 ;;
    n) AUTOCLEAN=0 ;;
    p) PC_TARGET=${OPTARG} ;;
    t) TARGET_NAME=${OPTARG} ;;
    s) SETUP_NEW_BOARD=${OPTARG} ;;
    S) SHOW_BOARDS=1 ;;
    u) UPLOAD_IMAGE=1 ;;
    G) CMAKE_GENERATOR=${OPTARG} ;;
    y) ANSWER_YES=1  ;;
    z) ZIP_MODE=1 ;;
    V) PIO_VENV_ROOT=${OPTARG} ;;
    h) show_help ;;
    *) show_help ;;
  esac
done
shift $((OPTIND - 1))

if [ $SHOW_BOARDS -eq 1 ] ; then
  display_board_names
  exit 1
fi

if [ $BUILD_ALL -eq 1 ] ; then
  # BUILD ALL platforms and exit
  chmod 755 $SCRIPT_DIR/build-platforms/build-all.sh
  $SCRIPT_DIR/build-platforms/build-all.sh
  exit $?
fi

# Set up the virtual environment if it exists
if [[ -d "$PIO_VENV_ROOT" && "$VIRTUAL_ENV" != "$PIO_VENV_ROOT" ]] ; then
  echo "Activating virtual environment"
  deactivate &>/dev/null
  source "$PIO_VENV_ROOT/bin/activate"
else
  echo "Error: Virtual environment not found in $PIO_VENV_ROOT."
  exit 1
fi

##############################################################
# PC BUILD using cmake
if [ ! -z "$PC_TARGET" ] ; then
  echo "PC Build Mode"
  # lets build_webui.py know we are using the generated INI file, this variable name is the one PIO uses when it calls subprocesses, so we use same name.
  export PROJECT_CONFIG=$INI_FILE
  GEN_CMD=""
  if [ -n "$CMAKE_GENERATOR" ] ; then
    GEN_CMD="-G $CMAKE_GENERATOR"
  fi

  mkdir -p "$SCRIPT_DIR/build"
  LAST_TARGET_FILE="$SCRIPT_DIR/build/last-target"
  LAST_TARGET=""
  if [ -f "${LAST_TARGET_FILE}" ]; then
    LAST_TARGET=$(cat ${LAST_TARGET_FILE})
  fi
  if [[ (-n ${LAST_TARGET}) && ("${LAST_TARGET}" != "$PC_TARGET") ]] ; then
    DO_CLEAN=1
  fi
  echo -n "$PC_TARGET" > ${LAST_TARGET_FILE}

  if [ $DO_CLEAN -eq 1 ] ; then
    echo "Removing old build artifacts"
    rm -rf $SCRIPT_DIR/build/*
    rm -f $SCRIPT_DIR/build/.ninja* 2>/dev/null
  fi

  cd $SCRIPT_DIR/build
  # Write out the compile commands for clangd etc to use
  if [ -z "$GEN_CMD" ]; then
    cmake .. -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DFUJINET_TARGET=$PC_TARGET "$@"
  else
    cmake "$GEN_CMD" .. -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DFUJINET_TARGET=$PC_TARGET "$@"
  fi
  if [ $? -ne 0 ]; then
    echo "cmake failed writing compile commands. Exiting"
    exit 1
  fi
  # Run the specific build
  BUILD_TYPE="Release"
  if [ $DEBUG_PC_BUILD -eq 1 ] ; then
    BUILD_TYPE="Debug"
  fi

  echo "Building for $BUILD_TYPE"
  if [ -z "$GEN_CMD" ]; then
    cmake .. -DFUJINET_TARGET=$PC_TARGET -DCMAKE_BUILD_TYPE=$BUILD_TYPE "$@"
  else
    cmake "$GEN_CMD" .. -DFUJINET_TARGET=$PC_TARGET -DCMAKE_BUILD_TYPE=$BUILD_TYPE "$@"
  fi
  if [ $? -ne 0 ] ; then
    echo "Error running initial cmake. Aborting"
    exit 1
  fi

  # python_modules.txt contains pairs of module name and installable package names, separated by pipe symbol
  MOD_LIST=$(sed '/^#/d' < "${SCRIPT_DIR}/python_modules.txt" | cut -d\| -f1 | tr '\n' ' ' | sed 's# *$##;s# \{1,\}# #g')
  echo "Checking python modules installed: $MOD_LIST"
  ${PYTHON} -c "import importlib.util, sys; sys.exit(0 if all(importlib.util.find_spec(mod.strip()) for mod in '''$MOD_LIST'''.split()) else 1)"
  if [ $? -eq 1 ] ; then
    echo "At least one of the required python modules is missing"
    bash ${SCRIPT_DIR}/install_python_modules.sh
  fi

  cmake --build .
  if [ $? -ne 0 ] ; then
    echo "Error running actual cmake build. Aborting"
    exit 1
  fi

  # write it into the dist dir
  cmake --build . --target dist
  if [ $? -ne 0 ] ; then
    echo "Error running cmake distribution. Aborting"
    exit 1
  fi

  echo "Built PC version in build/dist folder"
  exit 0
fi

if [ -z "$SETUP_NEW_BOARD" ] ; then
  # Did not specify -s flag, so do not overwrite local changes with new board
  # but do re-generate the INI file, this ensures upstream changes are pulled into
  # existing builds (e.g. upgrading platformio version)

  # Check the local ini file has been previously generated as we need to read which board the user is building
  if [ ! -f "$LOCAL_INI_VALUES_FILE" ] ; then
    echo "ERROR: local platformio ini file not found."
    echo "Please see documentation in build-sh.md, and re-run build as follows:"
    echo "   ./build.sh -s BUILD_BOARD"
    echo "BUILD_BOARD values include:"
    for f in $(ls -1 build-platforms/platformio-*.ini); do
      BASE_NAME=$(basename $f)
      BOARD_NAME=$(echo ${BASE_NAME//.ini} | cut -d\- -f2-)
      echo " - $BOARD_NAME"
    done
    echo "This is only required to be done once."
    exit 1
  fi

  if [ ${ZIP_MODE} -eq 1 ] ; then
    ${PYTHON} create-platformio-ini.py -o $INI_FILE -l $LOCAL_INI_VALUES_FILE -f platformio-ini-files/platformio.zip-options.ini
  else
    ${PYTHON} create-platformio-ini.py -o $INI_FILE -l $LOCAL_INI_VALUES_FILE
  fi
  create_result=$?
else
  # this will create a clean platformio INI file, but honours the command line args
  if [ -e ${LOCAL_INI_VALUES_FILE} -a $ANSWER_YES -eq 0 ] ; then
    echo "WARNING! This will potentially overwrite any local changes in $LOCAL_INI_VALUES_FILE"
    echo -n "Do you want to proceed? (y|N) "
    read answer
    answer=$(echo $answer | tr '[:upper:]' '[:lower:]')
    if [ "$answer" != "y" ]; then
      echo "Aborting"
      exit 1
    fi
  fi
  if [ ${ZIP_MODE} -eq 1 ] ; then
    ${PYTHON} create-platformio-ini.py -n $SETUP_NEW_BOARD -o $INI_FILE -l $LOCAL_INI_VALUES_FILE -f platformio-ini-files/platformio.zip-options.ini
  else
    ${PYTHON} create-platformio-ini.py -n $SETUP_NEW_BOARD -o $INI_FILE -l $LOCAL_INI_VALUES_FILE
  fi

  create_result=$?
fi
if [ $create_result -ne 0 ] ; then
  echo "Could not run build due to previous errors. Aborting"
  exit $create_result
fi

BUILD_BOARD=$(grep '^build_board = ' $INI_FILE | cut -d" " -f 3)

##############################################################
# ZIP MODE for building firmware zip file.
# This is Separate from the main build, and if chosen exits after running
if [ ${ZIP_MODE} -eq 1 ] ; then
  echo "=============================================================="
  echo "Running pio tasks: clean, buildfs for env $BUILD_BOARD"
  pio run -c $INI_FILE -t clean -t buildfs -e $BUILD_BOARD
  if [ $? -ne 0 ]; then
    echo "Error building filesystem."
    exit 1
  fi

  echo "=============================================================="
  echo "Running main pio build task"
  pio run -c $INI_FILE --disable-auto-clean -e $BUILD_BOARD
  exit $?
fi


##############################################################
# NORMAL BUILD MODES USING pio

ENV_ARG=""
if [ -n "${ENV_NAME}" ] ; then
  ENV_ARG="-e ${ENV_NAME}"
fi

TARGET_ARG=""
if [ -n "${TARGET_NAME}" ] ; then
  TARGET_ARG="-t ${TARGET_NAME}"
fi

DEV_MODE_ARG=""
if [ ${DEV_MODE} -eq 1 ] ; then
  DEV_MODE_ARG="-a dev"
fi

if [ ${DO_CLEAN} -eq 1 ] ; then
  pio run -c $INI_FILE -t clean ${ENV_ARG}
fi

AUTOCLEAN_ARG=""
if [ ${AUTOCLEAN} -eq 0 ] ; then
  AUTOCLEAN_ARG="--disable-auto-clean"
fi

# any stage that fails from this point should stop the script immediately, as they are designed to run
# on from each other sequentially as long as the previous passed.
set -e

if [ ${RUN_BUILD} -eq 1 ] ; then
  pio run -c $INI_FILE ${DEV_MODE_ARG} $ENV_ARG $TARGET_ARG $AUTOCLEAN_ARG 2>&1
fi

if [ ${UPLOAD_FS} -eq 1 ] ; then
  pio run -c $INI_FILE ${DEV_MODE_ARG} -t uploadfs 2>&1
fi

if [ ${UPLOAD_IMAGE} -eq 1 ] ; then
  pio run -c $INI_FILE ${DEV_MODE_ARG} -t upload 2>&1
fi

if [ ${SHOW_MONITOR} -eq 1 ] ; then
  # device monitor hard codes to using platformio.ini, let's grab all the data it would use directly from our generated ini.
  MONITOR_PORT=$(grep ^monitor_port $INI_FILE | cut -d= -f2 | cut -d\; -f1 | awk '{print $1}')
  MONITOR_SPEED=$(grep ^monitor_speed $INI_FILE | cut -d= -f2 | cut -d\; -f1 | awk '{print $1}')
  MONITOR_FILTERS=$(grep ^monitor_filters $INI_FILE | cut -d= -f2 | cut -d\; -f1 | tr ',' '\n' | sed 's/^ *//; s/ *$//' | awk '{printf("-f %s ", $1)}')  

  # warn the user if the build_board in platformio.ini (if exists) is not the same as INI_FILE version, as that means stacktrace will not work correctly
  # because the monitor does not allow an INI file to be set!!
  PIO_BOARD=$(grep "build_board *=" platformio.ini | awk '{print $3}')
  INI_BOARD=$(grep "build_board *=" ${INI_FILE} | awk '{print $3}')
  if [ "${PIO_BOARD}" != "${INI_BOARD}" ]; then
    echo "╔═════════════════════════════════════════╗"
    echo "║                WARNING                  ║"
    echo "╟─────────────────────────────────────────╢"
    echo "║ INCONSISTENT build_board VALUE DETECTED ║"
    echo "║   THIS MEANS STACKTRACE WILL NOT WORK   ║"
    echo "╚═════════════════════════════════════════╝"
    echo ""
    echo " platformio.ini = ${PIO_BOARD}"
    echo " $(basename ${INI_FILE}) = ${INI_BOARD}"
    echo ""
    echo " This is because 'pio device monitor' does not allow setting the INI file to use, but 'pio run' does."
    echo " You can fix this by copying the build_board to the old platformio.ini, or copy $(basename ${INI_FILE}) over platformio.ini entirely"
    echo ""
  fi

  pio device monitor -p $MONITOR_PORT -b $MONITOR_SPEED $MONITOR_FILTERS 2>&1
fi
