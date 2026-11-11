#!/bin/bash
# Universal (Apple Silicon + Intel) macOS build of dpgui.
# Run from dp_gui:  bash build_mac_universal.sh
#
# Homebrew libraries are single-architecture (Intel Homebrew = x86_64,
# Apple Silicon Homebrew = arm64), so this script builds FLTK and libusb
# from source as universal static libraries in dp_gui/deps-universal/.
# That happens once; later runs reuse them. The resulting dpgui links
# FLTK and libusb statically, so the app bundle needs no extra dylibs.
#
# Needs: Xcode command line tools, cmake (brew install cmake).
# Works on either an Intel or an Apple Silicon Mac.

set -e
cd "$(dirname "$0")"

SFDIR=..
DEPS="$PWD/deps-universal"
OUT="$PWD/build-universal"
ARCHS="-arch arm64 -arch x86_64"
MINVER=11.0
FLTK_VER=1.4.4
LIBUSB_VER=1.0.27
JOBS=$(sysctl -n hw.ncpu)

mkdir -p "$DEPS/src" "$OUT"

# ── libusb (static, universal) ──────────────────────────────────────────────
if [ ! -f "$DEPS/lib/libusb-1.0.a" ]; then
    echo "Building libusb $LIBUSB_VER (universal)..."
    (
        cd "$DEPS/src"
        curl -fL -o libusb.tar.bz2 \
            "https://github.com/libusb/libusb/releases/download/v$LIBUSB_VER/libusb-$LIBUSB_VER.tar.bz2"
        tar xjf libusb.tar.bz2
        cd "libusb-$LIBUSB_VER"
        ./configure --prefix="$DEPS" --enable-static --disable-shared \
            --disable-dependency-tracking \
            CFLAGS="$ARCHS -mmacosx-version-min=$MINVER -O2"
        make -j"$JOBS"
        make install
    )
fi

# ── FLTK (static, universal) ────────────────────────────────────────────────
if [ ! -f "$DEPS/lib/libfltk.a" ]; then
    echo "Building FLTK $FLTK_VER (universal)..."
    (
    cd "$DEPS/src"
    curl -fL -o fltk.tar.bz2 \
        "https://github.com/fltk/fltk/releases/download/release-$FLTK_VER/fltk-$FLTK_VER-source.tar.bz2"
    tar xjf fltk.tar.bz2
    cmake -S "fltk-$FLTK_VER" -B build-fltk \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=$MINVER \
        -DCMAKE_INSTALL_PREFIX="$DEPS" \
        -DFLTK_BUILD_SHARED_LIBS=OFF \
        -DFLTK_BUILD_TEST=OFF \
        -DFLTK_BUILD_EXAMPLES=OFF \
        -DFLTK_BUILD_FLUID=OFF \
        -DFLTK_BUILD_FLTK_OPTIONS=OFF \
        -DFLTK_USE_SYSTEM_LIBJPEG=OFF \
        -DFLTK_USE_SYSTEM_LIBPNG=OFF \
        -DFLTK_USE_SYSTEM_ZLIB=OFF
    cmake --build build-fltk -j"$JOBS"
    cmake --install build-fltk
    )
fi

FLTK_CONFIG="$DEPS/bin/fltk-config"
USB_CFLAGS="-I$DEPS/include/libusb-1.0"
USB_LIBS="$(PKG_CONFIG_PATH="$DEPS/lib/pkgconfig" pkg-config --static --libs libusb-1.0)"
CFLAGS="-O2 $ARCHS -mmacosx-version-min=$MINVER -I$SFDIR $USB_CFLAGS"

# ── Library sources ─────────────────────────────────────────────────────────
echo "Compiling library sources..."
for s in FlashCommand IntelHexFile MotorolaFile SerialFlash board parse project usbdriver; do
    cc $CFLAGS -c "$SFDIR/$s.c" -o "$OUT/sf_$s.o"
done
cc $CFLAGS -Dmain=dpcmd_main_unused -c "$SFDIR/dpcmd.c" -o "$OUT/sf_dpcmd.o"

# ── GUI ─────────────────────────────────────────────────────────────────────
echo "Compiling GUI..."
c++ $CFLAGS -std=c++11 $("$FLTK_CONFIG" --cxxflags) -c dpgui.cpp -o "$OUT/dpgui.o"

echo "Linking..."
c++ $ARCHS -mmacosx-version-min=$MINVER \
    "$OUT"/sf_*.o "$OUT/dpgui.o" \
    -o dpgui \
    $("$FLTK_CONFIG" --ldflags --use-images) \
    $USB_LIBS \
    -framework IOKit -framework CoreFoundation -lpthread

# ── dpcmd command-line tool (standalone, universal) ─────────────────────────
echo "Building dpcmd..."
cc $CFLAGS -c "$SFDIR/dpcmd.c" -o "$OUT/dpcmd_main.o"
cc $ARCHS -mmacosx-version-min=$MINVER \
    "$OUT"/sf_FlashCommand.o "$OUT"/sf_IntelHexFile.o "$OUT"/sf_MotorolaFile.o \
    "$OUT"/sf_SerialFlash.o "$OUT"/sf_board.o "$OUT"/sf_parse.o \
    "$OUT"/sf_project.o "$OUT"/sf_usbdriver.o "$OUT/dpcmd_main.o" \
    -o dpcmd \
    $USB_LIBS \
    -framework IOKit -framework CoreFoundation -lpthread

echo ""
for b in dpgui dpcmd; do
    lipo -info "$b"
    echo "Linked libraries for $b (should be system only, no fltk/libusb):"
    otool -L "$b" | tail -n +2
    echo ""
done
