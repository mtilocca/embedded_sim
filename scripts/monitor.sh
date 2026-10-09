#!/usr/bin/env bash
# Usage: scripts/monitor.sh <stm32|esp32>   (STM32 = ST-Link VCP, ESP32 = CP210x)
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
. "$here/scripts/env.sh"
case "${1:?stm32|esp32}" in
  stm32) dev=/dev/ttyACM0 ;;
  esp32) dev=/dev/ttyUSB0 ;;
esac
python -m serial.tools.miniterm "$dev" 115200
