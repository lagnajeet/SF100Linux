SFDIR   ?= ..
override SFDIR := $(abspath $(SFDIR))

CC       ?= cc
CXX      ?= g++

# ── Detect OS ─────────────────────────────────────────────────────────────────
UNAME := $(shell uname)

# ── Compiler flags ────────────────────────────────────────────────────────────
CFLAGS   := -O2 -Wall -I$(SFDIR) $(shell pkg-config --cflags libusb-1.0)
CXXFLAGS := $(CFLAGS) -std=c++11 $(shell fltk-config --cxxflags)

# ── Link flags ────────────────────────────────────────────────────────────────
# STATIC=1: statically link FLTK + X11, keep libusb/libc dynamic.
# On Ubuntu build machine first run:
#   sudo apt-get install libxcursor-dev libxft-dev libxfixes-dev libxinerama-dev
ifdef STATIC
    # Link only libfltk.a statically; let everything else remain dynamic.
    # This avoids the deep static dependency chain (freetype, xcb, expat etc.)
    FLTK_A   := $(shell fltk-config --ldstaticflags | awk '{print $$1}')
    LDFLAGS  := -Wl,-Bstatic $(FLTK_A) -Wl,-Bdynamic \
                -lXrender -lXcursor -lXfixes -lXext -lXft -lXinerama \
                -lfontconfig -lX11 \
                $(shell pkg-config --libs libusb-1.0) \
                -lpthread -lm -ldl
else
    LDFLAGS  := $(shell fltk-config --ldflags) \
                $(shell pkg-config --libs libusb-1.0) \
                -lpthread
endif

# ── macOS: always dynamic, add frameworks ─────────────────────────────────────
ifeq ($(UNAME), Darwin)
    LDFLAGS  := $(shell fltk-config --ldflags) \
                $(shell pkg-config --libs libusb-1.0) \
                -lpthread \
                -framework IOKit -framework CoreFoundation
endif

SF_NAMES := FlashCommand IntelHexFile MotorolaFile SerialFlash board parse project usbdriver dpcmd
SF_OBJS  := $(patsubst %, build/sf_%.o, $(SF_NAMES))

ifeq ($(UNAME), Darwin)
    SF_OBJS_LINK := $(SF_OBJS)
else
    SF_OBJS_LINK := $(filter-out build/sf_parse.o, $(SF_OBJS)) build/sf_parse_wrapped.o
endif

all: dpgui

build:
	mkdir -p build

build/sf_dpcmd.o: $(SFDIR)/dpcmd.c | build
	$(CC) $(CFLAGS) -Dmain=dpcmd_main_unused -c $< -o $@

build/sf_%.o: $(SFDIR)/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/dpgui.o: dpgui.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/sf_parse_wrapped.o: build/sf_parse.o
	objcopy --redefine-sym openChipInfoDb=openChipInfoDb_orig $< $@

dpgui: $(SF_OBJS_LINK) build/dpgui.o
	$(CXX) $^ -o $@ $(LDFLAGS)
	@echo ""
ifdef STATIC
	@echo "  Build successful (static FLTK): ./dpgui"
	@echo "  No libfltk needed on target — only libusb-1.0 required"
else
	@echo "  Build successful: ./dpgui"
	@echo "  Tip: 'make STATIC=1' for a portable binary (no libfltk on target)"
endif
ifeq ($(UNAME), Darwin)
	@echo "  macOS: make sure ChipInfoDb.dedicfg is next to the binary"
else
	@echo "  Don't forget the udev rule:"
	@echo "    sudo cp $(SFDIR)/60-dediprog.rules /etc/udev/rules.d/"
	@echo "    sudo udevadm control --reload"
endif

clean:
	rm -rf build dpgui

.PHONY: all clean
