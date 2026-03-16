/*
 * dpgui.cpp — DediProg SF100/SF600 Linux GUI (natively integrated)
 *
 * Links directly against SF100Linux V1.14.21.x C sources.
 * Layout mirrors the Windows DediProg SF7 GUI:
 *   - OS Info / File Info / Programmer Info / Memory Info panels
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
#include <FL/Fl_Group.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Select_Browser.H>
#include <FL/Fl_Progress.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <libgen.h>
#include <time.h>
#include <algorithm>
#include <cstdint>
#include <stdint.h>

// ── Override openChipInfoDb ───────────────────────────────────────────────────
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

// ── SF100Linux C API ──────────────────────────────────────────────────────────
extern "C" {
#include "dpcmd.h"
#include "project.h"
#include "usbdriver.h"
#include "Macro.h"
#include "board.h"
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
}

// ── Colours ───────────────────────────────────────────────────────────────────
#define COL_HEADER    fl_rgb_color(0x1A,0x3C,0x6E)
#define COL_PANEL_HDR fl_rgb_color(0xD0,0xDF,0xF0)
#define COL_PANEL_BG  fl_rgb_color(0xF6,0xF8,0xFC)
#define COL_BLUE_VAL  fl_rgb_color(0x00,0x55,0xBB)

// ── stdout capture ────────────────────────────────────────────────────────────
static int g_pipe_rd=-1, g_pipe_wr=-1, g_saved_stdout=-1;
static std::string g_pipe_buf;  // accumulates bytes between \r/\n delimiters

static void capture_start() {
    int fds[2]; pipe(fds);
    g_pipe_rd=fds[0]; g_pipe_wr=fds[1];
    fcntl(g_pipe_rd,F_SETFL,O_NONBLOCK);
    g_saved_stdout=dup(STDOUT_FILENO);
    dup2(g_pipe_wr,STDOUT_FILENO); fflush(stdout);
    g_pipe_buf.clear();
}
static void capture_stop() {
    fflush(stdout);
    dup2(g_saved_stdout,STDOUT_FILENO);
    close(g_saved_stdout); g_saved_stdout=-1;
    close(g_pipe_wr);      g_pipe_wr=-1;
}

// ── Shared state ──────────────────────────────────────────────────────────────
static std::atomic<bool> g_running{false};
static std::mutex        g_log_mutex;
static bool              g_usb_open     = false;
static bool              g_chip_selected = false;  // set true after successful Detect
static int               g_marquee_tick = 0;       // drives progress bar animation (chip-erase fallback)

// ── Native file picker ────────────────────────────────────────────────────────
static std::string native_pick(const char* title, const char* glob, bool save=false) {
    char cmd[1024];
    // Try zenity first
    if (!save)
        snprintf(cmd,sizeof(cmd),
            "zenity --file-selection --title='%s' "
            "--file-filter='Firmware files | %s' 2>/dev/null", title, glob);
    else
        snprintf(cmd,sizeof(cmd),
            "zenity --file-selection --save --confirm-overwrite "
            "--title='%s' 2>/dev/null", title);
    FILE* p=popen(cmd,"r");
    if (p) {
        char buf[4096]={};
        bool ok = (fgets(buf,sizeof(buf),p) != nullptr);
        int rc = pclose(p);
        if (ok && rc==0 && buf[0]) {
            buf[strcspn(buf,"\n")]=0; return buf;
        }
    }
    // Try kdialog
    if (!save)
        snprintf(cmd,sizeof(cmd),
            "kdialog --getopenfilename . '%s' --title '%s' 2>/dev/null",glob,title);
    else
        snprintf(cmd,sizeof(cmd),
            "kdialog --getsavefilename . '*.bin' --title '%s' 2>/dev/null",title);
    p=popen(cmd,"r");
    if (p) {
        char buf[4096]={};
        bool ok = (fgets(buf,sizeof(buf),p) != nullptr);
        int rc = pclose(p);
        if (ok && rc==0 && buf[0]) {
            buf[strcspn(buf,"\n")]=0; return buf;
        }
    }
    // FLTK fallback
    int mode = save ? Fl_File_Chooser::CREATE : Fl_File_Chooser::SINGLE;
    Fl_File_Chooser fc(".", (std::string(glob)+" files ("+glob+")").c_str(), mode, title);
    fc.show(); while(fc.shown()) Fl::wait();
    if (fc.count()>0 && fc.value(1)) return fc.value(1);
    return "";
}

// ── InfoPanel ─────────────────────────────────────────────────────────────────
class InfoPanel : public Fl_Group {
    static const int HDR_H=20, ROW_H=19, PAD=4;
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
    Fl_Box* add_val(int row, int col, int total_cols, const char* lbl) {
        int cw  = (w()-PAD*2)/total_cols;
        int cx  = x()+PAD + col*cw;
        int cy  = y()+HDR_H+PAD + row*ROW_H;
        int lw  = 82;
        Fl_Box* lb=new Fl_Box(cx,cy,lw,ROW_H-2,lbl);
        lb->align(FL_ALIGN_RIGHT|FL_ALIGN_INSIDE|FL_ALIGN_CLIP); lb->labelsize(11);
        Fl_Box* vb=new Fl_Box(cx+lw,cy,cw-lw-4,ROW_H-2,"—");
        vb->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE|FL_ALIGN_CLIP);
        vb->labelcolor(COL_BLUE_VAL);
        vb->labelfont(FL_HELVETICA_BOLD); vb->labelsize(11);
        return vb;
    }
    static void set(Fl_Box* b, const char* s) {
        if (!b) return;
        b->copy_label(s && *s ? s : "—"); b->redraw();
    }
};

// ── MainWindow ────────────────────────────────────────────────────────────────
class MainWindow : public Fl_Window {
public:
    Fl_Input*        inp_file;
    Fl_Button*       btn_browse;
    Fl_Choice*       cho_clk;
    Fl_Check_Button* chk_erase, *chk_verify;
    Fl_Button*       btn_detect,*btn_blank,*btn_erase,
                    *btn_prog,  *btn_verify,*btn_cancel,*btn_read;
    Fl_Progress*     progress;
    Fl_Box*          lbl_status;
    Fl_Text_Display* log_disp;
    Fl_Text_Buffer*  log_buf;

    // OS Info
    Fl_Box* inf_os;
    // File Info
    Fl_Box *inf_fn, *inf_fsz, *inf_fmod, *inf_fcrc, *inf_fck;
    // Programmer Info
    Fl_Box *inf_ptype, *inf_pfw, *inf_pfpga, *inf_phw,
           *inf_pvcc,  *inf_pspi, *inf_pio;
    // Memory Info
    Fl_Box *inf_mtype, *inf_mmanuf, *inf_mmid, *inf_mjedec,
           *inf_mvcc,  *inf_msz,   *inf_mpg,  *inf_msec;

    std::string cur_file;

    MainWindow(int W,int H) : Fl_Window(W,H,"DediProg Software  Linux GUI") {
        begin();
        int M=8;

        // ── Header (full width) ───────────────────────────────────────────────
        Fl_Box* hb=new Fl_Box(0,0,W,34); hb->box(FL_FLAT_BOX); hb->color(COL_HEADER);
        Fl_Box* ht=new Fl_Box(12,4,W-24,26,"DediProg  SF100/SF600  Linux GUI");
        ht->labelcolor(FL_WHITE); ht->labelfont(FL_HELVETICA_BOLD);
        ht->labelsize(14); ht->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE);
        int y=38;

        // ── Controls — full width ─────────────────────────────────────────────
        // File row
        new Fl_Box(M,y+3,36,22,"File:");
        inp_file=new Fl_Input(M+40,y,W-M-40-90-M,26);
        inp_file->callback(cb_file_changed,this); inp_file->when(FL_WHEN_CHANGED);
        btn_browse=new Fl_Button(W-90-M,y,84,26,"Browse…");
        btn_browse->callback(cb_browse,this); y+=32;

        // Clock + options
        new Fl_Box(M,y+3,46,22,"Clock:");
        cho_clk=new Fl_Choice(M+50,y,110,26);
        cho_clk->add("Auto|0.2 MHz|0.4 MHz|1 MHz|2 MHz|4 MHz|8 MHz|12 MHz|16 MHz|24 MHz");
        cho_clk->value(0);
        chk_erase =new Fl_Check_Button(M+170,y+3,110,20,"Erase first");
        chk_verify=new Fl_Check_Button(M+284,y+3,120,20,"Verify after");
        chk_erase->value(1); chk_verify->value(1); y+=32;

        // Operation buttons — full width
        static const char* labels[]={"Detect","Blank","Erase","Program","Verify","Cancel","Read"};
        Fl_Button** btns[]={&btn_detect,&btn_blank,&btn_erase,
                            &btn_prog,&btn_verify,&btn_cancel,&btn_read};
        Fl_Callback* cbs[]={cb_detect,cb_blank,cb_erase,
                            cb_prog,cb_verify,cb_cancel,cb_read};
        int bw=(W-M*2-24)/7;
        for(int i=0;i<7;i++){
            *btns[i]=new Fl_Button(M+i*(bw+4),y,bw,30,labels[i]);
            (*btns[i])->callback(cbs[i],this); (*btns[i])->labelsize(12);
        }
        btn_erase->color(fl_rgb_color(185,45,45));  btn_erase->labelcolor(FL_WHITE);
        btn_prog->color(fl_rgb_color(25,85,165));   btn_prog->labelcolor(FL_WHITE);
        btn_cancel->color(fl_rgb_color(140,60,10)); btn_cancel->labelcolor(FL_WHITE);
        btn_cancel->deactivate();  // enabled only while an operation is running
        y+=36;

        // Progress + status — full width
        progress=new Fl_Progress(M,y,W-M*2,14);
        progress->minimum(0); progress->maximum(100); progress->value(0);
        progress->color(fl_rgb_color(0xDD,0xDD,0xDD));
        progress->selection_color(fl_rgb_color(25,85,165));
        progress->labelsize(10); y+=17;
        lbl_status=new Fl_Box(M,y,W-M*2,16,"Ready — connect programmer and click Detect");
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
        log_disp=new Fl_Text_Display(LX,y,LW,BOT);
        log_disp->buffer(log_buf);
        log_disp->textfont(FL_COURIER); log_disp->textsize(11);
        log_disp->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS,0);
        resizable(log_disp);

        // ── RIGHT COLUMN: 4 info panels stacked ──────────────────────────────
        int ry = y;

        // OS Info — 1 data row
        int os_h = 20 + 1*19 + 8;
        InfoPanel* pos=new InfoPanel(RX,ry,RW,os_h,"OS Info"); pos->begin();
        inf_os=pos->add_val(0,0,1,"OS Version:"); pos->end();
        ry += os_h + M;

        // File Info — 5 data rows
        int fi_h = 20 + 5*19 + 8;
        InfoPanel* pfi=new InfoPanel(RX,ry,RW,fi_h,"File Info"); pfi->begin();
        inf_fn  =pfi->add_val(0,0,1,"Name :");
        inf_fsz =pfi->add_val(1,0,1,"Size :");
        inf_fmod=pfi->add_val(2,0,1,"Modified:");
        inf_fcrc=pfi->add_val(3,0,1,"CRC32 :");
        inf_fck =pfi->add_val(4,0,1,"Checksum:");
        pfi->end();
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

        // Fill OS info immediately
        struct utsname u; uname(&u);
        char osi[128]; snprintf(osi,sizeof(osi),"%s %s",u.sysname,u.release);
        InfoPanel::set(inf_os, osi);
    }

    // ── Logging ───────────────────────────────────────────────────────────────
    void log(const std::string& msg) {
        std::lock_guard<std::mutex> lk(g_log_mutex);
        log_buf->append((msg+"\n").c_str());
        log_disp->scroll(log_buf->count_lines(0,log_buf->length()),0);
        log_disp->redraw();
    }
    void set_status(const char* s){ lbl_status->copy_label(s); lbl_status->redraw(); }
    void set_progress(int v)      { progress->value((float)v); progress->redraw(); }

    // ── Pipe output processing ────────────────────────────────────────────────
    // The SF100 library progress loop does:  printf("\r%0.1fs elapsed", t)
    // — \r-terminated, no \n.  Real status lines (Erasing…, Erase OK, etc.)
    // use \n.  Strategy:
    //   • Accumulate bytes in g_pipe_buf (file-scope) across read() calls.
    //   • On \r: elapsed-time token — extract seconds, drive progress bar, discard.
    //   • On \n: real log line — trim and emit.

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
                    if(!line.empty()) log(line);
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
        char buf[4096]; ssize_t n;
        while((n=read(g_pipe_rd,buf,sizeof(buf)-1))>0){
            buf[n]='\0';
            w->process_pipe_chunk(buf,n);
        }
        if(g_running.load()) Fl::repeat_timeout(0.05, pipe_timer_cb, v);
    }

    // ── Progress timer — polls g_sf_progress from the worker thread ──────────
    // When total > 0 (program, read, sector erase): shows real % progress.
    // When total == 0 (whole-chip erase — single WIP poll, no byte loop):
    //   falls back to a marquee animation so the bar still moves.
    static void progress_timer_cb(void* v) {
        auto* w = (MainWindow*)v;
        bool running = g_running.load();

        // Keep cancel button in sync with running state (safe: timer runs in main thread)
        if (running) w->btn_cancel->activate();
        else         w->btn_cancel->deactivate();

        if (running) {
            size_t done  = g_sf_progress.done;
            size_t total = g_sf_progress.total;

            if (total > 0) {
                int pct = (int)(done * 100 / total);
                if (pct > 100) pct = 100;
                w->progress->value((float)pct);
            } else {
                // Marquee fallback for whole-chip erase (no byte loop)
                g_marquee_tick = (g_marquee_tick + 2) % 200;
                int val = (g_marquee_tick < 100) ? g_marquee_tick : (200 - g_marquee_tick);
                w->progress->value((float)val);
            }
            w->progress->redraw();
        }
        Fl::repeat_timeout(0.05, progress_timer_cb, v);
    }

    // ── Drain remaining pipe after op ─────────────────────────────────────────
    void drain_and_stop() {
        char buf[4096]; ssize_t n;
        while((n=read(g_pipe_rd,buf,sizeof(buf)-1))>0){
            buf[n]='\0';
            process_pipe_chunk(buf,n);
        }
        capture_stop();
        close(g_pipe_rd); g_pipe_rd=-1;
    }

    // ── File info ─────────────────────────────────────────────────────────────
    void refresh_file_info(const std::string& path) {
        if(path.empty()){
            for(Fl_Box* b:{inf_fn,inf_fsz,inf_fmod,inf_fcrc,inf_fck})
                InfoPanel::set(b,nullptr);
            return;
        }
        char tp[4096]; strncpy(tp,path.c_str(),sizeof(tp)-1);
        InfoPanel::set(inf_fn, basename(tp));
        struct stat st;
        if(stat(path.c_str(),&st)==0){
            char tmp[128];
            snprintf(tmp,sizeof(tmp),"0x%lX",(unsigned long)st.st_size);
            InfoPanel::set(inf_fsz, tmp);
            struct tm* t=localtime(&st.st_mtime);
            strftime(tmp,sizeof(tmp),"%Y-%m-%d %H:%M",t);
            InfoPanel::set(inf_fmod, tmp);
        }
        // CRC32 + byte checksum
        FILE* f=fopen(path.c_str(),"rb");
        if(f){
            static uint32_t tbl[256]; static bool init=false;
            if(!init){ for(int i=0;i<256;i++){uint32_t c=i;for(int j=0;j<8;j++)c=(c&1)?(0xEDB88320^(c>>1)):(c>>1);tbl[i]=c;}init=true;}
            uint32_t crc=0xFFFFFFFF,ck=0;
            uint8_t buf[4096]; size_t n;
            while((n=fread(buf,1,sizeof(buf),f))>0)
                for(size_t i=0;i<n;i++){crc=tbl[(crc^buf[i])&0xFF]^(crc>>8);ck+=buf[i];}
            fclose(f); crc^=0xFFFFFFFF;
            char tmp[32];
            snprintf(tmp,sizeof(tmp),"0x%08X",crc);  InfoPanel::set(inf_fcrc,tmp);
            snprintf(tmp,sizeof(tmp),"0x%08X",ck);   InfoPanel::set(inf_fck, tmp);
        }
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
        if(g_usb_open) return true;
        GetLogPath(g_LogPath);
        log("Initialising USB…");
        if(OpenUSB()){
            LeaveStandaloneMode(0);
            QueryBoard(0);
            g_usb_open=true;
            log("USB OK — programmer found");
            refresh_prog_info();
        } else {
            log("USB FAILED — check connection and udev rules");
        }
        return g_usb_open;
    }

    // ── Generic async op ──────────────────────────────────────────────────────
    void run_async(const std::string& name,
                   std::function<void()> setup,
                   std::function<void(bool)> done=nullptr) {
        if(g_running.load()){fl_alert("Operation in progress.");return;}
        if(!g_usb_open&&!init_usb()){fl_alert("Programmer not connected.");return;}
        if(!g_chip_selected){fl_alert("No chip selected.\nClick Detect first.");return;}
        g_running=true;
        set_status((name+"…").c_str()); set_progress(0);
        log("─── "+name+" ───");
        apply_settings(); setup();

        g_bDisplayTimer = false;   // silence \r elapsed-time noise from library
        g_marquee_tick  = 0;       // marquee fallback counter (used when total==0)
        g_sf_progress.done  = 0;   // reset real progress counters
        g_sf_progress.total = 0;
        capture_start();
        Fl::add_timeout(0.05, pipe_timer_cb,    this);
        // (progress_timer_cb runs permanently from main)

        std::thread([this,name,done](){
            int r=Handler(); bool ok=(r==EXCODE_PASS);
            fflush(stdout);
            Fl::lock();
            Fl::remove_timeout(pipe_timer_cb,    this);
        // (progress_timer_cb runs permanently from main)
            drain_and_stop();
            set_progress(100);
            // code: 0=PASS 1=USB 2=ERASE 3=PROG 4=VERIFY 5=LFWV 6=READ 7=BLANK 8=BATCH 9=CSUM 10=IDENTIFY 11=FW 12=OTHER
            log(ok ? ("✓  "+name+" OK") : ("✗  "+name+" FAILED (code "+std::to_string(r)+")"));
            set_status(ok?(name+" OK").c_str():(name+" FAILED").c_str());
            refresh_prog_info(); refresh_mem_info();
            g_running=false;
            if(done) done(ok);
            Fl::unlock(); Fl::awake();
        }).detach();
    }

    // ── Callbacks ─────────────────────────────────────────────────────────────
    static void cb_file_changed(Fl_Widget*,void* v){
        auto* w=(MainWindow*)v;
        w->cur_file=w->inp_file->value();
        w->refresh_file_info(w->cur_file);
    }
    static void cb_browse(Fl_Widget*,void* v){
        auto* w=(MainWindow*)v;
        std::string p=native_pick("Select Firmware File","*.bin *.hex *.img *.s19 *.srec");
        if(!p.empty()){
            w->inp_file->value(p.c_str());
            w->cur_file=p; w->refresh_file_info(p);
            w->log("File: "+p);
        }
    }

    // Detect: GetFirstDetectionMatch handles VCC cycling + SetTargetFlash.
    // It never writes its TypeName parameter — check ci.UniqueID for success.
    // After success, strTypeName (global) contains space-separated matches from
    // the last FlashIdentifier call inside GetFirstDetectionMatch.
    static void cb_detect(Fl_Widget*,void* v){
        auto* w=(MainWindow*)v;
        if(g_running.load()){fl_alert("Operation in progress.");return;}
        if(!g_usb_open&&!w->init_usb()){fl_alert("Programmer not connected.");return;}
        g_running=true;
        w->set_status("Detecting…"); w->set_progress(0); w->log("─── Detect ───");
        w->apply_settings();

        std::thread([w](){
            int dcnt=get_usb_dev_cnt(); bool ok=false;

            for(int i=0;i<dcnt;i++){
                // Do NOT clear strTypeName before calling — GetFirstDetectionMatch
                // reads strlen(TypeName) to decide VCC save/restore behaviour.
                CHIP_INFO ci = GetFirstDetectionMatch(strTypeName, i);

                if(!ci.UniqueID){
                    Fl::lock();
                    char msg[64]; snprintf(msg,sizeof(msg),"Device %d: chip not identified.",i+1);
                    w->log(msg); Fl::unlock();
                    continue;
                }

                // strTypeName now holds the last FlashIdentifier result —
                // space-separated when search_all=0 found one match (just that name),
                // but may hold multiple names if the DB appended them.
                // Parse into a deduplicated list.
                std::vector<std::string> unames;
                {
                    char buf[1024]; strncpy(buf, strTypeName, sizeof(buf)-1);
                    char* tok = strtok(buf, " ");
                    while(tok){
                        if(*tok){
                            bool dup=false;
                            for(auto& u:unames) if(u==tok){dup=true;break;}
                            if(!dup) unames.push_back(tok);
                        }
                        tok=strtok(nullptr," ");
                    }
                }
                // Always include ci.TypeName as the first/only option if list is empty
                if(unames.empty()) unames.push_back(ci.TypeName);

                unsigned int uid = ReadUID(i);
                std::string chosen = ci.TypeName;  // default: what GetFirstDetectionMatch found

                if(unames.size() > 1){
                    // Log all candidates
                    Fl::lock();
                    char msg[256];
                    snprintf(msg,sizeof(msg),
                             "Device %d (SF%06u): %zu matching chips — select one:",
                             i+1, uid, unames.size());
                    w->log(msg);
                    for(size_t n=0;n<unames.size();n++){
                        char line[128];
                        snprintf(line,sizeof(line),"  [%zu] %s", n+1, unames[n].c_str());
                        w->log(line);
                    }
                    Fl::unlock();

                    // ── Modal list dialog: select chip or cancel ─────────────
                    // s_sel=-1 means cancelled; >=0 is the chosen index.
                    Fl::lock();
                    static int s_sel;
                    s_sel = 0;   // default highlight

                    const int DW = 420, ITEM_H = 22, LIST_H = std::min((int)unames.size(),12)*ITEM_H;
                    const int DH = 8+24+8+LIST_H+8+28+8;

                    Fl_Window* dlg = new Fl_Window(DW, DH, "Select Chip");
                    dlg->begin();

                    // Prompt label
                    Fl_Box* prompt = new Fl_Box(8, 8, DW-16, 24,
                        "Multiple chips match this JEDEC ID — select the installed chip:");
                    prompt->labelsize(11);
                    prompt->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE|FL_ALIGN_WRAP);

                    // Scrollable browser (Fl_Select_Browser = single-select list)
                    int by = 8+24+8;
                    Fl_Select_Browser* lst = new Fl_Select_Browser(8, by, DW-16, LIST_H);
                    lst->textsize(12);
                    for(auto& n : unames) lst->add(n.c_str());
                    lst->value(1);   // highlight first item
                    // Double-click closes dialog as OK
                    lst->callback([](Fl_Widget* wb, void* dlgv){
                        if(Fl::event_clicks() >= 1)
                            ((Fl_Window*)dlgv)->hide();
                    }, dlg);

                    // OK / Cancel buttons
                    int btn_y = by + LIST_H + 8;
                    Fl_Button* btn_ok  = new Fl_Button(DW-180, btn_y, 80, 26, "OK");
                    Fl_Button* btn_can = new Fl_Button(DW-90,  btn_y, 80, 26, "Cancel");

                    btn_ok->callback([](Fl_Widget* wb, void* dlgv){
                        ((Fl_Window*)dlgv)->hide();
                    }, dlg);
                    btn_can->callback([](Fl_Widget* wb, void* dlgv){
                        s_sel = -1;            // signal: cancelled
                        ((Fl_Window*)dlgv)->hide();
                    }, dlg);

                    // Pressing Escape = Cancel
                    dlg->callback([](Fl_Widget* dlgw, void*){
                        s_sel = -1;
                        dlgw->hide();
                    });

                    dlg->end();
                    dlg->set_modal();
                    dlg->show();
                    while(dlg->shown()) Fl::wait();

                    // Read selection before deleting
                    if(s_sel >= 0) s_sel = lst->value() - 1;  // Fl_Browser is 1-based
                    delete dlg;
                    Fl::unlock();

                    if(s_sel < 0){
                        // User cancelled — leave chip unidentified
                        Fl::lock();
                        w->log("✗  Chip selection cancelled — no chip selected.");
                        w->set_status("No chip selected");
                        g_chip_selected = false;
                        Fl::unlock();
                        continue;   // skip to next device (loop); ok stays false
                    }

                    chosen = unames[s_sel];

                    // Load chosen chip's full CHIP_INFO
                    char name_buf[256]; strncpy(name_buf, chosen.c_str(), sizeof(name_buf)-1);
                    if(Dedi_Search_Chip_Db_ByTypeName(name_buf, &Chip_Info))
                        strncpy(strTypeName, chosen.c_str(), 1023);

                    Fl::lock(); w->log("Selected: " + chosen); Fl::unlock();
                } else {
                    // Single match — use ci directly (already the correct CHIP_INFO)
                    Chip_Info = ci;
                    strncpy(strTypeName, ci.TypeName, 1023);
                    char msg[256];
                    snprintf(msg,sizeof(msg),"Device %d (SF%06u):  [ %s ]  —  %zu KB",
                             i+1, uid, ci.TypeName, ci.ChipSizeInByte/1024);
                    Fl::lock(); w->log(msg); Fl::unlock();
                }

                // Auto-set VCC from chip VoltageInMv
                if(Chip_Info.VoltageInMv > 0){
                    if     (Chip_Info.VoltageInMv <= 1800) g_Vcc = 0x12;
                    else if(Chip_Info.VoltageInMv <= 2500) g_Vcc = 0x11;
                    else                                    g_Vcc = 0x10;
                }
                ok = true;
            }

            Fl::lock();
            w->set_progress(100);
            g_chip_selected = ok;
            w->log(ok ? "✓  Detect OK" : "✗  Detect FAILED — chip not identified");
            w->set_status(ok ? "Detect OK" : "Detect FAILED");
            w->refresh_prog_info(); w->refresh_mem_info();
            g_running = false;
            Fl::unlock(); Fl::awake();
        }).detach();
    }

    static void cb_blank(Fl_Widget*,void* v){
        ((MainWindow*)v)->run_async("Blank Check",[](){g_ucOperation=BLANK;});
    }
    static void cb_erase(Fl_Widget*,void* v){
        if(fl_choice("Erase the whole chip?","Cancel","Erase",nullptr)!=1)return;
        ((MainWindow*)v)->run_async("Erase",[](){g_ucOperation=ERASE;});
    }
    static void cb_prog(Fl_Widget*,void* v){
        auto* w=(MainWindow*)v;
        std::string fp=w->inp_file->value();
        if(fp.empty()){fl_alert("Select a firmware file first.");return;}
        bool de=w->chk_erase->value(), dv=w->chk_verify->value();
        static std::string sp; sp=fp;
        w->run_async("Program",[de,dv](){
            g_ucOperation=PROGRAM;
            if(de) g_ucOperation|=ERASE;
            if(dv) g_ucOperation|=VERIFY;
            g_parameter_program=const_cast<char*>(sp.c_str());
        });
    }
    static void cb_verify(Fl_Widget*,void* v){
        auto* w=(MainWindow*)v;
        std::string fp=w->inp_file->value();
        if(fp.empty()){fl_alert("Select a firmware file first.");return;}
        if(g_running.load()){fl_alert("Operation in progress.");return;}
        if(!g_usb_open&&!w->init_usb()){fl_alert("Programmer not connected.");return;}
        if(!g_chip_selected){fl_alert("No chip selected.\nClick Detect first.");return;}

        static std::string sp; sp=fp;
        g_running=true;
        w->set_status("Verify…"); w->set_progress(0);
        w->log("─── Verify ───");
        w->apply_settings();

        g_bDisplayTimer=false;
        g_marquee_tick=0;
        g_sf_progress.done  = 0;
        g_sf_progress.total = 0;
        capture_start();
        Fl::add_timeout(0.05, pipe_timer_cb,    w);
        // (progress_timer_cb runs permanently from main)

        std::thread([w](){
            g_uiAddr = 0;
            g_uiLen  = 0;
            char fname[4096]; strncpy(fname, sp.c_str(), sizeof(fname)-1);
            LoadFile(fname);
            SaveProgContextChanges();
            g_ucOperation = VERIFY;

            int r=Handler(); bool ok=(r==EXCODE_PASS);
            fflush(stdout);
            Fl::lock();
            Fl::remove_timeout(pipe_timer_cb,    w);
        // (progress_timer_cb runs permanently from main)
            w->drain_and_stop();
            w->set_progress(100);
            w->log(ok ? "✓  Verify OK" : ("✗  Verify FAILED (code "+std::to_string(r)+")"));
            w->set_status(ok ? "Verify OK" : "Verify FAILED");
            w->refresh_prog_info(); w->refresh_mem_info();
            g_running=false;
            Fl::unlock(); Fl::awake();
        }).detach();
    }
    static void cb_cancel(Fl_Widget*,void* v){
        // Only reachable when btn_cancel is active (i.e. op in progress)
        int choice = fl_choice(
            "⚠  WARNING: Cancelling mid-operation can corrupt the chip.\n\n"
            "The programmer and chip may need to be power-cycled before\n"
            "the next operation will succeed.\n\n"
            "Cancel the current operation?",
            "Keep Running", "Cancel Operation", nullptr);
        if(choice != 1) return;   // user chose Keep Running

        SerialFlash_SetCancelOperationFlag();
        auto* w = (MainWindow*)v;
        w->log("⚠  Operation cancelled by user — power-cycle programmer and chip.");
        w->set_status("Cancelled");
    }
    static void cb_read(Fl_Widget*,void* v){
        auto* w=(MainWindow*)v;
        std::string path=native_pick("Save Read Output As","*.bin",true);
        if(path.empty())return;
        static std::string sp; sp=path;
        w->run_async("Read",[](){
            g_ucOperation=READ_TO_FILE;
            g_parameter_read=const_cast<char*>(sp.c_str());
        });
    }
};

// ── main ──────────────────────────────────────────────────────────────────────
int main(int argc,char** argv){
    Fl::lock();
    Fl::scheme("gtk+");
    Fl::background(0xF0,0xF0,0xF0);
    Fl::background2(0xFF,0xFF,0xFF);
    Fl::foreground(0x22,0x22,0x22);
    MainWindow* win=new MainWindow(1100,680);
    win->resizable(win);
    win->size_range(900,560,0,0);
    win->show(argc,argv);
    win->log("DediProg Linux GUI  —  V1.14.21.x natively integrated");
    win->log("Connect your SF100/SF600 via USB, then click Detect.");
    // Permanent 50ms timer drives Cancel button enable/disable via g_running
    Fl::add_timeout(0.05, MainWindow::progress_timer_cb, win);
    return Fl::run();
}
