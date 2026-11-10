#!/usr/bin/env bash
# One-time: fetch the TensorFlow Lite Micro Zephyr module (not part of the
# default Zephyr workspace). scripts/build.sh picks it up automatically via
# ZEPHYR_EXTRA_MODULES. Override the location with TFLM_DIR.
#
# Zephyr 4.5-rc1's own CMake glue (zephyr/modules/tflite-micro) does not match
# any single branch of the zephyrproject-rtos/tflite-micro fork, so this script
# combines two of them (revisit when Zephyr 4.5 final is out):
#   * "main"          has the newer TFLM sources the glue lists, but no
#                     zephyr/module.yml and no third_party_static/
#   * "zephyr-v4.4.0" has zephyr/module.yml and third_party_static/ (flatbuffers,
#                     gemmlowp, ruy, kissfft headers) but older sources
# Result: main + module.yml (3 lines, written below) + third_party_static/.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
. "$here/scripts/env.sh"
dir="${TFLM_DIR:-$HOME/zephyrproject/modules/lib/tflite-micro}"
fork=https://github.com/zephyrproject-rtos/tflite-micro

if [ ! -d "$dir/.git" ]; then
  git clone --depth 1 --branch main "$fork" "$dir"
fi
if [ ! -f "$dir/zephyr/module.yml" ]; then
  mkdir -p "$dir/zephyr"
  cat > "$dir/zephyr/module.yml" <<'EOF'
name: tflite-micro
build:
  cmake-ext: True
  kconfig-ext: True
EOF
fi
if [ ! -d "$dir/third_party_static" ]; then
  tmp="$(mktemp -d)"
  git clone --depth 1 --branch zephyr-v4.4.0 "$fork" "$tmp/old"
  cp -r "$tmp/old/third_party_static" "$dir/third_party_static"
  rm -rf "$tmp"
fi
echo "ok: $dir"
