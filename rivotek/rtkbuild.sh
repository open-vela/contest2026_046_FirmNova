#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOP_DIR="${SCRIPT_DIR}"
BOARD_DIR="vendor/allwinnertech/boards/r528/r528s3-gemini-s1"
CONFIG_NAME="${2:-scooterdemo_exbt}"
CONFIG_PATH="${BOARD_DIR}/configs/${CONFIG_NAME}"
JOBS="${JOBS:-8}"
LUNCH_TARGET="${LUNCH_TARGET:-2}"
OUT_DIR="${TOP_DIR}/pack_out/${CONFIG_NAME}"
ACTION="${1:-build}"

usage()
{
  cat <<EOF
Usage: $(basename "$0") <clean|build|rebuild|pack|menuconfig> [config]

Defaults:
  config       ${CONFIG_NAME}
  jobs         ${JOBS}  (override with JOBS=16)
  lunch target ${LUNCH_TARGET}  (override with LUNCH_TARGET=<menu index/name>)
  output dir   ${OUT_DIR}

Examples:
  ./$(basename "$0") build scooterdemo_exbt
  ./$(basename "$0") rebuild scooterdemo
  ./$(basename "$0") menuconfig eca
  JOBS=16 ./$(basename "$0") build scooterdemo_exbt
EOF
}

list_actions()
{
  printf '%s\n' clean build rebuild pack menuconfig
}

list_configs()
{
  find "${TOP_DIR}/${BOARD_DIR}/configs" -maxdepth 1 -mindepth 1 -type d -printf '%f\n' | sort
}

require_config()
{
  if [ ! -d "${TOP_DIR}/${CONFIG_PATH}" ]; then
    echo "Error: config not found: ${CONFIG_PATH}" >&2
    echo "Available configs:" >&2
    list_configs | sed 's/^/  /' >&2
    exit 1
  fi
}

run_clean()
{
  require_config
  echo "==> clean: ${CONFIG_PATH}"
  (
    cd "${TOP_DIR}"
    ./build.sh "${CONFIG_PATH}/" distclean "-j${JOBS}"
  )
}

run_build()
{
  require_config
  echo "==> build: ${CONFIG_PATH}"
  (
    cd "${TOP_DIR}"
    ./build.sh "${CONFIG_PATH}/" "-j${JOBS}"
  )
}

run_pack()
{
  local lichee_dir="${TOP_DIR}/vendor/allwinnertech/lichee"
  local pack_status=0

  echo "==> pack: lunch_nuttx ${LUNCH_TARGET}"
  (
    cd "${lichee_dir}"
    set +u
    # shellcheck disable=SC1091
    source ./envsetup.sh
    lunch_nuttx "${LUNCH_TARGET}"
    pack
  ) || pack_status=$?

  if [ "${pack_status}" -ne 0 ] && ! find "${lichee_dir}/out" -type f -name '*.img' -print -quit | grep -q .; then
    echo "Error: pack failed with status ${pack_status}, and no .img files were generated" >&2
    exit "${pack_status}"
  fi

  if [ "${pack_status}" -ne 0 ]; then
    echo "Warning: pack returned status ${pack_status}, but .img files were generated; continue copying images" >&2
  fi

  echo "==> copy images: vendor/allwinnertech/lichee/out -> ${OUT_DIR}"
  rm -rf "${OUT_DIR}"
  mkdir -p "${OUT_DIR}"

  if ! find "${lichee_dir}/out" -type f -name '*.img' -exec cp -v {} "${OUT_DIR}/" \; | grep -q .; then
    echo "Warning: no .img files found under ${lichee_dir}/out" >&2
  fi
}

run_menuconfig()
{
  require_config
  echo "==> menuconfig: ${CONFIG_PATH}"
  (
    cd "${TOP_DIR}"
    ./build.sh "${CONFIG_PATH}/" menuconfig
  )
}

case "${ACTION}" in
  clean)
    run_clean
    ;;
  build)
    run_build
    run_pack
    ;;
  rebuild)
    run_clean
    run_build
    run_pack
    ;;
  pack)
    run_pack
    ;;
  menuconfig)
    run_menuconfig
    ;;
  __actions)
    list_actions
    ;;
  __configs)
    list_configs
    ;;
  completion)
    cat "${TOP_DIR}/rtkenv"
    ;;
  -h|--help|help)
    usage
    ;;
  *)
    usage >&2
    exit 1
    ;;
esac
