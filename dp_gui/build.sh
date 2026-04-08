#!/bin/bash
# Windows build script for DediProg GUI (MSYS2 UCRT64)
# Run from dp_gui directory: bash build.sh

set -e

SFDIR=".."

echo "Creating build directory..."
mkdir -p build

echo "Compiling library sources..."
cc -O2 -I"$SFDIR" $(pkg-config --cflags libusb-1.0) \
    -c "$SFDIR/FlashCommand.c" -o build/sf_FlashCommand.o

cc -O2 -I"$SFDIR" $(pkg-config --cflags libusb-1.0) \
    -c "$SFDIR/IntelHexFile.c" -o build/sf_IntelHexFile.o

cc -O2 -I"$SFDIR" $(pkg-config --cflags libusb-1.0) \
    -c "$SFDIR/MotorolaFile.c" -o build/sf_MotorolaFile.o

cc -O2 -I"$SFDIR" $(pkg-config --cflags libusb-1.0) \
    -c "$SFDIR/SerialFlash.c" -o build/sf_SerialFlash.o

cc -O2 -I"$SFDIR" $(pkg-config --cflags libusb-1.0) \
    -c "$SFDIR/board.c" -o build/sf_board.o

cc -O2 -I"$SFDIR" $(pkg-config --cflags libusb-1.0) \
    -c "$SFDIR/parse.c" -o build/sf_parse.o

cc -O2 -I"$SFDIR" $(pkg-config --cflags libusb-1.0) \
    -c "$SFDIR/project.c" -o build/sf_project.o

cc -O2 -I"$SFDIR" $(pkg-config --cflags libusb-1.0) \
    -c "$SFDIR/usbdriver.c" -o build/sf_usbdriver.o

cc -O2 -I"$SFDIR" $(pkg-config --cflags libusb-1.0) \
    -Dmain=dpcmd_main_unused \
    -c "$SFDIR/dpcmd.c" -o build/sf_dpcmd.o

echo "Compiling resource file..."
windres dpgui.rc -o build/dpgui_res.o

echo "Compiling GUI..."
g++ -O2 -I"$SFDIR" \
    $(pkg-config --cflags libusb-1.0) \
    $(fltk-config --cxxflags) \
    -c dpgui.cpp -o build/dpgui.o

echo "Linking..."
g++ build/sf_FlashCommand.o build/sf_IntelHexFile.o build/sf_MotorolaFile.o \
    build/sf_SerialFlash.o build/sf_board.o build/sf_parse.o \
    build/sf_project.o build/sf_usbdriver.o build/sf_dpcmd.o \
    build/dpgui.o build/dpgui_res.o \
    -o dpgui.exe \
    $(fltk-config --ldflags --use-images) \
    $(pkg-config --libs libusb-1.0) \
    -lpthread -mwindows

echo ""
echo "  Build successful: dpgui.exe"