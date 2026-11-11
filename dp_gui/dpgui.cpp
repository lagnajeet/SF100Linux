/*
 * dpgui.cpp — DediProg SF100/SF600 Linux GUI (natively integrated)
 *
 * Links directly against SF100Linux V1.14.21.x C sources.
 * Layout mirrors the Windows DediProg SF7 GUI:
 *   - System Info / File Info / Programmer Info / Memory Info panels
 *   - VCC auto-detected from chip database (VoltageInMv)
 *   - Native Linux file picker (zenity → kdialog → FLTK fallback)
 *
 * Build:  see Makefile  (requires FLTK 1.3, libusb-1.0)
 */

#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/Fl_Native_File_Chooser.H>
#ifndef __APPLE__
#ifndef _WIN32
#include <FL/x.H>   // fl_xid() for X11 window ID
#include <dlfcn.h>  // dlopen() for GTK probe
#include <sys/file.h>  // flock()
#endif
#endif
#include <FL/Fl_Group.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Select_Browser.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Round_Button.H>
#include <FL/Fl_Progress.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>

#include <cstdio>
#include <sys/file.h>
#include <signal.h>
#ifdef _WIN32
#include <io.h>      // _pipe, _dup, _dup2, _read, _close
#include <fcntl.h>   // O_BINARY
#include <direct.h>  // _mkdir
#endif
#ifdef _WIN32
#include <winsock2.h>  // for timeval on Windows
#include <windows.h>
#include <FL/x.H>
#endif
#include <fcntl.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <chrono>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#ifndef _WIN32
#include <sys/utsname.h>
#endif
#ifdef __APPLE__
#include <sys/sysctl.h>  // sysctlbyname() for System Info
#endif
#include <libgen.h>
#include <time.h>
#include <algorithm>
#include <cstdint>
#include <stdint.h>

// ── Override openChipInfoDb (Linux only) ─────────────────────────────────────
// On macOS, parse.c already has a native _NSGetExecutablePath implementation
// that finds the DB next to the binary — no override needed.
#if !defined(__APPLE__) && !defined(_WIN32)
extern "C" FILE* openChipInfoDb_orig(void);
extern "C" FILE* openChipInfoDb(void) {
    if (FILE* f = openChipInfoDb_orig()) return f;
    char buf[1024];
    if (const char* pe = getenv("PATH")) {
        char pc[4096]; strncpy(pc, pe, sizeof(pc)-1); pc[sizeof(pc)-1]=0;
        char* dir = strtok(pc, ":");
        while (dir) {
            snprintf(buf, sizeof(buf), "%s/ChipInfoDb.dedicfg", dir);
            if (FILE* f = fopen(buf, "rt")) return f;
            dir = strtok(nullptr, ":");
        }
    }
    for (const char* p : {"/usr/share/DediProg/ChipInfoDb.dedicfg",
                          "/usr/local/share/DediProg/ChipInfoDb.dedicfg"})
        if (FILE* f = fopen(p, "rt")) return f;
    fprintf(stderr, "Error: ChipInfoDb.dedicfg not found.\n");
    return nullptr;
}
#endif // !__APPLE__ && !_WIN32

// ── SF100Linux C API ──────────────────────────────────────────────────────────
extern "C" {
#include "dpcmd.h"
#include "project.h"
#include "usbdriver.h"
#include "Macro.h"
#include "board.h"
#ifdef __APPLE__
#undef FREAD  // conflicts with macOS fcntl.h macro
#endif
#include "SerialFlash.h"

    extern char           g_board_type[8];
    extern char           g_FW_ver[10];
    extern char           g_HW_ver[8];
    extern int            g_firmversion;
    extern CHIP_INFO      Chip_Info;
    extern unsigned int   g_Vcc;
    extern unsigned int   g_ucSPIClock;
    extern unsigned int   g_uiDevNum;
    extern unsigned long  g_ucOperation;
    extern char*          g_parameter_program;
    extern char*          g_parameter_read;
    extern char*          g_parameter_auto;
    extern char*          g_parameter_vcc;
    extern char*          g_parameter_loadfile_with_verify;
    extern volatile bool  g_bIsSF600[16];
    extern volatile bool  g_bIsSF700[16];
    extern volatile bool  g_bIsSF600PG2[16];
    extern char           g_LogPath[512];
    extern unsigned char  g_BatchIndex;
    extern char           strTypeName[1024];
    extern unsigned int   g_uiTimeout;
    extern unsigned int   g_uiAddr;
    extern size_t         g_uiLen;
    extern unsigned long  g_ulFileSize;
    extern unsigned int   g_ucFill;
    extern bool           g_bDisplayTimer;
    extern volatile bool  g_is_operation_on_going;

    int          OpenUSB(void);
    int          Handler(void);
    void         GetLogPath(char* path);
    void         LeaveStandaloneMode(int index);
    void         QueryBoard(int index);
    int          get_usb_dev_cnt(void);
    CHIP_INFO    GetFirstDetectionMatch(char* TypeName, int Index);
    int          FlashIdentifier(CHIP_INFO* ci, int search_all, int Index);
    int          Dedi_Search_Chip_Db_ByTypeName(char* TypeName, CHIP_INFO* ci);
    unsigned int GetFPGAVersion(int Index);
    bool         GetFirmwareVer(int Index);
    unsigned int ReadUID(int Index);
    bool         LoadFile(char* filename);
    void         SaveProgContextChanges(void);
    bool         HexFileToBin(const char* path, unsigned char* buf, unsigned long* size, unsigned char fill);
    bool         S19FileToBin(const char* path, unsigned char* buf, unsigned long* size, unsigned char fill);
    int          ReadBINFile(const char* path, unsigned char* buf, unsigned long* size);
#include "IntelHexFile.h"
#include "MotorolaFile.h"
}

// ── Colours ───────────────────────────────────────────────────────────────────
// ---- Light theme colours
#define COL_HEADER_LIGHT    fl_rgb_color(0x1A,0x3C,0x6E)
#define COL_PANEL_HDR_LIGHT fl_rgb_color(0xD0,0xDF,0xF0)
#define COL_PANEL_BG_LIGHT  fl_rgb_color(0xF6,0xF8,0xFC)
#define COL_BLUE_VAL_LIGHT  fl_rgb_color(0x00,0x55,0xBB)
// ---- Dark theme colours
#define COL_HEADER_DARK     fl_rgb_color(0x0D,0x1F,0x3C)
#define COL_PANEL_HDR_DARK  fl_rgb_color(0x2D,0x2D,0x30)
#define COL_PANEL_BG_DARK   fl_rgb_color(0x25,0x25,0x26)
#define COL_BLUE_VAL_DARK   fl_rgb_color(0x4F,0xC3,0xF7)

static bool g_dark_mode    = false;
#ifndef __APPLE__
static bool g_gtk_available = false;
#endif

// Active theme colours (set by apply_theme)
static Fl_Color COL_HEADER    = COL_HEADER_LIGHT;
static Fl_Color COL_PANEL_HDR = COL_PANEL_HDR_LIGHT;
static Fl_Color COL_PANEL_BG  = COL_PANEL_BG_LIGHT;
static Fl_Color COL_BLUE_VAL  = COL_BLUE_VAL_LIGHT;

// Centre a dialog over the main window
static void center_over_parent(Fl_Window* dlg) {
    // Find the largest visible window (the main window) as parent
    Fl_Window* parent = nullptr;
    int max_area = 0;
    for (Fl_Window* w = Fl::first_window(); w; w = Fl::next_window(w)) {
        if (w == dlg) continue;
        int area = w->w() * w->h();
        if (area > max_area) { max_area = area; parent = w; }
    }
    if (!parent) parent = Fl::first_window();
    if (parent && parent != dlg) {
        int px = parent->x() + (parent->w() - dlg->w()) / 2;
        int py = parent->y() + (parent->h() - dlg->h()) / 2;
        dlg->position(px, py);
    }
}

// ── Chip DB parser ────────────────────────────────────────────────────────────
// openChipInfoDb is defined in parse.c on macOS and Windows
#if defined(__APPLE__) || defined(_WIN32)
extern "C" FILE* openChipInfoDb(void);
#endif
// Parses ChipInfoDb.dedicfg (XML, UTF-16LE with CRLF line endings) and returns
// a flat list of {TypeName, Manufacturer} for every SPI NOR chip entry.
struct ChipEntry { std::string name, manufacturer; };

static std::vector<ChipEntry> parse_chip_db() {
    std::vector<ChipEntry> chips;
    FILE* fp = openChipInfoDb();
    if (!fp) return chips;

    // File is UTF-16LE. Read entire file then extract ASCII chars (strip nulls).
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    std::vector<char> raw(sz+1, 0);
    fread(raw.data(), 1, sz, fp);
    fclose(fp);

    // Strip null bytes to get ASCII-compatible text
    std::string text;
    text.reserve(sz);
    for (long i = 0; i < sz; i++)
        if (raw[i] != '\0') text += raw[i];

    // Walk through <Chip ...> blocks extracting TypeName and Manufacturer
    size_t pos = 0;
    while ((pos = text.find("<Chip ", pos)) != std::string::npos) {
        // Find end of this chip element (ends at "/>")
        size_t end = text.find("/>", pos);
        if (end == std::string::npos) break;
        std::string block = text.substr(pos, end - pos + 2);
        pos = end + 2;

        // Helper: extract attribute value from block
        auto attr = [&](const std::string& key) -> std::string {
            std::string pat = key + "=\"";
            size_t a = block.find(pat);
            if (a == std::string::npos) return "";
            a += pat.size();
            size_t b = block.find('"', a);
            if (b == std::string::npos) return "";
            return block.substr(a, b - a);
        };

        // Only include SPI NOR chips (same filter as Windows dialog "SPI NOR" type)
        std::string ictype = attr("ICType");
        if (ictype != "SPI_NOR") continue;

        std::string name = attr("TypeName");
        std::string mfr  = attr("Manufacturer");
        if (name.empty()) continue;
        chips.push_back({name, mfr});
    }
    return chips;
}

// ── Chip Select Dialog ────────────────────────────────────────────────────────
// Mirrors the Windows "Manually Select Memory Type" dialog.
// auto_detected: list of chip names the programmer identified (highlighted).
// Returns chosen TypeName, or "" if cancelled.
static std::string show_chip_select_dialog(
        const std::vector<std::string>& auto_detected,
        const std::vector<ChipEntry>&   all_chips)
{
    // Build sorted unique manufacturer list
    std::vector<std::string> mfrs;
    for (auto& c : all_chips) {
        if (c.manufacturer.empty()) continue;
        if (std::find(mfrs.begin(), mfrs.end(), c.manufacturer) == mfrs.end())
            mfrs.push_back(c.manufacturer);
    }
    std::sort(mfrs.begin(), mfrs.end());

    // Result: empty = cancelled
    static std::string s_result;
    s_result = "";

    const int DW = 640, DH = 500;
    const int PAD = 10;
    // Left panel: chip type + manufacturer filter
    const int LW = 210, RW = DW - LW - PAD*3;
    const int LIST_H = DH - 120;

    Fl_Window* dlg = new Fl_Window(DW, DH, "Manually Select Memory Type");
    dlg->begin();

    // Chip Type label + dropdown (only SPI NOR supported)
    int y = PAD;
    new Fl_Box(PAD, y+3, 80, 22, "Chip Type:");
    Fl_Choice* cho_type = new Fl_Choice(PAD+85, y, 130, 24);
    cho_type->add("SPI NOR");
    cho_type->value(0);
    cho_type->deactivate(); // only SPI NOR supported
    y += 32;

    // Filters label
    new Fl_Box(PAD,               y, LW,  18, "Filters:");
    new Fl_Box(PAD*2+LW,          y, RW,  18, "Memory List:");
    ((Fl_Box*)Fl::focus()? Fl::focus() : dlg)->labelsize(11); // style both
    y += 20;

    // Manufacturer browser (left)
    Fl_Hold_Browser* lst_mfr = new Fl_Hold_Browser(PAD, y, LW, LIST_H);
    lst_mfr->textsize(12);

    // Chip name browser (right)
    Fl_Hold_Browser* lst_chip = new Fl_Hold_Browser(PAD*2+LW, y, RW, LIST_H);
    lst_chip->textsize(12);

    // Populate manufacturer list
    lst_mfr->add("<All>");
    lst_mfr->add("<Auto Detected Type(s)>");
    for (auto& m : mfrs) lst_mfr->add(m.c_str());

    // Populate chip list function (captures by pointer)
    struct State {
        Fl_Hold_Browser* lst_mfr;
        Fl_Hold_Browser* lst_chip;
        const std::vector<ChipEntry>* all;
        const std::vector<std::string>* auto_det;
        std::string* result;
        Fl_Window* dlg;

        void populate_chips(int mfr_sel) {
            lst_chip->clear();
            std::string filter = (mfr_sel >= 1) ? lst_mfr->text(mfr_sel) : "<All>";
            bool show_all      = (filter == "<All>");
            bool auto_det_mode = (filter == "<Auto Detected Type(s)>");

            // Collect matching names, then sort before adding to browser
            std::vector<std::string> entries;
            for (auto& c : *all) {
                bool include = show_all
                    || (auto_det_mode && std::find(auto_det->begin(), auto_det->end(), c.name) != auto_det->end())
                    || (!auto_det_mode && !show_all && c.manufacturer == filter);
                if (include) entries.push_back(c.name);
            }
            std::sort(entries.begin(), entries.end());

            int first_auto = -1;
            int idx = 1;
            for (auto& name : entries) {
                bool is_auto = std::find(auto_det->begin(), auto_det->end(), name) != auto_det->end();
                std::string entry = is_auto ? ("@b" + name) : name;
                lst_chip->add(entry.c_str());
                if (is_auto && first_auto < 0) first_auto = idx;
                idx++;
            }
            // Select and scroll to first auto-detected chip
            if (first_auto > 0) {
                lst_chip->value(first_auto);
                lst_chip->middleline(first_auto);
            } else if (lst_chip->size() > 0) {
                lst_chip->value(1);
            }
        }
    } state;
    state.lst_mfr  = lst_mfr;
    state.lst_chip = lst_chip;
    state.all      = &all_chips;
    state.auto_det = &auto_detected;
    state.result   = &s_result;
    state.dlg      = dlg;

    // Start with <Auto Detected Type(s)> selected if we have matches, else <All>
    if (!auto_detected.empty()) {
        lst_mfr->value(2); // "<Auto Detected Type(s)>"
        state.populate_chips(2);
    } else {
        lst_mfr->value(1); // "<All>"
        state.populate_chips(1);
    }

    // Manufacturer selection callback
    lst_mfr->callback([](Fl_Widget*, void* ud){
        State* s = (State*)ud;
        s->populate_chips(s->lst_mfr->value());
    }, &state);

    // Chip double-click = OK
    lst_chip->callback([](Fl_Widget*, void* ud){
        if (Fl::event_clicks() >= 1) {
            State* s = (State*)ud;
            int v = s->lst_chip->value();
            if (v > 0) {
                std::string raw = s->lst_chip->text(v);
                // strip @b prefix if present
                if (raw.size() > 2 && raw[0]=='@' && raw[1]=='b') raw = raw.substr(2);
                *s->result = raw;
                s->dlg->hide();
            }
        }
    }, &state);

    // OK / Cancel buttons
    y += LIST_H + PAD;
    Fl_Button* btn_ok  = new Fl_Button(DW-180, y, 80, 26, "OK");
    Fl_Button* btn_can = new Fl_Button(DW-90,  y, 80, 26, "Cancel");

    btn_ok->callback([](Fl_Widget*, void* ud){
        State* s = (State*)ud;
        int v = s->lst_chip->value();
        if (v > 0) {
            std::string raw = s->lst_chip->text(v);
            if (raw.size() > 2 && raw[0]=='@' && raw[1]=='b') raw = raw.substr(2);
            *s->result = raw;
        }
        s->dlg->hide();
    }, &state);

    btn_can->callback([](Fl_Widget*, void* ud){
        ((State*)ud)->dlg->hide();
    }, &state);

    dlg->callback([](Fl_Widget* w, void*){ w->hide(); });

    dlg->end();
    dlg->set_modal();
    center_over_parent(dlg);
    dlg->show();
    while (dlg->shown()) Fl::wait();
    delete dlg;

    return s_result;
}

// Centre a dialog over the parent window before showing
// Forward declaration (defined later with native file picker)
static std::string native_pick(const char* title, const char* glob, bool save, const char* preset=nullptr);

// ── Recent files ──────────────────────────────────────────────────────────────
static const int RECENT_MAX = 10;
static std::vector<std::string> g_recent_files;
static const char* RECENT_PATH = nullptr; // set at startup

static std::string recent_file_path() {
    static std::string p;
#ifdef _WIN32
    {
        const char* appdata = getenv("APPDATA");
        std::string dir = appdata ? std::string(appdata) + "\\dpgui" : "C:\\dpgui";
        _mkdir(dir.c_str());
        p = dir + "\\dpgui_recent";
    }
#else
    const char* home = getenv("HOME");
    if (home) {
#ifdef __APPLE__
        // macOS 15+: use ~/Library/Application Support/dpgui/
        // ~/.config is not reliably writable for Finder-launched apps
        p = std::string(home) + "/Library/Application Support/dpgui";
        mkdir(p.c_str(), 0755);
        p += "/dpgui_recent";
#else
        p = std::string(home) + "/.config/dpgui_recent";
#endif
    } else p = std::string(getenv("TEMP") ? getenv("TEMP") : "C:\\Temp") + "\\dpgui_recent";
#endif // _WIN32
    return p;
}

static void recent_load() {
    g_recent_files.clear();
    FILE* f = fopen(recent_file_path().c_str(), "r");
    if (!f) return;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1]=='\n'||line[len-1]=='\r')) line[--len]=0;
        if (len > 0) g_recent_files.push_back(line);
    }
    fclose(f);
}

static void recent_add(const std::string& path) {
    // Remove duplicate then prepend
    g_recent_files.erase(
        std::remove(g_recent_files.begin(), g_recent_files.end(), path),
        g_recent_files.end());
    g_recent_files.insert(g_recent_files.begin(), path);
    if ((int)g_recent_files.size() > RECENT_MAX)
        g_recent_files.resize(RECENT_MAX);
    FILE* f = fopen(recent_file_path().c_str(), "w");
    if (!f) return;
    for (auto& p : g_recent_files) fprintf(f, "%s\n", p.c_str());
    fclose(f);
}

// Escape path for Fl_Choice: only / @ & need escaping (FLTK special chars)
// We replace / with the Unicode lookalike ∕ for display,
// and store the real path separately in cur_file.
// Simpler approach: just replace / with a placeholder FLTK won't interpret
static std::string fltk_escape(const std::string& s) {
    // FLTK uses \ as escape char and / as submenu separator.
    // Double all backslashes and escape forward slashes.
    std::string out;
    out.reserve(s.size() * 2);
    for (char ch : s) {
        if (ch == '\\') { out += "\\\\"; } // \ -> \\
        else if (ch == '/') { out += "\\/"; }  // / -> \/
        else if (ch == '@') { out += "\\@"; }  // @ -> \@
        else if (ch == '&') { out += "\\&"; }  // & -> \&
        else { out += ch; }
    }
    return out;
}

static std::string fltk_unescape(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i+1 < s.size()) {
            r += s[++i]; // consume escape, keep next char as-is
        } else {
            r += s[i];
        }
    }
    return r;
}

static std::string prefs_path() {
    static std::string p;
#ifdef _WIN32
    {
        const char* appdata = getenv("APPDATA");
        std::string dir = appdata ? std::string(appdata) + "\\dpgui" : "C:\\dpgui";
        _mkdir(dir.c_str());
        p = dir + "\\dpgui_prefs";
    }
#else
    const char* home = getenv("HOME");
    if (home) {
#ifdef __APPLE__
        p = std::string(home) + "/Library/Application Support/dpgui";
        mkdir(p.c_str(), 0755);
        p += "/dpgui_prefs";
#else
        p = std::string(home) + "/.config/dpgui_prefs";
#endif
    } else p = std::string(getenv("TEMP") ? getenv("TEMP") : "C:\\Temp") + "\\dpgui_prefs";
#endif // _WIN32
    return p;
}
static void prefs_save(int x, int y, int w, int h, bool dark) {
    FILE* f = fopen(prefs_path().c_str(), "w");
    if (!f) return;
    fprintf(f, "x=%d\ny=%d\nw=%d\nh=%d\ndark=%d\n", x, y, w, h, dark?1:0);
    fclose(f);
}
static void prefs_load(int& x, int& y, int& w, int& h, bool& dark) {
    FILE* f = fopen(prefs_path().c_str(), "r");
    if (!f) return;
    char line[64];
    while (fgets(line, sizeof(line), f)) {
        int v;
        if      (sscanf(line, "x=%d",    &v)==1) x=v;
        else if (sscanf(line, "y=%d",    &v)==1) y=v;
        else if (sscanf(line, "w=%d",    &v)==1) w=v;
        else if (sscanf(line, "h=%d",    &v)==1) h=v;
        else if (sscanf(line, "dark=%d", &v)==1) dark=(v!=0);
    }
    fclose(f);
}

// ── File format ───────────────────────────────────────────────────────────────
enum FileFormat { FMT_AUTO=0, FMT_BIN, FMT_HEX, FMT_S19 };
static FileFormat g_file_format = FMT_AUTO;
static bool       g_truncate    = false;

// Load file using explicit format choice (bypasses extension detection)
static bool load_file_with_format(const char* path, FileFormat fmt) {
    unsigned long size = 0;
    bool ok = false;
    // pBufferforLoadedFile is allocated inside project.c — we go through LoadFile
    // but we can't override format there. Workaround: temporarily rename to a
    // path with the right extension using a symlink in /tmp.
    if (fmt == FMT_AUTO) {
        char buf[4096]; strncpy(buf, path, sizeof(buf)-1);
        return LoadFile(buf);
    }
    // Build a temp symlink with the right extension
    const char* ext = (fmt==FMT_HEX) ? ".hex" : (fmt==FMT_S19) ? ".s19" : ".bin";
    char lnk[256];
    snprintf(lnk, sizeof(lnk), "/tmp/dpgui_fmt_override%s", ext);
    unlink(lnk);
#ifdef _WIN32
    // symlink not available on Windows -- use original path directly
    ok = LoadFile(const_cast<char*>(path));
#else
    if (symlink(path, lnk) == 0) {
        ok = LoadFile(lnk);
        unlink(lnk);
    } else {
        // symlink failed -- fall back
        char buf[4096]; strncpy(buf, path, sizeof(buf)-1);
        ok = LoadFile(buf);
    }
#endif
    return ok;
}

// ── Load File dialog ──────────────────────────────────────────────────────────
// Returns true if user confirmed, populates out_path, out_fmt, out_truncate.
static bool show_load_file_dialog(std::string& out_path,
                                   FileFormat&   out_fmt,
                                   bool&         out_truncate,
                                   size_t        chip_size_bytes)
{
    static bool       s_ok       = false;
    static FileFormat s_fmt      = FMT_AUTO;
    static bool       s_trunc    = false;
    static std::string s_path;

    s_ok    = false;
    s_fmt   = g_file_format;
    s_trunc = g_truncate;
    s_path  = out_path;  // pre-fill with current path

    const int DW=560, DH=150, PAD=12;

    Fl_Window* dlg = new Fl_Window(DW, DH, "Load File");
    dlg->begin();

    int y = PAD;
    // File path label + combo (recent files)
    new Fl_Box(PAD, y+3, 72, 22, "File Path:");
    Fl_Choice* cho_path = new Fl_Choice(PAD+76, y, DW-PAD-76-80-PAD, 26);
    cho_path->textsize(11);
    // Populate with recent files
    for (auto& r : g_recent_files) cho_path->add(fltk_escape(r).c_str());
    if (!s_path.empty()) {
        // If current path isn't in list add it temporarily at top
        bool found = false;
        for (int i = 0; i < cho_path->size()-1; i++)
            if (std::string(cho_path->text(i)) == s_path) { cho_path->value(i); found=true; break; }
        if (!found) { cho_path->insert(0, fltk_escape(s_path).c_str(), 0, nullptr); cho_path->value(0); }
    } else if (cho_path->size() > 1) {
        cho_path->value(0);
    }

    Fl_Button* btn_find = new Fl_Button(DW-PAD-76, y, 76, 26, "Find");
    btn_find->callback([](Fl_Widget*, void* ud){
        Fl_Choice* c = (Fl_Choice*)ud;
        std::string p = native_pick("Select File",
            "*.bin *.hex *.img *.s19 *.srec *.mot *.rom", false);
        p=fltk_escape(p);
        if (!p.empty()) {
            bool found = false;
            for (int i = 0; i < c->size()-1; i++)
                if (std::string(c->text(i)) == p) { c->value(i); found=true; break; }
            if (!found) { c->insert(0, p.c_str(), 0, nullptr); c->value(0); }
            c->redraw();
        }
    }, cho_path);
    y += 34;

    // Data Format radios
    new Fl_Box(PAD, y+3, 86, 22, "Data Format:");
    static Fl_Round_Button* rb[4];
    const char* fmtlabels[] = {"Raw Binary","Intel Hex","Motorola S19","ROM"};
    int rx = PAD+90;
    for (int i = 0; i < 4; i++) {
        rb[i] = new Fl_Round_Button(rx, y, 120, 22, fmtlabels[i]);
        rb[i]->type(FL_RADIO_BUTTON);
        rb[i]->labelsize(12);
        rx += 120;
    }
    // ROM is same as BIN for our purposes
    rb[3]->deactivate();
    // Set initial selection
    int sel = (int)s_fmt; // 0=AUTO→Raw Binary, 1=BIN, 2=HEX, 3=S19
    rb[sel == 0 ? 0 : sel-1]->value(1); // AUTO→Raw Binary selected by default
    y += 30;

    // Truncate checkbox
    Fl_Check_Button* chk_trunc = new Fl_Check_Button(PAD, y,
        DW-PAD*2, 22, "Truncate file to fit in the target area.");
    chk_trunc->labelsize(12);
    chk_trunc->value(s_trunc ? 1 : 0);
    if (chip_size_bytes == 0) chk_trunc->deactivate(); // no chip selected yet
    y += 30;

    // Separator line
    y += 4;
    // OK / Cancel
    Fl_Button* btn_ok  = new Fl_Button(DW-190, y, 80, 26, "OK");
    Fl_Button* btn_can = new Fl_Button(DW-100, y, 80, 26, "Cancel");

    struct DlgState {
        Fl_Window*     dlg;
        Fl_Choice*     cho_path;
        Fl_Round_Button** rb;
        Fl_Check_Button* chk_trunc;
        bool*          ok;
        FileFormat*    fmt;
        bool*          trunc;
        std::string*   path;
    } ds { dlg, cho_path, rb, chk_trunc, &s_ok, &s_fmt, &s_trunc, &s_path };

    btn_ok->callback([](Fl_Widget*, void* ud){
        DlgState* d = (DlgState*)ud;
        int v = d->cho_path->value();
        if (v >= 0 && d->cho_path->text(v))
            *d->path = d->cho_path->text(v);
        // Determine format from radio buttons
        if      (d->rb[1]->value()) *d->fmt = FMT_HEX;
        else if (d->rb[2]->value()) *d->fmt = FMT_S19;
        else                         *d->fmt = FMT_BIN;
        *d->trunc = (d->chk_trunc->value() != 0);
        *d->ok    = !d->path->empty();
        d->dlg->hide();
    }, &ds);

    btn_can->callback([](Fl_Widget*, void* ud){
        ((DlgState*)ud)->dlg->hide();
    }, &ds);

    dlg->callback([](Fl_Widget* w, void*){ w->hide(); });

    dlg->end();
    dlg->set_modal();
    center_over_parent(dlg);
    dlg->position(dlg->x(), std::max(0, dlg->y() - 100));
    dlg->show();
    while (dlg->shown()) Fl::wait();
    delete dlg;

    if (s_ok) {
        out_path     = s_path;
        out_fmt      = s_fmt;
        out_truncate = s_trunc;
        // Persist choices
        g_file_format = s_fmt;
        g_truncate    = s_trunc;
        recent_add(s_path);
    }
    return s_ok;
}

// ── stdout capture ────────────────────────────────────────────────────────────
static int g_pipe_rd=-1, g_pipe_wr=-1, g_saved_stdout=-1;
static std::string g_pipe_buf;  // accumulates bytes between \r/\n delimiters

static std::atomic<bool> g_pipe_active{false}; // controls pipe_timer_cb

static void capture_start() {
#ifdef _WIN32
    // When launched without a console (e.g. double-click), stdout
    // fileno is -2 (invalid). Reopen to NUL first to get valid handle.
    if (_fileno(stdout) < 0)
        freopen("NUL", "w", stdout);
    int fds[2]; _pipe(fds, 65536, O_BINARY);
    g_pipe_rd=fds[0]; g_pipe_wr=fds[1];
    // Windows pipes are always blocking -- use PeekNamedPipe in read loop
#else
    int fds[2]; pipe(fds);
    g_pipe_rd=fds[0]; g_pipe_wr=fds[1];
    fcntl(g_pipe_rd,F_SETFL,O_NONBLOCK);
#endif
#ifdef _WIN32
    g_saved_stdout=_dup(_fileno(stdout));
    _dup2(g_pipe_wr,_fileno(stdout)); fflush(stdout);
#else
    g_saved_stdout=dup(STDOUT_FILENO);
    dup2(g_pipe_wr,STDOUT_FILENO); fflush(stdout);
#endif
    // Line-buffered on Linux/macOS; unbuffered on Windows (_IOLBF not supported)
#ifdef _WIN32
    setvbuf(stdout, nullptr, _IONBF, 0);
#else
    setvbuf(stdout, nullptr, _IOLBF, 4096);
#endif
    g_pipe_buf.clear();
}
static void capture_stop() {
    g_pipe_active = false; // stop pipe_timer_cb from repeating
    fflush(stdout);
    if (g_saved_stdout >= 0) {
#ifdef _WIN32
        _dup2(g_saved_stdout, _fileno(stdout));
        _close(g_saved_stdout);
#else
        dup2(g_saved_stdout, STDOUT_FILENO);
        close(g_saved_stdout);
#endif
        g_saved_stdout = -1;
    }
#ifdef _WIN32
    if (g_pipe_wr >= 0) { _close(g_pipe_wr); g_pipe_wr = -1; }
#else
    if (g_pipe_wr >= 0) { close(g_pipe_wr); g_pipe_wr = -1; }
#endif
}

// ── Shared state ──────────────────────────────────────────────────────────────
static std::atomic<bool> g_running{false};
static std::mutex        g_log_mutex;
static bool              g_usb_open     = false;
static bool              g_chip_selected = false;  // set true after successful Detect
static int               g_marquee_tick = 0;       // drives progress bar animation (chip-erase fallback)

// ── Native file picker ────────────────────────────────────────────────────────
// Save-dialog filter: one entry per extension in glob (first = default), then All Files.
// "*.img *.bin" -> "IMG Files\t*.img\nBIN Files\t*.bin\nAll Files\t*\n"
static std::string save_filter(const char* glob) {
    std::string out;
    char tmp[512]; strncpy(tmp, glob, sizeof(tmp)-1); tmp[sizeof(tmp)-1] = 0;
    for (char* tok = strtok(tmp, " "); tok; tok = strtok(nullptr, " ")) {
        const char* dot = strchr(tok, '.');
        if (!dot || !*(dot+1)) continue;
        std::string up = dot + 1;
        for (auto& ch : up) ch = (char)toupper((unsigned char)ch);
        out += up + " Files\t" + tok + "\n";
    }
    return out + "All Files\t*\n";
}

// Append the first extension from glob if the file name has none.
// ensure_ext("C:/x/dump", "*.img *.bin") -> "C:/x/dump.img"
static std::string ensure_ext(const std::string& path, const char* glob) {
    if (path.empty()) return path;
    size_t sl = path.find_last_of("/\\");
    size_t dot = path.find_last_of('.');
    if (dot != std::string::npos && (sl == std::string::npos || dot > sl)) return path;
    const char* g = strchr(glob, '.');
    if (!g) return path;
    size_t n = strcspn(g, " ");
    return path + std::string(g, n);
}

static std::string native_pick(const char* title, const char* glob, bool save=false,
                               const char* preset) {
    char cmd[2048];

#ifdef _WIN32
    // Windows: Fl_Native_File_Chooser uses Win32 GetOpenFileName natively
    (void)cmd;
    Fl_Native_File_Chooser fc;
    fc.title(title);
    fc.type(save ? Fl_Native_File_Chooser::BROWSE_SAVE_FILE
                 : Fl_Native_File_Chooser::BROWSE_FILE);
    if (!save) {
        std::string exts;
        char tmp[512]; strncpy(tmp, glob, sizeof(tmp)-1);
        char* tok = strtok(tmp, " ");
        while (tok) {
            const char* dot = strchr(tok, '.');
            if (dot && *(dot+1)) { if (!exts.empty()) exts += ","; exts += (dot+1); }
            tok = strtok(nullptr, " ");
        }
        std::string filter = "Firmware Files\t*.{" + exts + "}\n";
        fc.filter(filter.c_str());
    } else {
        fc.filter(save_filter(glob).c_str());
        if (preset) fc.preset_file(preset);
    }
    if (fc.show() == 0 && fc.filename())
    {
        // printf("Selected file: %s\n", fc.filename());
        return fc.filename();
    }
    return "";

#elif defined(__APPLE__)
    // macOS: Fl_Native_File_Chooser uses Cocoa natively
    (void)cmd;
    Fl_Native_File_Chooser fc;
    fc.title(title);
    fc.type(save ? Fl_Native_File_Chooser::BROWSE_SAVE_FILE
                 : Fl_Native_File_Chooser::BROWSE_FILE);
    if (!save) {
        std::string exts;
        char tmp[512]; strncpy(tmp, glob, sizeof(tmp)-1);
        char* tok = strtok(tmp, " ");
        while (tok) {
            const char* dot = strchr(tok, '.');
            if (dot && *(dot+1)) { if (!exts.empty()) exts += ","; exts += (dot+1); }
            tok = strtok(nullptr, " ");
        }
        std::string filter = "Firmware Files\t*.{" + exts + "}\n";
        fc.filter(filter.c_str());
    } else {
        fc.filter(save_filter(glob).c_str());
        if (preset) fc.preset_file(preset);
    }
    if (fc.show() == 0 && fc.filename()) return fc.filename();
    return "";

#else
    // Linux: use Fl_Native_File_Chooser if GTK available (true modal),
    // otherwise zenity -> kdialog -> FLTK fallback.
    unsigned long xwin = 0;
    Fl_Window* mw = Fl::first_window();
#ifndef _WIN32
    if (mw) xwin = fl_xid(mw);
#endif

    if (g_gtk_available) {
        Fl_Native_File_Chooser fc;
        fc.title(title);
        fc.type(save ? Fl_Native_File_Chooser::BROWSE_SAVE_FILE
                     : Fl_Native_File_Chooser::BROWSE_FILE);
        if (!save) {
            std::string exts;
            char etmp[512]; strncpy(etmp, glob, sizeof(etmp)-1);
            char* etok = strtok(etmp, " ");
            while (etok) {
                const char* dot = strchr(etok, '.');
                if (dot && *(dot+1)) { if (!exts.empty()) exts += ","; exts += (dot+1); }
                etok = strtok(nullptr, " ");
            }
            std::string filter = "Firmware Files\t*.{" + exts + "}\n";
            fc.filter(filter.c_str());
        } else {
            fc.filter(save_filter(glob).c_str());
            if (preset) fc.preset_file(preset);
        }
        if (fc.show() == 0 && fc.filename()) return fc.filename();
        return "";
    }

    // GTK not available: zenity -> kdialog -> FLTK
    if (!save)
        snprintf(cmd, sizeof(cmd),
            "zenity --file-selection --modal"
            " --title='%s'"
            " --file-filter='Firmware Files | %s'"
            " --file-filter='All Files | *'"
            "%s"
            " 2>/dev/null",
            title, glob,
            xwin ? (std::string(" --attach=") + std::to_string(xwin)).c_str() : "");
    else {
        // One --file-filter per extension (first is default), then All Files
        std::string zf;
        char ztmp[512]; strncpy(ztmp, glob, sizeof(ztmp)-1); ztmp[sizeof(ztmp)-1] = 0;
        for (char* t = strtok(ztmp, " "); t; t = strtok(nullptr, " ")) {
            const char* d = strchr(t, '.');
            if (!d || !*(d+1)) continue;
            std::string up = d + 1;
            for (auto& ch : up) ch = (char)toupper((unsigned char)ch);
            zf += " --file-filter='" + up + " Files | " + t + "'";
        }
        zf += " --file-filter='All Files | *'";
        if (preset) zf += std::string(" --filename='") + preset + "'";
        snprintf(cmd, sizeof(cmd),
            "zenity --file-selection --save --confirm-overwrite --modal"
            " --title='%s'"
            "%s"
            "%s"
            " 2>/dev/null",
            title, zf.c_str(),
            xwin ? (std::string(" --attach=") + std::to_string(xwin)).c_str() : "");
    }
    FILE* p = popen(cmd, "r");
    if (p) {
        char buf[4096] = {};
        bool got = (fgets(buf, sizeof(buf), p) != nullptr);
        int rc = pclose(p);
        if (mw) { mw->activate(); Fl::check(); }
        if (rc != 127) {
            if (got && buf[0]) { buf[strcspn(buf,"\n")]=0; return buf; }
            return "";
        }
    } else { if (mw) { mw->activate(); Fl::check(); } }
    // kdialog (KDE)
    if (mw) { mw->deactivate(); Fl::check(); }
    if (!save)
        snprintf(cmd, sizeof(cmd),
            "kdialog --getopenfilename . '%s' --title '%s' 2>/dev/null", glob, title);
    else
        snprintf(cmd, sizeof(cmd),
            "kdialog --getsavefilename './%s' '%s' --title '%s' 2>/dev/null",
            preset ? preset : "", glob, title);
    p = popen(cmd, "r");
    if (p) {
        char buf[4096] = {};
        bool got = (fgets(buf, sizeof(buf), p) != nullptr);
        int rc = pclose(p);
        if (rc != 127) {
            if (got && buf[0]) { buf[strcspn(buf,"\n")]=0; return buf; }
            return "";
        }
    }
    // Fallback: Fl_Native_File_Chooser
    Fl_Native_File_Chooser fc;
    fc.title(title);
    fc.type(save ? Fl_Native_File_Chooser::BROWSE_SAVE_FILE
                 : Fl_Native_File_Chooser::BROWSE_FILE);
    if (!save) {
        std::string exts;
        char tmp[512]; strncpy(tmp, glob, sizeof(tmp)-1);
        char* tok = strtok(tmp, " ");
        while (tok) {
            const char* dot = strchr(tok, '.');
            if (dot && *(dot+1)) { if (!exts.empty()) exts += ","; exts += (dot+1); }
            tok = strtok(nullptr, " ");
        }
        std::string filter = "Firmware Files\t*.{" + exts + "}\n";
        fc.filter(filter.c_str());
    } else {
        fc.filter(save_filter(glob).c_str());
        if (preset) fc.preset_file(preset);
    }
    if (fc.show() == 0 && fc.filename()) return fc.filename();
    return "";
#endif
}
// ── MD5 (RFC 1321, public domain reference implementation) ──────────────────
namespace md5impl {
    typedef uint32_t u32;
    struct Ctx { u32 a,b,c,d; uint64_t len; uint8_t buf[64]; size_t buflen; };

    static inline u32 rol(u32 x, int c){ return (x<<c)|(x>>(32-c)); }

    static void transform(Ctx& ctx, const uint8_t block[64]) {
        static const u32 K[64] = {
            0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
            0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
            0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
            0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
            0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
            0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
            0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
            0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391 };
        static const int S[64] = {
            7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
            5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
            4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
            6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21 };
        u32 M[16];
        for (int i=0;i<16;i++)
            M[i] = (u32)block[i*4] | ((u32)block[i*4+1]<<8) |
                   ((u32)block[i*4+2]<<16) | ((u32)block[i*4+3]<<24);
        u32 A=ctx.a,B=ctx.b,C=ctx.c,D=ctx.d;
        for (int i=0;i<64;i++) {
            u32 F; int g;
            if      (i<16){ F=(B&C)|(~B&D);      g=i; }
            else if (i<32){ F=(D&B)|(~D&C);      g=(5*i+1)&15; }
            else if (i<48){ F=B^C^D;             g=(3*i+5)&15; }
            else         { F=C^(B|~D);           g=(7*i)&15; }
            u32 tmp=D; D=C; C=B;
            B = B + rol(A + F + K[i] + M[g], S[i]);
            A = tmp;
        }
        ctx.a+=A; ctx.b+=B; ctx.c+=C; ctx.d+=D;
    }

    static void init(Ctx& c){ c.a=0x67452301; c.b=0xefcdab89; c.c=0x98badcfe; c.d=0x10325476; c.len=0; c.buflen=0; }

    static void update(Ctx& c, const uint8_t* data, size_t n){
        c.len += n;
        while (n) {
            size_t take = 64 - c.buflen; if (take > n) take = n;
            memcpy(c.buf + c.buflen, data, take);
            c.buflen += take; data += take; n -= take;
            if (c.buflen == 64){ transform(c, c.buf); c.buflen = 0; }
        }
    }

    static void final(Ctx& c, uint8_t out[16]){
        uint64_t bits = c.len * 8;
        uint8_t pad = 0x80;
        update(c, &pad, 1);
        uint8_t zero = 0;
        while (c.buflen != 56) update(c, &zero, 1);
        uint8_t lenbytes[8];
        for (int i=0;i<8;i++) lenbytes[i] = (uint8_t)(bits >> (8*i));
        // update() would re-add to len; write length block manually
        memcpy(c.buf + 56, lenbytes, 8);
        transform(c, c.buf);
        u32 parts[4] = {c.a,c.b,c.c,c.d};
        for (int i=0;i<4;i++){
            out[i*4+0]=(uint8_t)(parts[i]);
            out[i*4+1]=(uint8_t)(parts[i]>>8);
            out[i*4+2]=(uint8_t)(parts[i]>>16);
            out[i*4+3]=(uint8_t)(parts[i]>>24);
        }
    }
} // namespace md5impl

// ── Helpers ──────────────────────────────────────────────────────────────────
// "2.40 MB (2,516,582 bytes)"
static std::string format_size(unsigned long long bytes) {
    char grp[32]; snprintf(grp, sizeof(grp), "%llu", bytes);
    std::string c; int n = (int)strlen(grp);
    for (int i = 0; i < n; i++) { if (i > 0 && (n - i) % 3 == 0) c += ','; c += grp[i]; }
    char out[96];
    if      (bytes >= 1024ULL*1024*1024) snprintf(out,sizeof(out),"%.2f GB (%s bytes)",bytes/(1024.0*1024*1024),c.c_str());
    else if (bytes >= 1024ULL*1024)      snprintf(out,sizeof(out),"%.2f MB (%s bytes)",bytes/(1024.0*1024),c.c_str());
    else if (bytes >= 1024ULL)           snprintf(out,sizeof(out),"%.2f KB (%s bytes)",bytes/1024.0,c.c_str());
    else                                 snprintf(out,sizeof(out),"%s bytes",c.c_str());
    return out;
}

// Elapsed operation time, rounded to whole seconds: "42 s" or "2 min 05 s"
static std::string format_elapsed(double s) {
    char out[32];
    int t = (int)(s + 0.5);
    if (t < 60) snprintf(out, sizeof(out), "%d s", t);
    else        snprintf(out, sizeof(out), "%d min %02d s", t / 60, t % 60);
    return out;
}

// Pick the data format from the file extension
static FileFormat format_from_ext(const std::string& p) {
    size_t d = p.find_last_of('.');
    std::string ext = (d == std::string::npos) ? "" : p.substr(d + 1);
    for (auto& ch : ext) ch = (char)tolower((unsigned char)ch);
    if (ext == "hex" || ext == "ihx") return FMT_HEX;
    if (ext == "s19" || ext == "srec" || ext == "mot" || ext == "s28" || ext == "s37") return FMT_S19;
    return FMT_BIN;
}

// Turn dropped text into a filesystem path. Linux/macOS may deliver
// "file:///path%20with%20spaces"; Windows delivers plain paths.
// Only the first file is used if several are dropped.
static std::string parse_dropped_path(const std::string& raw) {
    std::string s = raw;
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    s = s.substr(a);
    size_t e = s.find_first_of("\r\n");
    if (e != std::string::npos) s = s.substr(0, e);
    if (s.compare(0, 7, "file://") == 0) {
        s = s.substr(7);
        auto hv = [](char ch) -> int {
            if (ch >= '0' && ch <= '9') return ch - '0';
            if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
            if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
            return -1;
        };
        std::string d;
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '%' && i + 2 < s.size() && hv(s[i+1]) >= 0 && hv(s[i+2]) >= 0) {
                d += (char)(hv(s[i+1]) * 16 + hv(s[i+2])); i += 2;
            } else d += s[i];
        }
        s = d;
#ifdef _WIN32
        if (s.size() >= 3 && s[0] == '/' && s[2] == ':') s = s.substr(1); // /C:/... -> C:/...
#endif
    }
    size_t b = s.find_last_not_of(" \t\r\n");
    return (b == std::string::npos) ? "" : s.substr(0, b + 1);
}

// Remove trailing 0xFF bytes from a file. If the whole file is 0xFF,
// the file is left untouched and new_sz is set to 0.
static bool trim_trailing_ff(const std::string& path, long long& old_sz, long long& new_sz) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long long sz = ftell(f);
    old_sz = new_sz = sz;
    const long CH = 65536;
    std::vector<uint8_t> buf(CH);
    long long pos = sz, last = -1;
    while (pos > 0 && last < 0) {
        long n = (long)std::min<long long>(CH, pos);
        pos -= n;
        fseek(f, (long)pos, SEEK_SET);
        if (fread(buf.data(), 1, n, f) != (size_t)n) { fclose(f); return false; }
        for (long i = n - 1; i >= 0; i--) if (buf[i] != 0xFF) { last = pos + i; break; }
    }
    fclose(f);
    if (last < 0) { new_sz = 0; return true; }   // entirely blank
    new_sz = last + 1;
    if (new_sz == sz) return true;               // nothing to trim
#ifdef _WIN32
    int fd = _open(path.c_str(), _O_RDWR | _O_BINARY);
    if (fd < 0) return false;
    int r = _chsize_s(fd, new_sz);
    _close(fd);
    return r == 0;
#else
    return truncate(path.c_str(), (off_t)new_sz) == 0;
#endif
}

// Info value box that copies its text to the clipboard when clicked
class CopyBox : public Fl_Box {
public:
    CopyBox(int x, int y, int w, int h, const char* l = 0) : Fl_Box(x, y, w, h, l) {}
    bool copyable() const {
        const char* l = label();
        return l && *l && strcmp(l, "—") != 0 && strcmp(l, "Not found") != 0;
    }
    int handle(int e) override {
        switch (e) {
            case FL_ENTER: if (copyable()) window()->cursor(FL_CURSOR_HAND); return 1;
            case FL_LEAVE: window()->cursor(FL_CURSOR_DEFAULT); return 1;
            case FL_PUSH:  return 1;
            case FL_RELEASE:
                if (copyable() && Fl::event_inside(this)) {
                    Fl::copy(label(), (int)strlen(label()), 1); // 1 = system clipboard
                    do_callback();
                }
                return 1;
        }
        return Fl_Box::handle(e);
    }
};

// Log display that accepts dropped files
class DropLogDisplay : public Fl_Text_Display {
public:
    std::function<void(const std::string&)> on_drop;
    DropLogDisplay(int x, int y, int w, int h) : Fl_Text_Display(x, y, w, h) {}
    int handle(int e) override {
        switch (e) {
            case FL_DND_ENTER: case FL_DND_DRAG:
            case FL_DND_LEAVE: case FL_DND_RELEASE:
                return 1;                          // accept the drag
            case FL_PASTE: {                       // the drop delivers its data here
                std::string s(Fl::event_text(), Fl::event_length());
                if (on_drop) on_drop(s);
                return 1;
            }
        }
        return Fl_Text_Display::handle(e);
    }
};

// ── InfoPanel ─────────────────────────────────────────────────────────────────
class InfoPanel : public Fl_Group {
    static const int HDR_H=20, ROW_H=19, PAD=2;
public:
    InfoPanel(int x,int y,int w,int h,const char* title) : Fl_Group(x,y,w,h) {
        box(FL_BORDER_BOX); color(COL_PANEL_BG);
        Fl_Box* hdr=new Fl_Box(x,y,w,HDR_H,title);
        hdr->box(FL_FLAT_BOX); hdr->color(COL_PANEL_HDR);
        hdr->labelfont(FL_HELVETICA_BOLD); hdr->labelsize(11);
        hdr->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE|FL_ALIGN_CLIP);
        end();
    }
    // col/total_cols: allows two-column layout within one panel
    Fl_Box* add_val(int row, int col, int total_cols, const char* lbl, bool copyable = false) {
        int cw  = (w()-PAD*2)/total_cols;
        int cx  = x()+PAD + col*cw;
        int cy  = y()+HDR_H+PAD + row*ROW_H;
        int lw  = 66;
        Fl_Box* lb=new Fl_Box(cx,cy,lw,ROW_H-2,lbl);
        lb->align(FL_ALIGN_RIGHT|FL_ALIGN_INSIDE|FL_ALIGN_CLIP); lb->labelsize(11);
        Fl_Box* vb = copyable ? new CopyBox(cx+lw,cy,cw-lw-4,ROW_H-2,"—")
                              : new Fl_Box (cx+lw,cy,cw-lw-4,ROW_H-2,"—");
        vb->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE|FL_ALIGN_CLIP);
        vb->labelcolor(COL_BLUE_VAL);
        vb->labelfont(FL_HELVETICA_BOLD); vb->labelsize(11);
        if (copyable) vb->tooltip("Click to copy");
        return vb;
    }
    static void set(Fl_Box* b, const char* s) {
        if (!b) return;
        b->copy_label(s && *s ? s : "—"); b->redraw();
    }
};

// ── System info (System Info panel + Save Log header) ────────────────────────
struct SysInfo { std::string os, kernel, cpu, arch, ram, libusb; };

// Strip (R)/(TM) and collapse whitespace in a CPU brand string
static std::string tidy_cpu(std::string s) {
    for (const char* t : {"(R)", "(r)", "(TM)", "(tm)"}) {
        size_t p;
        while ((p = s.find(t)) != std::string::npos) s.erase(p, strlen(t));
    }
    std::string o; bool sp = false;
    for (char c : s) {
        if (c==' ' || c=='\t' || c=='\n' || c=='\r') { sp = !o.empty(); continue; }
        if (sp) o += ' ';
        sp = false; o += c;
    }
    return o;
}

#if !defined(_WIN32) && !defined(__APPLE__)
// Map Linux uname() machine names to the spelling app_arch() uses
static std::string norm_arch(const std::string& a) {
    if (a == "aarch64" || a == "arm64") return "arm64";
    if (a == "amd64"   || a == "x86_64") return "x86_64";
    if (a.size() == 4 && a[0] == 'i' && a.compare(2, 2, "86") == 0) return "x86";
    if (a.compare(0, 3, "arm") == 0) return "arm";
    return a;
}
#endif

// Architecture this binary was compiled for
static const char* app_arch() {
#if defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#elif defined(__arm__)
    return "arm";
#else
    return "unknown";
#endif
}

static std::string format_ram(unsigned long long b) {
    if (!b) return "";
    double g = b / 1073741824.0;
    char t[32]; snprintf(t, sizeof(t), g >= 10 ? "%.0f GB" : "%.1f GB", g);
    return t;
}

#ifdef _WIN32
static std::string reg_str(const char* key, const char* val) {
    HKEY h; std::string r;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, key, 0, KEY_READ, &h) == ERROR_SUCCESS) {
        char buf[256] = {}; DWORD sz = sizeof(buf) - 1, type = 0;
        if (RegQueryValueExA(h, val, nullptr, &type, (LPBYTE)buf, &sz) == ERROR_SUCCESS
            && type == REG_SZ) r = buf;
        RegCloseKey(h);
    }
    return r;
}
static DWORD reg_dword(const char* key, const char* val) {
    HKEY h; DWORD r = 0;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, key, 0, KEY_READ, &h) == ERROR_SUCCESS) {
        DWORD sz = sizeof(r), type = 0;
        if (RegQueryValueExA(h, val, nullptr, &type, (LPBYTE)&r, &sz) != ERROR_SUCCESS
            || type != REG_DWORD) r = 0;
        RegCloseKey(h);
    }
    return r;
}
#elif defined(__APPLE__)
static std::string sysctl_str(const char* name) {
    char b[256] = {}; size_t sz = sizeof(b) - 1;
    if (sysctlbyname(name, b, &sz, nullptr, 0) != 0) return "";
    return b;
}
static long long sysctl_num(const char* name) {
    unsigned char b[8] = {}; size_t sz = sizeof(b);
    if (sysctlbyname(name, b, &sz, nullptr, 0) != 0) return -1;
    if (sz == 4) { int32_t i; memcpy(&i, b, 4); return i; }
    if (sz == 8) { int64_t i; memcpy(&i, b, 8); return i; }
    return -1;
}
#endif

static SysInfo collect_sysinfo() {
    SysInfo si;
    std::string os_arch, emu_note;
    unsigned long long ram = 0;
#ifdef _WIN32
    {
        // RtlGetVersion reports the real version; GetVersionExW says 6.2 build 9200
        // on Windows 10/11 unless the exe carries a compatibility manifest.
        RTL_OSVERSIONINFOW vi = {}; vi.dwOSVersionInfoSize = sizeof(vi);
        typedef LONG (WINAPI *RtlGetVersion_t)(PRTL_OSVERSIONINFOW);
        HMODULE nt = GetModuleHandleW(L"ntdll.dll");
        RtlGetVersion_t rgv = nt ? (RtlGetVersion_t)(void*)GetProcAddress(nt, "RtlGetVersion") : nullptr;
        bool have_ver = rgv && rgv(&vi) == 0;

        const char* cv = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
        std::string name = reg_str(cv, "ProductName");          // e.g. "Windows 10 Pro"
        if (name.empty()) name = "Windows";
        // ProductName still says "Windows 10" on Windows 11 (build 22000+)
        if (have_ver && vi.dwBuildNumber >= 22000 && name.compare(0, 10, "Windows 10") == 0)
            name.replace(0, 10, "Windows 11");
        std::string rel = reg_str(cv, "DisplayVersion");        // e.g. "24H2"
        if (rel.empty()) rel = reg_str(cv, "ReleaseId");
        si.os = rel.empty() ? name : name + " " + rel;

        if (have_ver) {
            char t[64];
            DWORD ubr = reg_dword(cv, "UBR");
            if (ubr) snprintf(t, sizeof(t), "NT %lu.%lu.%lu.%lu", (unsigned long)vi.dwMajorVersion,
                              (unsigned long)vi.dwMinorVersion, (unsigned long)vi.dwBuildNumber,
                              (unsigned long)ubr);
            else     snprintf(t, sizeof(t), "NT %lu.%lu.%lu", (unsigned long)vi.dwMajorVersion,
                              (unsigned long)vi.dwMinorVersion, (unsigned long)vi.dwBuildNumber);
            si.kernel = t;
        }

        si.cpu = reg_str("HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", "ProcessorNameString");

        // IsWow64Process2 gives the true machine type even for an x64 build on ARM64
        // (GetNativeSystemInfo reports AMD64 there). Not present before Windows 10 1511.
        typedef BOOL (WINAPI *IW2_t)(HANDLE, USHORT*, USHORT*);
        HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
        IW2_t iw2 = k32 ? (IW2_t)(void*)GetProcAddress(k32, "IsWow64Process2") : nullptr;
        USHORT pm = 0, nm = 0;
        if (iw2 && iw2(GetCurrentProcess(), &pm, &nm))
            os_arch = nm == 0xAA64 ? "arm64" : nm == 0x8664 ? "x86_64" : nm == 0x014C ? "x86" : "";
        if (os_arch.empty()) {
            SYSTEM_INFO ns; GetNativeSystemInfo(&ns);
            switch (ns.wProcessorArchitecture) {
                case 9:  os_arch = "x86_64"; break;   // PROCESSOR_ARCHITECTURE_AMD64
                case 12: os_arch = "arm64";  break;   // PROCESSOR_ARCHITECTURE_ARM64
                case 0:  os_arch = "x86";    break;   // PROCESSOR_ARCHITECTURE_INTEL
                default: os_arch = "unknown";
            }
        }
        emu_note = " (emulated)";

        MEMORYSTATUSEX ms = {}; ms.dwLength = sizeof(ms);
        if (GlobalMemoryStatusEx(&ms)) ram = ms.ullTotalPhys;
    }
#elif defined(__APPLE__)
    {
        std::string ver = sysctl_str("kern.osproductversion");   // e.g. "15.1"
        // No API gives the marketing name — map the version. Unknown (newer)
        // versions just show the number.
        int maj = atoi(ver.c_str()), mnr = 0;
        if (const char* d = strchr(ver.c_str(), '.')) mnr = atoi(d + 1);
        const char* nick = nullptr;
        switch (maj) {
            case 10: nick = mnr == 13 ? "High Sierra" : mnr == 14 ? "Mojave" :
                            mnr == 15 ? "Catalina"    : mnr == 16 ? "Big Sur" : nullptr; break;
            case 11: nick = "Big Sur";  break;
            case 12: nick = "Monterey"; break;
            case 13: nick = "Ventura";  break;
            case 14: nick = "Sonoma";   break;
            case 15: nick = "Sequoia";  break;
            case 26: nick = "Tahoe";    break;   // version jumped 15 → 26 in 2025
            case 27: nick = "Golden Gate"; break;
        }
        si.os = "macOS";
        if (nick) si.os += std::string(" ") + nick;
        if (!ver.empty()) si.os += " " + ver;
        struct utsname u; uname(&u);
        si.kernel = std::string(u.sysname) + " " + u.release;
        si.cpu = sysctl_str("machdep.cpu.brand_string");         // "Apple M3", "Intel(R) Core(TM) …"
        // Under Rosetta uname() says x86_64; these report the real hardware
        bool arm_hw = sysctl_num("sysctl.proc_translated") == 1 || sysctl_num("hw.optional.arm64") == 1;
        os_arch = arm_hw ? "arm64" : "x86_64";
        emu_note = " (Rosetta)";
        long long m = sysctl_num("hw.memsize");
        if (m > 0) ram = (unsigned long long)m;
    }
#else
    {
        struct utsname u; uname(&u);
        si.kernel = std::string(u.sysname) + " " + u.release;
        os_arch = norm_arch(u.machine);
        if (FILE* f = fopen("/etc/os-release", "r")) {
            char line[256];
            while (fgets(line, sizeof(line), f)) {
                if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
                    char* p = line + 12;
                    if (*p == '"') p++;
                    size_t len = strlen(p);
                    while (len > 0 && (p[len-1]=='"'||p[len-1]=='\n'||p[len-1]=='\r')) p[--len]=0;
                    si.os = p;
                    break;
                }
            }
            fclose(f);
        }
        if (si.os.empty()) si.os = "Linux";
        // x86 has "model name"; many ARM boards only have "Model" (board) or "Hardware"
        if (FILE* f = fopen("/proc/cpuinfo", "r")) {
            char line[512]; std::string model, hw;
            while (fgets(line, sizeof(line), f)) {
                char* c = strchr(line, ':');
                if (!c) continue;
                std::string k(line, c - line);
                while (!k.empty() && (k.back()==' ' || k.back()=='\t')) k.pop_back();
                if      (k == "model name" && si.cpu.empty()) si.cpu = c + 1;
                else if (k == "Model"      && model.empty())  model  = c + 1;
                else if (k == "Hardware"   && hw.empty())     hw     = c + 1;
            }
            fclose(f);
            if (si.cpu.empty()) si.cpu = !model.empty() ? model : hw;
        }
        long pages = sysconf(_SC_PHYS_PAGES), psz = sysconf(_SC_PAGESIZE);
        if (pages > 0 && psz > 0) ram = (unsigned long long)pages * (unsigned long long)psz;
    }
#endif
    si.cpu = tidy_cpu(si.cpu);
    unsigned n = std::thread::hardware_concurrency();
    if (n) {
        char t[32]; snprintf(t, sizeof(t), "%s(%u CPUs)", si.cpu.empty() ? "" : " ", n);
        si.cpu += t;
    }

    std::string app = app_arch();
    si.arch = os_arch;
    if (!os_arch.empty() && app != os_arch) si.arch += ", app " + app + emu_note;

    si.ram = format_ram(ram);

    const struct libusb_version* lv = libusb_get_version();
    if (lv) {
        char t[48]; snprintf(t, sizeof(t), "%u.%u.%u%s", lv->major, lv->minor, lv->micro,
                             lv->rc ? lv->rc : "");
        si.libusb = t;
    }
    return si;
}

static Fl_Text_Display::Style_Table_Entry g_log_styles[6];
// Forward declaration
static void apply_theme(bool dark);

// ── MainWindow ────────────────────────────────────────────────────────────────
class MainWindow : public Fl_Window {
public:
    Fl_Choice*       inp_file;
    Fl_Button*       btn_browse;
    Fl_Choice*       cho_clk;
    Fl_Check_Button* chk_erase, *chk_verify, *chk_trim;
    Fl_Button*       btn_detect,*btn_blank,*btn_erase,
                    *btn_prog,  *btn_verify,*btn_cancel,*btn_read;
    Fl_Button*       btn_theme;
    Fl_Progress*     progress;
    Fl_Box*          lbl_status;
    Fl_Text_Display* log_disp;
    Fl_Text_Buffer*  log_buf;
    Fl_Text_Buffer*  log_style_buf;

    // System Info
    Fl_Box *inf_sys_os, *inf_sys_kernel, *inf_sys_cpu, *inf_sys_arch, *inf_sys_ram, *inf_sys_usb;
    SysInfo sysinfo;   // also written into the Save Log header
    // File Info
    Fl_Box *inf_fn, *inf_fsz, *inf_fmod, *inf_fcrc, *inf_fck, *inf_fmd5;
    // Programmer Info
    Fl_Box *inf_ptype, *inf_pfw, *inf_pfpga, *inf_phw,
           *inf_pvcc,  *inf_pspi, *inf_pio;
    // Memory Info
    Fl_Box *inf_mtype, *inf_mmanuf, *inf_mmid, *inf_mjedec,
           *inf_mvcc,  *inf_msz,   *inf_mpg,  *inf_msec;

    std::string cur_file;

    MainWindow(int W,int H) : Fl_Window(W,H,"DediProg Software") {
        begin();
        int M=8;

        // ── Header (full width) ───────────────────────────────────────────────
        Fl_Box* hb=new Fl_Box(0,0,W,34); hb->box(FL_FLAT_BOX); hb->color(COL_HEADER);
        Fl_Box* ht=new Fl_Box(12,4,W-194,26,"DediProg  SF100/SF600  GUI");
        ht->labelcolor(FL_WHITE); ht->labelfont(FL_HELVETICA_BOLD);
        ht->labelsize(14); ht->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE);
        // Theme toggle button
        btn_theme = new Fl_Button(W-182,4,84,26,"Dark Mode");
        btn_theme->box(FL_FLAT_BOX);
        btn_theme->color(COL_HEADER);
        btn_theme->labelcolor(fl_rgb_color(0xAA,0xCC,0xFF));
        btn_theme->labelfont(FL_HELVETICA);
        btn_theme->labelsize(12);
        btn_theme->callback([](Fl_Widget* wb, void* v){
            g_dark_mode = !g_dark_mode;
            wb->copy_label(g_dark_mode ? "Light Mode" : "Dark Mode");
            apply_theme(g_dark_mode);
            // Directly update checkboxes via MainWindow pointer
            MainWindow* mw = (MainWindow*)v;
            Fl_Color tick = g_dark_mode ? fl_rgb_color(0x4F,0xC3,0xF7) : fl_rgb_color(0x00,0x55,0xBB);
        }, this);
        // About button — top right of header
        Fl_Button* btn_about = new Fl_Button(W-90,4,82,26,"About");
        btn_about->box(FL_FLAT_BOX);
        btn_about->color(COL_HEADER);
        btn_about->labelcolor(fl_rgb_color(0xAA,0xCC,0xFF));
        btn_about->labelfont(FL_HELVETICA);
        btn_about->labelsize(12);
        btn_about->callback([](Fl_Widget*,void*){
            const int DW=440, DH=310, PAD=16;
            Fl_Window* dlg = new Fl_Window(DW, DH, "About DediProg GUI");
            dlg->begin();

            int y = PAD;

            // App name
            Fl_Box* name = new Fl_Box(PAD, y, DW-PAD*2, 28, "DediProg SF100/SF600 GUI  v1.1.0");
            name->labelfont(FL_HELVETICA_BOLD); name->labelsize(14);
            name->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE);
            y += 32;

            // Description
            Fl_Box* desc = new Fl_Box(PAD, y, DW-PAD*2, 90,
                "A native cross-platform GUI for DediProg SF100/SF600\n"
                "SPI NOR flash programmers, built on SF100Linux V1.14.21.x.\n"
                "Supports chip detection, programming, verification,\n"
                "erasing, blank check and read operations with\n"
                "real-time progress tracking.");
            desc->labelsize(12);
            desc->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE|FL_ALIGN_TOP);
            y += 96;

            // Developer info
            Fl_Box* dev = new Fl_Box(PAD, y, DW-PAD*2, 20, "Developer:  Lagnajeet Pradhan");
            dev->labelsize(12); dev->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE);
            y += 22;

            // Email link
            Fl_Button* email = new Fl_Button(PAD, y, DW-PAD*2, 20, "");
            email->box(FL_NO_BOX);
            email->labelsize(12); email->labelcolor(fl_rgb_color(0x00,0x55,0xBB));
            email->labelfont(FL_HELVETICA); email->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE);
            email->copy_label("Email:      lagnajeet@@gmail.com");
            email->callback([](Fl_Widget*,void*){
#ifdef _WIN32
                ShellExecuteA(NULL,"open","mailto:lagnajeet@gmail.com",NULL,NULL,SW_SHOWNORMAL);
#elif defined(__APPLE__)
                system("open mailto:lagnajeet@gmail.com");
#else
                system("xdg-open mailto:lagnajeet@gmail.com &");
#endif
            }, nullptr);
            y += 22;

            // GitHub link
            Fl_Button* gh = new Fl_Button(PAD, y, DW-PAD*2, 20, "GitHub:     https://github.com/lagnajeet");
            gh->box(FL_NO_BOX);
            gh->labelsize(12); gh->labelcolor(fl_rgb_color(0x00,0x55,0xBB));
            gh->labelfont(FL_HELVETICA); gh->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE);
            gh->callback([](Fl_Widget*,void*){
#ifdef _WIN32
                ShellExecuteA(NULL,"open","https://github.com/lagnajeet",NULL,NULL,SW_SHOWNORMAL);
#elif defined(__APPLE__)
                system("open https://github.com/lagnajeet");
#else
                system("xdg-open https://github.com/lagnajeet &");
#endif
            }, nullptr);
            y += 30;

            // Footer
            Fl_Box* foot = new Fl_Box(PAD, y, DW-PAD*2, 36,
                "Built with FLTK and libusb.\n"
                "SF100Linux courtesy of DediProg Software Co., Ltd.");
            foot->labelsize(11); foot->labelcolor(fl_rgb_color(0x66,0x66,0x66));
            foot->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE|FL_ALIGN_TOP);
            y += 42;

            // Close button
            Fl_Button* ok = new Fl_Button(DW/2-40, y, 80, 26, "Close");
            ok->callback([](Fl_Widget*,void* dlgv){ ((Fl_Window*)dlgv)->hide(); }, dlg);

            dlg->callback([](Fl_Widget* w,void*){ w->hide(); });
            dlg->end();
            dlg->set_modal();
            center_over_parent(dlg);
            dlg->show();
            while (dlg->shown()) Fl::wait();
            delete dlg;
        }, nullptr);
        int y=38;

        // ── Controls — full width ─────────────────────────────────────────────
        // File row — combo shows recent files, Load File button opens full dialog
        new Fl_Box(M,y+3,36,22,"File:");
        inp_file=new Fl_Choice(M+40,y,W-M-40-106-M,26);
        inp_file->textsize(11);
        inp_file->callback(cb_file_changed,this); inp_file->when(FL_WHEN_CHANGED);
        btn_browse=new Fl_Button(W-106-M,y,100,26,"Load File…");
        btn_browse->callback(cb_browse,this); y+=32;

        // Clock + options
        new Fl_Box(M,y+3,46,22,"Clock:");
        cho_clk=new Fl_Choice(M+50,y,110,26);
        cho_clk->add("Auto|0.2 MHz|0.4 MHz|1 MHz|2 MHz|4 MHz|8 MHz|12 MHz|16 MHz|24 MHz");
        cho_clk->value(0);
        chk_erase =new Fl_Check_Button(M+170,y+3,110,20,"Erase first");
        chk_verify=new Fl_Check_Button(M+284,y+3,120,20,"Verify after");
        chk_trim  =new Fl_Check_Button(M+410,y+3,190,20,"Trim blank (FF) on read");
        chk_trim->tooltip("After Read, remove trailing 0xFF (erased) bytes from the saved file");
        chk_erase->value(1); chk_verify->value(1); chk_trim->value(1); y+=32;

        // Operation buttons — full width
        static const char* labels[]={"Detect","Blank Check","Erase","Program","Verify","Read","Cancel"};
        Fl_Button** btns[]={&btn_detect,&btn_blank,&btn_erase,
                            &btn_prog,&btn_verify,&btn_read,&btn_cancel};
        Fl_Callback* cbs[]={cb_detect,cb_blank,cb_erase,
                            cb_prog,cb_verify,cb_read,cb_cancel};
        int bw=(W-M*2-24)/7;
        for(int i=0;i<7;i++){
            *btns[i]=new Fl_Button(M+i*(bw+4),y,bw,30,labels[i]);
            (*btns[i])->callback(cbs[i],this); (*btns[i])->labelsize(12);
        }
        btn_erase->color(fl_rgb_color(185,45,45));  btn_erase->labelcolor(FL_WHITE);
        btn_prog->color(fl_rgb_color(25,85,165));   btn_prog->labelcolor(FL_WHITE);
        btn_cancel->color(fl_rgb_color(140,60,10)); btn_cancel->labelcolor(FL_WHITE);
        btn_cancel->deactivate();  // enabled only while an operation is running
        btn_detect->tooltip("Probe the connected chip and select from matching database entries");
        btn_blank->tooltip("Check if the chip contains only 0xFF bytes (fully erased)");
        btn_erase->tooltip("Erase the entire chip (all bytes set to 0xFF)");
        btn_prog->tooltip("Write the loaded file to the chip");
        btn_verify->tooltip("Compare chip contents against the loaded file");
        btn_read->tooltip("Read the entire chip and save to a file");
        btn_cancel->tooltip("Abort the current operation (may require power-cycle after)");
        y+=36;

        // Progress + status — full width
        progress=new Fl_Progress(M,y,W-M*2,18);
        progress->minimum(0); progress->maximum(100); progress->value(0);
        progress->color(fl_rgb_color(0xDD,0xDD,0xDD));
        progress->selection_color(fl_rgb_color(25,85,165));
        progress->labelsize(10); progress->labelcolor(FL_WHITE);
        progress->labelfont(FL_HELVETICA_BOLD); y+=21;
        // Status label and Clear Log button on the same row
        lbl_status=new Fl_Box(M,y,W-M*2-140,18,"Ready — connect programmer and click Detect");
        lbl_status->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE); lbl_status->labelsize(11);
        y+=20;

        // ── Below status: Log (left) + Info panels (right) ───────────────────
        // Right column is 345px (≈5% wider than before → log is ~5% narrower).
        const int RW = 345;
        const int LW = W - RW - M*3;
        const int LX = M;
        const int RX = M*2 + LW;
        const int BOT = H - y - M;   // total remaining height

        // Log — left side, fills full remaining height
        log_buf=new Fl_Text_Buffer();
        log_style_buf=new Fl_Text_Buffer();
        // Clear Log button — right-aligned to log box, same row as status label
        Fl_Button* btn_clear = new Fl_Button(LX+LW-64, y-20, 64, 18, "Clear Log");
        btn_clear->labelsize(10);
        btn_clear->callback([](Fl_Widget*,void* v){
            MainWindow* w=(MainWindow*)v;
            w->log_buf->text("");
            w->log_style_buf->text("");
        }, this);
        // Save Log button — left of Clear Log
        Fl_Button* btn_savelog = new Fl_Button(LX+LW-64-4-64, y-20, 64, 18, "Save Log…");
        btn_savelog->labelsize(10);
        btn_savelog->tooltip("Save the log to a text file");
        btn_savelog->callback([](Fl_Widget*,void* v){ ((MainWindow*)v)->save_log(); }, this);
        DropLogDisplay* dld = new DropLogDisplay(LX,y,LW,BOT);
        dld->on_drop = [this](const std::string& raw){
            std::string p = parse_dropped_path(raw);
            if (!p.empty()) load_path(p);
        };
        log_disp = dld;
        log_disp->buffer(log_buf);
        log_disp->textfont(FL_COURIER); log_disp->textsize(11);
        log_disp->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS,0);
        // Rich-text style table
        // A=normal  B=error(red)  C=success(green)  D=info(blue)  E=warning(orange)  F=dim(grey)
        g_log_styles[0] = { fl_rgb_color(0x22,0x22,0x22), FL_COURIER,      11 };
        g_log_styles[1] = { fl_rgb_color(0xCC,0x00,0x00), FL_COURIER_BOLD, 11 };
        g_log_styles[2] = { fl_rgb_color(0x00,0x88,0x00), FL_COURIER_BOLD, 11 };
        g_log_styles[3] = { fl_rgb_color(0x00,0x55,0xBB), FL_COURIER,      11 };
        g_log_styles[4] = { fl_rgb_color(0xAA,0x66,0x00), FL_COURIER_BOLD, 11 };
        g_log_styles[5] = { fl_rgb_color(0x88,0x88,0x88), FL_COURIER,      11 };
        log_disp->highlight_data(log_style_buf, g_log_styles, 6, 'A', nullptr, nullptr);
        resizable(log_disp);

        // ── RIGHT COLUMN: 4 info panels stacked ──────────────────────────────
        int ry = y;

        // System Info — 5 data rows (RAM and libusb share the last row)
        int os_h = 20 + 5*19 + 8;
        InfoPanel* pos=new InfoPanel(RX,ry,RW,os_h,"System Info"); pos->begin();
        inf_sys_os    =pos->add_val(0,0,1,"OS:");
        inf_sys_kernel=pos->add_val(1,0,1,"Kernel:");
        inf_sys_cpu   =pos->add_val(2,0,1,"CPU:");
        inf_sys_arch  =pos->add_val(3,0,1,"Arch:");
        inf_sys_ram   =pos->add_val(4,0,2,"RAM:");
        inf_sys_usb   =pos->add_val(4,1,2,"libusb:");
        pos->end();
        ry += os_h + M;

        // File Info — 6 data rows
        int fi_h = 20 + 6*19 + 8;
        InfoPanel* pfi=new InfoPanel(RX,ry,RW,fi_h,"File Info"); pfi->begin();
        inf_fn  =pfi->add_val(0,0,1,"Name :",true);
        inf_fsz =pfi->add_val(1,0,1,"Size :",true);
        inf_fmod=pfi->add_val(2,0,1,"Modified:",true);
        inf_fcrc=pfi->add_val(3,0,1,"CRC32 :",true);
        inf_fck =pfi->add_val(4,0,1,"Checksum:",true);
        inf_fmd5=pfi->add_val(5,0,1,"MD5 :",true);
        pfi->end();
        for (Fl_Box* b : {inf_fn, inf_fsz, inf_fmod, inf_fcrc, inf_fck, inf_fmd5})
            b->callback([](Fl_Widget* wd, void* v){
                ((MainWindow*)v)->log(std::string("Copied to clipboard: ") + wd->label());
            }, this);
        ry += fi_h + M;

        // Programmer Info — 4 data rows × 2 cols
        int pi_h = 20 + 4*19 + 8;
        InfoPanel* ppi=new InfoPanel(RX,ry,RW,pi_h,"Programmer Info"); ppi->begin();
        inf_ptype=ppi->add_val(0,0,2,"Type:");
        inf_pfw  =ppi->add_val(1,0,2,"Firmware:");
        inf_pfpga=ppi->add_val(2,0,2,"FPGA Ver:");
        inf_phw  =ppi->add_val(3,0,2,"HW Ver:");
        inf_pvcc =ppi->add_val(0,1,2,"VCC:");
        inf_pspi =ppi->add_val(1,1,2,"SPI Clock:");
        inf_pio  =ppi->add_val(2,1,2,"IO Mode:");
        ppi->end();
        ry += pi_h + M;

        // Memory Info — 4 data rows × 2 cols
        InfoPanel* pmi=new InfoPanel(RX,ry,RW,pi_h,"Memory Info"); pmi->begin();
        inf_mtype =pmi->add_val(0,0,2,"Type:");
        inf_mmanuf=pmi->add_val(1,0,2,"Manufact.:");
        inf_mmid  =pmi->add_val(2,0,2,"Manu. ID:");
        inf_mjedec=pmi->add_val(3,0,2,"JEDEC ID:");
        inf_mvcc  =pmi->add_val(0,1,2,"Chip VCC:");
        inf_msz   =pmi->add_val(1,1,2,"Size(KB):");
        inf_mpg   =pmi->add_val(2,1,2,"Page(B):");
        inf_msec  =pmi->add_val(3,1,2,"Sector(B):");
        pmi->end();

        end();

        // Fill System Info (same data goes into the Save Log header)
        sysinfo = collect_sysinfo();
        const std::pair<Fl_Box*, const std::string*> sys_rows[] = {
            {inf_sys_os, &sysinfo.os}, {inf_sys_kernel, &sysinfo.kernel},
            {inf_sys_cpu, &sysinfo.cpu}, {inf_sys_arch, &sysinfo.arch},
            {inf_sys_ram, &sysinfo.ram}, {inf_sys_usb, &sysinfo.libusb}};
        for (const auto& r : sys_rows) {
            InfoPanel::set(r.first, r.second->c_str());
            if (!r.second->empty()) r.first->copy_tooltip(r.second->c_str());  // full text when clipped
        }
    }

    // ── Logging ───────────────────────────────────────────────────────────────
    void log(const std::string& msg) {
        std::lock_guard<std::mutex> lk(g_log_mutex);
        time_t now = time(nullptr); struct tm* ti = localtime(&now);
        char ts[12]; strftime(ts, sizeof(ts), "%H:%M:%S  ", ti);
        std::string stamped = std::string(ts) + msg;
        const std::string& msg_ref = stamped;
        // Determine style character for this line
        char sc = 'A'; // default: normal
        // Check first non-space UTF-8 char or known prefixes
        const char* p = msg_ref.c_str();
        while (*p == ' ') p++;
        // UTF-8 check_marks: ✓ = E2 9C 93, ✗ = E2 9C 97, ⚠ = E2 9A A0
        // ─── separator = E2 94 80
        if ((unsigned char)p[0]==0xE2 && (unsigned char)p[1]==0x9C && (unsigned char)p[2]==0x93)
            sc = 'C'; // ✓ success
        else if ((unsigned char)p[0]==0xE2 && (unsigned char)p[1]==0x9C && (unsigned char)p[2]==0x97)
            sc = 'B'; // ✗ error
        else if ((unsigned char)p[0]==0xE2 && (unsigned char)p[1]==0x9A && (unsigned char)p[2]==0xA0)
            sc = 'E'; // ⚠ warning
        else if ((unsigned char)p[0]==0xE2 && (unsigned char)p[1]==0x94 && (unsigned char)p[2]==0x80)
            sc = 'F'; // ─── separator
        else if (msg.find("FAILED")!=std::string::npos || msg.find("Error")!=std::string::npos
                 || msg.find("error")!=std::string::npos || msg.find("Warning")!=std::string::npos
                 || msg.find("not found")!=std::string::npos || msg.find("not identified")!=std::string::npos)
            sc = 'B'; // error red
        else if (msg.find("OK")!=std::string::npos || msg.find("ok")!=std::string::npos
                 || msg.find("Selected:")!=std::string::npos || msg.find("Detect OK")!=std::string::npos)
            sc = 'C'; // success green
        else if (msg.find("Device ")!=std::string::npos || msg.find("File:")!=std::string::npos
                 || msg.find("Format:")!=std::string::npos || msg.find("USB OK")!=std::string::npos)
            sc = 'D'; // info blue
        // Append text + matching style bytes (one style byte per char incl newline)
        std::string line = msg_ref + "\n";
        std::string style(line.size(), sc);
        log_buf->append(line.c_str());
        log_style_buf->append(style.c_str());
        log_disp->scroll(log_buf->count_lines(0,log_buf->length()),0);
        log_disp->redraw();
    }
    void set_status(const char* s){ lbl_status->copy_label(s); lbl_status->redraw(); }
    void set_ui_busy(bool busy) {
        if (busy) {
            Fl::focus(log_disp);
            btn_detect->deactivate(); btn_blank->deactivate();
            btn_erase->deactivate();  btn_prog->deactivate();
            btn_verify->deactivate(); btn_read->deactivate();
            btn_browse->deactivate(); inp_file->deactivate();
            cho_clk->deactivate();    chk_erase->deactivate();
            chk_verify->deactivate();
            chk_trim->deactivate();
        } else {
            btn_detect->activate();   btn_blank->activate();
            btn_erase->activate();    btn_prog->activate();
            btn_verify->activate();   btn_read->activate();
            btn_browse->activate();   inp_file->activate();
            cho_clk->activate();      chk_erase->activate();
            chk_verify->activate();
            chk_trim->activate();
        }
#ifdef __APPLE__
        // macOS FLTK 1.4: flush immediately after widget state changes so
        // AppKit processes all _NSMenuShortcutUpdater events in one batch
        // before the next run loop iteration schedules more.
        Fl::flush();
#endif
    }
    // Real progress: shows "N%" with colour that flips at 50% so it
    // is always readable against either the filled or unfilled bar.
    void set_progress(int v) {
        progress->value((float)v);
        if (v > 0 && v < 100) {
            static char pct_lbl[8];
            snprintf(pct_lbl, sizeof(pct_lbl), "%d%%", v);
            progress->label(pct_lbl);
            // In dark mode the unfilled track is dark — use light text throughout
            // In light mode flip at 50%: dark text on grey track, white on blue fill
            if (g_dark_mode)
                progress->labelcolor(FL_WHITE);
            else
                progress->labelcolor(v >= 50 ? FL_WHITE : fl_rgb_color(0x22,0x22,0x22));
        } else {
            progress->label(nullptr);
        }
        progress->redraw();
    }
    // Marquee animation: no label, just moves the bar silently
    void set_progress_marquee(int v) {
        progress->label(nullptr);
        progress->value((float)v);
        progress->redraw();
    }

    // ── Pipe output processing ────────────────────────────────────────────────
    // The SF100 library progress loop does:  printf("\r%0.1fs elapsed", t)
    // — \r-terminated, no \n.  Real status lines (Erasing…, Erase OK, etc.)
    // use \n.  Strategy:
    //   • Accumulate bytes in g_pipe_buf (file-scope) across read() calls.
    //   • On \r: elapsed-time token — extract seconds, drive progress bar, discard.
    //   • On \n: real log line — trim and emit.

    // ── Per-phase timing ──────────────────────────────────────────────────────
    // The library prints "Erasing, please wait ..." ... "Erase OK" (etc.) for each
    // phase. Time the gap and append it to the result line: "Erase OK (2.8 s)".
    std::chrono::steady_clock::time_point phase_t0;
    bool phase_on    = false;
    int  phase_count = 0;      // phases timed in the current operation
    void phase_reset() { phase_on = false; phase_count = 0; }
    std::string phase_time(const std::string& line) {
        static const char* starts[] = {
            "Erasing, please wait", "Programming, please wait", "Verifying, please wait",
            "Reading, please wait", "Checking, please wait" };
        static const char* ends[] = {
            "Erase OK", "Error: Erase Failed", "Program OK", "Error: Program Failed",
            "Verify OK", "Error: Verify Failed", "Read OK", "Read Failed",
            "The chip is blank", "The chip is NOT blank" };
        for (const char* s : starts)
            if (line.compare(0, strlen(s), s) == 0) {
                phase_t0 = std::chrono::steady_clock::now(); phase_on = true;
                return line;
            }
        if (phase_on)
            for (const char* e : ends)
                if (line.compare(0, strlen(e), e) == 0) {
                    double s = std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - phase_t0).count();
                    phase_on = false; phase_count++;
                    return line + " (" + format_elapsed(s) + ")";
                }
        return line;
    }

    void process_pipe_chunk(const char* data, ssize_t len) {
        for(ssize_t i=0;i<len;i++){
            char c=data[i];
            if(c=='\r'){
                // Elapsed-time noise — discard silently (progress driven by wall-clock)
                g_pipe_buf.clear();
            } else if(c=='\n'){
                // Real status line — trim and log
                size_t a=g_pipe_buf.find_first_not_of(" \t");
                if(a!=std::string::npos){
                    size_t b=g_pipe_buf.find_last_not_of(" \t\r\n");
                    std::string line=g_pipe_buf.substr(a,b-a+1);
                    if(!line.empty()) log(phase_time(line));
                }
                g_pipe_buf.clear();
            } else {
                g_pipe_buf+=c;
            }
        }
    }

    // ── Pipe timer — drains stdout only, no progress logic ───────────────────
    static void pipe_timer_cb(void* v) {
        auto* w=(MainWindow*)v;
        if (g_pipe_rd >= 0) {
            char buf[4096]; int n;
#ifdef _WIN32
            DWORD avail = 0;
            while (PeekNamedPipe((HANDLE)_get_osfhandle(g_pipe_rd),NULL,0,NULL,&avail,NULL) && avail > 0) {
                n = _read(g_pipe_rd, buf, (int)std::min((DWORD)(sizeof(buf)-1), avail));
#else
            while((n=read(g_pipe_rd,buf,sizeof(buf)-1))>0){
#endif
                if (n <= 0) break;
                buf[n]='\0';
                w->process_pipe_chunk(buf,n);
            }
        }
        if(g_pipe_active.load()) Fl::repeat_timeout(0.05, pipe_timer_cb, v);
    }

    // ── Progress timer — polls g_sf_progress from the worker thread ──────────
    // When total > 0 (program, read, sector erase): shows real % progress.
    // When total == 0 (whole-chip erase — single WIP poll, no byte loop):
    //   falls back to a marquee animation so the bar still moves.
    static void progress_timer_cb(void* v) {
        auto* w = (MainWindow*)v;
        bool running = g_running.load();

        // Only toggle cancel button when state changes to avoid
        // AppKit _NSMenuShortcutUpdater crash on macOS FLTK 1.4
        static bool last_running = false;
        if (running != last_running) {
            last_running = running;
            if (running) w->btn_cancel->activate();
            else         w->btn_cancel->deactivate();
            if (!running && w->cancel_anim) {   // operation ended — restore Cancel
                w->cancel_anim = false;
                w->btn_cancel->label("Cancel");
                w->btn_cancel->color(fl_rgb_color(140,60,10));
                w->btn_cancel->redraw();
            }
        }

        // "Stopping…" pulse while a cancel waits for the current 16 MB block
        if (running && w->cancel_anim) {
            static int ph = 0;
            ph = (ph + 1) % 24;                                  // 24 ticks × 50 ms = 1.2 s cycle
            float t = (ph < 12 ? ph : 24 - ph) / 12.0f;          // 0 → 1 → 0
            w->btn_cancel->color(fl_color_average(fl_rgb_color(240,150,40),
                                                  fl_rgb_color(140,60,10), t));
            w->btn_cancel->redraw();
        }

        if (running) {
            size_t done  = g_sf_progress.done;
            size_t total = g_sf_progress.total;

            if (total > 0) {
                int pct = (int)(done * 100 / total);
                if (pct > 100) pct = 100;
                w->set_progress(pct);
            } else {
                // Marquee fallback for whole-chip erase (no byte loop)
                // Increment every 2nd tick (100ms effective) = ~40% slower than before
                static int mq_skip = 0;
                if (++mq_skip >= 2) { mq_skip = 0; g_marquee_tick = (g_marquee_tick + 1) % 101; }
                int val = g_marquee_tick;
                w->set_progress_marquee(val);
            }
        }
        Fl::repeat_timeout(0.05, progress_timer_cb, v);
    }

    // ── Drain remaining pipe after op ─────────────────────────────────────────
    void drain_and_stop() {
        if (g_pipe_rd >= 0) {
            char buf[4096]; int n;
#ifdef _WIN32
            DWORD avail = 0;
            while (PeekNamedPipe((HANDLE)_get_osfhandle(g_pipe_rd),NULL,0,NULL,&avail,NULL) && avail > 0) {
                n = _read(g_pipe_rd, buf, (int)std::min((DWORD)(sizeof(buf)-1), avail));
#else
            while((n=read(g_pipe_rd,buf,sizeof(buf)-1))>0){
#endif
                if (n <= 0) break;
                buf[n]='\0';
                process_pipe_chunk(buf,n);
            }
        }
        capture_stop();
#ifdef _WIN32
        if (g_pipe_rd >= 0) { _close(g_pipe_rd); g_pipe_rd=-1; }
#else
        if (g_pipe_rd >= 0) { close(g_pipe_rd); g_pipe_rd=-1; }
#endif
    }

    // ── File info ─────────────────────────────────────────────────────────────
    void refresh_file_info(const std::string& path) {
        if(path.empty()){
            for(Fl_Box* b:{inf_fn,inf_fsz,inf_fmod,inf_fcrc,inf_fck,inf_fmd5})
                InfoPanel::set(b,nullptr);
            return;
        }
        // Extract filename -- handle both / and \ separators
        const char* fn = path.c_str();
        const char* sl = strrchr(fn, '/');
        const char* bs = strrchr(fn, '\\');
        if (bs && (!sl || bs > sl)) sl = bs;
        if (sl) fn = sl + 1;
        InfoPanel::set(inf_fn, fn);
        struct stat st;
        if(stat(path.c_str(),&st)!=0){
            // File doesn't exist — show name but mark rest as not found
            InfoPanel::set(inf_fsz,  "Not found");
            InfoPanel::set(inf_fmod, "Not found");
            InfoPanel::set(inf_fcrc, "Not found");
            InfoPanel::set(inf_fck,  "Not found");
            InfoPanel::set(inf_fmd5, "Not found");
            return;
        }
        char tmp[128];
        // Human-readable size + exact byte count with thousands separators
        InfoPanel::set(inf_fsz, format_size((unsigned long long)st.st_size).c_str());
        struct tm* t=localtime(&st.st_mtime);
        strftime(tmp,sizeof(tmp),"%Y-%m-%d %H:%M",t);
        InfoPanel::set(inf_fmod, tmp);
        // CRC32 + byte checksum + MD5 — single pass over the file
        FILE* f=fopen(path.c_str(),"rb");
        if(f){
            static uint32_t tbl[256]; static bool init=false;
            if(!init){ for(int i=0;i<256;i++){uint32_t c=i;for(int j=0;j<8;j++)c=(c&1)?(0xEDB88320^(c>>1)):(c>>1);tbl[i]=c;}init=true;}
            uint32_t crc=0xFFFFFFFF,ck=0;
            md5impl::Ctx md5; md5impl::init(md5);
            uint8_t buf[4096]; size_t n;
            while((n=fread(buf,1,sizeof(buf),f))>0){
                for(size_t i=0;i<n;i++){crc=tbl[(crc^buf[i])&0xFF]^(crc>>8);ck+=buf[i];}
                md5impl::update(md5, buf, n);
            }
            fclose(f); crc^=0xFFFFFFFF;
            uint8_t dig[16]; md5impl::final(md5, dig);
            char tmp[48];
            snprintf(tmp,sizeof(tmp),"0x%08X",crc);  InfoPanel::set(inf_fcrc,tmp);
            snprintf(tmp,sizeof(tmp),"0x%08X",ck);   InfoPanel::set(inf_fck, tmp);
            char md5str[33];
            for(int i=0;i<16;i++) snprintf(md5str+i*2,3,"%02X",dig[i]);
            InfoPanel::set(inf_fmd5, md5str);
        }
    }

    // Load a file from drag-and-drop (or any path). Format defaults to Raw
    // Binary; .hex/.ihx and .s19/.srec/.mot are recognised by extension.
    void load_path(const std::string& p) {
        if (g_running.load()) { log("⚠  Operation in progress — file not loaded."); return; }
        struct stat st;
        if (stat(p.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
            log("✗  Cannot load (not a file): " + p);
            return;
        }
        g_file_format = format_from_ext(p);
        recent_add(p);
        bool found = false;
        for (int i = 0; i < inp_file->size()-1; i++)
            if (inp_file->text(i) && std::string(inp_file->text(i)) == p) { inp_file->value(i); found = true; break; }
        if (!found) { inp_file->insert(0, fltk_escape(p).c_str(), 0, nullptr); inp_file->value(0); }
        inp_file->redraw();   // value(0) after insert(0) doesn't redraw by itself
        cur_file = p;
        refresh_file_info(p);
        log("File: " + p);
        const char* fmtname[] = {"Auto","Raw Binary","Intel Hex","Motorola S19"};
        log(std::string("Format: ") + fmtname[(int)g_file_format]);
    }

    // Compare file size to chip size before Program/Verify.
    // Returns false if the user cancels.
    bool confirm_size(const std::string& fp) {
        if (!g_chip_selected || Chip_Info.ChipSizeInByte == 0) return true;
        FileFormat eff = (g_file_format == FMT_AUTO) ? format_from_ext(fp) : g_file_format;
        if (eff != FMT_BIN) return true;   // hex/s19 size on disk != image size
        struct stat st;
        if (stat(fp.c_str(), &st) != 0) return true;
        unsigned long long fsz = st.st_size, csz = Chip_Info.ChipSizeInByte;
        std::string fs = format_size(fsz), cs = format_size(csz);
        if (fsz > csz) {
            if (g_truncate) {
                log("⚠  File (" + fs + ") is larger than chip (" + cs + ") — will be truncated.");
                return true;
            }
            int c = fl_choice("The file is larger than the chip.\n\nFile: %s\nChip: %s\n\n"
                              "Data past the end of the chip cannot be written.\nContinue anyway?",
                              "Cancel", "Continue", nullptr, fs.c_str(), cs.c_str());
            if (c != 1) { log("✗  Cancelled — file is larger than chip."); return false; }
            log("⚠  File is larger than chip — continuing at user request.");
        } else if (fsz < csz) {
            log("File size " + fs + ", chip size " + cs + ".");
        }
        return true;
    }

    // Write the log buffer to a text file chosen by the user
    void save_log() {
        std::string path = ensure_ext(native_pick("Save Log As", "*.txt", true, "dpgui_log.txt"), "*.txt");
        if (path.empty()) return;
        FILE* f = fopen(path.c_str(), "w");   // text mode: CRLF on Windows
        if (!f) { log("✗  Could not write log: " + path); return; }
        char ts[64]; time_t now = time(nullptr);
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&now));
        fprintf(f, "DediProg SF100/SF600 GUI v1.1.0 — log saved %s\n", ts);
        auto dash = [](const std::string& s) { return s.empty() ? "—" : s.c_str(); };
        fprintf(f, "OS:      %s\n", dash(sysinfo.os));
        fprintf(f, "Kernel:  %s\n", dash(sysinfo.kernel));
        fprintf(f, "CPU:     %s\n", dash(sysinfo.cpu));
        fprintf(f, "Arch:    %s\n", dash(sysinfo.arch));
        fprintf(f, "RAM:     %s\n", dash(sysinfo.ram));
        fprintf(f, "libusb:  %s\n\n", dash(sysinfo.libusb));
        char* txt = log_buf->text();
        if (txt) { fputs(txt, f); free(txt); }
        bool ok = (fclose(f) == 0);
        log(ok ? "✓  Log saved: " + path : "✗  Could not write log: " + path);
    }

    // ── Programmer info ───────────────────────────────────────────────────────
    void refresh_prog_info() {
        if(!g_usb_open) return;
        char tmp[64];
        const char* pt="SF100";
        if     (g_bIsSF700[0])    pt="SF700";
        else if(g_bIsSF600PG2[0]) pt="SF600Plus-G2";
        else if(g_bIsSF600[0])    pt="SF600Plus";
        InfoPanel::set(inf_ptype, pt);
        // Update window title with programmer model
        char wt[64]; snprintf(wt, sizeof(wt), "DediProg Software  --  %s", pt);
        copy_label(wt);

        GetFirmwareVer(0);
        char fw[11]={}; memcpy(fw,g_FW_ver,10);
        InfoPanel::set(inf_pfw, *fw?fw:nullptr);

        unsigned int fpga=GetFPGAVersion(0);
        if(fpga!=(unsigned int)-1){ snprintf(tmp,sizeof(tmp),"0x%04X",fpga); InfoPanel::set(inf_pfpga,tmp); }
        else InfoPanel::set(inf_pfpga,"N/A");

        char hw[9]={}; memcpy(hw,g_HW_ver,8);
        InfoPanel::set(inf_phw, *hw?hw:nullptr);

        // VCC status — use chip VCC if known
        if(Chip_Info.VoltageInMv>0)
            snprintf(tmp,sizeof(tmp),"%.1f V / OFF",Chip_Info.VoltageInMv/1000.0);
        else if(g_Vcc==0x12) snprintf(tmp,sizeof(tmp),"1.8 V / OFF");
        else if(g_Vcc==0x11) snprintf(tmp,sizeof(tmp),"2.5 V / OFF");
        else                  snprintf(tmp,sizeof(tmp),"3.5 V / OFF");
        InfoPanel::set(inf_pvcc, tmp);

        snprintf(tmp,sizeof(tmp),"%u MHz", g_ucSPIClock?g_ucSPIClock:12);
        InfoPanel::set(inf_pspi, tmp);
        InfoPanel::set(inf_pio, "Single IO");
    }

    // ── Memory info ───────────────────────────────────────────────────────────
    void refresh_mem_info() {
        if(!Chip_Info.UniqueID) return;
        char tmp[64];
        InfoPanel::set(inf_mtype, *Chip_Info.TypeName?Chip_Info.TypeName:nullptr);

        // Manufacturer lookup (JEDEC byte)
        uint8_t mfr=(Chip_Info.UniqueID>>16)&0xFF;
        const char* manuf="Unknown";
        switch(mfr){
            case 0x20: manuf="Micron(Numonyx)"; break;
            case 0xEF: manuf="Winbond";         break;
            case 0xC2: manuf="Macronix";         break;
            case 0x01: manuf="Spansion";         break;
            case 0xBF: manuf="SST";              break;
            case 0x9D: manuf="ISSI";             break;
            case 0xA1: manuf="Fudan";            break;
            case 0x1F: manuf="Atmel";            break;
        }
        InfoPanel::set(inf_mmanuf, manuf);
        snprintf(tmp,sizeof(tmp),"0x%02X",mfr);             InfoPanel::set(inf_mmid, tmp);
        snprintf(tmp,sizeof(tmp),"0x%04lX",Chip_Info.UniqueID&0xFFFF); InfoPanel::set(inf_mjedec,tmp);

        if(Chip_Info.VoltageInMv>0)
            snprintf(tmp,sizeof(tmp),"%.1f V",Chip_Info.VoltageInMv/1000.0);
        else strcpy(tmp,"3.3 V");
        InfoPanel::set(inf_mvcc, tmp);

        if(Chip_Info.ChipSizeInByte>0){
            snprintf(tmp,sizeof(tmp),"%zu",Chip_Info.ChipSizeInByte/1024);
            InfoPanel::set(inf_msz,tmp);
        }
        if(Chip_Info.PageSizeInByte>0){
            snprintf(tmp,sizeof(tmp),"%zu",Chip_Info.PageSizeInByte);
            InfoPanel::set(inf_mpg,tmp);
        }
        if(Chip_Info.SectorSizeInByte>0){
            snprintf(tmp,sizeof(tmp),"%zu",Chip_Info.SectorSizeInByte);
            InfoPanel::set(inf_msec,tmp);
        }
    }

    // ── Apply settings ────────────────────────────────────────────────────────
    void apply_settings() {
        // VCC: always auto — let the chip database and detection determine it.
        // If chip is already known, preset g_Vcc so it's applied correctly.
        g_parameter_vcc = const_cast<char*>("NO");
        if(Chip_Info.VoltageInMv>0){
            if     (Chip_Info.VoltageInMv<=1800) g_Vcc=0x12;
            else if(Chip_Info.VoltageInMv<=2500) g_Vcc=0x11;
            else                                  g_Vcc=0x10;
        }
        static const unsigned int clk_vals[]={12,0,0,1,2,4,8,12,16,24};
        int ci=cho_clk->value();
        g_ucSPIClock=clk_vals[ci<10?ci:0];
    }

    // ── USB init ──────────────────────────────────────────────────────────────
    bool init_usb() {
        bool reconnect=false;
        if(g_usb_open){
            if(usb_is_alive(0)) return true;
            // Programmer was unplugged (and maybe plugged back in): the old
            // libusb handle is stale. Release it and open the device again.
            log("⚠  Programmer was disconnected — reconnecting…");
            usb_driver_release();
            g_usb_open=false;
            reconnect=g_chip_selected;
            g_chip_selected=false;   // programmer lost its chip settings
        }
        GetLogPath(g_LogPath);
        log("Initialising USB…");
        if(OpenUSB()){
            LeaveStandaloneMode(0);
            QueryBoard(0);
            g_usb_open=true;
            prog_uid=ReadUID(0);     // remembered to check recovery after a cancel
            log("USB OK — programmer found");
            refresh_prog_info();
            if(reconnect) log("⚠  Programmer was reconnected — chip selection cleared, click Detect first.");
        } else {
            log("USB FAILED — check connection and udev rules");
        }
        return g_usb_open;
    }

    // ── Check after Cancel ────────────────────────────────────────────────────
    // The library now only stops Read/Program between 16 MB blocks, so the
    // programmer should be idle after a cancel. As a safety net, check its
    // serial number reads back correctly (it reads 0xFFFFFF while the firmware
    // is stuck mid-transfer, e.g. after cancelling a variant that still stops
    // mid-block). Runs on the main thread when a cancelled operation finished.
    bool         cancel_requested = false;
    bool         cancel_anim      = false;   // "Stopping…" pulse on the Cancel button
    unsigned int prog_uid         = 0;
    void recover_after_cancel() {
        if(!cancel_requested) return;
        cancel_requested=false;
        if(!g_usb_open) return;
        unsigned int uid=ReadUID(0);
        bool ok = uid!=0 && uid!=0xFFFFFF && (prog_uid==0 || uid==prog_uid);
        if(!ok) log("⚠  Programmer did not recover — unplug and replug it before the next operation.");
    }

    // ── Generic async op ──────────────────────────────────────────────────────
    void run_async(const std::string& name,
                   std::function<void()> setup,
                   std::function<void(bool)> done=nullptr) {
        if(g_running.load()){fl_alert("Operation in progress.");return;}
        if(!init_usb()){fl_alert("Programmer not connected.");return;}
        if(!g_chip_selected){fl_alert("No chip selected.\nClick Detect first.");return;}
        g_running=true;
        set_ui_busy(true);
        set_status((name+"…").c_str()); set_progress(0);
        log("─── "+name+" ───");
        phase_reset();
        // Upstream's clear calls are compiled out (#if 0 in project.c), so a
        // previous Cancel would otherwise make every later operation fail.
        SerialFlash_ClearCancelOperationFlag();
        cancel_requested=false;   // a Cancel confirmed after the last op ended
        apply_settings(); setup();

        g_bDisplayTimer = false;   // silence \r elapsed-time noise from library
        g_marquee_tick  = 0;       // marquee fallback counter (used when total==0)
        g_sf_progress.done  = 0;   // reset real progress counters
        g_sf_progress.total = 0;
        g_pipe_active = true;
        capture_start();
        Fl::add_timeout(0.05, pipe_timer_cb,    this);
        // (progress_timer_cb runs permanently from main)

        auto t0 = std::chrono::steady_clock::now();
        std::thread([this,name,done,t0](){
            int r=Handler(); bool ok=(r==EXCODE_PASS);
            double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            fflush(stdout);
            // Close pipe write end first so pipe_timer_cb sees EOF
            // and stops reading. Do NOT hold Fl::lock() during drain
            // to avoid deadlock with pipe_timer_cb on the main thread.
            capture_stop(); // closes pipe_wr, restores stdout
            // Small yield to let pipe_timer_cb drain remaining data
            // and exit on its own before we remove it
#ifdef _WIN32
            Sleep(100); // 100ms yield for pipe drain
#else
            struct timespec ts={0,100000000}; nanosleep(&ts,nullptr); // 100ms
#endif
            struct AsyncDone {
                MainWindow* w; std::string name;
                bool ok; int r;
                std::function<void(bool)> done;
                double secs;
            };
            auto* ad = new AsyncDone{this, name, ok, r, done, secs};
            Fl::awake([](void* ud){
                // Runs on main thread -- safe for AppKit and UI ops
                auto* ad = (AsyncDone*)ud;
                Fl::remove_timeout(pipe_timer_cb, ad->w);
                // Drain any remaining pipe data then close read end
                if (g_pipe_rd >= 0) {
                    // Ensure non-blocking before drain
#ifndef _WIN32
                    fcntl(g_pipe_rd, F_SETFL, O_NONBLOCK);
#endif
                    char buf[4096]; ssize_t n;
                    while((n=read(g_pipe_rd,buf,sizeof(buf)-1))>0){
                        buf[n]='\0';
                        ad->w->process_pipe_chunk(buf,n);
                    }
                    close(g_pipe_rd); g_pipe_rd=-1;
                }
                ad->w->set_progress(100);
                ad->w->set_ui_busy(false);
                std::string el = " (" + format_elapsed(ad->secs) + ")";
                // One timed phase: its line already shows the time. Several: show the total.
                std::string tl = ad->w->phase_count == 1 ? ""
                               : ad->w->phase_count > 1  ? " (total " + format_elapsed(ad->secs) + ")"
                               : el;
                ad->w->log(ad->ok
                    ? ("✓  "+ad->name+" OK"+tl)
                    : ("✗  "+ad->name+" FAILED (code "+std::to_string(ad->r)+")"+tl));
                ad->w->set_status(ad->ok
                    ? (ad->name+" OK"+el).c_str()
                    : (ad->name+" FAILED"+el).c_str());
                ad->w->recover_after_cancel();   // no-op unless Cancel was used
                ad->w->refresh_prog_info();
                ad->w->refresh_mem_info();
                g_running = false;
                if (ad->done) ad->done(ad->ok);
                delete ad;
            }, ad);
        }).detach();
    }

    // ── Callbacks ─────────────────────────────────────────────────────────────
    static void cb_file_changed(Fl_Widget*,void* v){
        auto* w=(MainWindow*)v;
        int idx=w->inp_file->value();
        const char* t=(idx>=0)?w->inp_file->text(idx):"";
        w->cur_file=t?t:"";
        w->refresh_file_info(w->cur_file);
    }
    static void cb_browse(Fl_Widget*,void* v){
        auto* w=(MainWindow*)v;
        std::string p=w->cur_file;
        FileFormat fmt=g_file_format;
        bool trunc=g_truncate;
        size_t csz=g_chip_selected?Chip_Info.ChipSizeInByte:0;
        if(!show_load_file_dialog(p,fmt,trunc,csz)) return;
        // Add to combo if not already present
        bool found=false;
        for(int i=0;i<w->inp_file->size()-1;i++)
            if(std::string(w->inp_file->text(i))==p){w->inp_file->value(i);found=true;break;}
        if(!found){w->inp_file->insert(0,fltk_escape(p).c_str(),0,nullptr);w->inp_file->value(0);}
        w->inp_file->redraw();
        w->cur_file=p;
        w->refresh_file_info(p);
        w->log("File: "+p);
        const char* fmtname[]={"Auto","Raw Binary","Intel Hex","Motorola S19"};
        w->log(std::string("Format: ")+fmtname[(int)fmt]+(trunc?" | Truncate to chip size":""));
    }

    // Detect: GetFirstDetectionMatch handles VCC cycling + SetTargetFlash.
    // It never writes its TypeName parameter — check ci.UniqueID for success.
    // After success, strTypeName (global) contains space-separated matches from
    // the last FlashIdentifier call inside GetFirstDetectionMatch.
    // Detect phase 2: runs on main thread after hardware scan completes
    struct DetectResult {
        MainWindow*              w;
        std::vector<std::string> auto_det;
        std::vector<ChipEntry>   all_chips;
        CHIP_INFO                ci;
        unsigned int             uid;
    };
    static void detect_phase2(void* ud) {
        DetectResult* dr = (DetectResult*)ud;
        MainWindow*   w  = dr->w;

        // Show dialog directly on main thread - no Fl::wait nesting issue
        std::string chosen = show_chip_select_dialog(dr->auto_det, dr->all_chips);

        if (chosen.empty()) {
            w->log("\xe2\x9c\x97  Chip selection cancelled.");
            w->set_status("No chip selected");
            g_chip_selected = false;
        } else {
            char name_buf[256]; strncpy(name_buf, chosen.c_str(), sizeof(name_buf)-1);
            if (Dedi_Search_Chip_Db_ByTypeName(name_buf, &Chip_Info))
                strncpy(strTypeName, chosen.c_str(), 1023);
            else if (dr->ci.UniqueID) {
                Chip_Info = dr->ci;
                strncpy(strTypeName, dr->ci.TypeName, 1023);
            }
            if (Chip_Info.VoltageInMv > 0) {
                if      (Chip_Info.VoltageInMv <= 1800) g_Vcc = 0x12;
                else if (Chip_Info.VoltageInMv <= 2500) g_Vcc = 0x11;
                else                                     g_Vcc = 0x10;
            }
            char logmsg[256];
            snprintf(logmsg, sizeof(logmsg), "\xe2\x9c\x93  Selected: %s  \xe2\x80\x94  %zu KB",
                Chip_Info.TypeName, Chip_Info.ChipSizeInByte/1024);
            w->log(logmsg);
            g_chip_selected = true;
            w->log("\xe2\x9c\x93  Detect OK");
            w->set_status("Detect OK");
        }
        w->set_progress(100);
        w->set_ui_busy(false);
        w->refresh_prog_info(); w->refresh_mem_info();
        g_running = false;
        delete dr;
    }

    static void cb_detect(Fl_Widget*,void* v){
        auto* w=(MainWindow*)v;
        if(g_running.load()){fl_alert("Operation in progress.");return;}
        if(!w->init_usb()){fl_alert("Programmer not connected.");return;}

        std::vector<ChipEntry> all_chips = parse_chip_db();

        g_running=true;
        w->set_ui_busy(true);
        w->set_status("Detecting\xe2\x80\xa6"); w->set_progress(0);
        w->log("\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80 Detect \xe2\x94\x80\xe2\x94\x80\xe2\x94\x80");
        w->apply_settings();

        std::thread([w, all_chips](){
            int dcnt = get_usb_dev_cnt();

            for (int i = 0; i < dcnt; i++) {
                CHIP_INFO ci = GetFirstDetectionMatch(strTypeName, i);
                unsigned int uid = ReadUID(i);

                std::vector<std::string> auto_det;
                if (ci.UniqueID) {
                    char buf[1024]; strncpy(buf, strTypeName, sizeof(buf)-1);
                    char* tok = strtok(buf, " ");
                    while (tok) {
                        if (*tok) {
                            bool dup=false;
                            for (auto& u:auto_det) if(u==tok){dup=true;break;}
                            if (!dup) auto_det.push_back(tok);
                        }
                        tok = strtok(nullptr, " ");
                    }
                    if (auto_det.empty()) auto_det.push_back(ci.TypeName);
                    std::sort(auto_det.begin(), auto_det.end());
                    char msg[256];
                    snprintf(msg, sizeof(msg),
                        "Device %d (SF%06u): auto-detected %zu match(es)", i+1, uid, auto_det.size());
                    Fl::lock(); w->log(msg); Fl::unlock();
                } else {
                    char msg[80];
                    snprintf(msg, sizeof(msg),
                        "Device %d: no chip detected - manual selection available.", i+1);
                    Fl::lock(); w->log(msg); Fl::unlock();
                }

                // Post phase 2 to main thread and exit worker thread.
                // This avoids nesting Fl::wait inside an Fl::awake callback (deadlock).
                DetectResult* dr = new DetectResult;
                dr->w         = w;
                dr->auto_det  = auto_det;
                dr->all_chips = all_chips;
                dr->ci        = ci;
                dr->uid       = uid;
                Fl::awake(detect_phase2, dr);
                return; // worker done, main thread takes over
            }

            // No devices found at all
            Fl::lock();
            w->set_progress(100);
            w->set_ui_busy(false);
            g_chip_selected = false;
            w->log("\xe2\x9c\x97  Detect FAILED - chip not identified");
            w->set_status("Detect FAILED");
            w->refresh_prog_info(); w->refresh_mem_info();
            g_running = false;
            Fl::unlock(); Fl::awake();
        }).detach();
    }

        static void cb_blank(Fl_Widget*,void* v){
        ((MainWindow*)v)->run_async("Blank Check",[](){g_ucOperation=BLANK;});
    }
    static void cb_erase(Fl_Widget*,void* v){
        auto* w=(MainWindow*)v;
        if(g_running.load()){fl_alert("Operation in progress.");return;}
        if(!w->init_usb()){fl_alert("Programmer not connected.");return;}
        if(!g_chip_selected){fl_alert("No chip selected.\nClick Detect first.");return;}
        if(fl_choice("Erase the whole chip?","Cancel","Erase",nullptr)!=1)return;
        w->run_async("Erase",[](){g_ucOperation=ERASE;});
    }
    static void cb_prog(Fl_Widget*,void* v){
        auto* w=(MainWindow*)v;
        std::string fp=w->cur_file;
        if(fp.empty()){fl_alert("Select a firmware file first.");return;}
        if(access(fp.c_str(),R_OK)!=0){fl_alert("File not found:\n%s",fp.c_str());return;}
        if(!g_running.load() && !w->confirm_size(fp)) return;
        bool de=w->chk_erase->value(), dv=w->chk_verify->value();
        static std::string sp; sp=fp;
        w->run_async("Program",[de,dv](){
            g_ucOperation=PROGRAM;
            if(de) g_ucOperation|=ERASE;
            if(dv) g_ucOperation|=VERIFY;
            g_uiAddr=0;
            g_uiLen=g_truncate?Chip_Info.ChipSizeInByte:0;
            load_file_with_format(sp.c_str(), g_file_format);
            SaveProgContextChanges();
            g_parameter_program=const_cast<char*>(sp.c_str());
        });
    }
    static void cb_verify(Fl_Widget*,void* v){
        auto* w=(MainWindow*)v;
        std::string fp=w->cur_file;
        if(fp.empty()){fl_alert("Select a firmware file first.");return;}
        if(access(fp.c_str(),R_OK)!=0){fl_alert("File not found:\n%s",fp.c_str());return;}
        if(g_running.load()){fl_alert("Operation in progress.");return;}
        if(!w->init_usb()){fl_alert("Programmer not connected.");return;}
        if(!g_chip_selected){fl_alert("No chip selected.\nClick Detect first.");return;}
        if(!w->confirm_size(fp)) return;

        static std::string sp; sp=fp;
        g_running=true;
        w->set_ui_busy(true);
        w->set_status("Verify…"); w->set_progress(0);
        w->log("─── Verify ───");
        w->phase_reset();
        SerialFlash_ClearCancelOperationFlag();   // see run_async
        w->cancel_requested=false;
        w->apply_settings();

        g_bDisplayTimer=false;
        g_marquee_tick=0;
        g_sf_progress.done  = 0;
        g_sf_progress.total = 0;
        g_pipe_active = true;
        capture_start();
        Fl::add_timeout(0.05, pipe_timer_cb,    w);
        // (progress_timer_cb runs permanently from main)

        auto t0 = std::chrono::steady_clock::now();
        std::thread([w,t0](){
            g_uiAddr = 0;
            g_uiLen  = g_truncate ? Chip_Info.ChipSizeInByte : 0;
            load_file_with_format(sp.c_str(), g_file_format);
            SaveProgContextChanges();
            g_ucOperation = VERIFY;

            int r=Handler(); bool ok=(r==EXCODE_PASS);
            double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            fflush(stdout);
            capture_stop();
            struct timespec ts={0,100000000}; nanosleep(&ts,nullptr);
            struct VDone { MainWindow* w; bool ok; int r; double secs; };
            auto* vd = new VDone{w, ok, r, secs};
            Fl::awake([](void* ud){
                auto* vd = (VDone*)ud;
                Fl::remove_timeout(pipe_timer_cb, vd->w);
                if (g_pipe_rd >= 0) {
#ifndef _WIN32
                    fcntl(g_pipe_rd, F_SETFL, O_NONBLOCK);
#endif
                    char buf[4096]; ssize_t n;
                    while((n=read(g_pipe_rd,buf,sizeof(buf)-1))>0){
                        buf[n]='\0';
                        vd->w->process_pipe_chunk(buf,n);
                    }
                    close(g_pipe_rd); g_pipe_rd=-1;
                }
                vd->w->set_progress(100);
                vd->w->set_ui_busy(false);
                std::string el = " (" + format_elapsed(vd->secs) + ")";
                std::string tl = vd->w->phase_count == 1 ? "" : el;   // phase line already timed
                vd->w->log(vd->ok ? ("✓  Verify OK"+tl)
                    : ("✗  Verify FAILED (code "+std::to_string(vd->r)+")"+tl));
                vd->w->set_status((vd->ok ? "Verify OK"+el : "Verify FAILED"+el).c_str());
                vd->w->recover_after_cancel();   // no-op unless Cancel was used
                vd->w->refresh_prog_info(); vd->w->refresh_mem_info();
                g_running=false;
                delete vd;
            }, vd);
        }).detach();
    }
    static void cb_cancel(Fl_Widget*,void* v){
        // Only reachable when btn_cancel is active (i.e. op in progress)
        if (((MainWindow*)v)->cancel_anim) return;   // already stopping — ignore repeat clicks
        int choice = fl_choice(
            "⚠  WARNING: Cancelling mid-operation can corrupt the chip.\n\n"
            "The programmer and chip may need to be power-cycled before\n"
            "the next operation will succeed.\n\n"
            "Cancel the current operation?",
            "Keep Running", "Cancel Operation", nullptr);
        if(choice != 1) return;   // user chose Keep Running
        if(!g_running.load()) return;   // operation finished while the dialog was open

        SerialFlash_SetCancelOperationFlag();
        auto* w = (MainWindow*)v;
        w->cancel_requested = true;   // check the programmer when the op ends
        // Cancel takes effect at the next 16 MB block. Meanwhile the button reads
        // "Stopping…" and pulses (progress_timer_cb); clicks are ignored above.
        // progress_timer_cb restores it when the operation ends.
        w->cancel_anim = true;
        w->btn_cancel->label("Stopping…");
        w->btn_cancel->redraw();
        w->log("⚠  Cancel requested — Read/Program/Verify stop at the end of the current 16 MB block.");
        w->set_status("Cancelled");
    }
    static void cb_read(Fl_Widget*,void* v){
        auto* w=(MainWindow*)v;
        if(g_running.load()){fl_alert("Operation in progress.");return;}
        if(!w->init_usb()){fl_alert("Programmer not connected.");return;}
        if(!g_chip_selected){fl_alert("No chip selected.\nClick Detect first.");return;}
        // Preset name from the chip type, e.g. "W25Q128JV.img"; default extension .img
        std::string pre = Chip_Info.TypeName[0] ? Chip_Info.TypeName : "read";
        for (auto& ch : pre) if (strchr("/\\:*?\"<>| ", ch)) ch = '_';
        pre += ".img";
        const char* rglob = "*.img *.bin";
        std::string path=ensure_ext(native_pick("Save Read Output As",rglob,true,pre.c_str()), rglob);
        if(path.empty())return;
        static std::string sp; sp=path;
        bool trim = w->chk_trim->value() != 0;
        // Remember whether the file existed before, so a failed Read only
        // deletes a file it actually wrote (the library saves the buffer even
        // when the read failed or was cancelled).
        struct stat pre_st;
        bool   existed   = stat(sp.c_str(), &pre_st) == 0;
        time_t pre_mtime = existed ? pre_st.st_mtime : 0;
        w->run_async("Read",[](){
            g_ucOperation=READ_TO_FILE;
            g_parameter_read=const_cast<char*>(sp.c_str());
        }, [w, trim, existed, pre_mtime](bool ok){
            if (!ok) {
                struct stat st;
                if (stat(sp.c_str(), &st) == 0 && (!existed || st.st_mtime != pre_mtime)) {
                    if (::remove(sp.c_str()) == 0)   // C remove(), not Fl_Group::remove()
                        w->log("✗  Read failed — incomplete file deleted: " + sp);
                    else
                        w->log("⚠  Read failed — could not delete incomplete file: " + sp);
                }
                return;
            }
            if (!trim) return;
            long long o = 0, n = 0;
            if (!trim_trailing_ff(sp, o, n))
                w->log("✗  Could not trim read file.");
            else if (n == 0)
                w->log("⚠  Chip is blank (all 0xFF) — file left untrimmed.");
            else if (n == o)
                w->log("No trailing blank bytes to trim.");
            else
                w->log("✓  Trimmed blank tail: " + format_size(o) + " → " + format_size(n));
        });
    }
};

// ── Theme application ─────────────────────────────
static void apply_theme(bool dark) {
    if (dark) {
        Fl::background (0x1E,0x1E,0x1E);
        Fl::background2(0x3C,0x3C,0x3F); // lighter than window bg so checkbox is visible
        Fl::foreground (0xD4,0xD4,0xD4);
        Fl::set_color(FL_SELECTION_COLOR, 0x4F,0xC3,0xF7); // light blue tick
        COL_HEADER    = COL_HEADER_DARK;
        COL_PANEL_HDR = COL_PANEL_HDR_DARK;
        COL_PANEL_BG  = COL_PANEL_BG_DARK;
        COL_BLUE_VAL  = COL_BLUE_VAL_DARK;
    } else {
        Fl::background (0xF0,0xF0,0xF0);
        Fl::background2(0xFF,0xFF,0xFF);
        Fl::foreground (0x22,0x22,0x22);
        Fl::set_color(FL_SELECTION_COLOR, 0x00,0x55,0xBB); // restore default blue
        COL_HEADER    = COL_HEADER_LIGHT;
        COL_PANEL_HDR = COL_PANEL_HDR_LIGHT;
        COL_PANEL_BG  = COL_PANEL_BG_LIGHT;
        COL_BLUE_VAL  = COL_BLUE_VAL_LIGHT;
    }
    // Update log style colours for theme
    g_log_styles[0].color = dark ? fl_rgb_color(0xCC,0xCC,0xCC) : fl_rgb_color(0x22,0x22,0x22);
    g_log_styles[3].color = dark ? fl_rgb_color(0x4F,0xC3,0xF7) : fl_rgb_color(0x00,0x55,0xBB);
    g_log_styles[4].color = dark ? fl_rgb_color(0xFF,0xB7,0x4D) : fl_rgb_color(0xAA,0x66,0x00);
    g_log_styles[5].color = dark ? fl_rgb_color(0x66,0x66,0x66) : fl_rgb_color(0x88,0x88,0x88);
    // Walk all widgets and recolour panels, value labels, header boxes
    for (Fl_Window* win = Fl::first_window(); win; win = Fl::next_window(win)) {
        // Redraw all children recursively via damage
        win->color(dark ? fl_rgb_color(0x1E,0x1E,0x1E) : fl_rgb_color(0xF0,0xF0,0xF0));
        for (int _pi = 0; _pi < win->children(); _pi++) {
            Fl_Progress* pp = dynamic_cast<Fl_Progress*>(win->child(_pi));
            if (pp) {
                pp->color(dark ? fl_rgb_color(0x3C,0x3C,0x3C) : fl_rgb_color(0xDD,0xDD,0xDD));
                pp->selection_color(dark ? fl_rgb_color(0x15,0x65,0xC0) : fl_rgb_color(25,85,165));
            }
        }
        // Walk every widget in the window
        for (int i = 0; i < win->children(); i++) {
            Fl_Widget* w = win->child(i);
            // Recolour InfoPanel groups and their children
            Fl_Group* g = w->as_group();
            if (g) {
                if (g->color() == (dark ? COL_PANEL_BG_LIGHT : COL_PANEL_BG_DARK))
                    g->color(COL_PANEL_BG);
                for (int j = 0; j < g->children(); j++) {
                    Fl_Widget* cw = g->child(j);
                    // Panel header rows
                    if (cw->color() == (dark ? COL_PANEL_HDR_LIGHT : COL_PANEL_HDR_DARK))
                        cw->color(COL_PANEL_HDR);
                    // Blue value labels
                    if (cw->labelcolor() == (dark ? COL_BLUE_VAL_LIGHT : COL_BLUE_VAL_DARK))
                        cw->labelcolor(COL_BLUE_VAL);
                }
            }
            // Header bar
            if (w->color() == (dark ? COL_HEADER_LIGHT : COL_HEADER_DARK)) {
                w->color(COL_HEADER);
            }
        }
        win->redraw();
    }
}

// ── main ──────────────────────────────────────────────────────────────────────
#if !defined(__APPLE__) && !defined(_WIN32)
static void probe_gtk() {
    void* h = dlopen("libgtk-3.so.0", RTLD_LAZY | RTLD_NOLOAD);
    if (!h) h = dlopen("libgtk-3.so.0", RTLD_LAZY);
    g_gtk_available = (h != nullptr);
    if (h) dlclose(h);
}
#endif

static bool single_instance_check() {
#ifdef _WIN32
    // Windows single instance via named mutex
    HANDLE hMutex = CreateMutexA(NULL, TRUE, "Global\\DpGuiSingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        return false;
    }
    return true;
#else
    int fd = open("/tmp/dpgui.lock", O_CREAT | O_RDWR, 0666);
    if (fd < 0) return true;
    if (flock(fd, LOCK_EX | LOCK_NB) == 0)
        return true; // first instance, lock held
    // Another instance running -- raise it
    close(fd);
#ifdef __APPLE__
    system("osascript -e 'tell app \"System Events\" to set frontmost"
           " of (first process whose name contains \"dpgui\") to true'"           " 2>/dev/null");
#else
    if (system("wmctrl -a \"DediProg\" 2>/dev/null") != 0)
        system("xdotool search --name \"DediProg\" windowactivate 2>/dev/null");
#endif
    return false;
#endif // not _WIN32
}

int main(int argc,char** argv){
#ifdef _WIN32
    if (HWND cw = GetConsoleWindow()) {
        ShowWindow(cw, SW_HIDE);
    }
#endif    
    if (!single_instance_check()) return 0; 
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN); // prevent crash if pipe write end closes unexpectedly
#endif
    Fl::lock();
#if !defined(__APPLE__) && !defined(_WIN32)
    probe_gtk();
#endif
    Fl::scheme("gtk+");
    Fl::background(0xF0,0xF0,0xF0);
    Fl::background2(0xFF,0xFF,0xFF);
    Fl::foreground(0x22,0x22,0x22);
    // Explicitly enable GTK native file chooser.
    // On some distros (Fedora/RHEL) libs are in /lib64 which dlopen may not
    // search by default — hint via LD_LIBRARY_PATH if not already set.
    Fl::option(Fl::OPTION_FNFC_USES_GTK, true);
    {
        const char* ldp = getenv("LD_LIBRARY_PATH");
        std::string newldp = "/usr/lib64:/lib64";
        if (ldp && *ldp) newldp += std::string(":") + ldp;
#ifndef _WIN32
        setenv("LD_LIBRARY_PATH", newldp.c_str(), 1);
#endif
    }
    recent_load();
    int px=100,py=100,pw=1100,ph=680;
    prefs_load(px,py,pw,ph,g_dark_mode);
    MainWindow* win=new MainWindow(pw,ph); 
    win->resizable(win);
    win->size_range(900,560,0,0);
    win->position(px,py);
    win->show(argc,argv);
    #ifdef _WIN32
        HWND hwnd = fl_xid(win);
        HICON hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(1));
        if (hwnd && hIcon) {
            SendMessage(hwnd, WM_SETICON, ICON_BIG,   (LPARAM)hIcon);
            SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        }
    #endif    
    Fl::focus(win->log_disp); // default focus on log, not buttons
    // Populate file combo with recent files
    for(auto& r:g_recent_files) win->inp_file->add(fltk_escape(r).c_str());
    if(win->inp_file->size()>1) { win->inp_file->value(0); win->cur_file=win->inp_file->text(0); win->refresh_file_info(win->cur_file); }
    win->log("Connect your SF100/SF600 via USB, then click Detect.");
    win->log("Tip: drag a firmware file onto this log to load it.");
    if (g_dark_mode) {
        apply_theme(true);
        win->btn_theme->copy_label("Light Mode");
    }
    // Permanent 50ms timer drives Cancel button enable/disable via g_running
    Fl::add_timeout(0.05, MainWindow::progress_timer_cb, win);
    int ret = Fl::run();
    prefs_save(win->x(), win->y(), win->w(), win->h(), g_dark_mode);
    return ret;
}
