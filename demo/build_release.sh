#!/bin/bash

# Usage: ./build_release.sh battlefield_demo/crg_battlefield.cpp

if [ -z "$1" ]; then
    echo "[ERROR] Please specify the source file path."
    exit 1
fi

SOURCE="$1"
# Get the directory where the source file is located
SOURCE_DIR=$(dirname "$SOURCE")
OUTNAME=$(basename "$SOURCE" .cpp)

echo "--- Release Compilation (Mac optimized) ---"

# 1. Include ImGui and rlImGui sources in the compilation.
# (Make sure the 'imgui' folder and 'rlImGui.cpp' file are located next to your crg_battlefield.cpp)
IMGUI_SOURCES="$SOURCE_DIR/imgui/*.cpp"
RLIMGUI_SOURCE="$SOURCE_DIR/rlImGui.cpp"

# 2. Add -lbox2d, -ltbb, and ImGui include directories
clang++ -O3 -DNDEBUG -std=c++20 \
  "$SOURCE" $RLIMGUI_SOURCE $IMGUI_SOURCES \
  -o "$SOURCE_DIR/$OUTNAME" \
  -I"$SOURCE_DIR" \
  -I"$SOURCE_DIR/imgui" \
  -I/opt/homebrew/include \
  -I/usr/local/include \
  -L/opt/homebrew/lib \
  -L/usr/local/lib \
  -lraylib \
  -lbox2d \
  -ltbb \
  -framework CoreVideo -framework IOKit -framework Cocoa -framework GLUT -framework OpenGL
  
if [ $? -eq 0 ]; then
    echo "[OK] Executable successfully generated at: $SOURCE_DIR/$OUTNAME"
else
    echo "[FAIL] Compilation error."
fi