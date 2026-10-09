#!/usr/bin/env bash
# Usage: scripts/flash.sh <stm32|esp32>
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
. "$here/scripts/env.sh"
case "${1:?stm32|esp32}" in
  stm32) west flash -d "$here/build/stm32" --runner pyocd ;;
  esp32) west flash -d "$here/build/esp32" ;;
esac
