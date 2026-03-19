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
                -lpthread -ldl
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

all: dpgui dpcmd

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

# dpcmd standalone binary (compiled normally with its own main)
build/dpcmd_main.o: $(SFDIR)/dpcmd.c | build
	$(CC) $(CFLAGS) -c $< -o $@

# dpcmd uses original parse.o (with its own openChipInfoDb), not the wrapped version
DPCMD_OBJS := $(filter-out build/sf_dpcmd.o build/sf_parse_wrapped.o, $(SF_OBJS_LINK)) build/sf_parse.o build/dpcmd_main.o

dpcmd: $(DPCMD_OBJS)
ifeq ($(UNAME), Darwin)
	$(CC) $^ -o $@ $(shell pkg-config --libs libusb-1.0) -lpthread
else
ifdef STATIC
	$(CC) $^ -o $@ -Wl,-Bstatic $(FLTK_A) -Wl,-Bdynamic \
		$(shell pkg-config --libs libusb-1.0) -lpthread -lm -ldl
else
	$(CC) $^ -o $@ $(shell pkg-config --libs libusb-1.0) -lpthread
endif
endif

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

# ── Linux release package ────────────────────────────────────────────────────
# Builds a static binary and packages everything into a .tar.gz
# Usage: make linux-release SFDIR=/path/to/SF100Linux
VERSION ?= 1.0
RELEASE_NAME := dediprog-software-$(VERSION)-linux-x86_64

linux-release: SFDIR_ABS := $(abspath $(SFDIR))
linux-release:
	@echo "Building static release binary..."
	$(MAKE) clean
	$(MAKE) STATIC=1 SFDIR=$(SFDIR)
	$(MAKE) dpcmd SFDIR=$(SFDIR)
	@echo "Packaging release..."
	rm -rf $(RELEASE_NAME)
	mkdir -p $(RELEASE_NAME)
	cp dpgui                      $(RELEASE_NAME)/
	cp dpcmd                      $(RELEASE_NAME)/
	cp $(SFDIR)/ChipInfoDb.dedicfg $(RELEASE_NAME)/
	cp $(SFDIR)/60-dediprog.rules  $(RELEASE_NAME)/
	cp install.sh                  $(RELEASE_NAME)/
	chmod +x $(RELEASE_NAME)/install.sh
	@printf "DediProg Software v$(VERSION) - Linux x86_64\n" > $(RELEASE_NAME)/README.txt
	@printf "\nRequirements:\n" >> $(RELEASE_NAME)/README.txt
	@printf "  - libusb-1.0 (sudo apt install libusb-1.0-0)\n" >> $(RELEASE_NAME)/README.txt
	@printf "  - Ubuntu 22.04+ / Fedora 36+ / any distro with glibc 2.35+\n" >> $(RELEASE_NAME)/README.txt
	@printf "\nInstall:\n" >> $(RELEASE_NAME)/README.txt
	@printf "  chmod +x install.sh && ./install.sh\n" >> $(RELEASE_NAME)/README.txt
	@printf "\nManual run (without installing):\n" >> $(RELEASE_NAME)/README.txt
	@printf "  ./dpgui\n" >> $(RELEASE_NAME)/README.txt
	@printf "\ndpcmd command line tool:\n" >> $(RELEASE_NAME)/README.txt
	@printf "  ./dpcmd --help\n" >> $(RELEASE_NAME)/README.txt
	tar -czf $(RELEASE_NAME).tar.gz $(RELEASE_NAME)/
	rm -rf $(RELEASE_NAME)
	@echo ""
	@echo "  Release package: $(RELEASE_NAME).tar.gz"

.PHONY: all clean linux-release dpcmd
