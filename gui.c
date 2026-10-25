/* gui.c - SRS64X / SRS32X: Win32 GUI front end, laid out the way SIV64X is:
 * a main panel of coloured text with a button row, and one pop-up panel per
 * feature (PCI Bus, USB Bus, ALL Dev, Lookup, Find, Data Files, About, Help).
 * Plain C + Win32 API (USER32/GDI32/comdlg32/shell32/RichEdit), no C++ runtime.
 * With command-line arguments the same image runs the CLI on the console,
 * the way "SIV64X -save | more" does. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <richedit.h>
#include <shlobj.h>
#include "srs.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- ids ---------- */
#define IDM_CLOSE   1001
#define IDM_COPY    1002
#define IDM_PCI     1003
#define IDM_USB     1004
#define IDM_ALL     1005
#define IDM_LOOKUP  1006
#define IDM_FIND    1007
#define IDM_DATA    1008
#define IDM_ABOUT   1009
#define IDM_HELP    1010
#define IDM_RUN     1011
#define IDM_NEXT    1012
#define IDM_PREV    1013
#define IDM_HILITE  1014
#define IDM_REFRESH 1015
#define IDM_BROWSE  1016
#define IDM_LATEST  1017
#define IDM_COLOUR  1018
#define IDM_SELALL  1019
#define IDM_MODE    1020
#define IDM_BUSPCI  1021
#define IDM_BUSUSB  1022
#define IDC_RICH    100
#define IDC_EDIT    101
#define IDC_ICASE   102
#define IDC_FIXED   103
#define IDC_HL      104
#define IDC_MAX     105
#define IDC_REPORT  106
#define IDC_MODELBL 107
#define WM_APP_FINDDONE (WM_APP + 1)

/* ---------- palette (SIV-style colours on #F5F5F5) ---------- */
enum { C_LABEL = 1, C_VALUE, C_PURPLE, C_TEAL, C_ORANGE, C_GREEN, C_RED, C_BLACK, C_GRAY, C_FIND, C_NCOL };
static COLORREF g_pal[C_NCOL] = {
    0, RGB(0x33, 0x99, 0xFF), RGB(0x00, 0x00, 0xCC), RGB(0x9C, 0x0F, 0xB8), RGB(0x00, 0x99, 0x99),
    RGB(0xFF, 0x88, 0x00), RGB(0x00, 0xA0, 0x00), RGB(0xE0, 0x00, 0x00), RGB(0, 0, 0), RGB(0x80, 0x80, 0x80),
    RGB(0xFF, 0xFF, 0x00)
};
#define BG_COLOUR RGB(0xF5, 0xF5, 0xF5)

/* ---------- panels ---------- */
typedef enum { P_MAIN, P_PCI, P_USB, P_ALL, P_LOOKUP, P_FIND, P_DATA, P_ABOUT, P_HELP, P_COUNT } ptype;

typedef struct { const char *label; int id; } button;

typedef struct {
    ptype type;
    HWND hwnd, rich, top[16];
    int ntop;
    HWND btn[16];
    int nbtn;
    /* device panels: rich-edit line -> device key */
    int nrow; int *rowline; char (*rowkey)[40]; enum srs_bus *rowbus;
    /* find panel */
    srs_findres fr; int have_fr; char fr_pattern[512]; int fr_rc; char fr_err[1400];
    int nmatch, curmatch; LONG *mstart, *mend;
    int busy;
    /* lookup panel */
    enum srs_bus lbus;
} panel;

static HINSTANCE g_inst;
static HFONT g_font, g_fontb;
static HACCEL g_accel;
static panel g_p[P_COUNT];
static srs_db g_db[2];
static int g_dbok[2];
static char g_image[64] = "SRS64X", g_ini[1100], g_host[256];
static int g_highlight = 1;
static const char *g_richclass = "RichEdit20A";

static const char *ptitle[P_COUNT] = {
    "", "PCI Bus", "USB Bus", "ALL Dev", "Lookup", "Find", "Data Files", "About", "Help"
};

static const button main_btns[] = {
    { "&OK", IDM_CLOSE }, { "&Copy", IDM_COPY }, { "&PCI Bus", IDM_PCI }, { "&USB Bus", IDM_USB },
    { "&ALL Dev", IDM_ALL }, { "&Lookup", IDM_LOOKUP }, { "&Find", IDM_FIND }, { "&Data Files", IDM_DATA },
    { "A&bout", IDM_ABOUT }, { "&Help", IDM_HELP }, { NULL, 0 }
};
static const button dev_btns[] = {
    { "&OK", IDM_CLOSE }, { "&Copy", IDM_COPY }, { "&PCI Bus", IDM_PCI }, { "&USB Bus", IDM_USB },
    { "&ALL Dev", IDM_ALL }, { "&Lookup", IDM_LOOKUP }, { "&Find", IDM_FIND }, { "&Refresh", IDM_REFRESH },
    { "&Help", IDM_HELP }, { NULL, 0 }
};
static const button lookup_btns[] = {
    { "&OK", IDM_CLOSE }, { "&Copy", IDM_COPY }, { "&ALL Dev", IDM_ALL }, { "&Find", IDM_FIND },
    { "&Data Files", IDM_DATA }, { "&Help", IDM_HELP }, { NULL, 0 }
};
static const button find_btns[] = {
    { "&OK", IDM_CLOSE }, { "&Copy", IDM_COPY }, { "&Next", IDM_NEXT }, { "Pre&v", IDM_PREV },
    { "&Highlight", IDM_HILITE }, { "Co&lour", IDM_COLOUR }, { "&Lookup", IDM_LOOKUP }, { "&Help", IDM_HELP },
    { NULL, 0 }
};
static const button info_btns[] = {
    { "&OK", IDM_CLOSE }, { "&Copy", IDM_COPY }, { "&Lookup", IDM_LOOKUP }, { "&Find", IDM_FIND },
    { "&Data Files", IDM_DATA }, { "A&bout", IDM_ABOUT }, { "&Help", IDM_HELP }, { NULL, 0 }
};

static void open_panel(ptype t);
static void render(panel *p);

/* ======================================================================
 * RTF builder - tracks the plain-text position so matches can be selected
 * ====================================================================== */
typedef struct { char *b; size_t n, cap; LONG pos; int line; } rtf;

static void rb_raw(rtf *r, const char *s)
{
    size_t l = strlen(s);
    if (r->n + l + 1 > r->cap) {
        size_t nc = (r->cap ? r->cap : 4096);
        char *nb;
        while (nc < r->n + l + 1) nc *= 2;
        nb = realloc(r->b, nc);
        if (!nb) return;
        r->b = nb; r->cap = nc;
    }
    memcpy(r->b + r->n, s, l + 1);
    r->n += l;
}

static void rb_fmt(rtf *r, const char *fmt, ...)
{
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    rb_raw(r, tmp);
}

/* UTF-8 text of given byte length -> escaped RTF; counts UTF-16 units into pos */
static void rb_textn(rtf *r, const char *s, int len)
{
    int wl, i;
    WCHAR *w;
    if (len <= 0) return;
    wl = MultiByteToWideChar(CP_UTF8, 0, s, len, NULL, 0);
    if (wl <= 0) return;
    w = malloc(sizeof(WCHAR) * (size_t)wl);
    if (!w) return;
    MultiByteToWideChar(CP_UTF8, 0, s, len, w, wl);
    for (i = 0; i < wl; i++) {
        WCHAR c = w[i];
        char e[16];
        if (c == '\t') { rb_raw(r, "\\tab "); r->pos++; continue; }
        if (c < 0x20) continue;
        if (c == '\\' || c == '{' || c == '}') { e[0] = '\\'; e[1] = (char)c; e[2] = 0; rb_raw(r, e); }
        else if (c < 0x80) { e[0] = (char)c; e[1] = 0; rb_raw(r, e); }
        else { snprintf(e, sizeof e, "\\u%d?", (int)(short)c); rb_raw(r, e); }
        r->pos++;
    }
    free(w);
}

static void rb_text(rtf *r, const char *s) { rb_textn(r, s, (int)strlen(s)); }
static void rb_col(rtf *r, int c) { rb_fmt(r, "\\cf%d ", c); }
static void rb_ct(rtf *r, int c, const char *s) { rb_col(r, c); rb_text(r, s); }
static void rb_tab(rtf *r) { rb_raw(r, "\\tab "); r->pos++; }
static void rb_par(rtf *r) { rb_raw(r, "\\par\n"); r->pos++; r->line++; }
static void rb_bold(rtf *r, int on) { rb_raw(r, on ? "\\b " : "\\b0 "); }

/* tab stops in pixels (96 DPI) -> twips */
static void rb_tabs(rtf *r, const int *px, int n)
{
    int i;
    rb_raw(r, "\\pard");
    for (i = 0; i < n; i++) rb_fmt(r, "\\tx%d", px[i] * 15);
    rb_raw(r, " ");
}

static void rb_begin(rtf *r)
{
    int i;
    memset(r, 0, sizeof *r);
    rb_raw(r, "{\\rtf1\\ansi\\ansicpg1252\\deff0{\\fonttbl{\\f0\\fswiss\\fcharset0 Tahoma;}"
              "{\\f1\\fmodern\\fcharset0 Courier New;}}{\\colortbl;");
    for (i = 1; i < C_NCOL; i++)
        rb_fmt(r, "\\red%d\\green%d\\blue%d;", GetRValue(g_pal[i]), GetGValue(g_pal[i]), GetBValue(g_pal[i]));
    rb_raw(r, "}\\uc1\\pard\\f0\\fs16 ");
}

static void rb_end(rtf *r) { rb_raw(r, "}"); }

typedef struct { const char *s; size_t n, off; } streamsrc;

static DWORD CALLBACK stream_cb(DWORD_PTR cookie, LPBYTE buf, LONG cb, LONG *pcb)
{
    streamsrc *src = (streamsrc *)cookie;
    size_t left = src->n - src->off, take = left < (size_t)cb ? left : (size_t)cb;
    memcpy(buf, src->s + src->off, take);
    src->off += take;
    *pcb = (LONG)take;
    return 0;
}

static void rich_set(HWND rich, rtf *r)
{
    EDITSTREAM es;
    streamsrc src;
    CHARRANGE cr = { 0, 0 };
    src.s = r->b ? r->b : "{\\rtf1 }";
    src.n = strlen(src.s);
    src.off = 0;
    es.dwCookie = (DWORD_PTR)&src;
    es.dwError = 0;
    es.pfnCallback = stream_cb;
    SendMessageA(rich, WM_SETREDRAW, FALSE, 0);
    SendMessageA(rich, EM_STREAMIN, SF_RTF, (LPARAM)&es);
    SendMessageA(rich, EM_EXSETSEL, 0, (LPARAM)&cr);
    SendMessageA(rich, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(rich, NULL, TRUE);
    free(r->b);
    r->b = NULL;
}

/* ======================================================================
 * settings - SRSDEF.ini beside the program (SIV keeps SIVDEF.txt beside SIV64X.exe)
 * ====================================================================== */
static void settings_load(void)
{
    char dir[1024], v[64];
    if (plat_exedir(dir, sizeof dir)) dir[0] = '.', dir[1] = 0;
    snprintf(g_ini, sizeof g_ini, "%s\\SRSDEF.ini", dir);
    GetPrivateProfileStringA("SRSDEF", "Colour Find Text", "FFFF00", v, sizeof v, g_ini);
    {
        unsigned long rgb = strtoul(v, NULL, 16);
        g_pal[C_FIND] = RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    }
    g_highlight = (int)GetPrivateProfileIntA("SRSDEF", "Highlight Matches", 1, g_ini);
}

static void settings_save(void)
{
    char v[16];
    snprintf(v, sizeof v, "%02X%02X%02X", GetRValue(g_pal[C_FIND]), GetGValue(g_pal[C_FIND]), GetBValue(g_pal[C_FIND]));
    WritePrivateProfileStringA("SRSDEF", "Colour Find Text", v, g_ini);
    WritePrivateProfileStringA("SRSDEF", "Highlight Matches", g_highlight ? "1" : "0", g_ini);
    WritePrivateProfileStringA("SRSDEF", "Font Name", "Tahoma", g_ini);
}

/* ======================================================================
 * content builders
 * ====================================================================== */
static void kv(rtf *r, const char *k, int vc, const char *v)
{
    rb_ct(r, C_LABEL, k); rb_tab(r); rb_ct(r, vc, v); rb_par(r);
}

static void db_ident(int b, char *out, size_t outsz)
{
    char recs[32];
    if (!g_dbok[b]) { snprintf(out, outsz, "not found"); return; }
    srs_thousands(g_db[b].n, recs, sizeof recs);
    snprintf(out, outsz, "Date %s  Version %s  (%s)", g_db[b].date[0] ? g_db[b].date : "?",
             g_db[b].version[0] ? g_db[b].version : "?", recs);
}

static void build_main(panel *p, rtf *r)
{
    static const int tabs[] = { 170, 560 };
    char buf[1400], rep[1100];
    SYSTEMTIME st;
    srs_dev *d;
    int n, b;
    (void)p;
    GetLocalTime(&st);
    rb_tabs(r, tabs, 2);
    rb_bold(r, 1);
    rb_ct(r, C_VALUE, g_image); rb_text(r, " - System Report Search V" SRS_VERSION "  ");
    rb_bold(r, 0);
    rb_ct(r, C_PURPLE, g_host);
    snprintf(buf, sizeof buf, "   %04u-%02u-%02u %02u:%02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    rb_ct(r, C_TEAL, buf);
    rb_par(r); rb_par(r);
    kv(r, "Program Build", C_ORANGE, plat_build_info());
    kv(r, "Platform", C_VALUE, plat_is_wine() ? "Wine on Linux - hardware read from sysfs (Z:\\sys)" :
                               plat_sysfs() ? "Linux sysfs" : "Windows - hardware read through SetupAPI");
    rb_par(r);
    for (b = 0; b < 2; b++) {
        db_ident(b, buf, sizeof buf);
        rb_ct(r, C_LABEL, b ? "USB Device Names" : "PCI Device Names"); rb_tab(r);
        rb_ct(r, C_PURPLE, g_dbok[b] ? g_db[b].path : (b ? "usb.ids" : "pci.ids")); rb_tab(r);
        rb_ct(r, g_dbok[b] ? C_TEAL : C_RED, buf); rb_par(r);
    }
    rb_par(r);
    for (b = 0; b < 2; b++) {
        int unres = 0, i;
        n = srs_devices(b ? BUS_USB : BUS_PCI, &d);
        for (i = 0; i < n && g_dbok[b]; i++) {
            srs_result res;
            char e[128];
            if (srs_resolve(&g_db[b], d[i].key, MODE_TREE, &res, e, sizeof e) || res.matched < 0 ||
                !strchr(res.chain[res.matched], ':'))
                unres++;
        }
        snprintf(buf, sizeof buf, "%d devices, %d resolved to a device name, %d vendor only or unknown  (%s)",
                 n < 0 ? 0 : n, n < 0 ? 0 : n - unres, unres, srs_devices_source());
        kv(r, b ? "USB Bus" : "PCI Bus", C_VALUE, buf);
        free(d);
    }
    if (srs_latest_report(rep, sizeof rep) == 0) kv(r, "Latest Report", C_PURPLE, rep);
    else kv(r, "Latest Report", C_RED, "none - run Full System Report (srs-sysreport) first");
    rb_par(r);
    rb_ct(r, C_GRAY, "PCI Bus / USB Bus / ALL Dev resolve every device; Lookup resolves one ID (Tree, Scan or Info mode);"
                     " Find searches the latest report. Ctrl+F Find, Ctrl+L Lookup, F3 next match, Ctrl+H highlight.");
    rb_par(r);
}

static void build_devices(panel *p, rtf *r)
{
    static const int tabs[] = { 110, 300, 450, 660 };
    enum srs_bus buses[2];
    int nb = 0, b, unresolved = 0;
    free(p->rowline); free(p->rowkey); free(p->rowbus);
    p->rowline = NULL; p->rowkey = NULL; p->rowbus = NULL; p->nrow = 0;
    if (p->type != P_USB) buses[nb++] = BUS_PCI;
    if (p->type != P_PCI) buses[nb++] = BUS_USB;
    rb_tabs(r, tabs, 4);
    for (b = 0; b < nb; b++) {
        srs_dev *d;
        char recs[32], buf[1400];
        int n = srs_devices(buses[b], &d), i;
        if (n < 0) n = 0;
        srs_thousands(g_dbok[buses[b]] ? g_db[buses[b]].n : 0, recs, sizeof recs);
        rb_bold(r, 1);
        snprintf(buf, sizeof buf, "[%s] %d devices, %s records ", buses[b] == BUS_USB ? "USB" : "PCI", n, recs);
        rb_ct(r, C_VALUE, buf);
        rb_bold(r, 0);
        rb_ct(r, C_PURPLE, g_dbok[buses[b]] ? g_db[buses[b]].path : "(no .ids file)");
        rb_ct(r, C_GRAY, "   read from "); rb_ct(r, C_GRAY, srs_devices_source());
        rb_par(r);
        rb_ct(r, C_LABEL, "Slot"); rb_tab(r); rb_text(r, "ID"); rb_tab(r); rb_text(r, "Matched"); rb_tab(r);
        rb_text(r, "Vendor"); rb_tab(r); rb_text(r, "Resolved"); rb_par(r);
        for (i = 0; i < n; i++) {
            srs_result res;
            char e[128];
            int vendor_only, colons = 0;
            const char *m;
            if (!g_dbok[buses[b]] || srs_resolve(&g_db[buses[b]], d[i].key, MODE_TREE, &res, e, sizeof e)) {
                memset(&res, 0, sizeof res);
                res.matched = -1;
            }
            vendor_only = res.matched < 0 || !strchr(res.chain[res.matched], ':');
            unresolved += vendor_only;
            {   /* remember which line holds which device for double-click drill-down */
                int *nl = realloc(p->rowline, sizeof(int) * (size_t)(p->nrow + 1));
                char (*nk)[40] = realloc(p->rowkey, 40 * (size_t)(p->nrow + 1));
                enum srs_bus *nbus = realloc(p->rowbus, sizeof(enum srs_bus) * (size_t)(p->nrow + 1));
                if (nl) p->rowline = nl;
                if (nk) p->rowkey = nk;
                if (nbus) p->rowbus = nbus;
                if (nl && nk && nbus) {
                    p->rowline[p->nrow] = r->line;
                    snprintf(p->rowkey[p->nrow], 40, "%s", d[i].key);
                    p->rowbus[p->nrow] = buses[b];
                    p->nrow++;
                }
            }
            rb_ct(r, C_LABEL, d[i].slot); rb_tab(r);
            rb_ct(r, C_PURPLE, d[i].key); rb_tab(r);
            rb_ct(r, C_TEAL, res.matched >= 0 ? res.chain[res.matched] : "-"); rb_tab(r);
            rb_ct(r, C_VALUE, res.vendor[0] ? res.vendor : "Unknown Vendor"); rb_tab(r);
            rb_ct(r, vendor_only ? C_RED : C_BLACK, vendor_only ? "Unknown Device" : res.resolved);
            if (res.matched >= 0 && res.subvendor[0]) {
                for (m = res.chain[res.matched]; *m; m++) colons += *m == ':';
                if (colons < 3) {
                    snprintf(buf, sizeof buf, "  [subsystem: %s]", res.subvendor);
                    rb_ct(r, C_GREEN, buf);
                }
            }
            rb_par(r);
        }
        rb_par(r);
        free(d);
    }
    {
        char buf[200];
        snprintf(buf, sizeof buf, "%d device(s) resolved only to vendor or not at all", unresolved);
        rb_ct(r, C_ORANGE, buf); rb_par(r);
        rb_ct(r, C_GRAY, "Double-click a device to open it in Lookup."); rb_par(r);
    }
}

static int lookup_mode(panel *p)
{
    LRESULT s = SendMessageA(p->top[2], BM_GETCHECK, 0, 0);
    return s == BST_CHECKED ? MODE_TREE : s == BST_INDETERMINATE ? MODE_SCAN : MODE_INFO;
}

static const char *mode_text(int m)
{
    return m == MODE_TREE ? "Tree Lookup mode - Do bsearch() of a sorted table" :
           m == MODE_SCAN ? "Scan Lookup mode - Scan the .ids file" :
                            "Info Lookup mode - Use the OS device database";
}

static void build_lookup(panel *p, rtf *r)
{
    static const int tabs[] = { 24, 200, 700 };
    char id[128], buf[1400], recs[32];
    int mode = lookup_mode(p), i;
    enum srs_bus bus = SendMessageA(p->top[4], BM_GETCHECK, 0, 0) == BST_CHECKED ? BUS_USB : BUS_PCI;
    GetWindowTextA(p->top[1], id, sizeof id);
    rb_tabs(r, tabs, 3);
    rb_bold(r, 1);
    snprintf(buf, sizeof buf, "Lookup %s on the %s bus", id, bus == BUS_USB ? "USB" : "PCI");
    rb_ct(r, C_VALUE, buf); rb_bold(r, 0);
    rb_ct(r, C_GRAY, "   "); rb_ct(r, C_TEAL, mode_text(mode)); rb_par(r); rb_par(r);
    if (mode == MODE_INFO) {
        srs_info in;
        int rc = srs_info_lookup(bus, id, &in);
        kv(r, "Query", C_PURPLE, in.query);
        if (in.err[0]) kv(r, "Error", C_RED, in.err);
        kv(r, "Vendor", C_VALUE, in.vendor[0] ? in.vendor : "-");
        kv(r, "Resolved", rc == 0 ? C_BLACK : C_RED, in.model[0] ? in.model : "-");
        snprintf(buf, sizeof buf, "info mode (%s) in %.2f ms", strncmp(in.query, "SetupAPI", 8) ? "systemd-hwdb" : "SetupAPI", in.ms);
        rb_par(r); rb_ct(r, C_GRAY, buf); rb_par(r);
        return;
    }
    if (!g_dbok[bus]) { rb_ct(r, C_RED, "cannot find the .ids file beside the program or in the hwdata directory"); rb_par(r); return; }
    {
        srs_result res;
        char err[256];
        if (srs_resolve(&g_db[bus], id, mode, &res, err, sizeof err)) {
            rb_ct(r, C_RED, err); rb_par(r);
            return;
        }
        rb_ct(r, C_LABEL, ""); rb_tab(r); rb_text(r, "Key"); rb_tab(r); rb_text(r, "Entry"); rb_par(r);
        for (i = 0; i < res.nchain; i++) {
            rb_ct(r, C_ORANGE, i == res.matched ? "<" : ""); rb_tab(r);
            rb_ct(r, i == res.matched ? C_PURPLE : C_GRAY, res.chain[i]); rb_tab(r);
            rb_ct(r, res.name[i][0] ? (i == res.matched ? C_BLACK : C_VALUE) : C_GRAY,
                  res.name[i][0] ? res.name[i] : "(no entry)");
            rb_par(r);
        }
        rb_par(r);
        {
            static const int t2[] = { 110 };
            rb_tabs(r, t2, 1);
        }
        kv(r, "Vendor", C_VALUE, res.vendor[0] ? res.vendor : "-");
        if (res.subvendor[0]) kv(r, "Subsystem", C_GREEN, res.subvendor);
        kv(r, "Stock", C_TEAL, res.stock[0] ? res.stock : "-");
        if (res.resolved[0]) kv(r, "Resolved", C_BLACK, res.resolved);
        else { snprintf(buf, sizeof buf, "No Matches for %s", id); kv(r, "Resolved", C_RED, buf); }
        srs_thousands(g_db[bus].n, recs, sizeof recs);
        snprintf(buf, sizeof buf, "%s mode: %s records loaded in %.1f ms, lookup %.3f ms   (%s)",
                 mode == MODE_TREE ? "tree" : "scan", recs, g_db[bus].load_ms, res.lookup_ms, g_db[bus].path);
        rb_par(r); rb_ct(r, C_GRAY, buf); rb_par(r);
    }
}

static void build_find(panel *p, rtf *r)
{
    char buf[1600];
    int i, j, k;
    free(p->mstart); free(p->mend);
    p->mstart = p->mend = NULL;
    p->nmatch = 0;
    p->curmatch = -1;
    rb_raw(r, "\\pard ");
    if (p->busy) {
        snprintf(buf, sizeof buf, "Searching for \"%s\" ...", p->fr_pattern);
        rb_ct(r, C_TEAL, buf); rb_par(r);
        return;
    }
    if (!p->have_fr) {
        rb_ct(r, C_GRAY, "Enter a pattern (Python-style regex, or tick Literal) and press Enter or Find."); rb_par(r);
        rb_ct(r, C_GRAY, "Searches every section (*.txt, lshw.html) of the selected sysreport folder."); rb_par(r);
        return;
    }
    if (p->fr_rc) { rb_ct(r, C_RED, p->fr_err); rb_par(r); return; }
    snprintf(buf, sizeof buf, "Searching %s for \"%s\"", p->fr.report, p->fr_pattern);
    rb_ct(r, C_TEAL, buf); rb_par(r);
    for (i = 0; i < p->fr.nsec; i++) {
        const srs_section *s = &p->fr.secs[i];
        int maxshow;
        char mx[16];
        GetWindowTextA(p->top[8], mx, sizeof mx);
        maxshow = atoi(mx) > 0 ? atoi(mx) : 20;
        rb_par(r);
        rb_bold(r, 1);
        snprintf(buf, sizeof buf, "== %s - %d Match%s", s->file, s->count, s->count != 1 ? "es" : "");
        rb_ct(r, C_VALUE, buf);
        rb_bold(r, 0);
        rb_par(r);
        for (j = 0; j < s->nshown; j++) {
            const srs_hit *h = &s->hits[j];
            int at = 0;
            snprintf(buf, sizeof buf, "%7d: ", h->lineno);
            rb_raw(r, "\\f1 ");
            rb_ct(r, C_GRAY, buf);
            rb_col(r, C_BLACK);
            for (k = 0; k < h->nspan; k++) {
                LONG *ns, *ne;
                rb_textn(r, h->text + at, h->spans[k].so - at);
                ns = realloc(p->mstart, sizeof(LONG) * (size_t)(p->nmatch + 1));
                ne = realloc(p->mend, sizeof(LONG) * (size_t)(p->nmatch + 1));
                if (ns) p->mstart = ns;
                if (ne) p->mend = ne;
                if (ns && ne) p->mstart[p->nmatch] = r->pos;
                if (g_highlight) rb_fmt(r, "{\\highlight%d\\cb%d ", C_FIND, C_FIND);
                rb_textn(r, h->text + h->spans[k].so, h->spans[k].eo - h->spans[k].so);
                if (g_highlight) rb_raw(r, "}");
                if (ns && ne) p->mend[p->nmatch++] = r->pos;
                at = h->spans[k].eo;
            }
            rb_text(r, h->text + at);
            rb_raw(r, "\\f0 ");
            rb_par(r);
        }
        if (s->count > s->nshown && s->count > maxshow) {
            snprintf(buf, sizeof buf, "         ... %d more (raise Max)", s->count - s->nshown);
            rb_ct(r, C_GRAY, buf); rb_par(r);
        }
    }
    rb_par(r);
    if (!p->fr.total) {
        snprintf(buf, sizeof buf, "No Matches for %s", p->fr_pattern);
        rb_ct(r, C_RED, buf);
    } else {
        snprintf(buf, sizeof buf, "%d Matches in %d of %d sections  (%.1f ms)", p->fr.total, p->fr.nsec, p->fr.nfiles, p->fr.ms);
        rb_ct(r, C_GREEN, buf);
    }
    rb_par(r);
}

static void build_data(panel *p, rtf *r)
{
    static const int tabs[] = { 140, 520, 820, 900 };
    static const char *urls[2] = { "https://pci-ids.ucw.cz/v2.2/pci.ids", "http://www.linux-usb.org/usb.ids" };
    char buf[1400], exe[1100], rep[1100];
    int b;
    (void)p;
    rb_tabs(r, tabs, 4);
    rb_ct(r, C_LABEL, "Description"); rb_tab(r); rb_ct(r, C_PURPLE, "File Specification"); rb_tab(r);
    rb_ct(r, C_TEAL, "Current Identification"); rb_tab(r); rb_ct(r, C_ORANGE, "Code Page"); rb_tab(r);
    rb_ct(r, C_VALUE, "WEB Download Location"); rb_par(r);
    for (b = 0; b < 2; b++) {
        db_ident(b, buf, sizeof buf);
        rb_ct(r, C_LABEL, b ? "USB Device Names" : "PCI Device Names"); rb_tab(r);
        rb_ct(r, C_PURPLE, g_dbok[b] ? g_db[b].path : "(not found)"); rb_tab(r);
        rb_ct(r, C_TEAL, buf); rb_tab(r); rb_ct(r, C_ORANGE, "UTF-8"); rb_tab(r);
        rb_ct(r, C_VALUE, urls[b]); rb_par(r);
    }
    GetModuleFileNameA(NULL, exe, sizeof exe);
    rb_ct(r, C_LABEL, "SRS Program"); rb_tab(r); rb_ct(r, C_PURPLE, exe); rb_tab(r);
    rb_ct(r, C_TEAL, "V" SRS_VERSION); rb_tab(r); rb_tab(r); rb_ct(r, C_ORANGE, plat_build_info()); rb_par(r);
    rb_ct(r, C_LABEL, "Settings"); rb_tab(r); rb_ct(r, C_PURPLE, g_ini); rb_tab(r);
    snprintf(buf, sizeof buf, "Colour Find Text %02X%02X%02X, Highlight %s", GetRValue(g_pal[C_FIND]),
             GetGValue(g_pal[C_FIND]), GetBValue(g_pal[C_FIND]), g_highlight ? "on" : "off");
    rb_ct(r, C_TEAL, buf); rb_par(r);
    rb_ct(r, C_LABEL, "Reports"); rb_tab(r); rb_ct(r, C_PURPLE, srs_reports_root() ? srs_reports_root() : "-"); rb_tab(r);
    rb_ct(r, C_TEAL, srs_latest_report(rep, sizeof rep) == 0 ? "latest: " : "no report yet");
    if (srs_latest_report(rep, sizeof rep) == 0) { const char *s = strrchr(rep, '\\'); rb_ct(r, C_TEAL, s ? s + 1 : rep); }
    rb_par(r);
    rb_ct(r, C_LABEL, "Devices"); rb_tab(r); rb_ct(r, C_PURPLE, plat_sysfs() ? plat_sysfs() : "SetupAPI");
    rb_tab(r); rb_ct(r, C_TEAL, srs_devices_source()); rb_par(r);
    rb_par(r);
    rb_ct(r, C_GRAY, "Data files are looked for beside the program first, then in the system hwdata directory"
                     " (SIV looks for PCIDEVS.txt beside SIV64X.exe the same way)."); rb_par(r);
}

static void build_about(panel *p, rtf *r)
{
    static const int tabs[] = { 140 };
    char exe[1100];
    (void)p;
    rb_tabs(r, tabs, 1);
    rb_bold(r, 1); rb_ct(r, C_VALUE, g_image); rb_text(r, " - System Report Search V" SRS_VERSION); rb_bold(r, 0); rb_par(r); rb_par(r);
    GetModuleFileNameA(NULL, exe, sizeof exe);
    kv(r, "Image", C_PURPLE, exe);
    kv(r, "Build", C_ORANGE, plat_build_info());
    kv(r, "Built like", C_TEAL, "SIV64X: plain C + Win32 API, system msvcrt.dll, PE32+ GUI, OS 6.00 / subsystem 5.02,"
                                " ImageBase 0x1000000, 24 MB stack - one source, one image per architecture");
    kv(r, "Images", C_VALUE, "SRS64X.exe (x64)  SRS32X.exe (x86)  srs-gui (Linux winelib ELF)  sysreport-search (Linux CLI)");
    kv(r, "Data", C_VALUE, "hwdata pci.ids / usb.ids (BSD-3-Clause / GPL-2.0+), sysfs, systemd-hwdb, SetupAPI");
    kv(r, "About", C_GRAY, "Inspired by SIV64X's search features and built to improve on them; independent, no SIV code or data is used.");
}

static void build_help(panel *p, rtf *r)
{
    static const int tabs[] = { 140 };
    (void)p;
    rb_tabs(r, tabs, 1);
    rb_bold(r, 1); rb_ct(r, C_VALUE, g_image); rb_text(r, " - Help"); rb_bold(r, 0); rb_par(r); rb_par(r);
    kv(r, "PCI Bus / USB Bus", C_TEAL, "Every present device, resolved V:D:S:s:RR -> V:D:S:s -> V:D:RR -> V:D -> V.");
    kv(r, "ALL Dev", C_TEAL, "Both buses. Double-click a row to open that ID in Lookup.");
    kv(r, "Lookup", C_TEAL, "One ID. The tri-state box picks the mode like SIV's Port Names box:");
    kv(r, "", C_VALUE, "Checked = Tree Lookup (bsearch of the sorted table)");
    kv(r, "", C_VALUE, "Grey = Scan Lookup (re-read the .ids file line by line)");
    kv(r, "", C_VALUE, "Unchecked = Info Lookup (systemd-hwdb on Linux, SetupAPI on Windows)");
    kv(r, "Find", C_TEAL, "Regex search of the newest ~/sysreports report on a worker thread; Literal = plain text.");
    kv(r, "", C_VALUE, "Invalid patterns report \"Match String [..] is Invalid\"; no hits report \"No Matches for ..\".");
    kv(r, "Highlight", C_TEAL, "Toggle match highlighting (Ctrl+H); Colour picks the Colour Find Text, saved to SRSDEF.ini.");
    kv(r, "Hot-keys", C_TEAL, "Ctrl+F Find  Ctrl+L Lookup  Ctrl+D ALL Dev  F3 / Shift+F3 next / previous match");
    kv(r, "", C_VALUE, "Ctrl+H Highlight Matches  F5 Refresh  F1 Help  Esc close panel  right-click for the panel menu");
    kv(r, "Command line", C_TEAL, "The same image runs the CLI when given arguments (like SIV64X -save):");
    kv(r, "", C_PURPLE, "SRS64X id 1002:7550 [--bus usb] [--mode tree|scan|info]");
    kv(r, "", C_PURPLE, "SRS64X find PATTERN [--report DIR] [-i] [-F] [--max N]");
    kv(r, "", C_PURPLE, "SRS64X devices [--bus pci|usb|all]      SRS64X --version");
    rb_par(r);
    kv(r, "Files", C_TEAL, "pci.ids / usb.ids beside the program, else Z:\\usr\\share\\hwdata under Wine; SRSDEF.ini settings.");
}

static void render(panel *p)
{
    rtf r;
    if (!p->hwnd) return;
    rb_begin(&r);
    switch (p->type) {
    case P_MAIN: build_main(p, &r); break;
    case P_PCI: case P_USB: case P_ALL: build_devices(p, &r); break;
    case P_LOOKUP: build_lookup(p, &r); break;
    case P_FIND: build_find(p, &r); break;
    case P_DATA: build_data(p, &r); break;
    case P_ABOUT: build_about(p, &r); break;
    case P_HELP: build_help(p, &r); break;
    default: break;
    }
    rb_end(&r);
    rich_set(p->rich, &r);
    if (p->type == P_FIND) {
        char t[300];
        if (p->have_fr && !p->fr_rc)
            snprintf(t, sizeof t, "%s - Find - %d Matches", g_image, p->fr.total);
        else
            snprintf(t, sizeof t, "%s - Find", g_image);
        SetWindowTextA(p->hwnd, t);
    }
}

/* ======================================================================
 * find worker thread (SIV runs its finds on a worker thread too)
 * ====================================================================== */
typedef struct { HWND hwnd; char report[1100], pattern[512]; int icase, fixed, max; srs_findres fr; int rc; char err[1400]; } findjob;

static DWORD WINAPI find_worker(LPVOID arg)
{
    findjob *j = arg;
    j->rc = srs_find(j->report[0] ? j->report : NULL, j->pattern, j->icase, j->fixed, j->max, &j->fr, j->err, sizeof j->err);
    PostMessageA(j->hwnd, WM_APP_FINDDONE, 0, (LPARAM)j);
    return 0;
}

static void start_find(panel *p)
{
    findjob *j;
    HANDLE th;
    char mx[16];
    if (p->busy) return;
    j = calloc(1, sizeof *j);
    if (!j) return;
    j->hwnd = p->hwnd;
    GetWindowTextA(p->top[1], j->pattern, sizeof j->pattern);
    GetWindowTextA(p->top[10], j->report, sizeof j->report);
    GetWindowTextA(p->top[8], mx, sizeof mx);
    j->max = atoi(mx) > 0 ? atoi(mx) : 20;
    j->icase = SendMessageA(p->top[2], BM_GETCHECK, 0, 0) == BST_CHECKED;
    j->fixed = SendMessageA(p->top[3], BM_GETCHECK, 0, 0) == BST_CHECKED;
    snprintf(p->fr_pattern, sizeof p->fr_pattern, "%s", j->pattern);
    p->busy = 1;
    render(p);
    th = CreateThread(NULL, 0, find_worker, j, 0, NULL);
    if (!th) {
        p->busy = 0;
        p->fr_rc = 2;
        p->have_fr = 1;
        snprintf(p->fr_err, sizeof p->fr_err, "Failed to Create Worker Thread");
        free(j);
        render(p);
        return;
    }
    CloseHandle(th);
}

static void find_done(panel *p, findjob *j)
{
    if (p->have_fr) srs_find_free(&p->fr);
    p->fr = j->fr;
    p->fr_rc = j->rc;
    snprintf(p->fr_err, sizeof p->fr_err, "%s", j->err);
    p->have_fr = 1;
    p->busy = 0;
    free(j);
    render(p);
    if (p->nmatch) SendMessageA(p->hwnd, WM_COMMAND, IDM_NEXT, 0);
}

static void goto_match(panel *p, int dir)
{
    CHARRANGE cr;
    if (!p->nmatch) { MessageBeep(MB_OK); return; }
    p->curmatch = (p->curmatch + dir + p->nmatch) % p->nmatch;
    cr.cpMin = p->mstart[p->curmatch];
    cr.cpMax = p->mend[p->curmatch];
    SendMessageA(p->rich, EM_EXSETSEL, 0, (LPARAM)&cr);
    SendMessageA(p->rich, EM_SCROLLCARET, 0, 0);
    SendMessageA(p->rich, EM_HIDESELECTION, FALSE, 0);
    {
        char t[300];
        snprintf(t, sizeof t, "%s - Find - Match %d of %d shown (%d total)", g_image, p->curmatch + 1, p->nmatch, p->fr.total);
        SetWindowTextA(p->hwnd, t);
    }
}

/* ======================================================================
 * windows
 * ====================================================================== */
static HWND mk(const char *cls, const char *text, DWORD style, HWND parent, int id)
{
    HWND h = CreateWindowExA(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, parent,
                             (HMENU)(INT_PTR)id, g_inst, NULL);
    SendMessageA(h, WM_SETFONT, (WPARAM)g_font, TRUE);
    return h;
}

static void create_top(panel *p)
{
    char rep[1100] = "";
    p->ntop = 0;
    if (p->type == P_LOOKUP) {
        p->top[p->ntop++] = mk("STATIC", "ID", SS_RIGHT, p->hwnd, -1);
        p->top[p->ntop++] = mk("EDIT", "1002:7550", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, p->hwnd, IDC_EDIT);
        p->top[p->ntop++] = mk("BUTTON", "", BS_AUTO3STATE | WS_TABSTOP, p->hwnd, IDM_MODE);
        p->top[p->ntop++] = mk("BUTTON", "PCI", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, p->hwnd, IDM_BUSPCI);
        p->top[p->ntop++] = mk("BUTTON", "USB", BS_AUTORADIOBUTTON, p->hwnd, IDM_BUSUSB);
        p->top[p->ntop++] = mk("BUTTON", "&Lookup", BS_DEFPUSHBUTTON | WS_TABSTOP, p->hwnd, IDM_RUN);
        p->top[p->ntop++] = mk("STATIC", mode_text(MODE_TREE), 0, p->hwnd, IDC_MODELBL);
        SendMessageA(p->top[2], BM_SETCHECK, BST_CHECKED, 0);
        SendMessageA(p->top[3], BM_SETCHECK, BST_CHECKED, 0);
    } else if (p->type == P_FIND) {
        srs_latest_report(rep, sizeof rep);
        p->top[p->ntop++] = mk("STATIC", "Match", SS_RIGHT, p->hwnd, -1);                                          /* 0 */
        p->top[p->ntop++] = mk("EDIT", "RX 9070", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, p->hwnd, IDC_EDIT);       /* 1 */
        p->top[p->ntop++] = mk("BUTTON", "Ignore &case", BS_AUTOCHECKBOX | WS_TABSTOP, p->hwnd, IDC_ICASE);         /* 2 */
        p->top[p->ntop++] = mk("BUTTON", "Li&teral", BS_AUTOCHECKBOX | WS_TABSTOP, p->hwnd, IDC_FIXED);             /* 3 */
        p->top[p->ntop++] = mk("BUTTON", "Highlight &Matches", BS_AUTOCHECKBOX | WS_TABSTOP, p->hwnd, IDC_HL);      /* 4 */
        p->top[p->ntop++] = mk("BUTTON", "&Find", BS_DEFPUSHBUTTON | WS_TABSTOP, p->hwnd, IDM_RUN);                 /* 5 */
        p->top[p->ntop++] = mk("STATIC", "", 0, p->hwnd, -1);                                                       /* 6 spare */
        p->top[p->ntop++] = mk("STATIC", "Max", SS_RIGHT, p->hwnd, -1);                                             /* 7 */
        p->top[p->ntop++] = mk("EDIT", "20", WS_BORDER | WS_TABSTOP | ES_NUMBER, p->hwnd, IDC_MAX);                  /* 8 */
        p->top[p->ntop++] = mk("STATIC", "Report", SS_RIGHT, p->hwnd, -1);                                          /* 9 */
        p->top[p->ntop++] = mk("EDIT", rep, WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, p->hwnd, IDC_REPORT);          /* 10 */
        p->top[p->ntop++] = mk("BUTTON", "&Browse...", WS_TABSTOP, p->hwnd, IDM_BROWSE);                            /* 11 */
        p->top[p->ntop++] = mk("BUTTON", "L&atest", WS_TABSTOP, p->hwnd, IDM_LATEST);                               /* 12 */
        SendMessageA(p->top[4], BM_SETCHECK, g_highlight ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}

static int top_height(panel *p) { return p->type == P_FIND ? 58 : p->type == P_LOOKUP ? 30 : 0; }

static void layout(panel *p)
{
    RECT rc;
    int w, h, i, x, th = top_height(p), bh = 21, by;
    GetClientRect(p->hwnd, &rc);
    w = rc.right; h = rc.bottom;
    by = h - bh - 4;
    if (p->type == P_LOOKUP) {
        MoveWindow(p->top[0], 6, 8, 20, 18, TRUE);
        MoveWindow(p->top[1], 30, 5, 200, 21, TRUE);
        MoveWindow(p->top[2], 240, 6, 16, 18, TRUE);
        MoveWindow(p->top[6], 258, 8, 330, 18, TRUE);
        MoveWindow(p->top[3], 600, 6, 46, 18, TRUE);
        MoveWindow(p->top[4], 648, 6, 46, 18, TRUE);
        MoveWindow(p->top[5], 704, 4, 80, 23, TRUE);
    } else if (p->type == P_FIND) {
        MoveWindow(p->top[0], 6, 8, 40, 18, TRUE);
        MoveWindow(p->top[1], 50, 5, 330, 21, TRUE);
        MoveWindow(p->top[2], 392, 6, 92, 18, TRUE);
        MoveWindow(p->top[3], 488, 6, 66, 18, TRUE);
        MoveWindow(p->top[4], 558, 6, 128, 18, TRUE);
        MoveWindow(p->top[5], 694, 4, 80, 23, TRUE);
        MoveWindow(p->top[6], 0, 0, 0, 0, TRUE);
        MoveWindow(p->top[7], 6, 35, 40, 18, TRUE);
        MoveWindow(p->top[8], 50, 32, 50, 21, TRUE);
        MoveWindow(p->top[9], 106, 35, 44, 18, TRUE);
        MoveWindow(p->top[10], 154, 32, w - 154 - 190, 21, TRUE);
        MoveWindow(p->top[11], w - 182, 31, 84, 23, TRUE);
        MoveWindow(p->top[12], w - 94, 31, 84, 23, TRUE);
    }
    MoveWindow(p->rich, 4, th + 4, w - 8, by - th - 8, TRUE);
    for (i = 0, x = 6; i < p->nbtn; i++, x += 74) MoveWindow(p->btn[i], x, by, 72, bh, TRUE);
}

static const button *buttons_for(ptype t)
{
    switch (t) {
    case P_MAIN: return main_btns;
    case P_PCI: case P_USB: case P_ALL: return dev_btns;
    case P_LOOKUP: return lookup_btns;
    case P_FIND: return find_btns;
    default: return info_btns;
    }
}

static panel *panel_of(HWND h)
{
    int i;
    for (i = 0; i < P_COUNT; i++)
        if (g_p[i].hwnd == h) return &g_p[i];
    return NULL;
}

static void copy_all(panel *p)
{
    CHARRANGE old, all = { 0, -1 };
    SendMessageA(p->rich, EM_EXGETSEL, 0, (LPARAM)&old);
    SendMessageA(p->rich, EM_EXSETSEL, 0, (LPARAM)&all);
    SendMessageA(p->rich, WM_COPY, 0, 0);
    SendMessageA(p->rich, EM_EXSETSEL, 0, (LPARAM)&old);
}

static void drilldown(panel *p, LPARAM lp)
{
    POINTL pt;
    LONG ch, line;
    int i;
    pt.x = (short)LOWORD(lp);
    pt.y = (short)HIWORD(lp);
    ch = (LONG)SendMessageA(p->rich, EM_CHARFROMPOS, 0, (LPARAM)&pt);
    line = (LONG)SendMessageA(p->rich, EM_EXLINEFROMCHAR, 0, ch);
    for (i = 0; i < p->nrow; i++) {
        if (p->rowline[i] == line) {
            panel *lp2;
            open_panel(P_LOOKUP);
            lp2 = &g_p[P_LOOKUP];
            SetWindowTextA(lp2->top[1], p->rowkey[i]);
            SendMessageA(lp2->top[3], BM_SETCHECK, p->rowbus[i] == BUS_PCI ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageA(lp2->top[4], BM_SETCHECK, p->rowbus[i] == BUS_USB ? BST_CHECKED : BST_UNCHECKED, 0);
            render(lp2);
            return;
        }
    }
}

static void context_menu(panel *p, int x, int y)
{
    HMENU m = CreatePopupMenu();
    AppendMenuA(m, MF_STRING, IDM_COPY, "&Copy All");
    AppendMenuA(m, MF_STRING, WM_COPY + 50000, "Copy &Selection\tCtrl+C");
    AppendMenuA(m, MF_STRING, IDM_SELALL, "Select &All");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING, IDM_FIND, "&Find...\tCtrl+F");
    AppendMenuA(m, MF_STRING, IDM_LOOKUP, "&Lookup...\tCtrl+L");
    AppendMenuA(m, MF_STRING, IDM_ALL, "ALL &Dev\tCtrl+D");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING | (g_highlight ? MF_CHECKED : 0), IDM_HILITE, "&Highlight Matches\tCtrl+H");
    AppendMenuA(m, MF_STRING, IDM_COLOUR, "Colour Find &Text...");
    AppendMenuA(m, MF_STRING, IDM_REFRESH, "&Refresh\tF5");
    TrackPopupMenu(m, TPM_RIGHTBUTTON, x, y, 0, p->hwnd, NULL);
    DestroyMenu(m);
}

static void choose_colour(panel *p)
{
    static COLORREF custom[16];
    CHOOSECOLORA cc;
    int i;
    memset(&cc, 0, sizeof cc);
    cc.lStructSize = sizeof cc;
    cc.hwndOwner = p->hwnd;
    cc.rgbResult = g_pal[C_FIND];
    cc.lpCustColors = custom;
    cc.Flags = CC_RGBINIT | CC_FULLOPEN;
    if (!ChooseColorA(&cc)) return;
    g_pal[C_FIND] = cc.rgbResult;
    settings_save();
    for (i = 0; i < P_COUNT; i++)
        if (g_p[i].hwnd && (i == P_FIND || i == P_DATA)) render(&g_p[i]);
}

static void browse_report(panel *p)
{
    BROWSEINFOA bi;
    LPITEMIDLIST pidl;
    char path[MAX_PATH];
    memset(&bi, 0, sizeof bi);
    bi.hwndOwner = p->hwnd;
    bi.lpszTitle = "Select a sysreport folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    pidl = SHBrowseForFolderA(&bi);
    if (!pidl) return;
    if (SHGetPathFromIDListA(pidl, path)) SetWindowTextA(p->top[10], path);
    CoTaskMemFree(pidl);
}

static void toggle_highlight(void)
{
    int i;
    g_highlight = !g_highlight;
    settings_save();
    if (g_p[P_FIND].hwnd) {
        SendMessageA(g_p[P_FIND].top[4], BM_SETCHECK, g_highlight ? BST_CHECKED : BST_UNCHECKED, 0);
        render(&g_p[P_FIND]);
    }
    for (i = 0; i < P_COUNT; i++) (void)i;
}

static LRESULT CALLBACK panel_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    panel *p = panel_of(h);
    switch (msg) {
    case WM_SIZE:
        if (p) layout(p);
        return 0;
    case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN: {
        static HBRUSH bg;
        if (!bg) bg = CreateSolidBrush(BG_COLOUR);
        SetBkColor((HDC)wp, BG_COLOUR);
        SetTextColor((HDC)wp, g_pal[C_VALUE]);
        return (LRESULT)bg;
    }
    case WM_APP_FINDDONE:
        if (p) find_done(p, (findjob *)lp);
        return 0;
    case WM_NOTIFY: {
        NMHDR *nh = (NMHDR *)lp;
        if (p && nh->idFrom == IDC_RICH && nh->code == EN_MSGFILTER) {
            MSGFILTER *mf = (MSGFILTER *)lp;
            if (mf->msg == WM_RBUTTONUP) {
                POINT pt = { (short)LOWORD(mf->lParam), (short)HIWORD(mf->lParam) };
                ClientToScreen(p->rich, &pt);
                context_menu(p, pt.x, pt.y);
                return 1;
            }
            if (mf->msg == WM_LBUTTONDBLCLK && (p->type == P_PCI || p->type == P_USB || p->type == P_ALL)) {
                drilldown(p, mf->lParam);
                return 1;
            }
        }
        return 0;
    }
    case WM_COMMAND:
        if (!p) break;
        switch (LOWORD(wp)) {
        case IDCANCEL:   /* Esc closes a panel; the main window closes only from OK */
            if (p->type != P_MAIN) DestroyWindow(h);
            return 0;
        case IDM_CLOSE:
            DestroyWindow(h);
            return 0;
        case IDOK: case IDM_RUN:
            if (p->type == P_FIND) start_find(p);
            else if (p->type == P_LOOKUP) render(p);
            return 0;
        case IDM_COPY: copy_all(p); return 0;
        case WM_COPY + 50000: SendMessageA(p->rich, WM_COPY, 0, 0); return 0;
        case IDM_SELALL: { CHARRANGE all = { 0, -1 }; SendMessageA(p->rich, EM_EXSETSEL, 0, (LPARAM)&all); return 0; }
        case IDM_PCI: open_panel(P_PCI); return 0;
        case IDM_USB: open_panel(P_USB); return 0;
        case IDM_ALL: open_panel(P_ALL); return 0;
        case IDM_LOOKUP: open_panel(P_LOOKUP); return 0;
        case IDM_FIND: open_panel(P_FIND); return 0;
        case IDM_DATA: open_panel(P_DATA); return 0;
        case IDM_ABOUT: open_panel(P_ABOUT); return 0;
        case IDM_HELP: open_panel(P_HELP); return 0;
        case IDM_REFRESH: render(p); return 0;
        case IDM_HILITE: toggle_highlight(); return 0;
        case IDC_HL:
            if (HIWORD(wp) == BN_CLICKED && (SendMessageA(p->top[4], BM_GETCHECK, 0, 0) == BST_CHECKED) != g_highlight)
                toggle_highlight();
            return 0;
        case IDM_COLOUR: choose_colour(p); return 0;
        case IDM_NEXT: if (p->type == P_FIND) goto_match(p, +1); else if (g_p[P_FIND].hwnd) goto_match(&g_p[P_FIND], +1); return 0;
        case IDM_PREV: if (p->type == P_FIND) goto_match(p, -1); else if (g_p[P_FIND].hwnd) goto_match(&g_p[P_FIND], -1); return 0;
        case IDM_BROWSE: browse_report(p); return 0;
        case IDM_LATEST: {
            char rep[1100] = "";
            srs_latest_report(rep, sizeof rep);
            SetWindowTextA(p->top[10], rep);
            return 0;
        }
        case IDM_MODE:
            if (HIWORD(wp) == BN_CLICKED) { SetWindowTextA(p->top[6], mode_text(lookup_mode(p))); render(p); }
            return 0;
        case IDM_BUSPCI: case IDM_BUSUSB:
            if (HIWORD(wp) == BN_CLICKED) render(p);
            return 0;
        }
        break;
    case WM_DESTROY:
        if (p) {
            int was_main = p->type == P_MAIN;
            if (p->have_fr) srs_find_free(&p->fr);
            free(p->rowline); free(p->rowkey); free(p->rowbus); free(p->mstart); free(p->mend);
            memset(p, 0, sizeof *p);
            if (was_main) PostQuitMessage(0);
        }
        return 0;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

static void open_panel(ptype t)
{
    panel *p = &g_p[t];
    const button *b;
    char title[300];
    int x, y, w, hgt;
    if (p->hwnd) {
        ShowWindow(p->hwnd, SW_SHOWNORMAL);
        SetForegroundWindow(p->hwnd);
        if (t != P_FIND && t != P_LOOKUP) render(p);
        return;
    }
    memset(p, 0, sizeof *p);
    p->type = t;
    if (t == P_MAIN) snprintf(title, sizeof title, "%s - System Report Search V%s  %s", g_image, SRS_VERSION, g_host);
    else snprintf(title, sizeof title, "%s - %s", g_image, ptitle[t]);
    w = t == P_MAIN ? 1000 : 1180;
    hgt = t == P_MAIN ? 420 : 560;
    x = t == P_MAIN ? 80 : 40 + (int)t * 24;
    y = t == P_MAIN ? 60 : 40 + (int)t * 22;
    p->hwnd = CreateWindowExA(0, "SRSPanel", title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                              x, y, w, hgt, t == P_MAIN ? NULL : g_p[P_MAIN].hwnd, NULL, g_inst, NULL);
    p->rich = CreateWindowExA(WS_EX_CLIENTEDGE, g_richclass, "",
                              WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY |
                              ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_NOHIDESEL,
                              0, 0, 10, 10, p->hwnd, (HMENU)IDC_RICH, g_inst, NULL);
    SendMessageA(p->rich, EM_SETBKGNDCOLOR, 0, BG_COLOUR);
    SendMessageA(p->rich, EM_SETEVENTMASK, 0, ENM_MOUSEEVENTS);
    SendMessageA(p->rich, EM_EXLIMITTEXT, 0, 64 * 1024 * 1024);
    create_top(p);
    for (b = buttons_for(t); b->label; b++)
        p->btn[p->nbtn++] = mk("BUTTON", b->label, BS_PUSHBUTTON | WS_TABSTOP, p->hwnd, b->id);
    layout(p);
    render(p);
    ShowWindow(p->hwnd, SW_SHOWNORMAL);
    UpdateWindow(p->hwnd);
    if (t == P_LOOKUP || t == P_FIND) SetFocus(p->top[1]);
}

/* ======================================================================
 * entry
 * ====================================================================== */
static int console_mode(void)
{
    /* GUI-subsystem image used from a shell: reuse inherited handles, else attach to the parent console */
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if ((!out || out == INVALID_HANDLE_VALUE) && AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
    {
        int rc = srs_cli_main(__argc, __argv);
        fflush(stdout);
        fflush(stderr);
        return rc;
    }
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASSA wc;
    MSG m;
    char path[1100], *base, *dot;
    DWORD hl = sizeof g_host;
    int b;
    (void)prev; (void)cmd; (void)show;

    g_inst = inst;
    GetModuleFileNameA(NULL, path, sizeof path);
    base = strrchr(path, '\\');
    base = base ? base + 1 : path;
    if ((dot = strrchr(base, '.')) && (!_stricmp(dot, ".exe") || !_stricmp(dot, ".so"))) *dot = 0;
    if ((dot = strrchr(base, '.')) && !_stricmp(dot, ".exe")) *dot = 0;
    snprintf(g_image, sizeof g_image, "%s", base);
    if (strlen(g_image) <= 8) CharUpperA(g_image);

    if (__argc > 1) return console_mode();

    if (!GetComputerNameA(g_host, &hl)) snprintf(g_host, sizeof g_host, "localhost");
    settings_load();
    if (LoadLibraryA("Msftedit.dll")) g_richclass = "RICHEDIT50W";
    else { LoadLibraryA("riched20.dll"); g_richclass = "RichEdit20A"; }
    CoInitialize(NULL);
    for (b = 0; b < 2; b++) g_dbok[b] = srs_db_load(&g_db[b], b ? BUS_USB : BUS_PCI) == 0;
    {
        HDC dc = GetDC(NULL);
        int px = -MulDiv(8, GetDeviceCaps(dc, LOGPIXELSY), 72);
        ReleaseDC(NULL, dc);
        g_font = CreateFontA(px, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Tahoma");
        g_fontb = CreateFontA(px, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Tahoma");
    }
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = panel_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconA(inst, MAKEINTRESOURCEA(1));
    if (!wc.hIcon) wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hbrBackground = CreateSolidBrush(BG_COLOUR);
    wc.lpszClassName = "SRSPanel";
    RegisterClassA(&wc);
    g_accel = LoadAcceleratorsA(inst, MAKEINTRESOURCEA(1));
    open_panel(P_MAIN);
    {   /* SIV-style start-up options: -find=PATTERN is not needed; panels open from buttons/hot-keys */
        const char *autop = getenv("SRS_OPEN");
        if (autop) {
            if (strstr(autop, "pci")) open_panel(P_PCI);
            if (strstr(autop, "usb")) open_panel(P_USB);
            if (strstr(autop, "all")) open_panel(P_ALL);
            if (strstr(autop, "lookup")) open_panel(P_LOOKUP);
            if (strstr(autop, "find")) open_panel(P_FIND);
            if (strstr(autop, "data")) open_panel(P_DATA);
            if (strstr(autop, "about")) open_panel(P_ABOUT);
            if (strstr(autop, "help")) open_panel(P_HELP);
        }
    }
    while (GetMessageA(&m, NULL, 0, 0) > 0) {
        HWND root = GetAncestor(m.hwnd, GA_ROOT);
        if (root && g_accel && TranslateAcceleratorA(root, g_accel, &m)) continue;
        if (root && IsDialogMessageA(root, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    for (b = 0; b < 2; b++) if (g_dbok[b]) srs_db_free(&g_db[b]);
    CoUninitialize();
    return (int)m.wParam;
}
