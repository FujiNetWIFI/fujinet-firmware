#!/usr/bin/env bash

# an interface to running pio builds
# args can be combined, e.g. '-cbufm' and in any order.
# switches with values must be specified separately.
# typical usage:
#   ./build.sh -h           # display help for this script!
#   ./build.sh -cb          # clean and build firmware
#   ./build.sh -m           # monitor device
#   ./build.sh -u           # upload firmware image
#   ./build.sh -f           # upload file system

# SEE build-sh.md FOR ADDITIONAL IMPORTANT INFORMATION ABOUT
# CONFIGURATION INI FILE USAGE

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

BUILD_ALL=0
RUN_BUILD=0
ENV_NAME=""
DO_CLEAN=0
SHOW_GRAPH=0
SHOW_MONITOR=0
TARGET_NAME=""
PC_TARGET=""
DEBUG_PC_BUILD=1
UPLOAD_IMAGE=0
UPLOAD_FS=0
DEV_MODE=0
ZIP_MODE=0
AUTOCLEAN=1
SETUP_NEW_BOARD=""
ANSWER_YES=0
INI_FILE="${SCRIPT_DIR}/platformio-generated.ini"
LOCAL_INI_VALUES_FILE="${SCRIPT_DIR}/platformio.local.ini"

function show_help {
  echo "Usage: $(basename $0) [-a|-b|-c|-d|-e ENV|-f|-g|-i FILE|-m|-n|-t TARGET|-p TARGET|-s BOARD_NAME|-u|-y|-z|-h] -- [additional args]"
  echo " Most common options:"
  echo "   -c       # run clean before build (applies to PC build too)"
  echo "   -b       # run build"
  echo "   -m       # run monitor after build"
  echo "   -u       # upload image (device code)"
  echo "   -f       # upload filesystem (webUI etc)"
  echo "   -p TGT   # perform PC build instead of ESP, for given target (e.g. APPLE|ATARI)"
  echo ""
  echo "Advanced options:"
  echo "   -a       # build ALL target platforms and exit. useful to test code against everything"
  echo "   -d       # add dev flag to build"
  echo "   -e ENV   # use specific environment"
  echo "   -g       # do NOT use debug for PC build, i.e. default is debug build"
  echo "   -i FILE  # use FILE as INI instead of platformio.ini -  used during build-all"
  echo "   -l FILE  # use FILE for local ini values when creating generated INI file"
  echo "   -n       # do not autoclean"
  echo "   -s NAME  # Setup a new board from name, writes a new file 'platformio.local.ini'"
  echo "   -t TGT   # run target task (default of none means do build, but -b must be specified"
  echo "   -y       # answers any questions with Y automatically, for unattended builds"
  echo "   -z       # build flashable zip"
  echo "   -h       # this help"
  echo ""
  echo "Additional Args can be accepted to pass values onto sub processes where supported."
  echo "  e.g. ./build.sh -p APPLE -- -DSLIP_PROTOCOL=COM"
  echo ""
  echo "Simple builds:"
  echo "    ./build.sh -cb        # for CLEAN + BUILD of current target in platformio.ini"
  echo "    ./build.sh -m         # View FujiNet Monitor"
  echo "    ./build.sh -cbum      # Clean/Build/Upload to FN/Monitor"
  exit 1
}

if [ $# -eq 0 ] ; then
  show_help
fi

while getopts "abcde:fghi:l:mnp:s:t:uyz" flag
do
  case "$flag" in
    a) BUILD_ALL=1 ;;
    b) RUN_BUILD=1 ;;
    c) DO_CLEAN=1 ;;
    d) DEV_MODE=1 ;;
    e) ENV_NAME=${OPTARG} ;;
    f) UPLOAD_FS=1 ;;
    g) DEBUG_PC_BUILD=0 ;;
    i) INI_FILE=${OPTARG} ;;
    l) LOCAL_INI_VALUES_FILE=${OPTARG} ;;
    m) SHOW_MONITOR=1 ;;
    n) AUTOCLEAN=0 ;;
    p) PC_TARGET=${OPTARG} ;;
    t) TARGET_NAME=${OPTARG} ;;
    s) SETUP_NEW_BOARD=${OPTARG} ;;
    u) UPLOAD_IMAGE=1 ;;
    y) ANSWER_YES=1  ;;
    z) ZIP_MODE=1 ;;
    h) show_help ;;
    *) show_help ;;
  esac
done
shift $((OPTIND - 1))

if [ $BUILD_ALL -eq 1 ] ; then
  # BUILD ALL platforms and exit
  chmod 755 $SCRIPT_DIR/build-platforms/build-all.sh
  $SCRIPT_DIR/build-platforms/build-all.sh
  exit $?
fi

##############################################################
# PC BUILD using cmake
if [ ! -z "$PC_TARGET" ] ; then
  echo "PC Build Mode"
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
    rm $SCRIPT_DIR/build/.ninja* 2>/dev/null
  fi
  cd $SCRIPT_DIR/build
  if [ $DEBUG_PC_BUILD -eq 1 ] ; then
    cmake .. -DFUJINET_TARGET=$PC_TARGET -DCMAKE_BUILD_TYPE=Debug "$@"
  else
    cmake .. -DFUJINET_TARGET=$PC_TARGET -DCMAKE_BUILD_TYPE=Release "$@"
  fi
  if [ $? -ne 0 ] ; then
    echo "Error running initial cmake. Aborting"
    exit 1
  fi
  # check if all the required python modules are installed
  python -c "import importlib.util, sys; sys.exit(0 if all(importlib.util.find_spec(mod.strip()) for mod in open('${SCRIPT_DIR}/python_modules.txt')) else 1)"
  if [ $? -eq 1 ] ; then
    echo "At least one of the required python modules is missing"
    sh ${SCRIPT_DIR}/install_python_modules.sh
  fi

  cmake --build .
  if [ $? -ne 0 ] ; then
    echo "Error running actual cmake build. Aborting"
    exit 1
  fi
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

  python create-platformio-ini.py -o $INI_FILE -l $LOCAL_INI_VALUES_FILE
  create_result=$?
else
  # this will create a clean platformio INI file, but honours the command line args
  if [ $ANSWER_YES -eq 0 ] ; then
    echo "WARNING! This will potentially overwrite any local changes in $LOCAL_INI_VALUES_FILE"
    echo -n "Do you want to proceed? (y|N) "
    read answer
    answer=$(echo $answer | tr '[:upper:]' '[:lower:]')
    if [ "$answer" != "y" ]; then
      echo "Aborting"
      exit 1
    fi
  fi
  python create-platformio-ini.py -n $SETUP_NEW_BOARD -o $INI_FILE -l $LOCAL_INI_VALUES_FILE
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
  pio device monitor 2>&1
fi
