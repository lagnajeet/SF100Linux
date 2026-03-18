SFDIR   ?= ..
override SFDIR := $(abspath $(SFDIR))

CC       ?= cc
CXX      ?= g++

# ── Detect OS ─────────────────────────────────────────────────────────────────
UNAME := $(shell uname)

# ── Compiler flags ────────────────────────────────────────────────────────────
CFLAGS   := -O2 -Wall -I$(SFDIR) $(shell pkg-config --cflags libusb-1.0)
CXXFLAGS := $(CFLAGS) -std=c++11 $(shell fltk-config --cxxflags)
LDFLAGS  := $(shell fltk-config --ldflags) \
            $(shell pkg-config --libs libusb-1.0) \
            -lpthread

# ── macOS extras ──────────────────────────────────────────────────────────────
ifeq ($(UNAME), Darwin)
    LDFLAGS    += -framework IOKit -framework CoreFoundation -framework Cocoa
    EXTRA_OBJS  = build/mac_filepicker.o
    SF_OBJS_LINK_EXTRA =
else
    EXTRA_OBJS  =
endif

SF_NAMES := FlashCommand IntelHexFile MotorolaFile SerialFlash board parse project usbdriver dpcmd
SF_OBJS  := $(patsubst %, build/sf_%.o, $(SF_NAMES))

# ── On Linux: rename parse.c's openChipInfoDb so dpgui.cpp's override takes
#    precedence. On macOS: parse.c has a native _NSGetExecutablePath version,
#    no rename needed.
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

# macOS only: compile Objective-C++ file for native file picker
build/mac_filepicker.o: mac_filepicker.mm | build
	$(CXX) $(CXXFLAGS) -fobjc-arc -c $< -o $@

# Linux only: rename symbol in parse.o
build/sf_parse_wrapped.o: build/sf_parse.o
	objcopy --redefine-sym openChipInfoDb=openChipInfoDb_orig $< $@

dpgui: $(SF_OBJS_LINK) build/dpgui.o $(EXTRA_OBJS)
	$(CXX) $^ -o $@ $(LDFLAGS)
	@echo ""
	@echo "  Build successful: ./dpgui"
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
