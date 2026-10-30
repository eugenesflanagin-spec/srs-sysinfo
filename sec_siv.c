/* sec_siv.c - SIV's own program items, re-pointed at srs-gui:
 * [about] [help] [hot-keys] [highlight] [rgb-debug] [menus] [siv-*] [image-*] [manifest] [install]
 * [tune] [tuning] [csv] [csv-titles] [initial] [final] [sequence] [words] [emoji] [languages]
 * [filter] [delta] [debug-api] [lck-setup] [mutex] [states] [makers] [map-*] [fak-*]
 * and the xxb snapshot buffers [xxb] [sgb-ini] [sgb-end] [smb-ini] [smb-end] [sdm] [dsb] [bsi]
 * [pmb] [tzm] [cgb] [gpu] [stc].   ([cpu] xxb lives in sec_cpu.c.)
 * Sources: /proc/self (exe, maps, task, status, locks), SRSDEF.ini, hwdata *.ids headers,
 * readelf on our own image, /proc/sys, /sys/class/{dmi,block,hwmon,thermal,power_supply,drm}.
 * Read-only; no network, no driver, no i2c/hidraw. */
#define _GNU_SOURCE
#include "sec.h"
#include "lists.h"
#include "srs.h"

#include <ctype.h>
#include <dirent.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

/* ======================================================================
 * helpers
 * ====================================================================== */
static const char *exe_path(void)
{
    static char b[1100];
    ssize_t n;
    if (*b) return b;
    if ((n = readlink("/proc/self/exe", b, sizeof b - 1)) > 0) b[n] = 0;
    else snprintf(b, sizeof b, "srs-gui");
    return b;
}

static const char *ini_path(void)
{
    static char p[1100];
    const char *cfg = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    if (*p) return p;
    if (cfg && *cfg) snprintf(p, sizeof p, "%s/srs/SRSDEF.ini", cfg);
    else snprintf(p, sizeof p, "%s/.config/srs/SRSDEF.ini", home ? home : "");
    return p;
}

/* value of key in [section] of SRSDEF.ini; 1 found */
static int ini_get(const char *sect, const char *key, char *out, size_t sz)
{
    char *txt, *p, *save, cur[128] = "";
    int found = 0;
    if (rd_all(ini_path(), &txt) < 0) return 0;
    for (p = strtok_r(txt, "\n", &save); p && !found; p = strtok_r(NULL, "\n", &save)) {
        char *eq;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '[') { snprintf(cur, sizeof cur, "%.*s", (int)strcspn(p + 1, "]"), p + 1); continue; }
        if (*p == '#' || *p == ';' || strcmp(cur, sect) || !(eq = strchr(p, '='))) continue;
        {
            char k[128];
            snprintf(k, sizeof k, "%.*s", (int)(eq - p), p);
            while (*k && k[strlen(k) - 1] == ' ') k[strlen(k) - 1] = 0;
            if (!strcmp(k, key)) {
                eq++;
                while (*eq == ' ') eq++;
                snprintf(out, sz, "%s", eq);
                out[strcspn(out, "\r")] = 0;
                found = 1;
            }
        }
    }
    free(txt);
    return found;
}

static void ini_def(const char *key, const char *def, char *out, size_t sz)
{
    if (!ini_get("SRSDEF", key, out, sz)) snprintf(out, sz, "%s", def);
}

static void trimsp(char *s);

/* "# Version: x" / "# Date: y" headers of an .ids file */
static int ids_header(const char *path, char *ver, size_t vsz, char *date, size_t dsz)
{
    FILE *f = fopen(path, "r");
    char line[512];
    int i;
    *ver = *date = 0;
    if (!f) return 0;
    for (i = 0; i < 40 && fgets(line, sizeof line, f); i++) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "#\tVersion:", 10) || !strncmp(line, "# Version:", 10)) snprintf(ver, vsz, "%s", line + 11);
        if (!strncmp(line, "#\tDate:", 7) || !strncmp(line, "# Date:", 7)) snprintf(date, dsz, "%s", line + 8);
    }
    fclose(f);
    trimsp(ver);
    trimsp(date);
    return 1;
}

static void trimsp(char *s)
{
    char *p = s;
    size_t l;
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    l = strlen(s);
    while (l && (s[l - 1] == ' ' || s[l - 1] == '\t' || s[l - 1] == '\n')) s[--l] = 0;
}

typedef struct { const char *desc, *file, *url; } datafile;

static void data_file_path(int i, char *out, size_t sz)
{
    if (i == 0) { if (!srs_db_find_file(BUS_PCI, out, sz)) snprintf(out, sz, "%s/pci.ids", plat_hwdata_dir()); }
    else if (i == 1) { if (!srs_db_find_file(BUS_USB, out, sz)) snprintf(out, sz, "%s/usb.ids", plat_hwdata_dir()); }
    else if (!srs_ids_path("pnp.ids", out, sz, NULL)) snprintf(out, sz, "%s/pnp.ids", plat_hwdata_dir());
}

static const datafile g_data[] = {
    { "PCI Device Names", "pci.ids", "https://pci-ids.ucw.cz/v2.2/pci.ids" },
    { "USB Device Names", "usb.ids", "http://www.linux-usb.org/usb.ids" },
    { "PnP / Monitor Names", "pnp.ids", "https://uefi.org/pnp_id_list" },
};

static void data_ident(int i, char *path, size_t psz, char *id, size_t isz)
{
    char ver[128], date[128];
    struct stat st;
    data_file_path(i, path, psz);
    if (stat(path, &st)) { snprintf(id, isz, "not found"); return; }
    ids_header(path, ver, sizeof ver, date, sizeof date);
    if (*ver || *date) snprintf(id, isz, "Date %s  Version %s", *date ? date : "?", *ver ? ver : "?");
    else {
        char t[32];
        strftime(t, sizeof t, "%Y-%m-%d", localtime(&st.st_mtime));
        snprintf(id, isz, "File date %s", t);
    }
}

/* distinct mapped shared objects of this process */
static int maps_libs(char names[][512], int max)
{
    char *txt, *p, *save;
    int n = 0, i;
    if (rd_all("/proc/self/maps", &txt) < 0) return 0;
    for (p = strtok_r(txt, "\n", &save); p; p = strtok_r(NULL, "\n", &save)) {
        char *path = strchr(p, '/');
        int dup = 0;
        if (!path || !strstr(path, ".so") || strstr(path, "ld.so.cache")) continue;
        for (i = 0; i < n; i++) if (!strcmp(names[i], path)) { dup = 1; break; }
        if (!dup && n < max) snprintf(names[n++], 512, "%s", path);
    }
    free(txt);
    return n;
}

static void file_size_str(const char *path, char *b, size_t sz)
{
    struct stat st;
    if (stat(path, &st)) snprintf(b, sz, "-");
    else fmt_bytes((double)st.st_size, b, sz);
}

static void file_date_str(const char *path, char *b, size_t sz)
{
    struct stat st;
    if (stat(path, &st)) snprintf(b, sz, "-");
    else strftime(b, sz, "%Y-%m-%d %H:%M:%S", localtime(&st.st_mtime));
}

static const char *user_name(void)
{
    const char *u = getenv("USER");
    return u && *u ? u : "user";
}

static void hostname_s(char *b, size_t sz) { if (gethostname(b, sz)) snprintf(b, sz, "localhost"); }

/* process start (seconds since boot) and uptime */
static double proc_elapsed(void)
{
    char *txt, *p;
    double up = 0, start = 0;
    long hz = sysconf(_SC_CLK_TCK);
    int f;
    if (rd_all("/proc/uptime", &txt) >= 0) { up = atof(txt); free(txt); }
    if (rd_all("/proc/self/stat", &txt) >= 0) {
        p = strrchr(txt, ')');
        for (f = 2; p && *p && f < 22; p++) if (*p == ' ') f++;
        if (p) start = atof(p) / (double)(hz > 0 ? hz : 100);
        free(txt);
    }
    return up - start;
}

/* ======================================================================
 * [about]
 * ====================================================================== */
static void s_about(srs_out *o)
{
    static const int tabs[] = { 160 };
    struct utsname u;
    char host[128], b[1400], path[1100], id[300];
    int i;
    hostname_s(host, sizeof host);
    so_tabs(o, tabs, 1);
    so_f(o, C_VALUE, "srs-gui - System Report Search V%s %s::%s", SRS_VERSION, host, user_name());
    so_par(o); so_par(o);
    so_kv(o, "Program", C_VALUE, "srs-gui / sysreport-search (one image: GUI without arguments, CLI with them)");
    so_kv(o, "Version", C_TEAL, "V" SRS_VERSION);
    so_kv(o, "Program Build", C_ORANGE, plat_build_info());
    so_kv(o, "Built", C_ORANGE, __DATE__ " " __TIME__);
    file_size_str(exe_path(), b, sizeof b);
    so_kvf(o, "Image", C_PURPLE, "%s  (%s)", exe_path(), b);
    if (!uname(&u)) so_kvf(o, "Running on", C_VALUE, "%s %s %s %s", u.sysname, u.release, u.version, u.machine);
    so_kv(o, "Operating System", C_VALUE, sys_os_name());
    so_kv(o, "Machine", C_VALUE, sys_board());
    so_kv(o, "Processor", C_VALUE, sys_cpu_name());
    so_par(o);
    for (i = 0; i < 3; i++) {
        data_ident(i, path, sizeof path, id, sizeof id);
        snprintf(b, sizeof b, "%s  %s", path, id);
        so_kv(o, g_data[i].desc, C_TEAL, b);
    }
    so_kv(o, "Settings", C_PURPLE, ini_path());
    so_kv(o, "Live data", C_VALUE, "sysfs, procfs, CPUID, hwmon, systemd-hwdb; root-only data from the srs-root helper's cache");
    so_par(o);
    so_kv(o, "Created by", C_GREEN, "Eugene Flanagin");
    so_kv(o, "Licensed under", C_GREEN, "GNU General Public License v2.0 or later (GPL-2.0-or-later)");
    so_kv(o, "Home page", C_TEAL, "https://github.com/eugenesflanagin-spec/srs-sysinfo");
    so_par(o);
    so_kv(o, "Why SRS", C_GREEN, "I wanted a Linux version of SIV (System Information Viewer) for myself, so I built one and set out to improve on it along the way.");
    so_kv(o, "", C_GREEN, "SRS is free and open source for the Linux and open-source community.");
    so_kv(o, "", C_GREEN, "(SRS is an independent project, not affiliated with SIV or its author.)");
    so_kv(o, "Credit", C_GREEN, "SIV is by Ray Hinchliffe, RH Software - https://rh-software.com - with thanks for the original.");
    so_kv(o, "", C_GRAY, "Layout and item names modelled on SIV64X's observable output (-save dump, panels, strings).");
    so_kv(o, "", C_GRAY, "No SIV code, binaries or data files are used, included or disassembled.");
    so_kv(o, "", C_GRAY, "SRS is not affiliated with, endorsed or supported by RH Software; please do not report SRS issues to them.");
    so_kv(o, "Disclaimer", C_GRAY, "This program is free software and you use it entirely at your own risk.");
}

/* ======================================================================
 * [help]
 * ====================================================================== */
static void s_help(srs_out *o)
{
    static const int tabs[] = { 160 };
    so_tabs(o, tabs, 1);
    so_siv_header(o, "Help");
    so_b(o, C_VALUE, "Main window buttons"); so_par(o);
    so_kv(o, "OK", C_TEAL, "Close the panel (Esc)");
    so_kv(o, "Copy", C_TEAL, "Copy the panel text (or the selection) to the clipboard");
    so_kv(o, "PCI Bus / USB Bus", C_TEAL, "Every present device, resolved V:D:S:s:RR -> V:D:S:s -> V:D:RR -> V:D -> V");
    so_kv(o, "ALL Dev", C_TEAL, "Both buses (Ctrl+D); double-click a row to open that ID in Lookup");
    so_kv(o, "Lookup", C_TEAL, "One device ID (Ctrl+L): Tree (bsearch), Scan (re-read .ids file) or Info (systemd-hwdb)");
    so_kv(o, "Find", C_TEAL, "Regex / literal search of the newest ~/sysreports report on a worker thread (Ctrl+F)");
    so_kv(o, "Data Files", C_TEAL, "pci.ids / usb.ids / pnp.ids versions, program image, settings file, reports folder");
    so_kv(o, "About / Help", C_TEAL, "Program and build information / this text (F1)");
    so_kv(o, "Menu", C_TEAL, "Every SIV item as a section (see [menus]); Run as root starts the srs-root cache helper");
    so_kv(o, "Refresh", C_TEAL, "Re-read the data of the current panel (F5); live sections refresh on their own timer");
    so_kv(o, "Next / Prev", C_TEAL, "Next / previous Find match (F3 / Shift+F3)");
    so_kv(o, "Highlight / Colour", C_TEAL, "Toggle match highlighting (Ctrl+H) / pick the Colour Find Text (saved to SRSDEF.ini)");
    so_par(o);
    so_b(o, C_VALUE, "Hot-keys"); so_par(o);
    so_kv(o, "Ctrl+F", C_PURPLE, "Find");
    so_kv(o, "Ctrl+L", C_PURPLE, "Lookup");
    so_kv(o, "Ctrl+D", C_PURPLE, "ALL Dev");
    so_kv(o, "F3 / Shift+F3", C_PURPLE, "Next / previous match");
    so_kv(o, "Ctrl+H", C_PURPLE, "Highlight Matches on/off");
    so_kv(o, "F5", C_PURPLE, "Refresh");
    so_kv(o, "F1", C_PURPLE, "Help");
    so_kv(o, "Esc", C_PURPLE, "Close the panel");
    so_par(o);
    so_b(o, C_VALUE, "Command line"); so_par(o);
    so_kv(o, "srs-gui", C_TEAL, "Start the GUI");
    so_kv(o, "-save[=[item]...][=file|STDOUT]", C_TEAL, "Write sections as text like SIV64X -save; [all] = every item");
    so_kv(o, "id ID", C_TEAL, "Device-ID lookup [--bus pci|usb] [--mode tree|scan|info]");
    so_kv(o, "find PATTERN", C_TEAL, "Search the report [--report DIR] [-i] [-F] [--max N] [--color auto|always|never]");
    so_kv(o, "devices", C_TEAL, "Resolve every local device [--bus pci|usb|all]");
    so_kv(o, "burn", C_TEAL, "CPU burn-in [--threads N | --cpus 0,2,5] [--seconds S] [--mode same|swap] [--limit C] [--cool S] [--cycles N]");
    so_kv(o, "--list", C_TEAL, "List every item id, category, bucket and group");
    so_kv(o, "--version / -h", C_TEAL, "Version and build / usage");
    so_par(o);
    so_kv(o, "Files", C_GRAY, "Settings in ~/.config/srs/SRSDEF.ini; root cache in ~/.cache/srs/root; reports in ~/sysreports.");
}

/* ======================================================================
 * [hot-keys] [highlight] [rgb-debug]
 * ====================================================================== */
static void s_hotkeys(srs_out *o)
{
    static const int tabs[] = { 35, 210, 340 };
    static const char *k[][3] = {
        { "F", "Find", "Ctrl+F" }, { "L", "Lookup", "Ctrl+L" }, { "D", "ALL Dev", "Ctrl+D" },
        { "t", "Highlight Matches", "Ctrl+H" }, { "n", "Next Match", "F3" }, { "p", "Previous Match", "Shift+F3" },
        { "R", "Refresh", "F5" }, { "H", "Help", "F1" }, { "X", "Close Panel", "Esc" }, { "C", "Copy Selection", "Ctrl+C" },
    };
    size_t i;
    so_tabs(o, tabs, 3);
    so_siv_header(o, "Hotkeys");
    so_t(o, C_LABEL, ""); so_tab(o); so_t(o, C_LABEL, "Profile"); so_tab(o); so_check(o, 1); so_t(o, C_LABEL, "Hotkeys Enabled");
    so_par(o); so_par(o);
    for (i = 0; i < sizeof k / sizeof k[0]; i++) {
        so_f(o, C_TEAL, "|%s|", k[i][0]); so_tab(o);
        so_f(o, C_VALUE, "[%-17s]", k[i][1]); so_tab(o);
        so_t(o, C_PURPLE, k[i][2]); so_tab(o);
        so_t(o, C_GRAY, "panel accelerator (fixed)");
        so_par(o);
    }
    so_par(o);
    so_note(o, "srs-gui's keys are window accelerators; SIV's global hot-key profiles (LEDs, screens) have no equivalent.");
}

static void s_highlight(srs_out *o)
{
    static const int tabs[] = { 35, 90, 300, 460 };
    char col[64], hl[16], font[128];
    ini_def("Colour Find Text", "FFFF00", col, sizeof col);
    ini_def("Highlight Matches", "1", hl, sizeof hl);
    ini_def("Font Name", "Tahoma", font, sizeof font);
    so_tabs(o, tabs, 4);
    so_siv_header(o, "Highlight Setup");
    so_row(o, 5, (const char *[]){ "", "Colour", "Find Description", "Setting", "Reported Value" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    so_t(o, C_TEAL, "[F]"); so_tab(o); so_f(o, C_ORANGE, "#%s", col); so_tab(o); so_t(o, C_VALUE, "Colour Find Text"); so_tab(o);
    so_t(o, C_PURPLE, "Colour Find Text"); so_tab(o); so_f(o, C_VALUE, "RGB( %ld, %ld, %ld )", strtol(col, NULL, 16) >> 16 & 255,
                                                                   strtol(col, NULL, 16) >> 8 & 255, strtol(col, NULL, 16) & 255);
    so_par(o);
    so_t(o, C_TEAL, ""); so_tab(o); so_check(o, atoi(hl) != 0); so_tab(o); so_t(o, C_VALUE, "Highlight Matches"); so_tab(o);
    so_t(o, C_PURPLE, "Highlight Matches"); so_tab(o); so_t(o, C_VALUE, atoi(hl) ? "on" : "off"); so_par(o);
    so_t(o, C_TEAL, ""); so_tab(o); so_tab(o); so_t(o, C_VALUE, "Panel Font"); so_tab(o);
    so_t(o, C_PURPLE, "Font Name"); so_tab(o); so_t(o, C_VALUE, font); so_par(o);
    so_par(o);
    so_note(o, path_exists(ini_path()) ? ini_path() : "SRSDEF.ini not written yet - defaults shown");
    so_note(o, "SIV's CPU/GPU speed highlight colours map to the coloured columns of [cpu] and [gpu-clocks].");
}

static void s_rgb_debug(srs_out *o)
{
    static const struct { const char *name, *rgb, *use; } pal[] = {
        { "C_LABEL", "3399FF", "labels / row names" }, { "C_VALUE", "0000CC", "values" },
        { "C_PURPLE", "9C0FB8", "paths, IDs, command lines" }, { "C_TEAL", "009999", "secondary values" },
        { "C_ORANGE", "FF8800", "build / warnings" }, { "C_GREEN", "00A000", "OK / enabled" },
        { "C_RED", "E00000", "errors / not found" }, { "C_BLACK", "000000", "plain text" },
        { "C_GRAY", "808080", "notes, disabled" },
    };
    static const int tabs[] = { 110, 260, 340 };
    char col[64];
    size_t i;
    so_tabs(o, tabs, 3);
    so_siv_header(o, "Colour RGB Debug");
    so_row(o, 4, (const char *[]){ "Colour", "Default Colour", "Hex", "Colour Description" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (i = 0; i < sizeof pal / sizeof pal[0]; i++) {
        long v = strtol(pal[i].rgb, NULL, 16);
        char rgb[64];
        snprintf(rgb, sizeof rgb, "RGB( %ld, %ld, %ld )", v >> 16 & 255, v >> 8 & 255, v & 255);
        so_row(o, 4, (const char *[]){ pal[i].name, rgb, pal[i].rgb, pal[i].use }, (const int[]){ C_VALUE, C_TEAL, C_ORANGE, C_GRAY });
    }
    ini_def("Colour Find Text", "FFFF00", col, sizeof col);
    so_row(o, 4, (const char *[]){ "Find Text", "(SRSDEF.ini)", col, "Colour Find Text match background" }, (const int[]){ C_VALUE, C_TEAL, C_ORANGE, C_GRAY });
}

/* ======================================================================
 * [menus]
 * ====================================================================== */
static void s_menus(srs_out *o)
{
    static const int tabs[] = { 160, 420, 520 };
    static const char *bk[] = { "Linux", "Absent", "Windows-only" };
    const char *cats[64];
    int ncat = 0, i, j, n = srs_section_count();
    so_tabs(o, tabs, 3);
    so_siv_header(o, "Menus");
    for (i = 0; i < n; i++) {
        const srs_item *s = srs_section_at(i);
        int seen = 0;
        for (j = 0; j < ncat; j++) if (!strcmp(cats[j], s->category)) { seen = 1; break; }
        if (!seen && ncat < 64) cats[ncat++] = s->category;
    }
    for (j = 0; j < ncat; j++) {
        int cnt = 0;
        for (i = 0; i < n; i++) if (!strcmp(srs_section_at(i)->category, cats[j])) cnt++;
        so_f(o, C_VALUE, "[%s]", cats[j]); so_tab(o); so_f(o, C_GRAY, "%d items", cnt); so_par(o);
        for (i = 0; i < n; i++) {
            const srs_item *s = srs_section_at(i);
            if (strcmp(s->category, cats[j])) continue;
            so_f(o, C_PURPLE, "  [%s]", s->id); so_tab(o);
            so_t(o, C_VALUE, s->title); so_tab(o);
            so_t(o, s->bucket == B_LINUX ? C_GREEN : C_GRAY, bk[s->bucket]); so_tab(o);
            so_t(o, C_TEAL, s->group ? s->group : "");
            if (!s->fn) so_t(o, C_GRAY, s->bucket == B_LINUX ? "  (stub)" : "");
            so_par(o);
        }
        so_par(o);
    }
    so_f(o, C_GRAY, "%d items in %d categories", n, ncat);
    so_par(o);
}

/* ======================================================================
 * settings: [siv-ini] [siv-setup] [siv-usage] [siv-location]
 * ====================================================================== */
static void s_siv_ini(srs_out *o)
{
    static const int tabs[] = { 200 };
    char *txt, *p, *save;
    int nsec = 0;
    so_tabs(o, tabs, 1);
    so_t(o, C_VALUE, "  "); so_siv_header(o, "running");
    if (rd_all(ini_path(), &txt) < 0) {
        so_t(o, C_RED, "  No Sections Found within the SRSDEF.ini file"); so_par(o); so_par(o);
        so_t(o, C_RED, "  Status 2 File not found."); so_par(o); so_par(o);
        so_f(o, C_PURPLE, "  %s", ini_path()); so_par(o);
        return;
    }
    for (p = strtok_r(txt, "\n", &save); p; p = strtok_r(NULL, "\n", &save)) {
        char line[1024];
        snprintf(line, sizeof line, "%s", p);
        trimsp(line);
        line[strcspn(line, "\r")] = 0;
        if (!*line) continue;
        if (*line == '[') { if (nsec++) so_par(o); so_b(o, C_VALUE, line); so_par(o); continue; }
        if (*line == '#' || *line == ';') { so_note(o, line); continue; }
        if (strchr(line, '=')) {
            char *eq = strchr(line, '=');
            *eq = 0;
            trimsp(line);
            so_f(o, C_LABEL, "  %s", line); so_tab(o); so_t(o, C_TEAL, eq + 1); so_par(o);
        }
    }
    free(txt);
    so_par(o);
    so_f(o, C_GRAY, "  %d section%s in %s", nsec, nsec == 1 ? "" : "s", ini_path());
    so_par(o);
}

static void s_siv_setup(srs_out *o)
{
    static const struct { const char *key, *def, *meaning; } k[] = {
        { "Colour Find Text", "FFFF00", "Find match background (RRGGBB)" },
        { "Highlight Matches", "1", "Highlight Find matches" },
        { "Font Name", "Tahoma", "Panel font" },
        { "SIO Sensors", "", "Re-enabled Super-I/O inputs, e.g. nct6799:AUXTIN1,..." },
        { "GPU Load Command", "", "Program started by the burner's GPU load option" },
        { "Tray Icon", "1", "Show the system tray icon" },
        { "Close To Tray", "0", "Close button hides to the tray" },
        { "Minimize To Tray", "1", "Minimize hides to the tray (X11 / XWayland)" },
        { "Start In Tray", "0", "Start hidden in the tray" },
        { "Theme", "Light", "Light / Dark / System (follow the desktop)" },
        { "Update Interval", "0", "Live panel refresh in ms (0 = each panel's own rate, -1 = paused)" },
        { "Redact Reports", "0", "Mask serials, UUIDs, MACs and the host name in saved reports" },
    };
    static const int tabs[] = { 30, 180, 340 };
    char v[512];
    size_t i;
    so_tabs(o, tabs, 3);
    {
        char what[1300];
        snprintf(what, sizeof what, "Showing %u Settings from %s", (unsigned)(sizeof k / sizeof k[0]), ini_path());
        so_siv_header(o, what);
    }
    so_row(o, 4, (const char *[]){ "", "Name", "Data", "Meaning" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (i = 0; i < sizeof k / sizeof k[0]; i++) {
        int set = ini_get("SRSDEF", k[i].key, v, sizeof v);
        so_t(o, set ? C_GREEN : C_GRAY, set ? "|X|" : "|_|"); so_tab(o);
        so_t(o, C_VALUE, k[i].key); so_tab(o);
        if (set) so_f(o, C_TEAL, "\"%s\"", v);
        else so_f(o, C_GRAY, "<default %s>", *k[i].def ? k[i].def : "none");
        so_tab(o);
        so_t(o, C_GRAY, k[i].meaning);
        so_par(o);
    }
    so_par(o);
    so_note(o, "|X| = present in SRSDEF.ini (SIV keeps these under HKCU\\Software\\SIV).");
}

static void s_siv_usage(srs_out *o)
{
    static const int tabs[] = { 200, 360 };
    char d1[64], d2[64];
    so_tabs(o, tabs, 2);
    so_row(o, 3, (const char *[]){ "Version", "Image Date", "Settings Last Written" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    file_date_str(exe_path(), d1, sizeof d1);
    file_date_str(ini_path(), d2, sizeof d2);
    so_row(o, 3, (const char *[]){ "   V" SRS_VERSION, d1, d2 }, (const int[]){ C_VALUE, C_TEAL, C_TEAL });
    so_par(o);
    so_note(o, "srs-gui keeps no usage counter; dates come from the image and SRSDEF.ini modification times.");
}

static void s_siv_location(srs_out *o)
{
    static const int tabs[] = { 160 };
    char b[256];
    so_tabs(o, tabs, 1);
    so_siv_header(o, "Window Location");
    so_kv(o, "Main window", C_VALUE, "1000 x 420 default size, placed by the window manager");
    so_kv(o, "Panels", C_VALUE, "1180 x 560 default size, cascaded +24/+22 from 40,40 (X11; Wayland compositors place them)");
    so_kv(o, "Saved positions", C_GRAY, "none - srs-gui does not persist window positions (SIV's Reset Positions not needed)");
    so_kv(o, "Session", C_TEAL, getenv("WAYLAND_DISPLAY") ? "Wayland" : getenv("DISPLAY") ? "X11" : "no display (CLI)");
    if (getenv("XDG_CURRENT_DESKTOP")) so_kv(o, "Desktop", C_TEAL, getenv("XDG_CURRENT_DESKTOP"));
    snprintf(b, sizeof b, "%s", getenv("GDK_SCALE") ? getenv("GDK_SCALE") : "1 (default)");
    so_kv(o, "GDK_SCALE", C_TEAL, b);
    so_kv(o, "Window poke", C_GRAY, "not supported (no cross-process window messages on Linux)");
}

/* ======================================================================
 * [siv-active]
 * ====================================================================== */
static int is_srs_comm(const char *c)
{
    return !strcmp(c, "srs-gui") || !strcmp(c, "srs-test") || !strncmp(c, "sysreport-searc", 15) || !strcmp(c, "srs-root") ||
           !strncmp(c, "SRS64X", 6) || !strncmp(c, "SRS32X", 6);
}

static void s_siv_active(srs_out *o)
{
    static const int tabs[] = { 110, 190, 300 };
    char **names;
    int n = dir_list("/proc", &names), i, found = 0;
    so_tabs(o, tabs, 3);
    so_siv_header(o, "Active Instances of srs-gui");
    so_t(o, C_GRAY, "srs-gui needs no kernel driver, so different versions can run side by side."); so_par(o); so_par(o);
    so_row(o, 4, (const char *[]){ "Name", "PID", "Version", "Image Filename" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (i = 0; i < n; i++) {
        char p[300], comm[64], exe[1100] = "";
        ssize_t l;
        if (!isdigit((unsigned char)names[i][0])) continue;
        snprintf(p, sizeof p, "/proc/%s/comm", names[i]);
        if (!rd_str(p, comm, sizeof comm) || !is_srs_comm(comm)) continue;
        snprintf(p, sizeof p, "/proc/%s/exe", names[i]);
        if ((l = readlink(p, exe, sizeof exe - 1)) > 0) exe[l] = 0; else snprintf(exe, sizeof exe, "(not readable)");
        so_row(o, 4, (const char *[]){ comm, names[i], atoi(names[i]) == getpid() ? "V" SRS_VERSION " (this)" : "-", exe },
               (const int[]){ C_VALUE, C_TEAL, C_GREEN, C_PURPLE });
        found++;
    }
    dir_free(names, n);
    so_par(o);
    so_f(o, C_GRAY, "%d instance%s found by /proc/*/comm", found, found == 1 ? "" : "s");
    so_par(o);
}

/* ======================================================================
 * [siv-sizes]
 * ====================================================================== */
static void s_siv_sizes(srs_out *o)
{
    static const int tabs[] = { 160, 240 };
    static const struct { const char *n; size_t s; const char *d; } t[] = {
        { "srs_item", sizeof(srs_item), "one SIV item / section" },
        { "srs_out", sizeof(srs_out), "output sink (GUI, text, -save)" },
        { "srs_db", sizeof(srs_db), "loaded .ids database" },
        { "srs_rec", sizeof(srs_rec), "one database record" },
        { "srs_result", sizeof(srs_result), "Lookup resolution chain" },
        { "srs_info", sizeof(srs_info), "Info (hwdb) lookup" },
        { "srs_findres", sizeof(srs_findres), "Find result header" },
        { "srs_hit", sizeof(srs_hit), "one Find hit" },
        { "srs_dev", sizeof(srs_dev), "one local device" },
        { "void *", sizeof(void *), "pointer" },
        { "long", sizeof(long), "long" },
        { "size_t", sizeof(size_t), "size_t" },
    };
    size_t i;
    char b[32];
    so_tabs(o, tabs, 2);
    so_siv_header(o, "Structure Sizes");
    so_row(o, 3, (const char *[]){ "Structure", "Bytes", "Use" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL });
    for (i = 0; i < sizeof t / sizeof t[0]; i++) {
        snprintf(b, sizeof b, "%zu", t[i].s);
        so_row(o, 3, (const char *[]){ t[i].n, b, t[i].d }, (const int[]){ C_VALUE, C_TEAL, C_GRAY });
    }
    so_par(o);
    so_kvf(o, "Items", C_TEAL, "%d sections registered", srs_section_count());
    so_kvf(o, "Page size", C_TEAL, "%ld", sysconf(_SC_PAGESIZE));
}

/* ======================================================================
 * images: [siv-dll] [image-exe] [image-tree] [manifest] [image-sys] [siv-drv] [siv-srv]
 * ====================================================================== */
static void s_siv_dll(srs_out *o)
{
    static const int tabs[] = { 150, 470, 760, 840 };
    char libs[64][512], b[1400], path[1100], id[300];
    int n = maps_libs(libs, 64), i;
    so_tabs(o, tabs, 4);
    so_siv_header(o, "Module Files");
    so_row(o, 5, (const char *[]){ "Description", "File Specification", "Current Identification", "Code Page", "WEB Download Location" },
           (const int[]){ C_LABEL, C_PURPLE, C_TEAL, C_ORANGE, C_VALUE });
    for (i = 0; i < 3; i++) {
        data_ident(i, path, sizeof path, id, sizeof id);
        so_row(o, 5, (const char *[]){ g_data[i].desc, path, id, "UTF-8", g_data[i].url }, (const int[]){ C_LABEL, C_PURPLE, C_TEAL, C_ORANGE, C_VALUE });
    }
    file_size_str(exe_path(), b, sizeof b);
    snprintf(id, sizeof id, "V%s  %s", SRS_VERSION, b);
    so_row(o, 5, (const char *[]){ "srs-gui Program", exe_path(), id, "UTF-8", "(local build)" }, (const int[]){ C_LABEL, C_PURPLE, C_TEAL, C_ORANGE, C_VALUE });
    file_date_str(ini_path(), b, sizeof b);
    so_row(o, 5, (const char *[]){ "Settings", ini_path(), path_exists(ini_path()) ? b : "not written yet", "UTF-8", "-" },
           (const int[]){ C_LABEL, C_PURPLE, C_TEAL, C_ORANGE, C_VALUE });
    so_row(o, 5, (const char *[]){ "Kernel driver", "none", "srs-gui uses no kernel driver; root data via srs-root helper", "", "" },
           (const int[]){ C_LABEL, C_PURPLE, C_TEAL, C_ORANGE, C_VALUE });
    so_par(o);
    so_b(o, C_VALUE, "Shared objects mapped into this process"); so_par(o);
    for (i = 0; i < n; i++) {
        char sz[32], dt[64];
        const char *base = strrchr(libs[i], '/');
        file_size_str(libs[i], sz, sizeof sz);
        file_date_str(libs[i], dt, sizeof dt);
        snprintf(b, sizeof b, "%s  %s", sz, dt);
        so_row(o, 3, (const char *[]){ base ? base + 1 : libs[i], libs[i], b }, (const int[]){ C_LABEL, C_PURPLE, C_TEAL });
    }
    so_par(o);
    so_note(o, "No update check is made (no network access); WEB columns show the upstream location only.");
}

static void s_image_exe(srs_out *o)
{
    const char *argv[] = { "readelf", "-S", "-W", exe_path(), NULL };
    char *out = NULL, sz[32];
    int rc;
    so_siv_header(o, "Image Sections");
    file_size_str(exe_path(), sz, sizeof sz);
    so_f(o, C_PURPLE, "%s", exe_path()); so_t(o, C_TEAL, "  "); so_t(o, C_TEAL, sz); so_par(o); so_par(o);
    rc = run_out(&out, argv);
    if (rc < 0 || !out || !*out) so_err(o, "readelf not available (binutils)");
    else so_lines(o, C_VALUE, out);
    free(out);
}

static void readelf_needed(const char *file, char names[][128], int *n, int max)
{
    const char *argv[] = { "readelf", "-d", "-W", file, NULL };
    char *out = NULL, *p, *save;
    *n = 0;
    if (run_out(&out, argv) < 0 || !out) { free(out); return; }
    for (p = strtok_r(out, "\n", &save); p; p = strtok_r(NULL, "\n", &save)) {
        char *a, *e;
        if (!strstr(p, "(NEEDED)") || !(a = strchr(p, '[')) || !(e = strchr(a, ']'))) continue;
        if (*n < max) { snprintf(names[*n], 128, "%.*s", (int)(e - a - 1), a + 1); (*n)++; }
    }
    free(out);
}

static void s_image_tree(srs_out *o)
{
    static const int tabs[] = { 260 };
    char libs[64][512], need[32][128], sub[32][128];
    int nl = maps_libs(libs, 64), nn, ns, i, j, k;
    so_tabs(o, tabs, 1);
    so_siv_header(o, "Image Tree");
    so_t(o, C_VALUE, strrchr(exe_path(), '/') ? strrchr(exe_path(), '/') + 1 : exe_path()); so_tab(o); so_t(o, C_PURPLE, exe_path()); so_par(o);
    readelf_needed(exe_path(), need, &nn, 32);
    for (i = 0; i < nn; i++) {
        const char *full = "(not loaded)";
        for (k = 0; k < nl; k++) { const char *b = strrchr(libs[k], '/'); if (b && !strcmp(b + 1, need[i])) { full = libs[k]; break; } }
        so_f(o, C_LABEL, "  +- %s", need[i]); so_tab(o); so_t(o, C_PURPLE, full); so_par(o);
        if (*full == '/') {
            readelf_needed(full, sub, &ns, 32);
            for (j = 0; j < ns; j++) { so_f(o, C_TEAL, "  |    +- %s", sub[j]); so_par(o); }
        }
    }
    if (!nn) so_note(o, "readelf -d found no NEEDED entries (binutils missing or static image)");
    so_par(o);
    so_f(o, C_GRAY, "%d shared objects mapped (from /proc/self/maps); ldd is not run since it executes the image.", nl);
    so_par(o);
}

static void s_manifest(srs_out *o)
{
    const char *argv[] = { "readelf", "-n", "-W", exe_path(), NULL };
    char *out = NULL;
    so_siv_header(o, "Image Notes");
    so_note(o, "Windows-only in SIV (application manifest). Linux equivalent: the ELF note sections of the image.");
    so_par(o);
    if (run_out(&out, argv) < 0 || !out || !*out) so_err(o, "readelf not available (binutils)");
    else so_lines(o, C_VALUE, out);
    free(out);
}

static const char *const g_mods[] = { "k10temp", "zenpower", "coretemp", "amdgpu", "i915", "nvidia", "nct6775", "nct6683", "nct6687",
                                      "it87", "asus_ec_sensors", "asus_wmi_sensors", "spd5118", "jc42", "ee1004", "ryzen_smu", "msr",
                                      "cpuid", "drivetemp", "nvme", "acpi_cpufreq", "amd_pstate", "zenergy", NULL };

static void s_image_sys(srs_out *o)
{
    static const int tabs[] = { 160, 260 };
    int i;
    so_tabs(o, tabs, 2);
    so_siv_header(o, "Kernel Driver Image");
    so_kv(o, "Status", C_ORANGE, "srs-gui uses no kernel driver; root data via srs-root helper");
    so_kv(o, "SIV equivalent", C_GRAY, "SIVX64.sys - not needed: Linux exposes the same data through kernel drivers in sysfs");
    so_par(o);
    so_b(o, C_VALUE, "Kernel modules srs-gui reads data from (/sys/module)"); so_par(o);
    for (i = 0; g_mods[i]; i++) {
        char p[256], v[128] = "", rc[32] = "";
        int loaded;
        snprintf(p, sizeof p, "/sys/module/%s", g_mods[i]);
        loaded = path_exists(p);
        if (!loaded) continue;
        rd_fmt(v, sizeof v, "/sys/module/%s/version", g_mods[i]);
        rd_fmt(rc, sizeof rc, "/sys/module/%s/refcnt", g_mods[i]);
        so_t(o, C_LABEL, g_mods[i]); so_tab(o);
        so_t(o, C_GREEN, *rc ? "loaded" : "built-in"); so_tab(o);
        if (*v) so_f(o, C_TEAL, "version %s  ", v);
        if (*rc) so_f(o, C_GRAY, "refcnt %s", rc);
        so_par(o);
    }
}

static void s_siv_drv(srs_out *o)
{
    static const int tabs[] = { 200 };
    char **names, p[1100], b[64];
    const char *h = getenv("HOME");
    int n, i;
    so_tabs(o, tabs, 1);
    so_siv_header(o, "Driver");
    so_kv(o, "Kernel driver", C_ORANGE, "srs-gui uses no kernel driver; root data via srs-root helper");
    so_kv(o, "Root helper", C_VALUE, "srs-root (Menu > Run as root) writes a read-only cache; sections never run as root");
    snprintf(p, sizeof p, "%s/.cache/srs/root", h ? h : "");
    so_kv(o, "Root cache", C_PURPLE, p);
    n = dir_list(p, &names);
    if (n <= 0) so_kv(o, "Cache state", C_RED, "empty - Needs root: Menu > Run as root (srs-root)");
    for (i = 0; i < n; i++) {
        char f[1300], sz[32];
        snprintf(f, sizeof f, "%s/%s", p, names[i]);
        file_size_str(f, sz, sizeof sz);
        file_date_str(f, b, sizeof b);
        so_t(o, C_LABEL, names[i]); so_tab(o); so_f(o, C_TEAL, "%s  %s", sz, b); so_par(o);
    }
    dir_free(names, n);
}

static void s_siv_srv(srs_out *o)
{
    static const int tabs[] = { 200 };
    so_tabs(o, tabs, 1);
    so_siv_header(o, "Driver Service");
    so_kv(o, "Status", C_ORANGE, "Windows-only in SIV (SIVDriver service control)");
    so_kv(o, "srs-gui", C_VALUE, "srs-gui uses no kernel driver and installs no service; root data via srs-root helper");
    so_kv(o, "Helper start", C_TEAL, "on demand from Menu > Run as root (polkit prompt); exits after writing the cache");
    so_kv(o, "Nearest Linux", C_GRAY, "kernel modules in /sys/module - see [image-sys]");
}

static void s_map_note(srs_out *o, const char *what)
{
    static const int tabs[] = { 200 };
    so_tabs(o, tabs, 1);
    so_siv_header(o, what);
    so_kv(o, "Status", C_ORANGE, "Windows-only in SIV (SIV kernel driver physical memory mapping)");
    so_kv(o, "srs-gui", C_VALUE, "srs-gui uses no kernel driver and never maps physical memory (/dev/mem is not opened)");
    so_kv(o, "Nearest Linux", C_GRAY, "kernel drivers export registers via sysfs; root-only tables come from the srs-root cache");
}
static void s_map_cache(srs_out *o) { s_map_note(o, "Mapping Cache"); }
static void s_map_unmap(srs_out *o) { s_map_note(o, "Map/Unmap Test"); }

/* ======================================================================
 * [siv-qualifiers] [siv-quals] [siv-use] [siv-autorun] [install]
 * ====================================================================== */
static const char *const g_quals[][2] = {
    { "-save", "Save the default item set to srs-save.txt style output on stdout" },
    { "-save=[item][item]...", "Save just those items; [all] saves every item" },
    { "-save=[...]=file|STDOUT", "Write the save to a file, or STDOUT" },
    { "id ID", "Device-ID lookup, most specific key first (VVVV[:DDDD[:SSSS:ssss]][:RR])" },
    { "  --bus pci|usb", "Bus for id (Default is --bus pci)" },
    { "  --mode tree|scan|info", "Tree = bsearch, Scan = re-read file, Info = systemd-hwdb (Default is tree)" },
    { "find PATTERN", "Regex search across the latest report's sections" },
    { "  --report DIR", "Report folder (Default is the newest in ~/sysreports)" },
    { "  -i  -F  --max N", "Ignore case, literal (fixed string), max matches shown" },
    { "  --color auto|always|never", "Colour the matches (Default is auto)" },
    { "devices", "Resolve every local device [--bus pci|usb|all] (Default is all)" },
    { "burn", "CPU burn-in [--threads N | --cpus L] [--seconds S] [--mode same|swap] [--limit C] [--cool S] [--cycles N]" },
    { "--list", "List every item: id, category, bucket, group, title" },
    { "--version", "Program version and build" },
    { "-h, --help", "Usage" },
};

static void s_qualifiers(srs_out *o)
{
    static const int tabs[] = { 230 };
    size_t i;
    so_tabs(o, tabs, 1);
    so_siv_header(o, "Qualifiers");
    so_f(o, C_LABEL, "Qualifier"); so_tab(o); so_f(o, C_LABEL, "Meaning for srs-gui V%s", SRS_VERSION); so_par(o);
    for (i = 0; i < sizeof g_quals / sizeof g_quals[0]; i++) {
        so_t(o, C_PURPLE, g_quals[i][0]); so_tab(o); so_t(o, C_VALUE, g_quals[i][1]); so_par(o);
    }
    so_par(o);
    so_kv(o, "Command Used", C_TEAL, exe_path());
}

static void s_quals(srs_out *o)
{
    size_t i;
    so_siv_header(o, "Qualifier List");
    for (i = 0; i < sizeof g_quals / sizeof g_quals[0]; i++) {
        if (g_quals[i][0][0] == ' ') continue;
        so_f(o, C_PURPLE, "%s", g_quals[i][0]);
        so_par(o);
    }
}

static void s_siv_use(srs_out *o)
{
    static const int tabs[] = { 170, 470 };
    char path[1100], id[300];
    int i;
    so_tabs(o, tabs, 2);
    so_siv_header(o, "Use / Update");
    so_kv(o, "Program", C_VALUE, "srs-gui V" SRS_VERSION " (local build; no update channel)");
    so_kv(o, "Update check", C_ORANGE, "not performed - srs-gui makes no network access");
    so_par(o);
    for (i = 0; i < 3; i++) {
        data_ident(i, path, sizeof path, id, sizeof id);
        so_t(o, C_LABEL, g_data[i].desc); so_tab(o); so_t(o, C_TEAL, id); so_tab(o); so_t(o, C_VALUE, g_data[i].url); so_par(o);
    }
    so_par(o);
    so_note(o, "Update the .ids files with the distribution's hwdata package (or update-pciids / update-usbids).");
}

static void desktop_exec(const char *path, char *out, size_t sz)
{
    char *txt, *p, *save;
    *out = 0;
    if (rd_all(path, &txt) < 0) return;
    for (p = strtok_r(txt, "\n", &save); p; p = strtok_r(NULL, "\n", &save))
        if (!strncmp(p, "Exec=", 5)) { snprintf(out, sz, "%s", p + 5); break; }
    free(txt);
}

static void s_siv_autorun(srs_out *o)
{
    static const int tabs[] = { 40, 220 };
    const char *h = getenv("HOME");
    char p[1100], ex[1100];
    const char *dirs[] = { "%s/.config/autostart/srs-gui.desktop", "/etc/xdg/autostart/srs-gui.desktop" };
    int i, on = 0;
    so_tabs(o, tabs, 2);
    so_siv_header(o, "Autorun");
    for (i = 0; i < 2; i++) {
        int ex_ok;
        if (i == 0) snprintf(p, sizeof p, dirs[0], h ? h : "");
        else snprintf(p, sizeof p, "%s", dirs[1]);
        ex_ok = path_exists(p);
        on |= ex_ok;
        so_check(o, ex_ok); so_tab(o); so_t(o, C_LABEL, i ? "System autostart" : "User autostart"); so_tab(o); so_t(o, C_PURPLE, p); so_par(o);
        if (ex_ok) { desktop_exec(p, ex, sizeof ex); so_tab(o); so_t(o, C_LABEL, "Exec"); so_tab(o); so_t(o, C_TEAL, ex); so_par(o); }
    }
    so_par(o);
    so_kv(o, "", C_GRAY, on ? "srs-gui starts at login." : "srs-gui does not start at login (copy srs-gui.desktop to ~/.config/autostart to enable).");
}

static void s_install(srs_out *o)
{
    static const int tabs[] = { 40, 220 };
    const char *h = getenv("HOME");
    char p[1100], ex[1100];
    int i;
    so_tabs(o, tabs, 2);
    so_siv_header(o, "Install");
    so_t(o, C_LABEL, ""); so_tab(o); so_t(o, C_LABEL, "Program image"); so_tab(o); so_t(o, C_PURPLE, exe_path()); so_par(o);
    for (i = 0; i < 3; i++) {
        if (i == 0) snprintf(p, sizeof p, "%s/.local/share/applications/srs-gui.desktop", h ? h : "");
        else if (i == 1) snprintf(p, sizeof p, "/usr/share/applications/srs-gui.desktop");
        else snprintf(p, sizeof p, "/usr/local/share/applications/srs-gui.desktop");
        so_check(o, path_exists(p)); so_tab(o); so_t(o, C_LABEL, "Menu entry"); so_tab(o); so_t(o, C_PURPLE, p); so_par(o);
        if (path_exists(p)) { desktop_exec(p, ex, sizeof ex); so_tab(o); so_t(o, C_LABEL, "Exec"); so_tab(o); so_t(o, C_TEAL, ex); so_par(o); }
    }
    snprintf(p, sizeof p, "%s/.config/srs", h ? h : "");
    so_check(o, path_exists(p)); so_tab(o); so_t(o, C_LABEL, "Settings folder"); so_tab(o); so_t(o, C_PURPLE, p); so_par(o);
    snprintf(p, sizeof p, "%s/.cache/srs/root", h ? h : "");
    so_check(o, path_exists(p)); so_tab(o); so_t(o, C_LABEL, "Root cache folder"); so_tab(o); so_t(o, C_PURPLE, p); so_par(o);
    so_par(o);
    so_note(o, "Installed with 'make install' (menu entry with an absolute Exec path); no installer or driver setup.");
}

/* ======================================================================
 * tuning: [tune] [tuning]
 * ====================================================================== */
static const struct { const char *name, *def, *other; } g_tune[] = {
    { "vm.swappiness", "60", "10" },
    { "vm.vfs_cache_pressure", "100", "50" },
    { "vm.dirty_ratio", "20", "10" },
    { "vm.dirty_background_ratio", "10", "5" },
    { "vm.dirty_bytes", "0", "268435456" },
    { "vm.dirty_background_bytes", "0", "67108864" },
    { "vm.page-cluster", "3", "0" },
    { "vm.max_map_count", "65530", "2147483642" },
    { "vm.compaction_proactiveness", "20", "0" },
    { "vm.watermark_boost_factor", "15000", "0" },
    { "vm.overcommit_memory", "0", "1" },
    { "vm.zone_reclaim_mode", "0", "0" },
    { "kernel.nmi_watchdog", "1", "0" },
    { "kernel.watchdog", "1", "0" },
    { "kernel.sched_autogroup_enabled", "1", "0" },
    { "kernel.split_lock_mitigate", "1", "0" },
    { "kernel.numa_balancing", "1", "0" },
    { "kernel.kptr_restrict", "0", "1" },
    { "kernel.dmesg_restrict", "0", "1" },
    { "kernel.perf_event_paranoid", "2", "1" },
    { "kernel.unprivileged_bpf_disabled", "2", "1" },
    { "kernel.printk", "4 4 1 7", "3 3 3 3" },
    { "kernel.timer_migration", "1", "0" },
    { "fs.inotify.max_user_watches", "8192", "524288" },
    { "fs.file-max", "-", "2097152" },
    { "net.core.default_qdisc", "pfifo_fast", "fq" },
    { "net.ipv4.tcp_congestion_control", "cubic", "bbr" },
    { "net.core.rmem_max", "212992", "16777216" },
    { "net.core.wmem_max", "212992", "16777216" },
    { "net.core.netdev_max_backlog", "1000", "16384" },
    { "net.ipv4.tcp_fastopen", "1", "3" },
    { "net.ipv4.tcp_mtu_probing", "0", "1" },
};

static void sysctl_path(const char *name, char *out, size_t sz)
{
    char *p;
    snprintf(out, sz, "/proc/sys/%s", name);
    for (p = out + 10; *p; p++) if (*p == '.') *p = '/';
}

static void s_tune(srs_out *o)
{
    static const int tabs[] = { 260, 360, 520, 620 };
    size_t i;
    so_tabs(o, tabs, 4);
    so_row(o, 5, (const char *[]){ "|A|Tune-System - Value Name", "Default", "Current", "Other", "Path" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (i = 0; i < sizeof g_tune / sizeof g_tune[0]; i++) {
        char p[256], v[256];
        int ok;
        sysctl_path(g_tune[i].name, p, sizeof p);
        ok = rd_str(p, v, sizeof v);
        { char *t; for (t = v; *t; t++) if (*t == '\t') *t = ' '; }
        so_t(o, C_VALUE, g_tune[i].name); so_tab(o);
        so_f(o, C_TEAL, "[%s]", g_tune[i].def); so_tab(o);
        so_t(o, !ok ? C_GRAY : strcmp(v, g_tune[i].def) ? C_ORANGE : C_GREEN, ok ? v : "<none>"); so_tab(o);
        so_f(o, C_TEAL, "[%s]", g_tune[i].other); so_tab(o);
        so_t(o, C_PURPLE, p);
        so_par(o);
    }
    so_par(o);
    so_note(o, "Read-only: [Apply] is not offered. Default = kernel default, Other = a common tuned value; orange = changed.");
}

static void s_tuning(srs_out *o)
{
    static const int tabs[] = { 200 };
    char b[512], **names;
    int n, i;
    so_tabs(o, tabs, 1);
    so_siv_header(o, "Tuning");
    if (rd_str("/etc/tuned/active_profile", b, sizeof b)) so_kv(o, "tuned profile", C_TEAL, b);
    else if (rd_str("/etc/tuned/profile_mode", b, sizeof b)) so_kv(o, "tuned mode", C_TEAL, b);
    else so_kv(o, "tuned profile", C_GRAY, "tuned not active");
    if (rd_str("/sys/devices/system/cpu/cpu0/cpufreq/scaling_driver", b, sizeof b)) so_kv(o, "cpufreq driver", C_TEAL, b);
    if (rd_str("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", b, sizeof b)) so_kv(o, "Governor (CPU-0)", C_TEAL, b);
    if (rd_str("/sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference", b, sizeof b)) so_kv(o, "EPP (CPU-0)", C_TEAL, b);
    if (rd_str("/sys/devices/system/cpu/amd_pstate/status", b, sizeof b)) so_kv(o, "amd_pstate", C_TEAL, b);
    if (rd_str("/sys/devices/system/cpu/cpufreq/boost", b, sizeof b)) so_kv(o, "Boost", C_TEAL, atoi(b) ? "enabled" : "disabled");
    if (rd_str("/sys/firmware/acpi/platform_profile", b, sizeof b)) so_kv(o, "Platform profile", C_TEAL, b);
    if (rd_str("/sys/kernel/sched_ext/state", b, sizeof b)) {
        char ops[128] = "";
        rd_str("/sys/kernel/sched_ext/root/ops", ops, sizeof ops);
        so_kvf(o, "sched_ext", C_TEAL, "%s%s%s", b, *ops ? "  " : "", ops);
    }
    if (rd_str("/sys/kernel/mm/transparent_hugepage/enabled", b, sizeof b)) so_kv(o, "THP enabled", C_TEAL, b);
    if (rd_str("/sys/kernel/mm/transparent_hugepage/defrag", b, sizeof b)) so_kv(o, "THP defrag", C_TEAL, b);
    if (rd_str("/sys/kernel/mm/ksm/run", b, sizeof b)) so_kv(o, "KSM run", C_TEAL, b);
    if (rd_str("/sys/module/pcie_aspm/parameters/policy", b, sizeof b)) so_kv(o, "PCIe ASPM policy", C_TEAL, b);
    if (rd_str("/proc/sys/vm/swappiness", b, sizeof b)) so_kv(o, "vm.swappiness", C_TEAL, b);
    n = dir_list("/sys/class/block", &names);
    for (i = 0; i < n; i++) {
        char p[300], s[256];
        if (!strncmp(names[i], "loop", 4) || !strncmp(names[i], "ram", 3) || !strncmp(names[i], "zram", 4)) continue;
        snprintf(p, sizeof p, "/sys/class/block/%s/queue/scheduler", names[i]);
        if (!rd_str(p, s, sizeof s)) continue;
        snprintf(b, sizeof b, "I/O scheduler %s", names[i]);
        so_kv(o, b, C_TEAL, s);
    }
    dir_free(names, n);
    so_par(o);
    so_note(o, "See [tune] for sysctl values against kernel defaults. Read-only: srs-gui changes nothing.");
}

/* ======================================================================
 * text/output: [csv] [csv-titles] [words] [emoji] [languages] [filter] [delta] [debug-api]
 * ====================================================================== */
static void s_csv(srs_out *o)
{
    static const int tabs[] = { 160 };
    so_tabs(o, tabs, 1);
    so_siv_header(o, "CSV Logging");
    so_kv(o, "CSV logging", C_ORANGE, "not separate - srs-gui saves sections as text");
    so_kv(o, "Save", C_VALUE, "srs-gui -save[=[item]...][=file|STDOUT]  (same layout as SIV64X -save)");
    so_kv(o, "Example", C_PURPLE, "srs-gui '-save=[sensors][cpu]=/tmp/log.txt'");
    so_kv(o, "Tabular data", C_VALUE, "columns are space-aligned at the panel tab stops (7 px per character)");
    so_kv(o, "Environmental log", C_GRAY, "SIV's -ENVLOG/-EXCEL .CSV sensor log has no equivalent; sample [sensors] with -save");
}

static void s_csv_titles(srs_out *o)
{
    static const int tabs[] = { 160 };
    so_tabs(o, tabs, 1);
    so_siv_header(o, "CSV Titles");
    so_kv(o, "File header", C_PURPLE, "srs-gui - System Report Search V" SRS_VERSION " <host> <YYYY-MM-DD HH:MM:SS>  [<items>]");
    so_kv(o, "Section header", C_PURPLE, "[<item>]  [<category>]");
    so_kv(o, "Section body", C_VALUE, "the section's own column titles, then rows");
    so_kv(o, "Separator", C_VALUE, "two blank lines after each section");
    so_par(o);
    so_note(o, "There is no CSV writer, so there are no CSV column titles; each section prints its own heading row.");
}

static void s_words(srs_out *o)
{
    static const char *w[] = { "OK", "Copy", "PCI Bus", "USB Bus", "ALL Dev", "Lookup", "Find", "Data Files", "About", "Help",
                               "Refresh", "Next", "Prev", "Highlight", "Colour", "Browse...", "Latest", "Ignore case", "Literal",
                               "Highlight Matches", "Menu", "Run as root", "Save", NULL };
    int i;
    so_siv_header(o, "Words");
    so_kv(o, "Language", C_TEAL, "English (built in, no translation tables)");
    so_par(o);
    for (i = 0; w[i]; i++) { so_t(o, C_VALUE, w[i]); if (w[i + 1]) so_t(o, C_GRAY, ", "); if (i % 8 == 7) so_par(o); }
    so_par(o);
}

static void s_emoji(srs_out *o)
{
    const char *argv[] = { "fc-list", ":charset=1f600", "family", NULL };
    char *out = NULL;
    so_siv_header(o, "Emoji glyph test");
    so_t(o, C_LABEL, "Glyphs  "); so_t(o, C_VALUE, "\xF0\x9F\x98\x80 \xF0\x9F\x96\xA5 \xF0\x9F\x8C\xA1 \xE2\x9A\xA1 \xF0\x9F\x94\x8B \xF0\x9F\x92\xBE \xE2\x9C\x94 \xE2\x9C\x98 \xC2\xB0" "C \xC2\xB5" "s \xE2\x86\x92");
    so_par(o);
    so_t(o, C_LABEL, "Boxes   "); so_check(o, 1); so_check(o, 0); so_par(o); so_par(o);
    so_b(o, C_VALUE, "Fonts with emoji coverage (fc-list :charset=1f600)"); so_par(o);
    if (run_out(&out, argv) < 0 || !out || !*out) so_note(o, "none found (or fontconfig missing)");
    else so_lines(o, C_TEAL, out);
    free(out);
}

static void s_languages(srs_out *o)
{
    static const int tabs[] = { 160 };
    const char *v[] = { "LANG", "LC_ALL", "LC_MESSAGES", "LC_CTYPE", "LANGUAGE", NULL };
    int i;
    so_tabs(o, tabs, 1);
    so_siv_header(o, "Languages");
    so_kv(o, "UI language", C_TEAL, "English only");
    so_kv(o, "Encoding", C_TEAL, "UTF-8 (all text, saves and .ids files)");
    for (i = 0; v[i]; i++) so_kv(o, v[i], C_VALUE, getenv(v[i]) ? getenv(v[i]) : "<unset>");
}

static void s_filter(srs_out *o)
{
    static const int tabs[] = { 160 };
    so_tabs(o, tabs, 1);
    so_siv_header(o, "Filter");
    so_kv(o, "Find pattern", C_VALUE, "regex (Python re-like subset) or Literal; Ignore case; Max matches");
    so_kv(o, "Lookup filter", C_VALUE, "PCI / USB bus; Tree / Scan / Info mode");
    so_kv(o, "Device panels", C_VALUE, "PCI Bus / USB Bus / ALL Dev select the bus");
    so_kv(o, "-save filter", C_VALUE, "[item][item]... chooses which sections are written");
    so_kv(o, "Column filter", C_GRAY, "none - every column is always shown");
}

static void s_delta(srs_out *o)
{
    static const int tabs[] = { 160 };
    so_tabs(o, tabs, 1);
    so_siv_header(o, "Delta/Cost Settings");
    so_kv(o, "Energy cost", C_GRAY, "not used - srs-gui has no power cost / delta accounting settings");
    so_kv(o, "Nearest", C_VALUE, "live power is shown in [pmb] and the sensors sections");
}

static void s_debug_api(srs_out *o)
{
    static const int tabs[] = { 160 };
    so_tabs(o, tabs, 1);
    so_siv_header(o, "Debug API");
    so_kv(o, "Build", C_ORANGE, plat_build_info());
#ifdef NDEBUG
    so_kv(o, "Assertions", C_TEAL, "off (NDEBUG)");
#else
    so_kv(o, "Assertions", C_TEAL, "on");
#endif
    so_kv(o, "Item list", C_PURPLE, "srs-gui --list");
    so_kv(o, "Dump any item", C_PURPLE, "srs-gui -save=[item]=STDOUT");
    so_kv(o, "Process state", C_VALUE, "see [stc] (threads), [mutex] (locks), [siv-dll] (modules)");
}

/* ======================================================================
 * process: [initial] [final] [sequence] [lck-setup] [mutex] [states] [stc]
 * ====================================================================== */
static void s_initial(srs_out *o)
{
    static const int tabs[] = { 40 };
    static const char *steps[] = {
        "Read SRSDEF.ini (~/.config/srs) - every key is listed in [siv-setup]",
        "Load pci.ids and usb.ids (beside the program, else /usr/share/hwdata) into sorted tables",
        "Enumerate PCI and USB devices from sysfs and resolve their names",
        "Locate the newest report under ~/sysreports for Find",
        "Build the section registry (SIV item order) for the Menu",
        "Open the main window and register the accelerators (Ctrl+F/L/D/H, F1/F3/F5, Esc)",
        "Start live-section timers only for open panels (live_ms)",
        NULL };
    int i;
    so_tabs(o, tabs, 1);
    so_siv_header(o, "Initial Processing");
    for (i = 0; steps[i]; i++) { so_f(o, C_TEAL, "%d", i + 1); so_tab(o); so_t(o, C_VALUE, steps[i]); so_par(o); }
    so_par(o);
    so_note(o, "With arguments the same image skips the GUI and runs the CLI / -save directly.");
}

static void s_final(srs_out *o)
{
    static const int tabs[] = { 160 };
    char b[64];
    struct timespec ts;
    so_tabs(o, tabs, 1);
    so_siv_header(o, "Final Processing");
    snprintf(b, sizeof b, "%.3f seconds", proc_elapsed());
    so_kv(o, "Elapsed", C_TEAL, b);
    if (!clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts)) {
        snprintf(b, sizeof b, "%.3f seconds", ts.tv_sec + ts.tv_nsec / 1e9);
        so_kv(o, "CPU time", C_TEAL, b);
    }
    so_kvf(o, "Sections", C_TEAL, "%d registered", srs_section_count());
    so_kv(o, "On exit", C_VALUE, "save SRSDEF.ini (GUI only), free the .ids tables, close panels; no driver to unload");
}

static const char *const g_default_save[] = {
#include "sec_default_save.inc"
    NULL
};

static void s_sequence(srs_out *o)
{
    static const int tabs[] = { 50, 200 };
    int i;
    so_tabs(o, tabs, 2);
    so_siv_header(o, "Sequence Setup");
    so_note(o, "Order of the default -save (no item list), the same order SIV64X -save uses:");
    so_par(o);
    for (i = 0; g_default_save[i]; i++) {
        const srs_item *s = srs_section_find(g_default_save[i]);
        so_f(o, C_TEAL, "%d", i + 1); so_tab(o);
        so_f(o, C_PURPLE, "[%s]", g_default_save[i]); so_tab(o);
        so_t(o, C_VALUE, s ? s->title : "?");
        so_par(o);
    }
}

static void s_lck_setup(srs_out *o)
{
    static const int tabs[] = { 160 };
    so_tabs(o, tabs, 1);
    so_siv_header(o, "Lock Setup");
    so_kv(o, "Instance lock", C_VALUE, "none - several srs-gui instances may run (no shared driver state)");
    so_kv(o, "Find", C_VALUE, "runs on one worker thread; the GUI thread owns all widgets");
    so_kv(o, "Burn-in", C_VALUE, "one thread per CPU, pinned by affinity; stop flag polled, no hardware locks");
    so_kv(o, "Hardware locks", C_GRAY, "none - no SMBus/ISA/PCI config access, so no global mutexes like SIV's");
}

static void s_mutex(srs_out *o)
{
    static const int tabs[] = { 60, 140, 240, 340 };
    char *txt, *p, *save, me[32];
    int n = 0;
    so_tabs(o, tabs, 4);
    so_siv_header(o, "Mutex/Lock Info");
    snprintf(me, sizeof me, " %d ", getpid());
    so_row(o, 5, (const char *[]){ "#", "Class", "Mode", "Access", "PID / Inode" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    if (rd_all("/proc/locks", &txt) >= 0) {
        for (p = strtok_r(txt, "\n", &save); p; p = strtok_r(NULL, "\n", &save)) {
            char num[16], cls[16], mode[16], acc[16], rest[256];
            if (!strstr(p, me)) continue;
            if (sscanf(p, "%15s %15s %15s %15s %255[^\n]", num, cls, mode, acc, rest) == 5) {
                so_row(o, 5, (const char *[]){ num, cls, mode, acc, rest }, (const int[]){ C_TEAL, C_VALUE, C_VALUE, C_VALUE, C_PURPLE });
                n++;
            }
        }
        free(txt);
    }
    so_par(o);
    so_f(o, C_GRAY, "%d file lock%s held by this process (/proc/locks). srs-gui creates no named mutexes.", n, n == 1 ? "" : "s");
    so_par(o);
}

static void s_states(srs_out *o)
{
    static const int tabs[] = { 80, 220, 280 };
    char **names, base[64];
    int n, i;
    so_tabs(o, tabs, 3);
    so_siv_header(o, "Lock States");
    so_row(o, 4, (const char *[]){ "TID", "Thread", "State", "Wait channel" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    snprintf(base, sizeof base, "/proc/%d/task", getpid());
    n = dir_list(base, &names);
    for (i = 0; i < n; i++) {
        char comm[64] = "", st[512] = "", wch[128] = "", state[8] = "?";
        rd_fmt(comm, sizeof comm, "%s/%s/comm", base, names[i]);
        rd_fmt(wch, sizeof wch, "%s/%s/wchan", base, names[i]);
        if (rd_fmt(st, sizeof st, "%s/%s/stat", base, names[i])) {
            char *r = strrchr(st, ')');
            if (r && r[1] == ' ') snprintf(state, sizeof state, "%c", r[2]);
        }
        so_row(o, 4, (const char *[]){ names[i], comm, state, *wch && strcmp(wch, "0") ? wch : "-" },
               (const int[]){ C_TEAL, C_VALUE, C_GREEN, C_GRAY });
    }
    dir_free(names, n);
}

static void s_stc(srs_out *o)
{
    static const int tabs[] = { 80, 220, 300, 400, 560, 640 };
    char **names, base[64];
    long hz = sysconf(_SC_CLK_TCK);
    int n, i;
    so_tabs(o, tabs, 6);
    so_row(o, 7, (const char *[]){ "TID", "Name", "State", "Total CPU", "Affinity", "Last CPU", "Priority / Nice" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    snprintf(base, sizeof base, "/proc/%d/task", getpid());
    n = dir_list(base, &names);
    for (i = 0; i < n; i++) {
        char comm[64] = "", *st = NULL, p[128], tot[32] = "-", aff[64] = "-", cpu[16] = "-", pr[32] = "-", state[4] = "?";
        snprintf(p, sizeof p, "%s/%s/comm", base, names[i]);
        rd_str(p, comm, sizeof comm);
        snprintf(p, sizeof p, "%s/%s/stat", base, names[i]);
        if (rd_all(p, &st) >= 0) {
            char *r = strrchr(st, ')');
            if (r) {
                char *f[50];
                int nf = 0;
                char *q, *sv;
                for (q = strtok_r(r + 2, " ", &sv); q && nf < 50; q = strtok_r(NULL, " ", &sv)) f[nf++] = q;
                if (nf > 38) {
                    double t = (atof(f[11]) + atof(f[12])) / (double)(hz > 0 ? hz : 100);
                    snprintf(state, sizeof state, "%s", f[0]);
                    snprintf(tot, sizeof tot, "%d:%06.3f", (int)(t / 60), t - 60 * (int)(t / 60));
                    snprintf(pr, sizeof pr, "%s / %s", f[15], f[16]);
                    snprintf(cpu, sizeof cpu, "CPU-%s", f[36]);
                }
            }
            free(st);
        }
        snprintf(p, sizeof p, "%s/%s/status", base, names[i]);
        {
            char *s2;
            if (rd_all(p, &s2) >= 0) {
                char *a = strstr(s2, "Cpus_allowed_list:");
                if (a) { a += 18; while (*a == '\t' || *a == ' ') a++; snprintf(aff, sizeof aff, "%.*s", (int)strcspn(a, "\n"), a); }
                free(s2);
            }
        }
        so_row(o, 7, (const char *[]){ names[i], comm, state, tot, aff, cpu, pr },
               (const int[]){ C_TEAL, C_VALUE, C_GREEN, C_VALUE, C_PURPLE, C_TEAL, C_GRAY });
    }
    dir_free(names, n);
    so_par(o);
    so_f(o, C_GRAY, "%d thread%s of PID %d (/proc/self/task)", n, n == 1 ? "" : "s", getpid());
    so_par(o);
}

/* ======================================================================
 * xxb buffers
 * ====================================================================== */
static void dmi(const char *f, char *b, size_t sz)
{
    char p[128];
    snprintf(p, sizeof p, "/sys/class/dmi/id/%s", f);
    if (!rd_str(p, b, sz)) snprintf(b, sz, "%s", "");
}

/* serial needs root: try the dmidecode cache */
static void dmi_serial(const char *field, const char *dmikey, char *b, size_t sz)
{
    const char *f;
    dmi(field, b, sz);
    if (*b) return;
    if ((f = so_root_file("dmidecode.txt"))) {
        char *txt, *p, *save;
        int in = 0;
        if (rd_all(f, &txt) >= 0) {
            for (p = strtok_r(txt, "\n", &save); p; p = strtok_r(NULL, "\n", &save)) {
                if (strstr(p, dmikey) && !strncmp(p, dmikey, strlen(dmikey))) in = 1;
                else if (*p && *p != '\t') in = 0;
                if (in && strstr(p, "Serial Number:")) { snprintf(b, sz, "%s", strstr(p, ":") + 2); break; }
            }
            free(txt);
        }
    }
    if (!*b) snprintf(b, sz, "Needs root: Menu > Run as root (srs-root)");
}

static void s_sgb_ini(srs_out *o)
{
    static const int tabs[] = { 120, 400 };
    char b[512], c[256];
    long nc = sysconf(_SC_NPROCESSORS_CONF), no = sysconf(_SC_NPROCESSORS_ONLN);
    cpu_set_t cs;
    so_tabs(o, tabs, 2);
    so_row(o, 3, (const char *[]){ "Motherboard", "Item", "Location" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    dmi("board_vendor", b, sizeof b); so_kv(o, "Maker Name", C_VALUE, b);
    dmi("board_name", b, sizeof b); so_kv(o, "Model Name", C_VALUE, b);
    dmi("board_version", b, sizeof b); so_kv(o, "Board Version", C_VALUE, b);
    dmi_serial("board_serial", "Base Board Information", b, sizeof b); so_kv(o, "Serial Number", C_VALUE, b);
    dmi("sys_vendor", b, sizeof b); dmi("product_name", c, sizeof c);
    so_kvf(o, "System", C_VALUE, "%s %s", b, c);
    dmi("bios_vendor", c, sizeof c); so_kv(o, "BIOS Vendor", C_VALUE, c);
    dmi("bios_version", b, sizeof b); so_kvf(o, "BIOS Version", C_VALUE, "BIOS %s", b);
    dmi("bios_date", b, sizeof b); so_kv(o, "BIOS Date", C_VALUE, b);
    dmi("bios_release", b, sizeof b); so_kv(o, "BIOS Release", C_VALUE, b);
    dmi("chassis_type", b, sizeof b); so_kv(o, "Chassis Type", C_VALUE, b);
    {
        char p[1100];
        if (rd_str("/sys/bus/pci/devices/0000:00:00.0/vendor", b, sizeof b) && rd_str("/sys/bus/pci/devices/0000:00:00.0/device", c, sizeof c)) {
            snprintf(p, sizeof p, "%s-%s", b + 2, c + 2);
            so_kv(o, "MCH (00:00.0)", C_TEAL, p);
        }
    }
    so_par(o);
    so_kvf(o, "ACT CPUs", C_TEAL, "%ld (%ld)", no, nc);
    if (!sched_getaffinity(0, sizeof cs, &cs)) {
        unsigned long long m = 0;
        int i;
        for (i = 0; i < 64; i++) if (CPU_ISSET(i, &cs)) m |= 1ULL << i;
        so_kvf(o, "SRS Affinity", C_TEAL, "%016llX", m);
    }
    so_kvf(o, "SRS PID", C_TEAL, "%d", getpid());
    so_kv(o, "SRS Driver", C_GRAY, "none (srs-gui uses no kernel driver; root data via srs-root helper)");
    so_kv(o, "Root cache", C_TEAL, so_root_file("dmidecode.txt") ? "dmidecode.txt present" : "not present");
    so_kv(o, "RAW Command", C_PURPLE, exe_path());
}

static void s_sgb_end(srs_out *o)
{
    static const int tabs[] = { 160 };
    so_tabs(o, tabs, 1);
    so_kv(o, "SGB end", C_GRAY, "end of the system/board buffer - srs-gui keeps no shared buffer; [sgb-ini] reads sysfs live");
    so_kvf(o, "Elapsed", C_TEAL, "%.3f seconds since start", proc_elapsed());
}

static void s_smb_ini(srs_out *o)
{
    static const int tabs[] = { 100, 380 };
    char **names;
    int n = dir_list("/sys/bus/i2c/devices", &names), i;
    so_tabs(o, tabs, 2);
    so_siv_header(o, "SMBus Setup");
    so_row(o, 3, (const char *[]){ "Device", "Name (kernel attribute)", "Driver" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL });
    for (i = 0; i < n; i++) {
        char nm[128] = "", drv[512] = "", p[300];
        ssize_t l;
        rd_fmt(nm, sizeof nm, "/sys/bus/i2c/devices/%s/name", names[i]);
        snprintf(p, sizeof p, "/sys/bus/i2c/devices/%s/driver", names[i]);
        if ((l = readlink(p, drv, sizeof drv - 1)) > 0) drv[l] = 0;
        so_row(o, 3, (const char *[]){ names[i], nm, *drv ? strrchr(drv, '/') + 1 : "-" }, (const int[]){ C_PURPLE, C_VALUE, C_TEAL });
    }
    if (n <= 0) so_note(o, "no i2c devices registered");
    dir_free(names, n);
    so_par(o);
    so_note(o, "Listed from sysfs only - srs-gui never opens /dev/i2c-* or probes SMBus addresses.");
}

static void s_smb_end(srs_out *o)
{
    static const int tabs[] = { 160 };
    so_tabs(o, tabs, 1);
    so_kv(o, "SMB end", C_GRAY, "end of the SMBus buffer - srs-gui does no SMBus access; see [smb-ini] for the sysfs view");
}

static int hwmon_list(char ***names) { return dir_list("/sys/class/hwmon", names); }

static void s_sdm(srs_out *o)
{
    static const int tabs[] = { 30, 130, 400, 480, 580, 680, 760 };
    char **names;
    int n = dir_list("/sys/class/block", &names), i, k = 0;
    so_tabs(o, tabs, 7);
    so_row(o, 8, (const char *[]){ "#", "Device Name", "Model", "Size", "Reads", "Writes", "In flight", "Rotational / Removable" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (i = 0; i < n; i++) {
        char p[300], model[128] = "", sz[32] = "-", st[512] = "", rot[8] = "", rem[8] = "", rd[32] = "-", wr[32] = "-", fl[16] = "-", num[8];
        long sec;
        if (!strncmp(names[i], "loop", 4) || !strncmp(names[i], "ram", 3)) continue;
        snprintf(p, sizeof p, "/sys/class/block/%s/partition", names[i]);
        if (path_exists(p)) continue;
        rd_fmt(model, sizeof model, "/sys/class/block/%s/device/model", names[i]);
        snprintf(p, sizeof p, "/sys/class/block/%s/size", names[i]);
        if (rd_long(p, &sec)) fmt_bytes(sec * 512.0, sz, sizeof sz);
        if (rd_fmt(st, sizeof st, "/sys/class/block/%s/stat", names[i])) {
            unsigned long long v[11] = { 0 };
            if (sscanf(st, "%llu %llu %llu %llu %llu %llu %llu %llu %llu", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8]) >= 9) {
                fmt_bytes(v[2] * 512.0, rd, sizeof rd);
                fmt_bytes(v[6] * 512.0, wr, sizeof wr);
                snprintf(fl, sizeof fl, "%llu", v[8]);
            }
        }
        rd_fmt(rot, sizeof rot, "/sys/class/block/%s/queue/rotational", names[i]);
        rd_fmt(rem, sizeof rem, "/sys/class/block/%s/removable", names[i]);
        snprintf(num, sizeof num, "%d", k++);
        snprintf(p, sizeof p, "%s / %s", atoi(rot) ? "rotational" : "solid state", atoi(rem) ? "removable" : "fixed");
        so_row(o, 8, (const char *[]){ num, names[i], *model ? model : "-", sz, rd, wr, fl, p },
               (const int[]){ C_TEAL, C_VALUE, C_VALUE, C_TEAL, C_TEAL, C_TEAL, C_GRAY, C_GRAY });
    }
    dir_free(names, n);
    so_par(o);
    so_f(o, C_GRAY, "%d whole-disk block devices (/sys/class/block)", k);
    so_par(o);
}

static void s_dsb(srs_out *o)
{
    static const int tabs[] = { 30, 130, 280, 370, 460 };
    char **names;
    int n = hwmon_list(&names), i, k = 0;
    so_tabs(o, tabs, 5);
    so_row(o, 6, (const char *[]){ "#", "hwmon", "Device", "Current", "Maximum", "Critical" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (i = 0; i < n; i++) {
        char nm[64] = "", dev[512] = "", p[300], num[8], cur[32] = "-", mx[32] = "-", cr[32] = "-";
        long v;
        ssize_t l;
        rd_fmt(nm, sizeof nm, "/sys/class/hwmon/%s/name", names[i]);
        if (strcmp(nm, "spd5118") && strcmp(nm, "jc42")) continue;
        snprintf(p, sizeof p, "/sys/class/hwmon/%s/device", names[i]);
        if ((l = readlink(p, dev, sizeof dev - 1)) > 0) dev[l] = 0;
        snprintf(p, sizeof p, "/sys/class/hwmon/%s/temp1_input", names[i]);
        if (rd_long(p, &v)) snprintf(cur, sizeof cur, "%.2f\xC2\xB0" "C", v / 1000.0);
        snprintf(p, sizeof p, "/sys/class/hwmon/%s/temp1_max", names[i]);
        if (rd_long(p, &v)) snprintf(mx, sizeof mx, "%.2f\xC2\xB0" "C", v / 1000.0);
        snprintf(p, sizeof p, "/sys/class/hwmon/%s/temp1_crit", names[i]);
        if (rd_long(p, &v)) snprintf(cr, sizeof cr, "%.2f\xC2\xB0" "C", v / 1000.0);
        snprintf(num, sizeof num, "%d", k++);
        so_row(o, 6, (const char *[]){ num, names[i], *dev ? (strrchr(dev, '/') ? strrchr(dev, '/') + 1 : dev) : nm, cur, mx, cr },
               (const int[]){ C_TEAL, C_VALUE, C_PURPLE, C_GREEN, C_TEAL, C_TEAL });
    }
    dir_free(names, n);
    so_par(o);
    if (!k) so_note(o, "no DIMM temperature sensors (spd5118 / jc42 hwmon) loaded");
    so_note(o, "Read from the kernel's spd5118/jc42 hwmon drivers; no SMBus access. SPD contents: [spd] via the srs-root cache.");
}

static void s_bsi(srs_out *o)
{
    static const int tabs[] = { 30, 170, 280, 380, 480 };
    char **names;
    int n = dir_list("/sys/class/power_supply", &names), i, nh;
    so_tabs(o, tabs, 5);
    so_row(o, 6, (const char *[]){ "#", "Device Name", "Type", "Status", "Capacity", "Model" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (i = 0; i < n; i++) {
        char ty[32] = "-", stt[32] = "-", cap[16] = "-", mdl[128] = "-", num[8];
        rd_fmt(ty, sizeof ty, "/sys/class/power_supply/%s/type", names[i]);
        rd_fmt(stt, sizeof stt, "/sys/class/power_supply/%s/status", names[i]);
        if (rd_fmt(cap, sizeof cap, "/sys/class/power_supply/%s/capacity", names[i])) strncat(cap, "%", sizeof cap - strlen(cap) - 1);
        rd_fmt(mdl, sizeof mdl, "/sys/class/power_supply/%s/model_name", names[i]);
        snprintf(num, sizeof num, "%d", i);
        so_row(o, 6, (const char *[]){ num, names[i], ty, stt, cap, mdl }, (const int[]){ C_TEAL, C_VALUE, C_TEAL, C_GREEN, C_GREEN, C_GRAY });
    }
    so_f(o, C_GRAY, "act %d  num %d (/sys/class/power_supply)", n > 0 ? n : 0, n > 0 ? n : 0);
    so_par(o);
    dir_free(names, n);
    nh = dir_list("/sys/bus/hid/devices", &names);
    so_f(o, C_GRAY, "%d HID devices registered (/sys/bus/hid/devices; listed only, hidraw is never opened)", nh > 0 ? nh : 0);
    so_par(o);
    dir_free(names, nh);
}

static void s_pmb(srs_out *o)
{
    static const int tabs[] = { 30, 140, 330, 440 };
    char **names;
    int n = hwmon_list(&names), i, k = 0;
    so_tabs(o, tabs, 4);
    so_row(o, 5, (const char *[]){ "#", "Device", "Description", "Current", "Characteristic" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (i = 0; i < n; i++) {
        char nm[64] = "", **f;
        int nf, j;
        char d[300];
        rd_fmt(nm, sizeof nm, "/sys/class/hwmon/%s/name", names[i]);
        snprintf(d, sizeof d, "/sys/class/hwmon/%s", names[i]);
        nf = dir_list(d, &f);
        for (j = 0; j < nf; j++) {
            char p[400], lbl[64], val[32], num[8], base[32], ch[64] = "";
            long v;
            int ispow = !strncmp(f[j], "power", 5), iscur = !strncmp(f[j], "curr", 4);
            size_t fl = strlen(f[j]);
            int isin = fl > 6 && !strcmp(f[j] + fl - 6, "_input"), isavg = fl > 8 && !strcmp(f[j] + fl - 8, "_average");
            if (!((ispow || iscur) && isin) && !(ispow && isavg)) continue;
            snprintf(base, sizeof base, "%.*s", (int)strcspn(f[j], "_"), f[j]);
            snprintf(p, sizeof p, "%s/%s", d, f[j]);
            if (!rd_long(p, &v)) continue;
            snprintf(p, sizeof p, "%s/%s_label", d, base);
            if (!rd_str(p, lbl, sizeof lbl)) snprintf(lbl, sizeof lbl, "%s", base);
            if (ispow) snprintf(val, sizeof val, "%.3f W", v / 1e6);
            else snprintf(val, sizeof val, "%.3f A", v / 1e3);
            snprintf(p, sizeof p, "%s/%s_cap", d, base);
            if (ispow && rd_long(p, &v)) snprintf(ch, sizeof ch, "cap %.0f W", v / 1e6);
            snprintf(num, sizeof num, "%d", k++);
            so_row(o, 5, (const char *[]){ num, nm, lbl, val, ch }, (const int[]){ C_TEAL, C_VALUE, C_VALUE, C_GREEN, C_GRAY });
        }
        dir_free(f, nf);
    }
    dir_free(names, n);
    so_par(o);
    if (!k) so_note(o, "no power / current inputs in /sys/class/hwmon");
    so_note(o, "Live power monitor values from hwmon power*_input / power*_average / curr*_input.");
}

static void s_tzm(srs_out *o)
{
    static const int tabs[] = { 30, 200, 300, 400, 500 };
    char **names;
    int n = dir_list("/sys/class/thermal", &names), i, k = 0;
    so_tabs(o, tabs, 5);
    so_row(o, 6, (const char *[]){ "#", "Device Name (Zone)", "Current", "Passive", "Critical", "Policy" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (i = 0; i < n; i++) {
        char ty[64] = "", cur[32] = "-", pas[32] = "-", cri[32] = "-", pol[64] = "-", num[8], nm[160], p[300];
        long v;
        int t;
        if (strncmp(names[i], "thermal_zone", 12)) continue;
        rd_fmt(ty, sizeof ty, "/sys/class/thermal/%s/type", names[i]);
        rd_fmt(pol, sizeof pol, "/sys/class/thermal/%s/policy", names[i]);
        snprintf(p, sizeof p, "/sys/class/thermal/%s/temp", names[i]);
        if (rd_long(p, &v)) snprintf(cur, sizeof cur, "%.2f\xC2\xB0" "C", v / 1000.0);
        for (t = 0; t < 16; t++) {
            char tt[32] = "";
            if (!rd_fmt(tt, sizeof tt, "/sys/class/thermal/%s/trip_point_%d_type", names[i], t)) break;
            snprintf(p, sizeof p, "/sys/class/thermal/%s/trip_point_%d_temp", names[i], t);
            if (!rd_long(p, &v)) continue;
            if (!strcmp(tt, "passive")) snprintf(pas, sizeof pas, "%.2f\xC2\xB0" "C", v / 1000.0);
            if (!strcmp(tt, "critical")) snprintf(cri, sizeof cri, "%.2f\xC2\xB0" "C", v / 1000.0);
        }
        snprintf(nm, sizeof nm, "%s (%s)", ty, names[i] + 12);
        snprintf(num, sizeof num, "%d", k++);
        so_row(o, 6, (const char *[]){ num, nm, cur, pas, cri, pol }, (const int[]){ C_TEAL, C_VALUE, C_GREEN, C_TEAL, C_RED, C_GRAY });
    }
    dir_free(names, n);
    so_par(o);
    so_f(o, C_GRAY, "act %d  num %d (/sys/class/thermal)", k, k);
    so_par(o);
    if (!k) so_note(o, "no ACPI thermal zones; CPU/GPU temperatures come from hwmon (see the sensors sections)");
}

static void s_cgb(srs_out *o)
{
    static const int tabs[] = { 50, 100, 260, 460 };
    char on[256] = "", poss[256] = "", aff[32], mem[64], **names;
    int n, i, cnt = 0, mx = -1;
    unsigned long long m = 0;
    so_tabs(o, tabs, 4);
    rd_str("/sys/devices/system/cpu/online", on, sizeof on);
    rd_str("/sys/devices/system/cpu/possible", poss, sizeof poss);
    n = dir_list("/sys/devices/system/cpu", &names);
    for (i = 0; i < n; i++) {
        char p[128], v[8] = "1";
        int c;
        if (strncmp(names[i], "cpu", 3) || !isdigit((unsigned char)names[i][3])) continue;
        c = atoi(names[i] + 3);
        snprintf(p, sizeof p, "/sys/devices/system/cpu/%s/online", names[i]);
        rd_str(p, v, sizeof v);
        if (atoi(v) || c == 0) { if (c < 64) m |= 1ULL << c; cnt++; if (c > mx) mx = c; }
    }
    dir_free(names, n);
    so_row(o, 5, (const char *[]){ "CGN", "Num", "Affinity", "Members", "Online / Possible" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    snprintf(aff, sizeof aff, "%016llX", m);
    snprintf(mem, sizeof mem, "[CPU-0 to CPU-%d]", mx);
    {
        char num[8], ol[600];
        snprintf(num, sizeof num, "%d", cnt);
        snprintf(ol, sizeof ol, "%s / %s", on, poss);
        so_check(o, 1); so_t(o, C_VALUE, "0"); so_tab(o);
        so_t(o, C_TEAL, num); so_tab(o); so_t(o, C_PURPLE, aff); so_tab(o); so_t(o, C_VALUE, mem); so_tab(o); so_t(o, C_GRAY, ol); so_par(o);
    }
    so_par(o);
    so_note(o, "Linux has a single CPU group (no Windows processor groups); topology in /sys/devices/system/cpu.");
}

static void s_gpu(srs_out *o)
{
    static const int tabs[] = { 60, 130, 260, 340, 470, 600 };
    char **names;
    int n = dir_list("/sys/class/drm", &names), i, k = 0;
    so_tabs(o, tabs, 6);
    so_row(o, 7, (const char *[]){ "GPU", "Driver", "PCI ID / Slot", "Busy", "VRAM Used / Total", "Link", "Clocks (sclk / mclk)" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (i = 0; i < n; i++) {
        char d[300], drv[512] = "", ven[16] = "", dev[16] = "", slot[512] = "", busy[16] = "-", vram[64] = "-", link[64] = "-",
             clk[128] = "-", id[128], p[400];
        ssize_t l;
        long a, b;
        if (strncmp(names[i], "card", 4) || strchr(names[i], '-')) continue;
        snprintf(d, sizeof d, "/sys/class/drm/%s/device", names[i]);
        snprintf(p, sizeof p, "%s/driver", d);
        if ((l = readlink(p, drv, sizeof drv - 1)) > 0) drv[l] = 0;
        if ((l = readlink(d, slot, sizeof slot - 1)) > 0) slot[l] = 0;
        rd_fmt(ven, sizeof ven, "%s/vendor", d);
        rd_fmt(dev, sizeof dev, "%s/device", d);
        if (rd_fmt(busy, sizeof busy, "%s/gpu_busy_percent", d)) strncat(busy, "%", sizeof busy - strlen(busy) - 1);
        snprintf(p, sizeof p, "%s/mem_info_vram_used", d);
        if (rd_long(p, &a)) {
            char u[32], t[32];
            snprintf(p, sizeof p, "%s/mem_info_vram_total", d);
            if (!rd_long(p, &b)) b = 0;
            fmt_bytes((double)a, u, sizeof u);
            fmt_bytes((double)b, t, sizeof t);
            snprintf(vram, sizeof vram, "%s / %s", u, t);
        }
        {
            char sp[32] = "", w[16] = "";
            if (rd_fmt(sp, sizeof sp, "%s/current_link_speed", d) && rd_fmt(w, sizeof w, "%s/current_link_width", d))
                snprintf(link, sizeof link, "x%s %s", w, sp);
        }
        {
            char *t1 = NULL, *t2 = NULL, s1[48] = "-", s2[48] = "-";
            snprintf(p, sizeof p, "%s/pp_dpm_sclk", d);
            if (rd_all(p, &t1) >= 0) { char *s = strchr(t1, '*'); if (s) { char *b0 = s; while (b0 > t1 && b0[-1] != '\n') b0--; snprintf(s1, sizeof s1, "%.*s", (int)(s - b0), b0); trimsp(s1); { char *c = strchr(s1, ':'); if (c) memmove(s1, c + 2, strlen(c + 2) + 1); } } free(t1); }
            snprintf(p, sizeof p, "%s/pp_dpm_mclk", d);
            if (rd_all(p, &t2) >= 0) { char *s = strchr(t2, '*'); if (s) { char *b0 = s; while (b0 > t2 && b0[-1] != '\n') b0--; snprintf(s2, sizeof s2, "%.*s", (int)(s - b0), b0); trimsp(s2); { char *c = strchr(s2, ':'); if (c) memmove(s2, c + 2, strlen(c + 2) + 1); } } free(t2); }
            snprintf(clk, sizeof clk, "%s / %s", s1, s2);
        }
        snprintf(id, sizeof id, "%s-%s %s", *ven ? ven + 2 : "?", *dev ? dev + 2 : "?", strrchr(slot, '/') ? strrchr(slot, '/') + 1 : slot);
        so_row(o, 7, (const char *[]){ names[i], *drv ? strrchr(drv, '/') + 1 : "-", id, busy, vram, link, clk },
               (const int[]){ C_VALUE, C_TEAL, C_PURPLE, C_GREEN, C_TEAL, C_TEAL, C_GREEN });
        k++;
    }
    dir_free(names, n);
    so_par(o);
    so_f(o, C_GRAY, "%d GPU%s (/sys/class/drm)", k, k == 1 ? "" : "s");
    so_par(o);
}

static void s_xxb(srs_out *o)
{
    static const int tabs[] = { 80, 300 };
    static const char *b[][3] = {
        { "sgb-ini", "System/board buffer", "/sys/class/dmi/id, 00:00.0, process affinity" },
        { "sdm", "Storage device monitor", "/sys/class/block/*/stat" },
        { "dsb", "DIMM sensor buffer", "hwmon spd5118 / jc42" },
        { "bsi", "Battery / HID sensor buffer", "/sys/class/power_supply, /sys/bus/hid/devices" },
        { "pmb", "Power monitor buffer", "hwmon power* / curr*" },
        { "tzm", "Thermal zone buffer", "/sys/class/thermal" },
        { "cgb", "CPU group buffer", "/sys/devices/system/cpu/online" },
        { "cpu", "CPU info buffer", "/sys/devices/system/cpu, cpufreq" },
        { "gpu", "GPU info buffer", "/sys/class/drm" },
        { "stc", "Thread contexts", "/proc/self/task" },
    };
    size_t i;
    so_tabs(o, tabs, 2);
    so_siv_header(o, "Internal Buffers");
    so_row(o, 3, (const char *[]){ "Buffer", "Description", "srs-gui snapshot source" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL });
    for (i = 0; i < sizeof b / sizeof b[0]; i++) {
        char id[32];
        snprintf(id, sizeof id, "[%s]", b[i][0]);
        so_row(o, 3, (const char *[]){ id, b[i][1], b[i][2] }, (const int[]){ C_PURPLE, C_VALUE, C_TEAL });
    }
    so_par(o);
    so_note(o, "srs-gui keeps no shared buffers; each item reads its source live when rendered.");
}

/* ======================================================================
 * fake-mode replay: [fak-imcs] [fak-mchbar] [fak-pmts] [fak-spds] ; [makers]
 * ====================================================================== */
static void s_fak_mchbar(srs_out *o)
{
    static const int tabs[] = { 160 };
    so_tabs(o, tabs, 1);
    so_siv_header(o, "Fake MCHBAR");
    so_kv(o, "Status", C_ORANGE, "fake-mode replay of Intel MCHBAR register dumps (SIV developer feature)");
    so_kv(o, "srs-gui", C_VALUE, "reads no MCHBAR registers and never opens /dev/mem; no replay files are used");
    so_kv(o, "Memory timings", C_GRAY, "come from [spd] (srs-root cache spd.txt) and the kernel EDAC/amd64 drivers");
}

static void s_fak_imcs(srs_out *o)
{
    static const int tabs[] = { 160 };
    so_tabs(o, tabs, 1);
    so_siv_header(o, "Fake IMCs");
    so_kv(o, "Status", C_ORANGE, "Not present on this system");
    so_kv(o, "Checked", C_PURPLE, "Intel Xeon ICX IMC fixtures (fake-mode replay); srs-gui has no replay mode");
    so_kv(o, "Processor", C_VALUE, sys_cpu_name());
}

static void s_fak_pmts(srs_out *o)
{
    static const int tabs[] = { 200 };
    char b[64];
    struct stat st;
    so_tabs(o, tabs, 1);
    so_siv_header(o, "Fake PM tables");
    so_kv(o, "Status", C_ORANGE, "srs-gui has no fake-mode replay; the live PM table is shown in [amd-pmt-0]");
    if (!stat("/sys/kernel/ryzen_smu_drv/pm_table", &st)) {
        so_kv(o, "pm_table", C_GREEN, "/sys/kernel/ryzen_smu_drv/pm_table present");
        {   /* binary attributes: pm_table_version is a u32, pm_table_size a u64 (bytes) */
            unsigned char raw[8] = { 0 };
            FILE *f = fopen("/sys/kernel/ryzen_smu_drv/pm_table_version", "rb");
            size_t n = f ? fread(raw, 1, sizeof raw, f) : 0;
            if (f) fclose(f);
            if (n >= 4) { unsigned x; memcpy(&x, raw, 4); snprintf(b, sizeof b, "0x%06X", x); so_kv(o, "pm_table_version", C_TEAL, b); }
            memset(raw, 0, sizeof raw);
            f = fopen("/sys/kernel/ryzen_smu_drv/pm_table_size", "rb");
            n = f ? fread(raw, 1, sizeof raw, f) : 0;
            if (f) fclose(f);
            if (n == 8) { unsigned long long x; memcpy(&x, raw, 8); snprintf(b, sizeof b, "%llu bytes", x); so_kv(o, "pm_table_size", C_TEAL, b); }
        }
        if (rd_str("/sys/kernel/ryzen_smu_drv/version", b, sizeof b)) so_kv(o, "SMU firmware", C_TEAL, b);
    } else so_kv(o, "pm_table", C_GRAY, "ryzen_smu driver not loaded");
}

static void s_fak_spds(srs_out *o)
{
    static const int tabs[] = { 200 };
    const char *f = so_root_file("spd.txt");
    char **names;
    int n = hwmon_list(&names), i, k = 0;
    so_tabs(o, tabs, 1);
    so_siv_header(o, "Fake SPDs");
    so_kv(o, "Status", C_ORANGE, "srs-gui has no fake-mode replay; real SPD data is shown in [spd]");
    so_kv(o, "SPD cache", f ? C_GREEN : C_RED, f ? f : "Needs root: Menu > Run as root (srs-root)");
    for (i = 0; i < n; i++) {
        char nm[64] = "", dev[512] = "", p[300];
        ssize_t l;
        rd_fmt(nm, sizeof nm, "/sys/class/hwmon/%s/name", names[i]);
        if (strcmp(nm, "spd5118") && strcmp(nm, "jc42")) continue;
        snprintf(p, sizeof p, "/sys/class/hwmon/%s/device", names[i]);
        if ((l = readlink(p, dev, sizeof dev - 1)) > 0) dev[l] = 0;
        so_kvf(o, nm, C_TEAL, "%s at i2c %s", names[i], strrchr(dev, '/') ? strrchr(dev, '/') + 1 : "?");
        k++;
    }
    dir_free(names, n);
    if (!k) so_kv(o, "DIMM sensors", C_GRAY, "no spd5118 / jc42 hwmon devices");
}

static void s_makers(srs_out *o)
{
    static const int tabs[] = { 80, 400 };
    srs_db db;
    srs_dev *d = NULL;
    char vend[64][8], vname[64][256];
    int cnt[64], nv = 0, n, i, j;
    so_tabs(o, tabs, 2);
    so_siv_header(o, "Makers");
    if (srs_db_load(&db, BUS_PCI)) { so_err(o, "pci.ids not found"); return; }
    n = srs_devices(BUS_PCI, &d);
    for (i = 0; i < n; i++) {
        char v[8];
        snprintf(v, sizeof v, "%.4s", d[i].key);
        for (j = 0; j < nv; j++) if (!strcasecmp(vend[j], v)) { cnt[j]++; break; }
        if (j == nv && nv < 64) {
            srs_result r;
            char e[128];
            snprintf(vend[nv], sizeof vend[nv], "%s", v);
            if (!srs_resolve(&db, v, MODE_TREE, &r, e, sizeof e) && *r.vendor) snprintf(vname[nv], sizeof vname[nv], "%s", r.vendor);
            else snprintf(vname[nv], sizeof vname[nv], "(unknown)");
            cnt[nv++] = 1;
        }
    }
    so_row(o, 3, (const char *[]){ "Vendor", "Maker", "PCI functions" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL });
    for (j = 0; j < nv; j++) {
        char c[16];
        snprintf(c, sizeof c, "%d", cnt[j]);
        so_row(o, 3, (const char *[]){ vend[j], vname[j], c }, (const int[]){ C_PURPLE, C_VALUE, C_TEAL });
    }
    free(d);
    srs_db_free(&db);
    so_par(o);
    so_note(o, "srs-gui shows maker names from pci.ids instead of SIV's maker icons.");
}

/* ======================================================================
 * table
 * ====================================================================== */
const srs_item sec_siv[] = {
    { "about", "tuning", "About SRS", B_LINUX, 0, "internal: program version/build; uname; /etc/os-release; hwdata *.ids", s_about, 0, "siv" },
    { "bsi", "xxb", "BSB info (battery/HID sensor buffer)", B_LINUX, 0, "/sys/class/power_supply; /sys/bus/hid/devices", s_bsi, 2000, "siv" },
    { "cgb", "xxb", "CPU group buffer", B_LINUX, 0, "/sys/devices/system/cpu/online", s_cgb, 0, "siv" },
    { "csv", "tuning", "CSV logging", B_LINUX, 0, "internal: -save text writer", s_csv, 0, "siv" },
    { "csv-titles", "tuning", "CSV titles", B_LINUX, 0, "internal: -save text writer", s_csv_titles, 0, "siv" },
    { "debug-api", "tuning", "Debug API", B_LINUX, 0, "internal: build info", s_debug_api, 0, "siv" },
    { "delta", "tuning", "Delta/cost settings", B_LINUX, 0, "internal: settings", s_delta, 0, "siv" },
    { "dsb", "xxb", "DSB info (DIMM sensor buffer)", B_LINUX, 0, "/sys/class/hwmon (spd5118, jc42)", s_dsb, 2000, "siv" },
    { "emoji", "tuning", "Emoji glyph test", B_LINUX, 0, "fc-list; internal", s_emoji, 0, "siv" },
    { "fak-imcs", "tuning", "Fake IMCs", B_ABSENT, 0, "n/a (Intel Xeon ICX IMC fixtures)", s_fak_imcs, 0, "siv" },
    { "fak-mchbar", "tuning", "Fake MCHBAR", B_LINUX, 0, "none (no /dev/mem access; no replay mode)", s_fak_mchbar, 0, "siv" },
    { "fak-pmts", "tuning", "Fake PM tables", B_LINUX, 0, "/sys/kernel/ryzen_smu_drv (presence only)", s_fak_pmts, 0, "siv" },
    { "fak-spds", "tuning", "Fake SPDs", B_LINUX, 0, "hwmon spd5118/jc42; srs-root spd.txt", s_fak_spds, 0, "siv" },
    { "filter", "tuning", "Filter", B_LINUX, 0, "internal: display filter", s_filter, 0, "siv" },
    { "final", "tuning", "Final processing", B_LINUX, 0, "/proc/self/stat; /proc/uptime", s_final, 0, "siv" },
    { "gpu", "xxb", "GPU info buffer", B_LINUX, 0, "/sys/class/drm/card*/device", s_gpu, 2000, "siv" },
    { "help", "tuning", "Help", B_LINUX, 0, "internal", s_help, 0, "siv" },
    { "highlight", "hardware", "Highlight setup", B_LINUX, 0, "SRSDEF.ini", s_highlight, 0, "siv" },
    { "hot-keys", "hardware", "Hotkeys", B_LINUX, 0, "internal: GUI accelerators", s_hotkeys, 0, "siv" },
    { "image-exe", "tuning", "SIV image (EXE)", B_LINUX, 0, "readelf -S /proc/self/exe", s_image_exe, 0, "siv" },
    { "image-sys", "tuning", "SIV image (SYS driver)", B_WINONLY, 0, "none (no kernel driver needed); nearest /sys/module", s_image_sys, 0, "siv" },
    { "image-tree", "tuning", "Image tree", B_LINUX, 0, "readelf -d; /proc/self/maps", s_image_tree, 0, "siv" },
    { "initial", "tuning", "Initial processing", B_LINUX, 0, "internal", s_initial, 0, "siv" },
    { "install", "tuning", "Install", B_LINUX, 0, "srs-gui.desktop menu entries", s_install, 0, "siv" },
    { "languages", "tuning", "SIV languages", B_LINUX, 0, "internal; LANG/LC_*", s_languages, 0, "siv" },
    { "lck-setup", "tuning", "Lock setup", B_LINUX, 0, "internal", s_lck_setup, 0, "siv" },
    { "makers", "tuning", "Maker icons", B_LINUX, 0, "pci.ids vendors of present devices", s_makers, 0, "siv" },
    { "manifest", "tuning", "SIV manifest", B_WINONLY, 0, "none; nearest readelf -n", s_manifest, 0, "siv" },
    { "map-cache", "tuning", "Mapping cache", B_WINONLY, 0, "none (SIV driver mapping)", s_map_cache, 0, "siv" },
    { "map-unmap", "tuning", "Map/unmap test", B_WINONLY, 0, "none (IOCTL_SIV_MAP_UNMAP)", s_map_unmap, 0, "siv" },
    { "menus", "tuning", "Menus", B_LINUX, 0, "internal: section registry", s_menus, 0, "siv" },
    { "mutex", "tuning", "Mutex info", B_LINUX, 0, "/proc/locks", s_mutex, 0, "siv" },
    { "pmb", "xxb", "PMB info (power monitor buffer)", B_LINUX, 0, "/sys/class/hwmon power*/curr*", s_pmb, 1000, "siv" },
    { "rgb-debug", "hardware", "Colour RGB debug", B_LINUX, 0, "internal: palette; SRSDEF.ini", s_rgb_debug, 0, "siv" },
    { "sdm", "xxb", "SDM info (storage device monitor)", B_LINUX, 0, "/sys/class/block", s_sdm, 2000, "siv" },
    { "sequence", "tuning", "Sequence setup", B_LINUX, 0, "internal: default -save order", s_sequence, 0, "siv" },
    { "sgb-end", "tuning", "SGB end", B_LINUX, 0, "internal", s_sgb_end, 0, "siv" },
    { "sgb-ini", "xxb", "SGB info (system/board buffer)", B_LINUX, 0, "/sys/class/dmi/id; srs-root dmidecode.txt (serial)", s_sgb_ini, 0, "siv" },
    { "siv-active", "windows", "Active SIV instances", B_LINUX, 0, "/proc/*/comm", s_siv_active, 0, "siv" },
    { "siv-autorun", "tuning", "SIV autorun", B_LINUX, 0, "~/.config/autostart/srs-gui.desktop", s_siv_autorun, 0, "siv" },
    { "siv-dll", "tuning", "SIV DLLs", B_LINUX, 0, "/proc/self/maps; hwdata *.ids", s_siv_dll, 0, "siv" },
    { "siv-drv", "tuning", "SIV driver", B_WINONLY, 0, "none (no driver needed); srs-root cache", s_siv_drv, 0, "siv" },
    { "siv-ini", "tuning", "SIVDEF.ini", B_LINUX, 0, "~/.config/srs/SRSDEF.ini", s_siv_ini, 0, "siv" },
    { "siv-location", "tuning", "SIV location / window poke", B_LINUX, 0, "internal", s_siv_location, 0, "siv" },
    { "siv-qualifiers", "tuning", "SIV qualifiers", B_LINUX, 0, "internal: command line", s_qualifiers, 0, "siv" },
    { "siv-quals", "tuning", "SIV qualifier list", B_LINUX, 0, "internal: command line", s_quals, 0, "siv" },
    { "siv-setup", "tuning", "SIV setup", B_LINUX, 0, "~/.config/srs/SRSDEF.ini", s_siv_setup, 0, "siv" },
    { "siv-sizes", "tuning", "SIV sizes", B_LINUX, 0, "internal: sizeof", s_siv_sizes, 0, "siv" },
    { "siv-srv", "tuning", "SIV driver service", B_WINONLY, 0, "none", s_siv_srv, 0, "siv" },
    { "siv-usage", "tuning", "SIV usage history", B_LINUX, 0, "image / SRSDEF.ini dates", s_siv_usage, 0, "siv" },
    { "siv-use", "tuning", "SIV use / update", B_LINUX, 0, "hwdata *.ids headers (no network)", s_siv_use, 0, "siv" },
    { "smb-end", "tuning", "SMB end", B_LINUX, 0, "internal", s_smb_end, 0, "siv" },
    { "smb-ini", "tuning", "SMB info", B_LINUX, 0, "/sys/bus/i2c/devices (names only)", s_smb_ini, 0, "siv" },
    { "states", "tuning", "Lock states", B_LINUX, 0, "/proc/self/task/*/stat,wchan", s_states, 0, "siv" },
    { "stc", "xxb", "STC info (thread contexts)", B_LINUX, 0, "/proc/self/task", s_stc, 2000, "siv" },
    { "tune", "tuning", "Tune system", B_LINUX, 0, "/proc/sys", s_tune, 0, "siv" },
    { "tuning", "tuning", "Tuning", B_LINUX, 0, "tuned; cpufreq; sched_ext; THP; I/O schedulers", s_tuning, 0, "siv" },
    { "tzm", "xxb", "TZM info (thermal zone buffer)", B_LINUX, 0, "/sys/class/thermal", s_tzm, 2000, "siv" },
    { "words", "tuning", "Words", B_LINUX, 0, "internal", s_words, 0, "siv" },
    { "xxb", "tuning", "XXB internal buffers", B_LINUX, 0, "internal", s_xxb, 0, "siv" },
    { NULL, NULL, NULL, B_LINUX, 0, NULL, NULL, 0, NULL }
};
