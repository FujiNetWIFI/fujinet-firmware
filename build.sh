#!/bin/bash

# a simple interface to pio so I don't have to remember all the args it needs
# args can be combined, e.g. '-cbxufm' and in any order.
# switches with values must be specified separately.
# typical usage:
#   ./build.sh -h           # display help for this script!
#   ./build.sh -cb          # clean and build firmware
#   ./build.sh -m           # monitor device
#   ./build.sh -u           # upload image
#   ./build.sh -f           # upload file system

# This beast finds the directory the build.sh script is in, no matter where it's run from
# which should be the root of the project
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
INI_FILE="${SCRIPT_DIR}/platformio.ini"

OS_TYPE=$(uname)
if [ "$OS_TYPE" = "Linux" ]; then
    echo "Running on a Linux system. Wise choice."
elif [ "$OS_TYPE" = "Darwin" ]; then
    echo "Running on a Macintosh system."
    BINARY_PATH="/usr/local/opt/gnu-sed/libexec/gnubin/sed"
    if [ -f "$BINARY_PATH" ]; then
      echo "Proper sed Binary file found: $BINARY_PATH"
    else
      echo "gnu sed Binary file not found; please install via brew install gnu-sed and re-run"
      exit 1
    fi
else
    echo "Running on an unidentified system."
fi


# This beast finds the directory the build.sh script is in, no matter where it's run from
# which should be the root of the project
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

function show_help {
  echo "Usage: $(basename $0) [-a|-b|-c|-d|-e ENV|-f|-g|-i FILE|-m|-n|-t TARGET|-p TARGET|-u|-z|-h] -- [additional args]"
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
  echo "   -n       # do not autoclean"
  echo "   -t TGT   # run target task (default of none means do build, but -b must be specified"
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

while getopts "abcde:fghi:mnp:t:uz" flag
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
    m) SHOW_MONITOR=1 ;;
    n) AUTOCLEAN=0 ;;
    p) PC_TARGET=${OPTARG} ;;
    t) TARGET_NAME=${OPTARG} ;;
    u) UPLOAD_IMAGE=1 ;;
    z) ZIP_MODE=1 ;;
    h) show_help ;;
    *) show_help ;;
  esac
done
shift $((OPTIND - 1))

# remove any AUTOADD option left from previous run, and delete the generated backup file
sed -i.bu '/# AUTOADD/d' platformio.ini
rm 2>/dev/null platformio.ini.bu

##############################################################
# ZIP MODE for building firmware zip file.
# This is Separate from the main build, and if chosen exits after running
if [ ${ZIP_MODE} -eq 1 ] ; then
  # find line with post:build_firmwarezip.py and add before it the option uncommented
  sed -i.bu '/^;[ ]*post:build_firmwarezip.py/i\
    post:build_firmwarezip.py # AUTOADD
' platformio.ini
  pio run -t clean -t buildfs
  pio run --disable-auto-clean
  sed -i.bu '/# AUTOADD/d' platformio.ini
  rm 2>/dev/null platformio.ini.bu
  exit 0
fi

##############################################################
# PC BUILD using cmake
if [ ! -z "$PC_TARGET" ] ; then
  echo "PC Build Mode"
  mkdir -p "$SCRIPT_DIR/build"
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

##############################################################
# BUILD ALL platforms
if [ $BUILD_ALL -eq 1 ] ; then
  chmod 755 $SCRIPT_DIR/build-platforms/build-all.sh
  $SCRIPT_DIR/build-platforms/build-all.sh
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
