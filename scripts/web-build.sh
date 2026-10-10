#!/usr/bin/env bash
# Build the frontend into web/dist (embedded into the firmware at the next firmware build).
# Needs Node >= 18:  sudo apt install -y nodejs npm
set -euo pipefail
cd "$(dirname "$0")/../web"
command -v npm >/dev/null || { echo "npm not found: sudo apt install -y nodejs npm"; exit 1; }
[ -d node_modules ] || npm install
npm run build
echo "--- embedded size (gzip):"
for f in index.html app.js app.css; do printf '%-12s %7s bytes\n' "$f" "$(gzip -9c "dist/$f" | wc -c)"; done
