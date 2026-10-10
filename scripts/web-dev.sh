#!/usr/bin/env bash
# Live-reload UI development against a real board:  scripts/web-dev.sh [device-ip]
set -euo pipefail
cd "$(dirname "$0")/../web"
[ -d node_modules ] || npm install
DEVICE="${1:-192.168.1.45}" exec npm run dev -- --host
