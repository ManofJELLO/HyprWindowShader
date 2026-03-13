#!/bin/bash

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
