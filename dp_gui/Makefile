SFDIR   ?= ..
override SFDIR := $(abspath $(SFDIR))

CC       ?= cc
CXX      ?= g++
CFLAGS   := -O2 -Wall -I$(SFDIR) $(shell pkg-config --cflags libusb-1.0 libxml-2.0)
CXXFLAGS := $(CFLAGS) -std=c++11 $(shell fltk-config --cxxflags)
LDFLAGS  := $(shell fltk-config --ldflags) \
            $(shell pkg-config --libs libusb-1.0 libxml-2.0) \
            -lpthread

SF_NAMES := FlashCommand IntelHexFile MotorolaFile SerialFlash board parse project usbdriver dpcmd
SF_OBJS  := $(patsubst %, build/sf_%.o, $(SF_NAMES))

# parse.c has its own openChipInfoDb() — rename it so dpgui.cpp's
# extended version (which also searches PATH and system dirs) takes precedence.
SF_OBJS_FINAL := $(filter-out build/sf_parse.o, $(SF_OBJS)) build/sf_parse_wrapped.o

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

dpgui: $(SF_OBJS) build/sf_parse_wrapped.o build/dpgui.o
	$(CXX) $(filter-out build/sf_parse.o, $^) -o $@ $(LDFLAGS)
	@echo ""
	@echo "  Build successful: ./dpgui"
	@echo "  Don't forget the udev rule:"
	@echo "    sudo cp $(SFDIR)/60-dediprog.rules /etc/udev/rules.d/"
	@echo "    sudo udevadm control --reload"

clean:
	rm -rf build dpgui

.PHONY: all clean
