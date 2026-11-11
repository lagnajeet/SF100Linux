# DediProg SF100/SF600 GUI

A native cross-platform GUI for DediProg SF100/SF600 SPI NOR flash programmers,
built directly on top of the [SF100Linux V1.14.21.x](https://github.com/DediProgSW/SF100Linux) library.
Supports Linux, macOS (Intel and Apple Silicon) and Windows with a clean, professional interface.

[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20Windows-blue)](#platform-compatibility)
[![Release](https://img.shields.io/github/v/release/lagnajeet/SF100Linux)](https://github.com/lagnajeet/SF100Linux/releases)
[![License](https://img.shields.io/badge/license-LGPL--2.0-green)](LICENSE)

---

## Screenshots
<h3>macOS Tahoe 26 on Apple Silicon (dark mode)</h3>
<img width="1200" height="723" alt="Screenshot_Tahoe_M5" src="https://github.com/user-attachments/assets/473515b5-5020-4ae9-9b18-f15b66a89144" />


<details>
<summary><b>More screenshots</b> — Windows 11, Ubuntu (light mode), macOS on Intel</summary>

### Windows 11
<img width="1200" height="723" alt="Screenshot_Win11" src="https://github.com/user-attachments/assets/3baad0d2-23f5-4d59-b14a-281115d3a4a1" />

### Ubuntu (light mode)
<img width="1200" height="697" alt="Screenshot_Ubuntu_Light" src="https://github.com/user-attachments/assets/0d42f85c-e71e-4a68-aed3-a09c2d77f686" />

### macOS on Intel
<img width="1200" height="773" alt="Screenshot_Mac_intel" src="https://github.com/user-attachments/assets/15f41f41-3972-4c68-81bd-c684158d5f58" />

</details>


---

## Features

- **Chip Detection** — auto-detects from a database of 1700+ SPI NOR flash chips
- **Operations** — Program, Verify, Erase, Blank Check, Read
- **Real-time logging** — per-phase progress (Erasing → Programming → Verifying) with the
  time each phase took, plus a total when several phases run
- **Progress bar** — real percentage during program/verify, marquee during erase
- **Drag and drop** — drop a firmware file onto the log to load it
- **File size check** — warns before Program/Verify when a raw binary is larger or smaller
  than the chip
- **Trim blank on read** — optionally strips trailing erased (0xFF) bytes from the read image
- **Safe cancel** — Read/Program/Verify stop cleanly with a "Stopping…" indicator; a failed
  or cancelled Read removes its incomplete file
- **USB reconnect** — unplugging and replugging the programmer is detected automatically
- **Save Log** — save the log to a text file, headed with the app version and system info
- **Dark / Light mode** — toggle in the header bar, persists across sessions
- **Native file picker** — Cocoa on macOS, GTK on Linux, standard dialogs on Windows;
  save dialogs suggest a file name and extension (Read → `<chip>.img`)
- **Recent files** — dropdown history of previously loaded firmware files
- **File info panel** — name, size (readable and exact bytes), modified date, CRC32,
  checksum, MD5; click any value to copy it
- **System info panel** — OS, kernel, CPU, architecture, RAM, libusb version
- **Programmer info** — type, firmware version, FPGA version, VCC, SPI clock
- **Memory info** — chip type, manufacturer, JEDEC ID, size, page and sector size
- **Single instance** — prevents multiple instances running simultaneously
- **Preferences** — window size, position and theme saved between sessions
- **Universal macOS app** — runs natively on both Intel and Apple Silicon Macs
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
tar -xzf dediprog-software-1.1.0-linux-x86_64.tar.gz
cd dediprog-software-1.1.0-linux-x86_64
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
cd dediprog-software-1.1.0-linux-x86_64
./dpgui
```

---

### macOS

**Requirements:** macOS 13.0 (Ventura) or newer, Intel or Apple Silicon. The app is a
universal binary, so it runs natively on both (no Rosetta needed).

1. Download `DediProg-Software-1.1.0-macOS.dmg`
2. Open the DMG and drag **DediProg Software** to your Applications folder
3. Launch it. If macOS says the app can't be opened (it isn't signed with an Apple
   developer ID, and macOS may block it after a browser download):
   - macOS 14 (Sonoma) and older: right-click the app → **Open** → **Open**
   - macOS 15 (Sequoia) and newer: go to **System Settings → Privacy & Security**,
     scroll down and click **Open Anyway**

---

### Windows

**Requirements:** 64-bit Windows 10 or 11.

1. Download `DediProg-Software-1.1.0-Windows.zip` and extract it to a folder of your choice
   (keep all files together — `dpgui.exe` needs the DLLs and `ChipInfoDb.dedicfg` next to it)
2. Install the **WinUSB** driver for the programmer, if it doesn't have one yet:
   - If DediProg's own Windows software is installed, the driver is already there.
   - Otherwise use [Zadig](https://zadig.akeo.ie/): plug in the programmer, choose it from
     the device list (enable **Options → List All Devices** if it's missing), select
     **WinUSB** and click **Install Driver**.
3. Run `dpgui.exe`. Windows SmartScreen may warn about an unrecognised app — click
   **More info → Run anyway**.

Settings and recent files are stored in `%APPDATA%\dpgui\`.

---

## Platform Compatibility

| Platform | Version | Hardware | Status |
|----------|---------|----------|--------|
| Ubuntu | 22.04 LTS | x86_64 | ✅ Tested |
| Ubuntu | 24.04 LTS | x86_64 | ✅ Tested |
| Fedora | 36+ | x86_64 | ✅ Tested |
| openSUSE | Leap / Tumbleweed | x86_64 | ✅ Tested |
| RHEL / Rocky Linux | 9+ | x86_64 | ✅ Tested |
| macOS | Ventura 13.x | Intel | ✅ Tested |
| macOS | Tahoe 26.x | Intel | ✅ Tested |
| macOS | Tahoe 26.x | Apple Silicon | ✅ Tested |
| Windows | 10 | x86_64 | ✅ Tested |
| Windows | 11 | x86_64 | ✅ Tested |

**Minimum requirements:**
- Linux: glibc 2.35+ (Ubuntu 22.04 or equivalent)
- macOS: 13.0 (Ventura), Intel or Apple Silicon
- Windows: 64-bit Windows 10 or 11; the programmer needs the
  WinUSB driver (installed by DediProg's software, or with Zadig)

---

## Programmer Support

| Programmer | Status |
|------------|--------|
| SF600Plus | ✅ Tested |
| SF600Plus-G2 | ✅ Tested |
| SF600 | ✅ Tested |
| SF700 | ⚠️ Expected to work (untested) |
| SF100 | ⚠️ Expected to work (untested) |

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
make linux-release SFDIR=.. VERSION=1.1.0
# Output: dediprog-software-1.1.0-linux-x86_64.tar.gz
```

---

### macOS

The release build is a **universal binary** (Apple Silicon + Intel) and can be built on
either kind of Mac. Homebrew's libraries only exist for the Mac's own architecture, so
`build_mac_universal.sh` builds FLTK and libusb from source as universal static libraries
(once — later runs reuse them) and links them into `dpgui` and `dpcmd`. The result needs
no extra libraries at runtime.

**Install dependencies:**
```bash
xcode-select --install     # Xcode command line tools
brew install cmake
```

**Build:**
```bash
git clone https://github.com/lagnajeet/SF100Linux.git
cd SF100Linux
git checkout v1-14-21-dpgui
cd dp_gui
bash build_mac_universal.sh
```

The script ends by printing `lipo -info` (should list `x86_64 arm64`) and the linked
libraries (system libraries only).

**Build app bundle and DMG:**
```bash
mkdir -p "DediProg Software.app/Contents/MacOS"
mkdir -p "DediProg Software.app/Contents/Resources"
cp dpgui dpcmd "DediProg Software.app/Contents/MacOS/"
cp ../ChipInfoDb.dedicfg "DediProg Software.app/Contents/MacOS/"
cp your_icon.icns "DediProg Software.app/Contents/Resources/dpgui.icns"

cat > "DediProg Software.app/Contents/Info.plist" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>               <string>DediProg Software</string>
    <key>CFBundleDisplayName</key>        <string>DediProg Software</string>
    <key>CFBundleIdentifier</key>         <string>io.github.lagnajeet.dpgui</string>
    <key>CFBundleExecutable</key>         <string>dpgui</string>
    <key>CFBundleIconFile</key>           <string>dpgui</string>
    <key>CFBundlePackageType</key>        <string>APPL</string>
    <key>CFBundleShortVersionString</key> <string>1.1.0</string>
    <key>CFBundleVersion</key>            <string>1.1.0</string>
    <key>LSMinimumSystemVersion</key>     <string>13.0</string>
    <key>NSHighResolutionCapable</key>    <true/>
</dict>
</plist>
EOF

# Ad-hoc signature — required for the app to run on Apple Silicon
codesign --force --deep --sign - "DediProg Software.app"

hdiutil create -volname "DediProg Software" -srcfolder "DediProg Software.app" \
    -ov -format UDZO DediProg-Software-1.1.0-macOS.dmg
```

**Quick development build (current architecture only):**
```bash
brew install fltk libusb pkg-config
make SFDIR=..
```
This links Homebrew's libraries, so the binary only runs on the same kind of Mac it was
built on (an Intel build needs Rosetta on Apple Silicon). Use the universal script for
anything you distribute.

---

### Windows

Builds with [MSYS2](https://www.msys2.org/) in the **UCRT64** shell (the plain "MSYS"
shell has no compiler).

**Install dependencies** (in the MSYS2 UCRT64 shell):
```bash
pacman -S git mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-fltk \
          mingw-w64-ucrt-x86_64-libusb mingw-w64-ucrt-x86_64-pkg-config
```

**Build:**
```bash
git clone https://github.com/lagnajeet/SF100Linux.git
cd SF100Linux
git checkout v1-14-21-dpgui
cd dp_gui
bash build.sh
# Output: dpgui.exe and dpcmd.exe
```

**Package:** put these files in one folder and zip it:
- `dpgui.exe`, `dpcmd.exe`, `ChipInfoDb.dedicfg`
- From `C:\msys64\ucrt64\bin`: `libfltk-1.4.dll`, `libusb-1.0.dll`, `libwinpthread-1.dll`,
  `libgcc_s_seh-1.dll`, `libstdc++-6.dll`


---

## dpcmd — Command Line Tool

The `dpcmd` command line tool is included in the Linux release package, the Windows
zip (`dpcmd.exe`) and the macOS app (`/Applications/DediProg Software.app/Contents/MacOS/dpcmd`).
Use it for scripting and automation; it needs `ChipInfoDb.dedicfg` in the same folder
(already the case in all three packages). On Windows run it from a Command Prompt or
PowerShell window (`dpcmd.exe …`). On macOS, call it by its full path or add an alias:
`alias dpcmd="/Applications/DediProg\ Software.app/Contents/MacOS/dpcmd"`.

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
