#!/usr/bin/env bash
# Copy built web assets to firmware LittleFS storage partition
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WWW_DIR="$SCRIPT_DIR/.."
DIST_DIR="$WWW_DIR/dist"
SPIFFS_DIR="$WWW_DIR/../firmware/spiffs"

if [ ! -d "$DIST_DIR" ]; then
  echo "Error: dist/ not found. Run 'npm run build' first."
  exit 1
fi

# Clean previous deploy (preserve .gitkeep)
find "$SPIFFS_DIR" -mindepth 1 ! -name '.gitkeep' ! -name '.gitignore' -delete 2>/dev/null || true

# Copy built files
cp -r "$DIST_DIR"/* "$SPIFFS_DIR/"

# Pre-gzip assets for ESP32 serving
for f in "$SPIFFS_DIR"/*.html "$SPIFFS_DIR"/assets/*.js "$SPIFFS_DIR"/assets/*.css; do
  [ -f "$f" ] && gzip -k "$f"
done

echo "Deployed to $SPIFFS_DIR"
ls -la "$SPIFFS_DIR"/
[ -d "$SPIFFS_DIR/assets" ] && ls -la "$SPIFFS_DIR/assets/"
