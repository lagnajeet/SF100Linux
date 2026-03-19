#!/usr/bin/env bash
# DediProg Software — Linux Installer
# Installs dpgui, ChipInfoDb.dedicfg, udev rule, and optionally a .desktop entry

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_NAME="DediProg Software"
BIN_NAME="dpgui"
DB_NAME="ChipInfoDb.dedicfg"
UDEV_RULE="60-dediprog.rules"
DEFAULT_INSTALL="$HOME/DediProg"

# Colours
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'

echo ""
echo -e "${BOLD}======================================${NC}"
echo -e "${BOLD}   $APP_NAME — Linux Installer${NC}"
echo -e "${BOLD}======================================${NC}"
echo ""

# ── 1. Install location ───────────────────────────────────────────────────────
echo -e "${CYAN}Where would you like to install $APP_NAME?${NC}"
echo -e "Press Enter to use the default: ${BOLD}$DEFAULT_INSTALL${NC}"
read -rp "Install path: " INSTALL_DIR
INSTALL_DIR="${INSTALL_DIR:-$DEFAULT_INSTALL}"
INSTALL_DIR="${INSTALL_DIR/#\~/$HOME}"  # expand ~ if user typed it

echo ""
echo -e "Installing to: ${BOLD}$INSTALL_DIR${NC}"
mkdir -p "$INSTALL_DIR"

# ── 2. Copy binary and database ───────────────────────────────────────────────
echo -e "${CYAN}Copying files...${NC}"

if [ ! -f "$SCRIPT_DIR/$BIN_NAME" ]; then
    echo -e "${RED}ERROR: $BIN_NAME not found in $SCRIPT_DIR${NC}"
    exit 1
fi
if [ ! -f "$SCRIPT_DIR/$DB_NAME" ]; then
    echo -e "${RED}ERROR: $DB_NAME not found in $SCRIPT_DIR${NC}"
    exit 1
fi

cp "$SCRIPT_DIR/$BIN_NAME" "$INSTALL_DIR/"
cp "$SCRIPT_DIR/$DB_NAME"  "$INSTALL_DIR/"
chmod +x "$INSTALL_DIR/$BIN_NAME"
echo -e "  ${GREEN}✓${NC} $BIN_NAME"
echo -e "  ${GREEN}✓${NC} $DB_NAME"
if [ -f "$SCRIPT_DIR/dpcmd" ]; then
    cp "$SCRIPT_DIR/dpcmd" "$INSTALL_DIR/"
    chmod +x "$INSTALL_DIR/dpcmd"
    echo -e "  ${GREEN}✓${NC} dpcmd (command line tool)"
fi

# ── 3. udev rule (needs sudo) ─────────────────────────────────────────────────
echo ""
echo -e "${CYAN}Installing udev rule for non-root USB access...${NC}"
echo -e "${YELLOW}This step requires sudo to write to /etc/udev/rules.d/${NC}"

if [ ! -f "$SCRIPT_DIR/$UDEV_RULE" ]; then
    echo -e "${YELLOW}Warning: $UDEV_RULE not found, skipping udev setup.${NC}"
else
    if sudo cp "$SCRIPT_DIR/$UDEV_RULE" /etc/udev/rules.d/; then
        sudo udevadm control --reload-rules 2>/dev/null || true
        sudo udevadm trigger            2>/dev/null || true
        echo -e "  ${GREEN}✓${NC} udev rule installed"
        echo -e "  ${GREEN}✓${NC} udev rules reloaded"
        echo ""
        echo -e "${YELLOW}Note: You may need to unplug and replug the programmer${NC}"
        echo -e "${YELLOW}      for the udev rule to take effect.${NC}"
    else
        echo -e "${RED}Failed to install udev rule. You can do it manually:${NC}"
        echo -e "  sudo cp $SCRIPT_DIR/$UDEV_RULE /etc/udev/rules.d/"
        echo -e "  sudo udevadm control --reload-rules"
    fi
fi

# ── 4. Add to PATH ────────────────────────────────────────────────────────────
echo ""
echo -e "${CYAN}Would you like to add $INSTALL_DIR to your PATH?${NC}"
echo -e "This allows you to run '${BOLD}dpgui${NC}' from anywhere in the terminal."
read -rp "Add to PATH? [Y/n]: " ADD_PATH
ADD_PATH="${ADD_PATH:-Y}"

if [[ "$ADD_PATH" =~ ^[Yy] ]]; then
    PATH_LINE="export PATH=\"\$PATH:$INSTALL_DIR\""
    ADDED=0
    for RC in "$HOME/.bashrc" "$HOME/.zshrc"; do
        if [ -f "$RC" ]; then
            if ! grep -qF "$INSTALL_DIR" "$RC"; then
                echo "" >> "$RC"
                echo "# DediProg Software" >> "$RC"
                echo "$PATH_LINE" >> "$RC"
                echo -e "  ${GREEN}✓${NC} Added to $RC"
                ADDED=1
            else
                echo -e "  ${GREEN}✓${NC} Already in $RC"
                ADDED=1
            fi
        fi
    done
    if [ $ADDED -eq 0 ]; then
        echo -e "${YELLOW}No .bashrc or .zshrc found. Add manually:${NC}"
        echo -e "  $PATH_LINE"
    fi
fi

# ── 5. Desktop entry ──────────────────────────────────────────────────────────
echo ""
echo -e "${CYAN}Would you like to create a desktop menu entry?${NC}"
read -rp "Create desktop entry? [Y/n]: " ADD_DESKTOP
ADD_DESKTOP="${ADD_DESKTOP:-Y}"

if [[ "$ADD_DESKTOP" =~ ^[Yy] ]]; then
    DESKTOP_DIR="$HOME/.local/share/applications"
    mkdir -p "$DESKTOP_DIR"
    cat > "$DESKTOP_DIR/dediprog-software.desktop" << EOF
[Desktop Entry]
Version=1.0
Type=Application
Name=DediProg Software
Comment=SF100/SF600 SPI NOR Flash Programmer GUI
Exec=$INSTALL_DIR/$BIN_NAME
Icon=utilities-terminal
Terminal=false
Categories=Development;Electronics;
Keywords=dediprog;flash;programmer;spi;
EOF
    chmod +x "$DESKTOP_DIR/dediprog-software.desktop"
    # Refresh desktop database if available
    update-desktop-database "$DESKTOP_DIR" 2>/dev/null || true
    echo -e "  ${GREEN}✓${NC} Desktop entry created"
fi

# ── 6. Done ───────────────────────────────────────────────────────────────────
echo ""
echo -e "${BOLD}======================================${NC}"
echo -e "${GREEN}${BOLD}   Installation complete!${NC}"
echo -e "${BOLD}======================================${NC}"
echo ""
echo -e "Run the program:"
echo -e "  ${BOLD}$INSTALL_DIR/$BIN_NAME${NC}"
if [[ "$ADD_PATH" =~ ^[Yy] ]]; then
    echo -e "  or after restarting your terminal: ${BOLD}dpgui${NC}"
fi
echo ""
