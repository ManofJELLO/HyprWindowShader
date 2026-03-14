#!/bin/bash
# build.sh

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

PLUGIN_DIR="$HOME/.local/share/hyprland/plugins"
PLUGIN_PATH="$PLUGIN_DIR/HyprWindowShade.so"

echo -e "${GREEN}Starting build for HyprWindowShade...${NC}"

echo "[Plugin] Unloading previous version from memory..."
# --- THE FIX: USE ABSOLUTE PATH ---
# Hyprland strictly requires the full path to locate the plugin in memory
hyprctl plugin unload "$PLUGIN_PATH"
sleep 2

echo "[Build] Running make..."
make all

if [ $? -ne 0 ]; then
    echo -e "${RED}[Error] Compilation failed. Aborting load.${NC}"
    exit 1
fi

mkdir -p "$PLUGIN_DIR"
echo -n "Moving HyprWindowShade.so to $PLUGIN_DIR..."
rm -f "$PLUGIN_PATH" 
mv "$(pwd)/HyprWindowShade.so" "$PLUGIN_PATH"
echo -e "${GREEN}Complete!${NC}"

echo "[Plugin] Loading new version..."
hyprctl plugin load "$PLUGIN_PATH"

echo -e "${GREEN}[Success] HyprWindowShade is now live!${NC}"