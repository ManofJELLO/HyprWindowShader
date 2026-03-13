#!/bin/bash
# build.sh

# We keep the standard of commenting the workflow for future reference.

# 1. Compile the plugin using the Makefile
echo "[Build] Running make..."
make all

# 2. Halt the script if compilation fails
if [ $? -ne 0 ]; then
    echo "[Error] Compilation failed. Aborting load."
    exit 1
fi

echo "[Build] Compilation successful."

# 3. Define the absolute path to the compiled shared object
# This ensures hyprctl knows exactly where to find the newly minted .so file
PLUGIN_PATH="$(pwd)/HyprWindowShade.so"

# 4. Unload the plugin if it is already running (suppresses errors if it isn't loaded)
# Thanks to our explicit unhooking in PLUGIN_EXIT, this is completely safe and won't segfault Hyprland.
echo "[Plugin] Unloading previous version..."
hyprctl plugin unload "$PLUGIN_PATH" > /dev/null 2>&1

# 5. Load the fresh plugin into the compositor
echo "[Plugin] Loading new version..."
hyprctl plugin load "$PLUGIN_PATH"

echo "[Success] HyprWindowShade is now live!"#!/bin/bash

# --- CONFIGURATION ---
PLUGIN_NAME="HyprWindowShade"
INSTALL_DIR="$HOME/.local/share/hyprland/plugins"
SOURCE_FILE="main.cpp"
OUTPUT_LIB="${PLUGIN_NAME}.so"

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${GREEN}Starting build for ${PLUGIN_NAME}...${NC}"

# 1. Create install directory if it doesn't exist
mkdir -p "$INSTALL_DIR"

# 2. Run Make
if make; then
    echo -e "${GREEN}Build successful!${NC}"
else
    echo -e "${RED}Build failed. Please check the errors above.${NC}"
    exit 1
fi

# 3. Move the plugin to the install directory
echo "Moving ${OUTPUT_LIB} to ${INSTALL_DIR}..."
mv "$OUTPUT_LIB" "$INSTALL_DIR/"

# 4. Final instructions
echo -e "\n${GREEN}Complete!${NC}"
echo -e "To load the plugin, run:"
echo -e "  ${NC}hyprctl plugin load ${INSTALL_DIR}/${OUTPUT_LIB}"
echo -e "\nTo ensure it loads on startup, add this to your hyprland.conf:"
echo -e "  ${NC}exec-once = hyprctl plugin load ${INSTALL_DIR}/${OUTPUT_LIB}"
