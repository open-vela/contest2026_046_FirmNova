#!/usr/bin/env bash

DEFAULT_CONFIG="boards/r528/r528s3-gemini-s1/configs/eca"

function usage()
{
  echo "Usage: $0 [-m] [config-path] [-e <extraflags>] [--cmake] [-b <cmake_binary_dir>] [make options]"
  echo ""
  echo "Where:"
  echo "  config-path: Path to board configuration directory (default: $DEFAULT_CONFIG)"
  echo "  -m: out of tree build"
  echo "  -e: pass extra c/c++ flags"
  echo "  --cmake: switch the build mode to CMake compilation"
  echo "  -b: set custom binary directory for CMake"
  echo "  --dis-ninja: disable CMake Ninja generator"
  echo "  -c: set custom toolchain"
  echo "  menuconfig/xconfig/gconfig/nconfig: Kconfig configuration interfaces"
  exit 1
}

function setup_kconfig_frontends()
{
  KCONFIG_ARGS="--enable-mconf --disable-nconf --disable-gconf --disable-qconf"
  if [ `uname` == "Darwin" ]; then
    KCONFIG_ARGS+=" --disable-shared --enable-static"
  fi

  if [ ! -f "${TOPDIR}/prebuilts/kconfig-frontends/bin/kconfig-conf" ] &&
     [ ! -x "$(command -v kconfig-conf)" ]; then
    pushd ${TOPDIR}/prebuilts/kconfig-frontends
    ./configure --prefix=${TOPDIR}/prebuilts/kconfig-frontends ${KCONFIG_ARGS} 1>/dev/null
    touch aclocal.m4 Makefile.in
    make install 1>/dev/null
    popd
  fi
  export PATH=${TOPDIR}/prebuilts/kconfig-frontends/bin:$PATH
}

ROOTDIR=$(dirname $(readlink -f ${0}))
TOPDIR=$(realpath ${ROOTDIR}/..)

TOOLSDIR=${TOPDIR}/nuttx/tools
NUTTXDIR=${TOPDIR}/nuttx

source ${TOPDIR}/build/envsetup.sh

if [ "$1" == "distclean" ]; then
  rm -f ${TOPDIR}/apps/vendor/contest2026_046_FirmNova
  rm -f ${TOPDIR}/apps/builtin_list.c ${TOPDIR}/apps/builtin.h
  rm -rf ${NUTTXDIR}/staging/libapps.a ${NUTTXDIR}/staging/apps
  make -C ${NUTTXDIR} distclean
  exit 0
fi

ln -sf ${ROOTDIR} ${TOPDIR}/apps/vendor/contest2026_046_FirmNova

CONFIG_CMD=""
BOARD_CONFIG=""

for cmd in "menuconfig" "xconfig" "gconfig" "nconfig"; do
  if [ "$1" == "$cmd" ]; then
    CONFIG_CMD="$cmd"
    BOARD_CONFIG="$DEFAULT_CONFIG"
    shift
    break
  fi
done

if [ -z "$CONFIG_CMD" ]; then
  BOARD_CONFIG=${1:-$DEFAULT_CONFIG}
  if [ "${BOARD_CONFIG:0:1}" != "-" ]; then
    shift
  fi

  for cmd in "menuconfig" "xconfig" "gconfig" "nconfig"; do
    if [ "$1" == "$cmd" ]; then
      CONFIG_CMD="$cmd"
      shift
      break
    fi
  done
fi

if [ ! -d "${ROOTDIR}/${BOARD_CONFIG}" ] && [ ! -f "${ROOTDIR}/${BOARD_CONFIG}/defconfig" ]; then
  echo "Error: Configuration directory not found: ${ROOTDIR}/${BOARD_CONFIG}"
  usage
fi

EXTRA_FLAGS="-Wno-cpp -Wno-deprecated-declarations"
while [[ "$1" == "-e" ]]; do
  shift
  EXTRA_FLAGS+=" $1"
  echo "extraflags: $EXTRA_FLAGS"
  shift
done

CMAKE_BUILD=""
CMAKE_BINARY_DIR="cmake_out"
CMAKE_GENERATOR="-GNinja"

if [ "$1" == "--cmake" ]; then
  CMAKE_BUILD="cmake"
  shift
fi

if [ "$1" == "-b" ]; then
  shift
  CMAKE_BINARY_DIR="$1"
  echo "custom CMake binary dir: $CMAKE_BINARY_DIR"
  shift
fi

if [ "$1" == "--dis-ninja" ]; then
  CMAKE_GENERATOR=""
  shift
fi

if [ "$1" == "-c" ]; then
  shift
  CUSTOM_COMPILER="$1"
  echo "custom toolchain: $CUSTOM_COMPILER"
  shift
fi

config_path="${ROOTDIR}/${BOARD_CONFIG}"

if [ -z "$CMAKE_BUILD" ]; then
  if [ -n "$CONFIG_CMD" ]; then
    setup_kconfig_frontends
    echo "Config command line:"
    echo "  ${TOOLSDIR}/configure.sh -e ${config_path}"
    echo "  make -C ${NUTTXDIR} ${CONFIG_CMD}"

    if ! ${TOOLSDIR}/configure.sh -e ${config_path}; then
      echo "Error: ############# config ${BOARD_CONFIG} fail ##############"
      exit 1
    fi

    if ! make -C ${NUTTXDIR} ${CONFIG_CMD}; then
      echo "Error: ############# ${CONFIG_CMD} ${BOARD_CONFIG} fail ##############"
      exit 2
    fi

    echo "  make -C ${NUTTXDIR} savedefconfig"
    if ! make -C ${NUTTXDIR} savedefconfig; then
      echo "Error: ############# save ${BOARD_CONFIG} fail ##############"
      exit 3
    fi

    if grep -q "#include" "${config_path}/defconfig"; then
        echo "Note: skipping defconfig copy for debug defconfig."
      else
        cp ${NUTTXDIR}/defconfig ${config_path}
      fi

      echo "${CONFIG_CMD} completed successfully!"
      exit 0
  fi

  echo "Build command line:"
  echo "  ${TOOLSDIR}/configure.sh -e ${config_path}"
  echo "  make -C ${NUTTXDIR} EXTRAFLAGS=\"${EXTRA_FLAGS}\" ${@}"

  if ! ${TOOLSDIR}/configure.sh -e ${config_path}; then
    echo "Error: ############# config ${BOARD_CONFIG} fail ##############"
    exit 1
  fi

  # Clean stale build artifacts to avoid undefined-symbol errors
  echo "Cleaning stale rivotek build artifacts..."
  find ${ROOTDIR}/rivotek \( -name "*.o" -o -name ".built" -o -name "*.a" -o -name ".depend" -o -name "Make.dep" \) -delete 2>/dev/null

  if ! make -C ${NUTTXDIR} EXTRAFLAGS="${EXTRA_FLAGS}" ${@}; then
    echo "Error: ############# build ${BOARD_CONFIG} fail ##############"
    exit 2
  fi

  if ! echo "${@}" | grep -q "distclean"; then
    echo "  make -C ${NUTTXDIR} savedefconfig"
    if ! make -C ${NUTTXDIR} savedefconfig; then
      echo "Error: ############# save ${BOARD_CONFIG} fail ##############"
      exit 3
    fi

    if grep -q "#include" "${config_path}/defconfig"; then
      echo "Note: skipping defconfig copy for debug defconfig."
    else
      cp ${NUTTXDIR}/defconfig ${config_path}
    fi
  fi
else
  echo "Build command line (CMake):"
  echo "  lunch ${config_path} ${CMAKE_BINARY_DIR}"
  echo "  m ${@}"

  lunch ${config_path} ${CMAKE_BINARY_DIR}
  if ! m ${@}; then
    echo "Error: ############# build ${BOARD_CONFIG} fail ##############"
    exit 2
  fi
fi

echo "Build completed successfully!"