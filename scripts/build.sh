#!/usr/bin/env bash
# Usage: scripts/build.sh <stm32|esp32> [extra west args...]
# Wi-Fi credentials: put CONFIG_SIM_WIFI=y / SSID / PSK in secrets.conf (gitignored).
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
. "$here/scripts/env.sh"
target="${1:?stm32|esp32}"; shift || true
extra=()
[ -f "$here/secrets.conf" ] && extra+=(-DEXTRA_CONF_FILE="$here/secrets.conf")
# TFLite Micro module (STM32 AI sim), fetched by scripts/setup-tflm.sh
tflm="${TFLM_DIR:-$HOME/zephyrproject/modules/lib/tflite-micro}"
[ -d "$tflm" ] && extra+=(-DZEPHYR_EXTRA_MODULES="$tflm")
case "$target" in
  stm32) west build -b nucleo_u575zi_q -d "$here/build/stm32" "$here" "$@" -- -DSHIELD=x_nucleo_67w61m1 "${extra[@]}" ;;
  esp32) west build -b esp32_devkitc/esp32/procpu -d "$here/build/esp32" "$here" "$@" -- "${extra[@]}" ;;
  *) echo "unknown target $target"; exit 1 ;;
esac
