# DediProg SF100/SF600 GUI

A native cross-platform GUI for DediProg SF100/SF600 SPI NOR flash programmers,
built directly on top of the [SF100Linux V1.14.21.x](https://github.com/DediProgSW/SF100Linux) library.
Supports Linux and macOS with a clean, professional interface.

[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS-blue)](#platform-compatibility)
[![Release](https://img.shields.io/github/v/release/lagnajeet/SF100Linux)](https://github.com/lagnajeet/SF100Linux/releases)
[![License](https://img.shields.io/badge/license-LGPL--2.0-green)](LICENSE)

---

## Screenshots

### Light Mode (Ubuntu 22.04)
![lightMode-Ubuntu22](https://github.com/user-attachments/assets/85a5b8d6-9b73-4ec1-b472-cab4fff9007e)

### Dark Mode (Ubuntu 24.04)
![Darkmode-Ubuntu24](https://github.com/user-attachments/assets/8b716ea0-19d4-4ee7-915a-bcde8e47c730)

### macOS Ventura
![Darkmode-MacOS](https://github.com/user-attachments/assets/cbcdc68f-a1c9-4dd4-bf25-ef0622dc806d)


---

## Features

- **Chip Detection** — auto-detects from a database of 1900+ SPI NOR flash chips
- **Operations** — Program, Verify, Erase, Blank Check, Read
- **Real-time logging** — per-phase progress (Erasing → Programming → Verifying)
- **Progress bar** — real percentage during program/verify, marquee during erase
- **Dark / Light mode** — toggle in the header bar, persists across sessions
- **Native file picker** — Cocoa on macOS, GTK on Linux
- **Recent files** — dropdown history of previously loaded firmware files
- **File info panel** — name, size, modified date, CRC32, checksum
- **Programmer info** — type, firmware version, FPGA version, VCC, SPI clock
- **Memory info** — chip type, manufacturer, JEDEC ID, size, page and sector size
- **Single instance** — prevents multiple instances running simultaneously
- **Preferences** — window size, position and theme saved between sessions
- **dpcmd** — command line tool included for scripting and automation

---

## Download

Pre-built binaries are available on the [Releases](https://github.com/lagnajeet/SF100Linux/releases) page.

---

## Installation

### Linux

**Requirements:**
```bash
# Ubuntu / Debian
sudo apt-get install libusb-1.0-0

# Fedora / RHEL
sudo dnf install libusb1

# openSUSE
sudo zypper install libusb-1_0-0
```

**Install:**
```bash
tar -xzf dediprog-software-1.0-linux-x86_64.tar.gz
cd dediprog-software-1.0-linux-x86_64
./install.sh
```

The installer will:
- Copy `dpgui` and `dpcmd` to your chosen install directory (default `~/DediProg`)
- Install the udev rule for non-root USB access (`60-dediprog.rules`)
- Optionally add the install directory to your `PATH`
- Optionally create a desktop menu entry

After installing, unplug and replug the programmer for the udev rule to take effect.

**Manual run (without installing):**
```bash
cd dediprog-software-1.0-linux-x86_64
./dpgui
```

---

### macOS

**Requirements:** macOS 13.0 (Ventura) or newer.

1. Download `DediProg-Software-1.0-macOS.dmg`
2. Open the DMG and drag **DediProg Software** to your Applications folder
3. On first launch: right-click → **Open** (required for unsigned apps on macOS)

---

## Building from Source

### Linux

**Install dependencies:**
```bash
# Ubuntu / Debian
sudo apt-get install build-essential pkg-config libfltk1.3-dev libusb-1.0-0-dev

# Fedora
sudo dnf install gcc-c++ make fltk-devel libusb1-devel

# openSUSE
sudo zypper install gcc-c++ make fltk-devel libusb-1_0-devel
```

**Build:**
```bash
git clone https://github.com/lagnajeet/SF100Linux.git
cd SF100Linux
git checkout v1-14-21-dpgui
cd dp_gui
make
```

**Build portable static binary (no libfltk needed on target):**
```bash
# Ubuntu: install extra dev headers first
sudo apt-get install libxcursor-dev libxft-dev libxfixes-dev libxinerama-dev

make STATIC=1
```

**Build release package (.tar.gz with installer):**
```bash
make linux-release SFDIR=..
# Output: dediprog-software-1.0-linux-x86_64.tar.gz
```

---

### macOS

**Install dependencies:**
```bash
brew install fltk libusb dylibbundler
```

**Build:**
```bash
git clone https://github.com/lagnajeet/SF100Linux.git
cd SF100Linux
git checkout v1-14-21-dpgui
cd dp_gui
make
```

**Build app bundle:**
```bash
mkdir -p "DediProg Software.app/Contents/MacOS"
mkdir -p "DediProg Software.app/Contents/Resources"
cp dpgui "DediProg Software.app/Contents/MacOS/"
cp ../ChipInfoDb.dedicfg "DediProg Software.app/Contents/MacOS/"
cp your_icon.icns "DediProg Software.app/Contents/Resources/dpgui.icns"
# Create Info.plist (see dp_gui/Info.plist.example)

dylibbundler -od -b \
    -x "DediProg Software.app/Contents/MacOS/dpgui" \
    -d "DediProg Software.app/Contents/libs/" \
    -p @executable_path/../libs/
```

---

## Platform Compatibility

| Platform | Version | Status |
|----------|---------|--------|
| Ubuntu | 22.04 LTS | ✅ Tested |
| Ubuntu | 24.04 LTS | ✅ Tested |
| Fedora | 36+ | ✅ Tested |
| openSUSE | Leap / Tumbleweed | ✅ Tested |
| RHEL / Rocky Linux | 9+ | ✅ Tested |
| macOS | Ventura 13.x | ✅ Tested |
| macOS | Tahoe 26.x | ✅ Tested |

**Minimum requirements:**
- Linux: glibc 2.35+ (Ubuntu 22.04 or equivalent)
- macOS: 13.0 (Ventura)

---

## Programmer Support

| Programmer | Status |
|------------|--------|
| SF600Plus | ✅ |
| SF600Plus-G2 | ✅ |
| SF600 | ✅ |
| SF700 | ✅ |
| SF100 | ✅ |

---

## dpcmd — Command Line Tool

The `dpcmd` command line tool is included in the Linux release package for
scripting and automation:

```bash
# Auto-detect chip, program and verify
./dpcmd --auto firmware.bin --verify

# Erase chip
./dpcmd -e

# Read chip to file
./dpcmd -r output.bin

# Show help
./dpcmd --help
```

---

## Credits

- **SF100Linux** — [DediProg Software Co., Ltd.](https://github.com/DediProgSW/SF100Linux)
- **FLTK** — [Fast Light Toolkit](https://www.fltk.org/)
- **libusb** — [libusb project](https://libusb.info/)
- **GUI development** — Lagnajeet Pradhan

---

## License

This project is licensed under the LGPL-2.0 License — see the
[LICENSE](LICENSE) file for details.

SF100Linux library copyright © DediProg Software Co., Ltd.
