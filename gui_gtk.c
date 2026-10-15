/* gui_gtk.c - srs-gui: native Linux GUI (plain C + GTK3) laid out the way SIV64X is.
 *
 * The main window shows [overview] with SIV's button row; every SIV item (538, see sec.h /
 * CHECKLIST.tsv) opens as a panel with SIV's own sub-button row, and Menu lists them all.
 * Tool windows: CPU Burnin, CPU Stress, Benchmark, IPI Delay, Latency, Device ID Lookup, Find.
 * With command-line arguments the same binary runs the CLI ("SIV64X -save" style). */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include "srs.h"
#include "sec.h"
#include "burner.h"
#include "bench.h"

#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* optional hooks provided by sec_sensors.c (SIV's Zero and Hide buttons) */
void sensors_zero(void) __attribute__((weak));
void sensors_set_hide_unused(int on) __attribute__((weak));

static const char *g_pal[C_NCOL] = { "#000000", "#3399FF", "#0000CC", "#9C0FB8", "#009999", "#FF8800", "#00A000", "#E00000", "#000000", "#808080" };
static char g_findcol[16] = "#FFFF00", g_ini[1100], g_host[256] = "localhost", g_image[64] = "srs-gui", g_gpucmd[512];
static int g_highlight = 1, g_hide_unused;
/* SIV lives in the Icon Tray: tray options (SRSDEF.ini) */
static int g_opt_tray = 1, g_opt_close_tray = 0, g_opt_min_tray = 1, g_opt_start_tray = 0;

typedef enum { W_SECTION, W_IDLOOKUP, W_FIND, W_BURN, W_BENCH } wkind;
enum { BK_BENCH, BK_IPI, BK_LAT };

typedef struct swin {
    wkind kind;
    char id[160];
    int is_main, sub;                     /* sub: W_BURN 0 burnin / 1 stress; W_BENCH BK_* */
    GtkWidget *win, *view, *plot;
    GtkTextBuffer *buf;
    guint timer;
    double vscroll;
    /* id lookup */
    GtkWidget *l_id, *l_mode, *l_modelbl, *l_pci, *l_usb;
    int l_state;
    /* find */
    GtkWidget *f_pat, *f_icase, *f_fixed, *f_hl, *f_max, *f_report;
    srs_findres fr;
    int have_fr, fr_rc, busy, curmatch;
    char fr_pattern[512], fr_err[1400];
    GPtrArray *marks;
    /* burn */
    GtkWidget *b_time, *b_hours, *b_cool, *b_cycles, *b_limit, *b_same, *b_gpu, *b_gpucmd, *b_start, *b_stop;
    GtkWidget *b_cpu[BURN_MAXCPU];
    int nb_cpu;
    double hist[4][600];
    int nhist;
    burn_status st;
    /* bench */
    GtkWidget *bm_mode, *bm_secs, *bm_run, *bm_cpus, *bm_rounds, *bm_max, *bm_status;
    int running;
} swin;

static GPtrArray *g_wins;
static swin *g_main;

static swin *open_section(const char *id);
static void render(swin *w);
static void open_tool(wkind k, int sub);

/* ======================================================================
 * GTK text backend for srs_out
 * ====================================================================== */
typedef struct { swin *w; GtkTextIter end; const char *tabtag; } gctx;

static void ensure_tags(GtkTextBuffer *b)
{
    GtkTextTagTable *tt = gtk_text_buffer_get_tag_table(b);
    char n[8];
    int i;
    if (gtk_text_tag_table_lookup(tt, "c1")) return;
    for (i = 0; i < C_NCOL; i++) {
        snprintf(n, sizeof n, "c%d", i);
        gtk_text_buffer_create_tag(b, n, "foreground", g_pal[i], NULL);
    }
    gtk_text_buffer_create_tag(b, "b", "weight", PANGO_WEIGHT_BOLD, NULL);
    gtk_text_buffer_create_tag(b, "m", "family", "Courier New", NULL);
    gtk_text_buffer_create_tag(b, "hl", "background", g_findcol, NULL);
}

static const char *tabs_tag(GtkTextBuffer *b, const int *px, int n)
{
    static char name[512];
    GtkTextTagTable *tt = gtk_text_buffer_get_tag_table(b);
    int i;
    size_t l;
    snprintf(name, sizeof name, "tabs");
    for (i = 0; i < n; i++) { l = strlen(name); snprintf(name + l, sizeof name - l, "_%d", px[i]); }
    if (!gtk_text_tag_table_lookup(tt, name)) {
        PangoTabArray *ta = pango_tab_array_new(n ? n : 1, TRUE);
        for (i = 0; i < n; i++) pango_tab_array_set_tab(ta, i, PANGO_TAB_LEFT, px[i]);
        gtk_text_buffer_create_tag(b, name, "tabs", ta, NULL);
        pango_tab_array_free(ta);
    }
    return gtk_text_tag_table_lookup(tt, name) ? name : NULL;
}

static char *clean(const char *s, gssize len)
{
    char *v = g_utf8_make_valid(s, len), *o = v, *i;
    for (i = v; *i; i++)
        if ((unsigned char)*i >= 0x20 || *i == '\t' || *i == '\n') *o++ = *i;
    *o = 0;
    return v;
}

static void ins(gctx *g, int col, int bold, int mono, int hl, const char *s, gssize len)
{
    char cn[8], *c = clean(s, len);
    GtkTextIter start;
    GtkTextBuffer *b = g->w->buf;
    int off = gtk_text_iter_get_offset(&g->end);
    snprintf(cn, sizeof cn, "c%d", col >= 0 && col < C_NCOL ? col : C_BLACK);
    gtk_text_buffer_insert(b, &g->end, c, -1);
    gtk_text_buffer_get_iter_at_offset(b, &start, off);
    gtk_text_buffer_apply_tag_by_name(b, cn, &start, &g->end);
    if (bold) gtk_text_buffer_apply_tag_by_name(b, "b", &start, &g->end);
    if (mono) gtk_text_buffer_apply_tag_by_name(b, "m", &start, &g->end);
    if (hl) gtk_text_buffer_apply_tag_by_name(b, "hl", &start, &g->end);
    if (g->tabtag) gtk_text_buffer_apply_tag_by_name(b, g->tabtag, &start, &g->end);
    g_free(c);
}

static void gx_text(srs_out *o, int c, int b, int m, const char *s) { ins(o->ctx, c, b, m, 0, s, -1); }
static void gx_tab(srs_out *o) { ins(o->ctx, C_BLACK, 0, 0, 0, "\t", -1); }
static void gx_par(srs_out *o) { ins(o->ctx, C_BLACK, 0, 0, 0, "\n", -1); }
static void gx_tabs(srs_out *o, const int *px, int n) { gctx *g = o->ctx; g->tabtag = tabs_tag(g->w->buf, px, n); }

static void out_begin(swin *w, srs_out *o, gctx *g, const char *arg)
{
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(w->buf, &s, &e);
    gtk_text_buffer_delete(w->buf, &s, &e);
    memset(g, 0, sizeof *g);
    g->w = w;
    gtk_text_buffer_get_end_iter(w->buf, &g->end);
    memset(o, 0, sizeof *o);
    o->text = gx_text; o->tab = gx_tab; o->par = gx_par; o->tabs = gx_tabs;
    o->ctx = g;
    o->gui = 1;
    o->arg = arg;
}

/* ======================================================================
 * settings (SIV keeps SIVDEF.txt; Linux: ~/.config/srs/SRSDEF.ini)
 * ====================================================================== */
static void settings_load(void)
{
    GKeyFile *kf = g_key_file_new();
    char *dir = g_build_filename(g_get_user_config_dir(), "srs", NULL);
    snprintf(g_ini, sizeof g_ini, "%s/SRSDEF.ini", dir);
    g_free(dir);
    if (g_key_file_load_from_file(kf, g_ini, G_KEY_FILE_KEEP_COMMENTS, NULL)) {
        char *c = g_key_file_get_string(kf, "SRSDEF", "Colour Find Text", NULL);
        char *gc = g_key_file_get_string(kf, "SRSDEF", "GPU Load Command", NULL);
        GError *e = NULL;
        int h = g_key_file_get_integer(kf, "SRSDEF", "Highlight Matches", &e);
        if (c && strlen(c) == 6) snprintf(g_findcol, sizeof g_findcol, "#%s", c);
        if (!e) g_highlight = h; else g_error_free(e);
        if (gc) snprintf(g_gpucmd, sizeof g_gpucmd, "%s", gc);
        {
            static const char *const keys[] = { "Tray Icon", "Close To Tray", "Minimize To Tray", "Start In Tray" };
            int *vars[] = { &g_opt_tray, &g_opt_close_tray, &g_opt_min_tray, &g_opt_start_tray };
            int k;
            for (k = 0; k < 4; k++) {
                GError *e2 = NULL;
                int v = g_key_file_get_integer(kf, "SRSDEF", keys[k], &e2);
                if (!e2) *vars[k] = v; else g_error_free(e2);
            }
        }
        g_free(c);
        g_free(gc);
    }
    g_key_file_free(kf);
}

static void settings_save(void)
{
    GKeyFile *kf = g_key_file_new();
    char *dir = g_path_get_dirname(g_ini);
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);
    g_key_file_load_from_file(kf, g_ini, G_KEY_FILE_KEEP_COMMENTS, NULL);
    g_key_file_set_string(kf, "SRSDEF", "Colour Find Text", g_findcol + 1);
    g_key_file_set_integer(kf, "SRSDEF", "Highlight Matches", g_highlight);
    g_key_file_set_string(kf, "SRSDEF", "Font Name", "Tahoma");
    g_key_file_set_string(kf, "SRSDEF", "GPU Load Command", g_gpucmd);
    g_key_file_set_integer(kf, "SRSDEF", "Tray Icon", g_opt_tray);
    g_key_file_set_integer(kf, "SRSDEF", "Close To Tray", g_opt_close_tray);
    g_key_file_set_integer(kf, "SRSDEF", "Minimize To Tray", g_opt_min_tray);
    g_key_file_set_integer(kf, "SRSDEF", "Start In Tray", g_opt_start_tray);
    g_key_file_save_to_file(kf, g_ini, NULL);
    g_key_file_free(kf);
}

/* ======================================================================
 * SIV button rows (transcribed from SIV64X's panels; "Windows" -> "Linux")
 * ====================================================================== */
typedef struct { const char *label, *target; } btn;

#define B(l, t) { l, t }
static const btn row_main[] = { B("_OK", "!close"), B("_Copy", "!copy"), B("_Linux", "windows"), B("_Machine", "machine"),
    B("_Sensors", "sensors"), B("_USB Bus", "usb-bus"), B("_Processors", "processors"), B("_Volumes", "volumes"),
    B("_Adapters", "adapters"), B("_PCI Bus", "pci-bus"), B("A_bout", "about"), B("_Help", "help"), B("Me_nu", "!menu"), B(NULL, NULL) };
static const btn row_windows[] = { B("_OK", "!close"), B("_Copy", "!copy"), B("_Linux", "windows"), B("_Machine", "machine"),
    B("_Sensors", "sensors"), B("_USB Bus", "usb-bus"), B("_Processors", "processors"), B("_Volumes", "volumes"),
    B("_PCI Bus", "pci-bus"), B("ALL _Dev", "all-dev"), B("_Graphics", "graphics"), B(NULL, NULL) };
static const btn row_machine[] = { B("_OK", "!close"), B("_Copy", "!copy"), B("_Linux", "windows"), B("_Machine", "machine"),
    B("_DIMMs", "dimms"), B("_Status", "dimm-status"), B("_USB Bus", "usb-bus"), B("_Volumes", "volumes"),
    B("_PCI Bus", "pci-bus"), B("ALL D_ev", "all-dev"), B(NULL, NULL) };
static const btn row_sensors[] = { B("_OK", "!close"), B("_Copy", "!copy"), B("_Zero", "!zero"), B("_Linux", "windows"),
    B("_Sensors", "sensors"), B("S_tatus", "hw-status"), B("C_ooling", "cooling"), B("_Power", "power"),
    B("_Volumes", "volumes"), B("_Hide", "!hide"), B(NULL, NULL) };
static const btn row_usb[] = { B("_OK", "!close"), B("_Copy", "!copy"), B("_Linux", "windows"), B("_Machine", "machine"),
    B("L_ink Devices", "link-devices"), B("_USB Bus", "usb-bus"), B("USB _Disks", "usb-disks"), B("_HID Bus", "hid-bus"),
    B("_PCI Bus", "pci-bus"), B("USB _Info", "usb-info"), B(NULL, NULL) };
static const btn row_processors[] = { B("_OK", "!close"), B("_Copy", "!copy"), B("_Linux", "windows"), B("_Processors", "processors"),
    B("_Features", "features"), B("_Hypervisor", "hypervisor"), B("_Groups", "group"), B("CPU-_0", "processor:0"),
    B("CPU-_1", "processor:1"), B("CPU-_4", "processor:4"), B("CPU-_8", "processor:8"), B("Cache-0", "cache:0"),
    B("Cache-1", "cache:1"), B("Cache-2", "cache:2"), B("Cache-3", "cache:3"), B("CPU _Stress", "!stress"),
    B("CPU _Burnin", "!burn"), B("Bench_mark", "!bench"), B(NULL, NULL) };
static const btn row_volumes[] = { B("_OK", "!close"), B("_Copy", "!copy"), B("_Linux", "windows"), B("_Machine", "machine"),
    B("_Sensors", "sensors"), B("_USB Bus", "usb-bus"), B("_Volumes", "volumes"), B("Drive _/", "volumes:/"),
    B("_FS", "ntfs:/"), B("ALL _Dev", "all-dev"), B(NULL, NULL) };
static const btn row_adapters[] = { B("_OK", "!close"), B("_Copy", "!copy"), B("_Linux", "windows"), B("_Adapters", "adapters"),
    B("Co_unters", "counters"), B("_Interface", "interface"), B("A_RP", "arp-table"), B("Rou_ting", "routing"),
    B("TCP V_4", "tcp-ip4-ports"), B("TCP V_6", "tcp-ip6-ports"), B("_Wi-Fi", "wi-fi"), B("_Bluetooth", "bluetooth"),
    B("Show _DNS", "dns-names"), B("_Ping", "ping"), B("_Main", "!main"), B(NULL, NULL) };
static const btn row_pci[] = { B("_OK", "!close"), B("_Copy", "!copy"), B("_Linux", "windows"), B("_Machine", "machine"),
    B("_USB Bus", "usb-bus"), B("P_roperties", "properties"), B("P_nP Dev", "pnp-dev"), B("_Volumes", "volumes"),
    B("_Drives", "drives"), B("_PCI Bus", "pci-bus"), B("PCI D_ev", "pci-dev"), B("U_AA Dev", "uaa-dev"),
    B("ALL De_v", "all-dev"), B(NULL, NULL) };
static const btn row_help[] = { B("_OK", "!close"), B("_Copy", "!copy"), B("_Linux", "windows"), B("_DNS Names", "dns-names"),
    B("_Lookup", "lookup"), B("_USB Dev", "usb-dev"), B("A_bout", "about"), B("_Help", "help"), B(NULL, NULL) };
static const btn row_lookup[] = { B("_OK", "!close"), B("_Copy", "!copy"), B("_Linux", "windows"), B("_DNS Names", "dns-names"),
    B("_Signature", "signing"), B("SRS _DLLs", "siv-dll"), B("_Lookup", "lookup"), B("_ID Lookup", "!idlookup"),
    B("A_bout", "about"), B("_Help", "help"), B(NULL, NULL) };
static const btn row_default[] = { B("_OK", "!close"), B("_Copy", "!copy"), B("_Refresh", "!refresh"), B("Me_nu", "!menu"),
    B("_Help", "help"), B(NULL, NULL) };

static int is(const char *base, const char *const *list)
{
    for (; *list; list++) if (!strcmp(base, *list)) return 1;
    return 0;
}

static const btn *row_for(const char *id, int is_main)
{
    static const char *const l_mach[] = { "machine", "dimms", "dimm-status", NULL };
    static const char *const l_sens[] = { "sensors", "hw-status", "cooling", "power", NULL };
    static const char *const l_usb[] = { "usb-bus", "usb-info", "hid-bus", "usb-disks", "link-devices", NULL };
    static const char *const l_proc[] = { "processors", "processor", "features", "hypervisor", "group", "cache", NULL };
    static const char *const l_vol[] = { "volumes", "ntfs", "fat", NULL };
    static const char *const l_ad[] = { "adapters", "counters", "interface", "arp-table", "routing", "tcp-ip4-ports", "tcp-ip6-ports",
                                        "wi-fi", "bluetooth", "dns-names", "ping", NULL };
    static const char *const l_pci[] = { "pci-bus", "pci-dev", "pnp-dev", "properties", "uaa-dev", "drives", "all-dev", NULL };
    static const char *const l_help[] = { "help", "about", NULL };
    static const char *const l_look[] = { "lookup", "siv-dll", "signing", NULL };
    char base[64];
    snprintf(base, sizeof base, "%.*s", (int)strcspn(id, ":"), id);
    if (is_main) return row_main;
    if (!strcmp(base, "windows")) return row_windows;
    if (is(base, l_mach)) return row_machine;
    if (is(base, l_sens)) return row_sensors;
    if (is(base, l_usb)) return row_usb;
    if (is(base, l_proc)) return row_processors;
    if (is(base, l_vol)) return row_volumes;
    if (is(base, l_ad)) return row_adapters;
    if (is(base, l_pci)) return row_pci;
    if (is(base, l_help)) return row_help;
    if (is(base, l_look)) return row_lookup;
    return row_default;
}

/* ======================================================================
 * window bookkeeping
 * ====================================================================== */
static swin *find_win(wkind k, const char *id, int sub)
{
    guint i;
    for (i = 0; i < g_wins->len; i++) {
        swin *w = g_ptr_array_index(g_wins, i);
        if (w->kind == k && !w->is_main && (k != W_SECTION || !strcmp(w->id, id)) && (k == W_SECTION || w->sub == sub)) return w;
    }
    return NULL;
}

static void on_destroy(GtkWidget *wid, gpointer data)
{
    swin *w = data;
    (void)wid;
    if (w->timer) g_source_remove(w->timer);
    if (w->kind == W_BURN && burn_active()) burn_stop();
    if (w->have_fr) srs_find_free(&w->fr);
    if (w->marks) g_ptr_array_free(w->marks, TRUE);
    g_ptr_array_remove(g_wins, w);
    if (w->is_main) gtk_main_quit();
    g_free(w);
}

static void copy_all(swin *w)
{
    GtkTextIter s, e;
    char *txt;
    if (!w->buf) return;
    gtk_text_buffer_get_bounds(w->buf, &s, &e);
    txt = gtk_text_buffer_get_text(w->buf, &s, &e, FALSE);
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), txt, -1);
    gtk_clipboard_store(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
    g_free(txt);
}

/* ======================================================================
 * Menu (SIV's tray menu): every item, grouped
 * ====================================================================== */
static void save_dialog(int all);
static void build_options(GtkWidget *menu);
static void run_root(void);
static void on_menu_item(GtkMenuItem *mi, gpointer data) { (void)mi; open_section(data); }
static void on_menu_tool(GtkMenuItem *mi, gpointer data)
{
    const char *t = data;
    (void)mi;
    if (!strcmp(t, "burn")) open_tool(W_BURN, 0);
    else if (!strcmp(t, "stress")) open_tool(W_BURN, 1);
    else if (!strcmp(t, "bench")) open_tool(W_BENCH, BK_BENCH);
    else if (!strcmp(t, "ipi")) open_tool(W_BENCH, BK_IPI);
    else if (!strcmp(t, "lat")) open_tool(W_BENCH, BK_LAT);
    else if (!strcmp(t, "idlookup")) open_tool(W_IDLOOKUP, 0);
    else if (!strcmp(t, "find")) open_tool(W_FIND, 0);
    else if (!strcmp(t, "save")) save_dialog(0);
    else if (!strcmp(t, "saveall")) save_dialog(1);
    else if (!strcmp(t, "root")) run_root();
    else if (!strcmp(t, "exit") && g_main) gtk_widget_destroy(g_main->win);
}

static GtkWidget *menu_add(GtkWidget *menu, const char *label)
{
    GtkWidget *it = gtk_menu_item_new_with_mnemonic(label);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), it);
    return it;
}

static void build_menu(GtkWidget *menu)
{
    static const struct { const char *group, *label; } groups[] = {
        { "cpu", "_Processors" }, { "machine", "_Machine / BIOS / Memory" }, { "sensors", "_Sensors / Cooling / Power" },
        { "storage", "_Volumes / Drives" }, { "network", "_Network" }, { "os", "_Linux (SIV Windows items)" },
        { "gpu", "_Graphics / Monitors / Audio" }, { "bus", "_Buses / Devices" }, { "tools", "_Tools" }, { "siv", "S_RS (SIV items)" },
        { NULL, NULL } };
    static const struct { const char *label, *tool; } tools[] = {
        { "CPU _Burnin...", "burn" }, { "CPU _Stress...", "stress" }, { "_Benchmark...", "bench" }, { "_IPI Delay...", "ipi" },
        { "_Latency...", "lat" }, { "Device _ID Lookup...", "idlookup" }, { "_Find in Report...", "find" }, { NULL, NULL } };
    int g, i, n = srs_section_count();
    for (i = 0; tools[i].label; i++) g_signal_connect(menu_add(menu, tools[i].label), "activate", G_CALLBACK(on_menu_tool), (gpointer)tools[i].tool);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    for (g = 0; groups[g].group; g++) {
        GtkWidget *sub = gtk_menu_new(), *top = menu_add(menu, groups[g].label);
        int cnt = 0;
        for (i = 0; i < n; i++) {
            const srs_item *s = srs_section_at(i);
            char lab[200];
            GtkWidget *it;
            if (!s || !s->group || strcmp(s->group, groups[g].group)) continue;
            snprintf(lab, sizeof lab, "%s  [%s]%s", s->title, s->id,
                     s->bucket == B_ABSENT ? "  (not present)" : s->bucket == B_WINONLY ? "  (Windows-only)" : "");
            it = gtk_menu_item_new_with_label(lab);
            g_signal_connect(it, "activate", G_CALLBACK(on_menu_item), (gpointer)s->id);
            gtk_menu_shell_append(GTK_MENU_SHELL(sub), it);
            cnt++;
        }
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(top), sub);
        if (!cnt) gtk_widget_set_sensitive(top, FALSE);
    }
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    {
        GtkWidget *opt = gtk_menu_new();
        build_options(opt);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_add(menu, "_Options")), opt);
    }
    g_signal_connect(menu_add(menu, "Save _Report..."), "activate", G_CALLBACK(on_menu_tool), (gpointer)"save");
    g_signal_connect(menu_add(menu, "Save _All Items..."), "activate", G_CALLBACK(on_menu_tool), (gpointer)"saveall");
    g_signal_connect(menu_add(menu, "Run as _root (srs-root)..."), "activate", G_CALLBACK(on_menu_tool), (gpointer)"root");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    g_signal_connect(menu_add(menu, "E_xit"), "activate", G_CALLBACK(on_menu_tool), (gpointer)"exit");
}

static void popup_menu(GtkWidget *anchor)
{
    GtkWidget *menu = gtk_menu_new();
    build_menu(menu);
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_widget(GTK_MENU(menu), anchor, GDK_GRAVITY_NORTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
}

/* ======================================================================
 * Save / Run as root
 * ====================================================================== */
static void save_dialog(int all)
{
    GtkWidget *d = gtk_file_chooser_dialog_new(all ? "Save All Items" : "Save Report", g_main ? GTK_WINDOW(g_main->win) : NULL,
                                               GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, NULL);
    char name[300];
    snprintf(name, sizeof name, "srs-%s%s.txt", g_host, all ? "-all" : "");
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(d), name);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(d), TRUE);
    if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT) {
        char *f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
        srs_save(all ? "[all]" : NULL, f);
        g_free(f);
    }
    gtk_widget_destroy(d);
}

static void root_done(GPid pid, gint status, gpointer data)
{
    guint i;
    (void)data;
    g_spawn_close_pid(pid);
    for (i = 0; i < g_wins->len; i++) { swin *w = g_ptr_array_index(g_wins, i); if (w->kind == W_SECTION) render(w); }
    if (g_main) {
        GtkWidget *m = gtk_message_dialog_new(GTK_WINDOW(g_main->win), GTK_DIALOG_MODAL, status == 0 ? GTK_MESSAGE_INFO : GTK_MESSAGE_WARNING,
                                              GTK_BUTTONS_OK, "%s", status == 0 ? "Root data collected into ~/.cache/srs/root"
                                                                                : "srs-root did not complete (cancelled or failed)");
        gtk_dialog_run(GTK_DIALOG(m));
        gtk_widget_destroy(m);
    }
}

static void run_root(void)
{
    char exe[1024] = "", helper[1100];
    ssize_t l = readlink("/proc/self/exe", exe, sizeof exe - 1);
    GtkWidget *m;
    GPid pid;
    if (l > 0) { exe[l] = 0; if (strrchr(exe, '/')) *strrchr(exe, '/') = 0; }
    snprintf(helper, sizeof helper, "%s/srs-root", exe);
    if (access(helper, X_OK)) snprintf(helper, sizeof helper, "%s/.local/bin/srs-root", g_get_home_dir());
    m = gtk_message_dialog_new(g_main ? GTK_WINDOW(g_main->win) : NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_OK_CANCEL,
                               "Collect root-only data with one pkexec prompt?\n\n"
                               "dmidecode, decode-dimms, smartctl / nvme smart-log, ACPI table copies, MSR reads, "
                               "lspci config space, ioports/iomem, dmesg.\nRead-only; results go to ~/.cache/srs/root.\n\n%s", helper);
    if (gtk_dialog_run(GTK_DIALOG(m)) == GTK_RESPONSE_OK) {
        char *argv[] = { (char *)"pkexec", helper, NULL };
        if (g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &pid, NULL))
            g_child_watch_add(pid, root_done, NULL);
    }
    gtk_widget_destroy(m);
}

/* ======================================================================
 * section windows
 * ====================================================================== */
static gboolean restore_idle(gpointer data)
{
    swin *w = data;
    if (g_ptr_array_find(g_wins, w, NULL) && w->view)
        gtk_adjustment_set_value(gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(w->view)), w->vscroll);
    return G_SOURCE_REMOVE;
}

static void render(swin *w)
{
    srs_out o;
    gctx g;
    const srs_item *it;
    const char *colon;
    if (w->kind != W_SECTION) return;
    w->vscroll = gtk_adjustment_get_value(gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(w->view)));
    colon = strchr(w->id, ':');
    out_begin(w, &o, &g, colon ? colon + 1 : NULL);
    it = srs_section_find(w->id);
    if (!it) { so_err(&o, "No such item"); return; }
    srs_section_render(it, &o);
    g_idle_add(restore_idle, w);
}

static gboolean live_tick(gpointer data) { render(data); return G_SOURCE_CONTINUE; }

static void on_btn(GtkButton *b, gpointer data);

static GtkWidget *button_row(swin *w, const btn *row)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    int i;
    for (i = 0; row[i].label; i++) {
        GtkWidget *b = gtk_button_new_with_mnemonic(row[i].label);
        gtk_widget_set_name(b, "sivbtn");
        gtk_widget_set_size_request(b, 66, -1);
        g_object_set_data(G_OBJECT(b), "target", (gpointer)row[i].target);
        g_signal_connect(b, "clicked", G_CALLBACK(on_btn), w);
        gtk_box_pack_start(GTK_BOX(box), b, FALSE, FALSE, 0);
    }
    return box;
}

/* double-click: open a device ID found on the line in Device ID Lookup */
static gboolean on_view_click(GtkWidget *view, GdkEventButton *ev, gpointer data)
{
    swin *w = data;
    GtkTextIter it, s, e, b0;
    int bx, by, usb;
    char *line, *p, *above, *pu, *pp;
    if (ev->type != GDK_2BUTTON_PRESS || ev->button != 1) return FALSE;
    gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(view), GTK_TEXT_WINDOW_WIDGET, (int)ev->x, (int)ev->y, &bx, &by);
    gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(view), &it, bx, by);
    s = it;
    gtk_text_iter_set_line_offset(&s, 0);
    e = s;
    gtk_text_iter_forward_to_line_end(&e);
    line = gtk_text_buffer_get_text(w->buf, &s, &e, FALSE);
    gtk_text_buffer_get_start_iter(w->buf, &b0);
    above = gtk_text_buffer_get_text(w->buf, &b0, &s, FALSE);
    pu = g_strrstr(above, "[USB]");
    pp = g_strrstr(above, "[PCI]");
    usb = pu && (!pp || pu > pp);
    g_free(above);
    for (p = line; *p; p++) {
        int k, ok = (p == line || p[-1] == ' ' || p[-1] == '\t') && p[4] == ':';
        for (k = 0; ok && k < 9; k++) if (k != 4 && !g_ascii_isxdigit(p[k])) ok = 0;
        if (ok && !(p[0] == '0' && p[1] == '0' && p[2] == '0' && p[3] == '0')) {
            char id[64], tgt[100];
            size_t l = strcspn(p, " \t");
            snprintf(id, sizeof id, "%.*s", (int)(l < 40 ? l : 40), p);
            snprintf(tgt, sizeof tgt, "id-lookup:%s%s", usb ? "usb:" : "", id);
            g_free(line);
            open_section(tgt);
            return TRUE;
        }
    }
    g_free(line);
    return FALSE;
}

static void on_populate(GtkTextView *v, GtkWidget *popup, gpointer data)
{
    GtkWidget *sep = gtk_separator_menu_item_new(), *sub = gtk_menu_new(), *top;
    (void)v; (void)data;
    gtk_menu_shell_append(GTK_MENU_SHELL(popup), sep);
    top = gtk_menu_item_new_with_mnemonic("SRS _Menu");
    build_menu(sub);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(top), sub);
    gtk_menu_shell_append(GTK_MENU_SHELL(popup), top);
    gtk_widget_show_all(popup);
}

static gboolean on_key(GtkWidget *wid, GdkEventKey *ev, gpointer data);

static GtkWidget *make_window(swin *w, const char *title, int width, int height)
{
    w->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(w->win), title);
    gtk_window_set_default_size(GTK_WINDOW(w->win), width, height);
    gtk_window_set_icon_name(GTK_WINDOW(w->win), "srs");
    if (!w->is_main && g_main) {
        gtk_window_set_transient_for(GTK_WINDOW(w->win), GTK_WINDOW(g_main->win));
        gtk_window_move(GTK_WINDOW(w->win), 40 + (int)(g_wins->len % 12) * 24, 40 + (int)(g_wins->len % 12) * 22);
    }
    g_signal_connect(w->win, "destroy", G_CALLBACK(on_destroy), w);
    g_signal_connect(w->win, "key-press-event", G_CALLBACK(on_key), w);
    g_ptr_array_add(g_wins, w);
    return w->win;
}

static GtkWidget *make_view(swin *w)
{
    GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
    w->view = gtk_text_view_new();
    gtk_widget_set_name(w->view, "panel");
    gtk_text_view_set_editable(GTK_TEXT_VIEW(w->view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(w->view), GTK_WRAP_NONE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(w->view), 6);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(w->view), 4);
    w->buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(w->view));
    ensure_tags(w->buf);
    g_signal_connect(w->view, "button-press-event", G_CALLBACK(on_view_click), w);
    g_signal_connect(w->view, "populate-popup", G_CALLBACK(on_populate), w);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(sw), w->view);
    return sw;
}

static swin *open_section(const char *id)
{
    swin *w = find_win(W_SECTION, id, 0);
    const srs_item *it = srs_section_find(id);
    GtkWidget *vb;
    char title[300];
    if (w) { gtk_window_present(GTK_WINDOW(w->win)); render(w); return w; }
    w = g_new0(swin, 1);
    w->kind = W_SECTION;
    snprintf(w->id, sizeof w->id, "%s", id);
    if (it && strchr(id, ':')) snprintf(title, sizeof title, "%s - %s %s", g_image, it->title, strchr(id, ':') + 1);
    else snprintf(title, sizeof title, "%s - %s", g_image, it ? it->title : id);
    make_window(w, title, 1180, 600);
    vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(vb), 4);
    gtk_container_add(GTK_CONTAINER(w->win), vb);
    gtk_box_pack_start(GTK_BOX(vb), make_view(w), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vb), button_row(w, row_for(id, 0)), FALSE, FALSE, 0);
    render(w);
    if (it && it->live_ms > 0) w->timer = g_timeout_add(it->live_ms < 500 ? 500 : (guint)it->live_ms, live_tick, w);
    gtk_widget_show_all(w->win);
    return w;
}

/* ======================================================================
 * Device ID Lookup tool (tri-state Tree / Scan / Info like SIV's Port Names box)
 * ====================================================================== */
static srs_db g_db[2];
static int g_dbok[2];

static const char *mode_text(int m)
{
    return m == MODE_TREE ? "Tree Lookup mode - Do bsearch() of a sorted table" :
           m == MODE_SCAN ? "Scan Lookup mode - Scan the .ids file" : "Info Lookup mode - Use the OS device database";
}

static void render_idlookup(swin *w)
{
    static const int tabs[] = { 24, 200, 700 }, t2[] = { 110 };
    srs_out o;
    gctx g;
    char buf[1400], recs[32];
    const char *id = gtk_entry_get_text(GTK_ENTRY(w->l_id));
    int mode = w->l_state, i;
    enum srs_bus bus = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->l_usb)) ? BUS_USB : BUS_PCI;
    out_begin(w, &o, &g, NULL);
    so_tabs(&o, tabs, 3);
    snprintf(buf, sizeof buf, "Lookup %s on the %s bus", id, bus == BUS_USB ? "USB" : "PCI");
    so_b(&o, C_VALUE, buf); so_t(&o, C_GRAY, "   "); so_t(&o, C_TEAL, mode_text(mode)); so_par(&o); so_par(&o);
    if (mode == MODE_INFO) {
        srs_info in;
        int rc = srs_info_lookup(bus, id, &in);
        so_tabs(&o, t2, 1);
        so_kv(&o, "Query", C_PURPLE, in.query);
        if (in.err[0]) so_kv(&o, "Error", C_RED, in.err);
        so_kv(&o, "Vendor", C_VALUE, in.vendor[0] ? in.vendor : "-");
        so_kv(&o, "Resolved", rc == 0 ? C_BLACK : C_RED, in.model[0] ? in.model : "-");
        snprintf(buf, sizeof buf, "info mode (systemd-hwdb) in %.2f ms", in.ms);
        so_par(&o); so_note(&o, buf);
        return;
    }
    if (!g_dbok[bus] && srs_db_load(&g_db[bus], bus) == 0) g_dbok[bus] = 1;
    if (!g_dbok[bus]) { so_err(&o, "cannot find the .ids file"); return; }
    {
        srs_result r;
        char err[256];
        if (srs_resolve(&g_db[bus], id, mode, &r, err, sizeof err)) { so_err(&o, err); return; }
        so_tab(&o); so_t(&o, C_LABEL, "Key"); so_tab(&o); so_t(&o, C_LABEL, "Entry"); so_par(&o);
        for (i = 0; i < r.nchain; i++) {
            so_t(&o, C_ORANGE, i == r.matched ? "<" : ""); so_tab(&o);
            so_t(&o, i == r.matched ? C_PURPLE : C_GRAY, r.chain[i]); so_tab(&o);
            so_t(&o, r.name[i][0] ? (i == r.matched ? C_BLACK : C_VALUE) : C_GRAY, r.name[i][0] ? r.name[i] : "(no entry)");
            so_par(&o);
        }
        so_par(&o);
        so_tabs(&o, t2, 1);
        so_kv(&o, "Vendor", C_VALUE, r.vendor[0] ? r.vendor : "-");
        if (r.subvendor[0]) so_kv(&o, "Subsystem", C_GREEN, r.subvendor);
        so_kv(&o, "Stock", C_TEAL, r.stock[0] ? r.stock : "-");
        if (r.resolved[0]) so_kv(&o, "Resolved", C_BLACK, r.resolved);
        else { snprintf(buf, sizeof buf, "No Matches for %s", id); so_kv(&o, "Resolved", C_RED, buf); }
        srs_thousands(g_db[bus].n, recs, sizeof recs);
        snprintf(buf, sizeof buf, "%s mode: %s records loaded in %.1f ms, lookup %.3f ms   (%s)", mode == MODE_TREE ? "tree" : "scan",
                 recs, g_db[bus].load_ms, r.lookup_ms, g_db[bus].path);
        so_par(&o); so_note(&o, buf);
    }
}

static void set_mode_box(swin *w)
{
    g_signal_handlers_block_matched(w->l_mode, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, w);
    gtk_toggle_button_set_inconsistent(GTK_TOGGLE_BUTTON(w->l_mode), w->l_state == MODE_SCAN);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->l_mode), w->l_state != MODE_INFO);
    g_signal_handlers_unblock_matched(w->l_mode, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, w);
    gtk_label_set_text(GTK_LABEL(w->l_modelbl), mode_text(w->l_state));
}

static void on_mode(GtkToggleButton *b, gpointer data)
{
    swin *w = data;
    (void)b;
    w->l_state = w->l_state == MODE_TREE ? MODE_SCAN : w->l_state == MODE_SCAN ? MODE_INFO : MODE_TREE;
    set_mode_box(w);
    render_idlookup(w);
}
static void on_bus(GtkToggleButton *b, gpointer data) { if (gtk_toggle_button_get_active(b)) render_idlookup(data); }
static void on_lookup_run(GtkWidget *x, gpointer data) { (void)x; render_idlookup(data); }

static GtkWidget *lbl(const char *s) { GtkWidget *l = gtk_label_new_with_mnemonic(s); gtk_widget_set_name(l, "lbl"); return l; }
static GtkWidget *sbtn(const char *s) { GtkWidget *b = gtk_button_new_with_mnemonic(s); gtk_widget_set_name(b, "sivbtn"); return b; }

/* ======================================================================
 * Find tool (worker thread, highlight, F3 navigation)
 * ====================================================================== */
typedef struct { swin *w; char report[1100], pattern[512]; int icase, fixed, max; srs_findres fr; int rc; char err[1400]; } findjob;

static void goto_match(swin *w, int dir)
{
    int n = w->marks ? (int)w->marks->len / 2 : 0;
    GtkTextIter a, b;
    char title[300];
    if (!n) { gtk_widget_error_bell(w->win); return; }
    w->curmatch = (w->curmatch + dir + n) % n;
    gtk_text_buffer_get_iter_at_mark(w->buf, &a, g_ptr_array_index(w->marks, 2 * w->curmatch));
    gtk_text_buffer_get_iter_at_mark(w->buf, &b, g_ptr_array_index(w->marks, 2 * w->curmatch + 1));
    gtk_text_buffer_select_range(w->buf, &a, &b);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(w->view), g_ptr_array_index(w->marks, 2 * w->curmatch), 0.1, FALSE, 0, 0);
    snprintf(title, sizeof title, "%s - Find - Match %d of %d shown (%d total)", g_image, w->curmatch + 1, n, w->fr.total);
    gtk_window_set_title(GTK_WINDOW(w->win), title);
}

static void render_find(swin *w)
{
    srs_out o;
    gctx g;
    char buf[1600], title[300];
    int i, j, k, maxshow = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w->f_max));
    out_begin(w, &o, &g, NULL);
    g_ptr_array_set_size(w->marks, 0);
    w->curmatch = -1;
    if (w->busy) { snprintf(buf, sizeof buf, "Searching for \"%s\" ...", w->fr_pattern); so_t(&o, C_TEAL, buf); so_par(&o); return; }
    if (!w->have_fr) {
        so_note(&o, "Enter a pattern (Python-style regex, or tick Literal) and press Enter or Find.");
        so_note(&o, "Searches every section (*.txt, lshw.html) of the selected sysreport folder.");
        return;
    }
    if (w->fr_rc) { so_err(&o, w->fr_err); return; }
    snprintf(buf, sizeof buf, "Searching %s for \"%s\"", w->fr.report, w->fr_pattern);
    so_t(&o, C_TEAL, buf); so_par(&o);
    for (i = 0; i < w->fr.nsec; i++) {
        const srs_section *s = &w->fr.secs[i];
        so_par(&o);
        snprintf(buf, sizeof buf, "== %s - %d Match%s", s->file, s->count, s->count != 1 ? "es" : "");
        so_b(&o, C_VALUE, buf); so_par(&o);
        for (j = 0; j < s->nshown; j++) {
            const srs_hit *h = &s->hits[j];
            int at = 0;
            snprintf(buf, sizeof buf, "%7d: ", h->lineno);
            ins(&g, C_GRAY, 0, 1, 0, buf, -1);
            for (k = 0; k < h->nspan; k++) {
                ins(&g, C_BLACK, 0, 1, 0, h->text + at, h->spans[k].so - at);
                g_ptr_array_add(w->marks, gtk_text_buffer_create_mark(w->buf, NULL, &g.end, TRUE));
                ins(&g, C_BLACK, 0, 1, g_highlight, h->text + h->spans[k].so, h->spans[k].eo - h->spans[k].so);
                g_ptr_array_add(w->marks, gtk_text_buffer_create_mark(w->buf, NULL, &g.end, TRUE));
                at = h->spans[k].eo;
            }
            ins(&g, C_BLACK, 0, 1, 0, h->text + at, -1);
            so_par(&o);
        }
        if (s->count > s->nshown && s->count > maxshow) { snprintf(buf, sizeof buf, "         ... %d more (raise Max)", s->count - s->nshown); so_note(&o, buf); }
    }
    so_par(&o);
    if (!w->fr.total) { snprintf(buf, sizeof buf, "No Matches for %s", w->fr_pattern); so_err(&o, buf); }
    else { snprintf(buf, sizeof buf, "%d Matches in %d of %d sections  (%.1f ms)", w->fr.total, w->fr.nsec, w->fr.nfiles, w->fr.ms); so_t(&o, C_GREEN, buf); so_par(&o); }
    snprintf(title, sizeof title, "%s - Find - %d Matches", g_image, w->fr.total);
    gtk_window_set_title(GTK_WINDOW(w->win), title);
}

static gboolean find_done(gpointer data)
{
    findjob *j = data;
    swin *w = j->w;
    if (g_ptr_array_find(g_wins, w, NULL)) {
        if (w->have_fr) srs_find_free(&w->fr);
        w->fr = j->fr;
        w->fr_rc = j->rc;
        snprintf(w->fr_err, sizeof w->fr_err, "%s", j->err);
        w->have_fr = 1;
        w->busy = 0;
        render_find(w);
        if (w->marks->len) goto_match(w, +1);
    } else if (!j->rc) srs_find_free(&j->fr);
    g_free(j);
    return G_SOURCE_REMOVE;
}

static gpointer find_worker(gpointer data)
{
    findjob *j = data;
    j->rc = srs_find(j->report[0] ? j->report : NULL, j->pattern, j->icase, j->fixed, j->max, &j->fr, j->err, sizeof j->err);
    g_idle_add(find_done, j);
    return NULL;
}

static void start_find(swin *w)
{
    findjob *j;
    GThread *th;
    if (w->busy) return;
    j = g_new0(findjob, 1);
    j->w = w;
    gtk_spin_button_update(GTK_SPIN_BUTTON(w->f_max));
    snprintf(j->pattern, sizeof j->pattern, "%s", gtk_entry_get_text(GTK_ENTRY(w->f_pat)));
    snprintf(j->report, sizeof j->report, "%s", gtk_entry_get_text(GTK_ENTRY(w->f_report)));
    j->max = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w->f_max));
    j->icase = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->f_icase));
    j->fixed = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->f_fixed));
    snprintf(w->fr_pattern, sizeof w->fr_pattern, "%s", j->pattern);
    w->busy = 1;
    render_find(w);
    th = g_thread_try_new("srs-find", find_worker, j, NULL);
    if (!th) { w->busy = 0; w->fr_rc = 2; w->have_fr = 1; snprintf(w->fr_err, sizeof w->fr_err, "Failed to Create Worker Thread"); g_free(j); render_find(w); return; }
    g_thread_unref(th);
}

static void on_find_run(GtkWidget *x, gpointer data) { (void)x; start_find(data); }

static void toggle_highlight(void)
{
    guint i;
    g_highlight = !g_highlight;
    settings_save();
    for (i = 0; i < g_wins->len; i++) {
        swin *w = g_ptr_array_index(g_wins, i);
        if (w->kind == W_FIND) {
            g_signal_handlers_block_matched(w->f_hl, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, w);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->f_hl), g_highlight);
            g_signal_handlers_unblock_matched(w->f_hl, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, w);
            render_find(w);
        }
    }
}
static void on_hl_check(GtkToggleButton *b, gpointer data) { (void)data; if (gtk_toggle_button_get_active(b) != g_highlight) toggle_highlight(); }

static void choose_colour(swin *w)
{
    GtkWidget *d = gtk_color_chooser_dialog_new("Colour Find Text", GTK_WINDOW(w->win));
    GdkRGBA c;
    guint i;
    gdk_rgba_parse(&c, g_findcol);
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(d), &c);
    if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(d), &c);
        snprintf(g_findcol, sizeof g_findcol, "#%02X%02X%02X", (int)(c.red * 255 + .5), (int)(c.green * 255 + .5), (int)(c.blue * 255 + .5));
        settings_save();
        for (i = 0; i < g_wins->len; i++) {
            swin *x = g_ptr_array_index(g_wins, i);
            GtkTextTag *t = x->buf ? gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(x->buf), "hl") : NULL;
            if (t) g_object_set(t, "background", g_findcol, NULL);
        }
    }
    gtk_widget_destroy(d);
}

static void on_browse(GtkWidget *x, gpointer data)
{
    swin *w = data;
    GtkWidget *d = gtk_file_chooser_dialog_new("Select a sysreport folder", GTK_WINDOW(w->win), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                               "_Cancel", GTK_RESPONSE_CANCEL, "_Select", GTK_RESPONSE_ACCEPT, NULL);
    (void)x;
    if (srs_reports_root()) gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(d), srs_reports_root());
    if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT) {
        char *f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
        gtk_entry_set_text(GTK_ENTRY(w->f_report), f);
        g_free(f);
    }
    gtk_widget_destroy(d);
}

static void on_latest(GtkWidget *x, gpointer data)
{
    swin *w = data;
    char rep[1100] = "";
    (void)x;
    srs_latest_report(rep, sizeof rep);
    gtk_entry_set_text(GTK_ENTRY(w->f_report), rep);
}

/* ======================================================================
 * CPU Burnin / CPU Stress window
 * ====================================================================== */
static void fmt_hms(double s, char *b, size_t n)
{
    long t = s < 0 ? 0 : (long)s;
    snprintf(b, n, "%02ld:%02ld:%02ld", t / 3600, (t / 60) % 60, t % 60);
}

static void render_burn(swin *w)
{
    static const int tabs[] = { 200, 330, 460, 590, 720 };
    static const int t2[] = { 70, 130, 200, 270, 350, 440, 530, 620, 710, 790 };
    static const char *const states[] = { "Idle", "Burning", "Cooling", "Pass", "Fail", "Stopped" };
    static const char *const rows[7] = { "CPU Load", "CPU Frequency", "Hottest Core Temp", "Hottest SIO Sensor Temp", "GPU Load", "GPU Temp", "GPU Frequency" };
    static const char *const fmt[7] = { "%.1f%%", "%.0f MHz", "%.1f°C", "%.1f°C", "%.0f%%", "%.1f°C", "%.0f MHz" };
    srs_out o;
    gctx g;
    burn_status *st = &w->st;
    char a[64], b[64];
    int i;
    double vs[7][4] = {
        { st->cur.cpu_load, st->avg.cpu_load, st->roll.cpu_load, st->max.cpu_load },
        { st->cur.cpu_mhz, st->avg.cpu_mhz, st->roll.cpu_mhz, st->max.cpu_mhz },
        { st->cur.cpu_temp, st->avg.cpu_temp, st->roll.cpu_temp, st->max.cpu_temp },
        { st->cur.sio_temp, st->avg.sio_temp, st->roll.sio_temp, st->max.sio_temp },
        { st->cur.gpu_load, st->avg.gpu_load, st->roll.gpu_load, st->max.gpu_load },
        { st->cur.gpu_temp, st->avg.gpu_temp, st->roll.gpu_temp, st->max.gpu_temp },
        { st->cur.gpu_mhz, st->avg.gpu_mhz, st->roll.gpu_mhz, st->max.gpu_mhz } };
    w->vscroll = gtk_adjustment_get_value(gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(w->view)));
    out_begin(w, &o, &g, NULL);
    so_tabs(&o, tabs, 5);
    fmt_hms(st->elapsed, a, sizeof a);
    fmt_hms(st->remaining > 0 ? st->remaining : 0, b, sizeof b);
    so_t(&o, C_LABEL, "Status"); so_tab(&o);
    so_b(&o, st->state == BURN_FAIL ? C_RED : st->state == BURN_PASS ? C_GREEN : C_ORANGE, states[st->state >= 0 && st->state <= 5 ? st->state : 0]);
    so_tab(&o); so_t(&o, C_LABEL, "Elapsed"); so_tab(&o); so_t(&o, C_BLACK, a); so_tab(&o); so_t(&o, C_LABEL, "Remaining  "); so_t(&o, C_BLACK, b); so_par(&o);
    so_t(&o, C_GRAY, st->status); so_par(&o); so_par(&o);
    so_row(&o, 5, (const char *const[]){ "", "Current", "Overall Average", "Rolling Average", "Maximum" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    for (i = 0; i < 7; i++) {
        char c[4][48];
        int k;
        for (k = 0; k < 4; k++) snprintf(c[k], 48, fmt[i], vs[i][k]);
        so_row(&o, 5, (const char *const[]){ rows[i], c[0], c[1], c[2], c[3] }, (const int[]){ C_VALUE, C_BLACK, C_TEAL, C_TEAL, C_RED });
    }
    so_f(&o, C_GRAY, "Hottest sensors: %s / %s", st->cur.cpu_temp_name, st->cur.sio_name);
    so_par(&o); so_par(&o);
    so_tabs(&o, t2, 10);
    so_row(&o, 11, (const char *const[]){ "Thread", "CPU", "On CPU", "User", "Kernel", "Clock", "Whetstone", "Average", "Min", "Max", "Affinity" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    for (i = 0; i < st->nthreads; i++) {
        const burn_thread *t = &st->th[i];
        int cc = t->cur_cpu >= 0 && t->cur_cpu < BURN_MAXCPU ? t->cur_cpu : 0;
        char c[11][32];
        snprintf(c[0], 32, "%d", i);
        snprintf(c[1], 32, "CPU-%d", t->cpu);
        snprintf(c[2], 32, "CPU-%d", t->cur_cpu);
        snprintf(c[3], 32, "%.0f%%", st->per_cpu_user[cc]);
        snprintf(c[4], 32, "%.0f%%", st->per_cpu_sys[cc]);
        snprintf(c[5], 32, "%.0f MHz", st->per_cpu_mhz[cc]);
        snprintf(c[6], 32, "%.0f", t->mwips_last);
        snprintf(c[7], 32, "%.0f", t->mwips_avg);
        snprintf(c[8], 32, "%.0f", t->mwips_min);
        snprintf(c[9], 32, "%.0f", t->mwips_max);
        snprintf(c[10], 32, "%s", t->apic_errors ? "FAIL" : t->passes ? "ok" : "-");
        so_row(&o, 11, (const char *const[]){ c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], c[8], c[9], c[10] },
               (const int[]){ C_GRAY, C_LABEL, C_PURPLE, C_BLACK, C_GRAY, C_BLACK, C_BLACK, C_TEAL, C_GRAY, C_GRAY, t->apic_errors ? C_RED : C_GREEN });
    }
    g_idle_add(restore_idle, w);
}

static gboolean burn_timer(gpointer data)
{
    swin *w = data;
    int k;
    burn_tick(&w->st);
    if (w->nhist == 600) { for (k = 0; k < 4; k++) memmove(w->hist[k], w->hist[k] + 1, sizeof(double) * 599); w->nhist--; }
    w->hist[0][w->nhist] = w->st.cur.cpu_load;
    w->hist[1][w->nhist] = w->st.cur.cpu_temp;
    w->hist[2][w->nhist] = w->st.cur.sio_temp;
    w->hist[3][w->nhist] = w->st.cur.gpu_temp;
    w->nhist++;
    render_burn(w);
    gtk_widget_set_sensitive(w->b_start, !burn_active());
    gtk_widget_set_sensitive(w->b_stop, burn_active());
    gtk_widget_queue_draw(w->plot);
    return G_SOURCE_CONTINUE;
}

static gboolean draw_plot(GtkWidget *wid, cairo_t *cr, gpointer data)
{
    swin *w = data;
    int W = gtk_widget_get_allocated_width(wid), H = gtk_widget_get_allocated_height(wid), k, i;
    static const double col[4][3] = { { 0, .63, 0 }, { .88, 0, 0 }, { 1, .53, 0 }, { 0, .2, .8 } };
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_set_source_rgb(cr, .85, .85, .85);
    for (i = 1; i < 4; i++) { cairo_move_to(cr, 0, H * i / 4.0); cairo_line_to(cr, W, H * i / 4.0); }
    cairo_stroke(cr);
    if (w->kind == W_BURN) {
        for (k = 0; k < 4; k++) {
            cairo_set_source_rgb(cr, col[k][0], col[k][1], col[k][2]);
            for (i = 0; i < w->nhist; i++) {
                double x = W - (w->nhist - 1 - i) * 2.0, y = H - w->hist[k][i] / 100.0 * H;
                if (i == 0) cairo_move_to(cr, x, y); else cairo_line_to(cr, x, y);
            }
            cairo_stroke(cr);
        }
        cairo_set_source_rgb(cr, .3, .3, .3);
        cairo_move_to(cr, 6, 14);
        cairo_show_text(cr, "green CPU load %   red hottest core C   orange SIO C   blue GPU C   (scale 0-100)");
    } else if (w->sub == BK_IPI) {
        int n;
        const double *m = bench_ipi_last(&n);
        double mx = 1;
        if (m) {
            for (i = 0; i < n * n; i++) if (m[i] > mx) mx = m[i];
            for (i = 0; i < n; i++)
                for (k = 0; k < n; k++) {
                    double v = i == k ? 0 : m[i * n + k] / mx;
                    cairo_set_source_rgb(cr, v, 1 - v * 0.8, 0.2);
                    cairo_rectangle(cr, k * (double)W / n, i * (double)H / n, (double)W / n - 1, (double)H / n - 1);
                    cairo_fill(cr);
                }
        }
    } else if (w->sub == BK_LAT) {
        const long *sz;
        const double *ns;
        int n = bench_latency_last(&sz, &ns);
        double mx = 1;
        for (i = 0; i < n; i++) if (ns[i] > mx) mx = ns[i];
        cairo_set_source_rgb(cr, 0, .2, .8);
        for (i = 0; i < n; i++) {
            double x = n > 1 ? i * (W - 20.0) / (n - 1) + 10 : 10, y = H - 10 - ns[i] / mx * (H - 20);
            if (i == 0) cairo_move_to(cr, x, y); else cairo_line_to(cr, x, y);
        }
        cairo_stroke(cr);
    }
    return FALSE;
}

static void on_burn_start(GtkWidget *x, gpointer data)
{
    swin *w = data;
    burn_cfg c;
    int i, n = 0;
    (void)x;
    burn_cfg_default(&c);
    for (i = 0; i < w->nb_cpu; i++) { c.enabled[i] = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->b_cpu[i])); n += c.enabled[i]; }
    c.mode = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->b_same)) ? BURN_SAME : BURN_SWAP;
    gtk_spin_button_update(GTK_SPIN_BUTTON(w->b_limit));
    c.temp_limit_c = gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->b_limit));
    if (w->sub == 0) {
        double t;
        gtk_spin_button_update(GTK_SPIN_BUTTON(w->b_time));
        gtk_spin_button_update(GTK_SPIN_BUTTON(w->b_cool));
        gtk_spin_button_update(GTK_SPIN_BUTTON(w->b_cycles));
        t = gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->b_time));
        c.burn_s = t * (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->b_hours)) ? 3600 : 60);
        c.cool_s = gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->b_cool));
        c.cycles = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w->b_cycles));
        if (c.burn_s <= 0) c.burn_s = 60;
    } else {
        c.burn_s = 0;   /* CPU Stress: until stopped (capped) */
        c.cool_s = 0;
    }
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->b_gpu))) {
        snprintf(g_gpucmd, sizeof g_gpucmd, "%s", gtk_entry_get_text(GTK_ENTRY(w->b_gpucmd)));
        settings_save();
        c.gpu_cmd = *g_gpucmd ? g_gpucmd : NULL;
    }
    if (!n) { gtk_widget_error_bell(w->win); return; }
    if (!getenv("SRS_TEST_NOCONFIRM")) {   /* SIV-style warning before making CPUs 100% busy */
        char msg[700];
        GtkWidget *m;
        int ok;
        snprintf(msg, sizeof msg, "%s will make %d CPU%s 100%% busy %s.\n\nThe run stops automatically if the hottest CPU sensor "
                 "reaches %.0f°C, or on Stop / closing this window.%s\n\nStart now?", w->sub ? "CPU Stress" : "CPU Burnin", n, n == 1 ? "" : "s",
                 w->sub ? "until you press Stop" : "for the configured burn time", c.temp_limit_c,
                 c.gpu_cmd ? "\nThe GPU load command will run too." : "");
        m = gtk_message_dialog_new(GTK_WINDOW(w->win), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK_CANCEL, "%s", msg);
        gtk_window_set_title(GTK_WINDOW(m), "Start CPU load?");
        ok = gtk_dialog_run(GTK_DIALOG(m)) == GTK_RESPONSE_OK;
        gtk_widget_destroy(m);
        if (!ok) return;
    }
    if (getenv("SRS_TEST_MAXSECS")) c.cap_s = atof(getenv("SRS_TEST_MAXSECS"));   /* test hook: hard cap */
    w->nhist = 0;
    burn_start(&c);
    burn_timer(w);
}

static void on_burn_stop(GtkWidget *x, gpointer data) { (void)x; burn_stop(); burn_timer(data); }
static void on_burn_all(GtkWidget *x, gpointer data)
{
    swin *w = data;
    int i, on = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(x), "on"));
    for (i = 0; i < w->nb_cpu; i++) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->b_cpu[i]), on);
}

static GtkWidget *spin(double lo, double hi, double step, double val)
{
    GtkWidget *s = gtk_spin_button_new_with_range(lo, hi, step);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(s), val);
    return s;
}

static void build_burn(swin *w, GtkWidget *vb)
{
    GtkWidget *grid = gtk_grid_new(), *cpus = gtk_flow_box_new(), *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6), *b, *swap;
    int i, ncpu = burn_ncpu();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    i = 0;
    if (w->sub == 0) {
        gtk_grid_attach(GTK_GRID(grid), lbl("Burn Time"), 0, 0, 1, 1);
        w->b_time = spin(0, 750, 1, 15);
        gtk_grid_attach(GTK_GRID(grid), w->b_time, 1, 0, 1, 1);
        w->b_hours = gtk_check_button_new_with_label("Hours (otherwise Minutes)");
        gtk_grid_attach(GTK_GRID(grid), w->b_hours, 2, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), lbl("Cool Time"), 3, 0, 1, 1);
        w->b_cool = spin(0, 900, 5, 60);
        gtk_grid_attach(GTK_GRID(grid), w->b_cool, 4, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), lbl("Seconds   Cycles"), 5, 0, 1, 1);
        w->b_cycles = spin(1, 99, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), w->b_cycles, 6, 0, 1, 1);
        i = 1;
    }
    gtk_grid_attach(GTK_GRID(grid), lbl("Temperature limit °C"), 0, i, 1, 1);
    w->b_limit = spin(50, 100, 1, 90);
    gtk_grid_attach(GTK_GRID(grid), w->b_limit, 1, i, 1, 1);
    w->b_same = gtk_radio_button_new_with_label(NULL, "Same CPU (Whetstone)");
    swap = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(w->b_same), "Swap CPU (affinity test)");
    gtk_grid_attach(GTK_GRID(grid), w->b_same, 2, i, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), swap, 3, i, 2, 1);
    w->b_gpu = gtk_check_button_new_with_label("GPU Load command:");
    w->b_gpucmd = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(w->b_gpucmd), g_gpucmd);
    gtk_entry_set_placeholder_text(GTK_ENTRY(w->b_gpucmd), "e.g. gpu-burn-rocm 600  (none by default)");
    gtk_widget_set_hexpand(w->b_gpucmd, TRUE);
    gtk_grid_attach(GTK_GRID(grid), w->b_gpu, 0, i + 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w->b_gpucmd, 1, i + 1, 6, 1);
    gtk_box_pack_start(GTK_BOX(vb), grid, FALSE, FALSE, 0);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(cpus), 16);
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(cpus), GTK_SELECTION_NONE);
    w->nb_cpu = ncpu;
    for (i = 0; i < ncpu; i++) {
        char n[32];
        snprintf(n, sizeof n, "CPU-%d", i);
        w->b_cpu[i] = gtk_check_button_new_with_label(n);
        gtk_widget_set_tooltip_text(w->b_cpu[i], "Make this CPU busy or idle");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->b_cpu[i]), TRUE);
        gtk_container_add(GTK_CONTAINER(cpus), w->b_cpu[i]);
    }
    gtk_box_pack_start(GTK_BOX(vb), cpus, FALSE, FALSE, 0);
    b = sbtn("_Select All"); g_object_set_data(G_OBJECT(b), "on", GINT_TO_POINTER(1)); g_signal_connect(b, "clicked", G_CALLBACK(on_burn_all), w);
    gtk_widget_set_tooltip_text(b, "Select all the Worker Threads");
    gtk_box_pack_start(GTK_BOX(row), b, FALSE, FALSE, 0);
    b = sbtn("_Deselect All"); g_object_set_data(G_OBJECT(b), "on", GINT_TO_POINTER(0)); g_signal_connect(b, "clicked", G_CALLBACK(on_burn_all), w);
    gtk_widget_set_tooltip_text(b, "Deselect all the Worker Threads");
    gtk_box_pack_start(GTK_BOX(row), b, FALSE, FALSE, 0);
    w->b_start = sbtn("S_tart");
    g_signal_connect(w->b_start, "clicked", G_CALLBACK(on_burn_start), w);
    gtk_box_pack_start(GTK_BOX(row), w->b_start, FALSE, FALSE, 0);
    w->b_stop = sbtn("St_op");
    g_signal_connect(w->b_stop, "clicked", G_CALLBACK(on_burn_stop), w);
    gtk_box_pack_start(GTK_BOX(row), w->b_stop, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), lbl(w->sub ? "  CPU Stress: worker threads stay busy until Stop" :
                                         "  CPU Burnin: burn / cool cycles, Pass or Fail at the end"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vb), row, FALSE, FALSE, 0);
}

/* ======================================================================
 * Benchmark / IPI / Latency window (runs on a worker thread, never automatically)
 * ====================================================================== */
typedef struct { swin *w; int sub, mode, cpus, rounds; double secs; long max; } benchjob;

static gboolean bench_done(gpointer data)
{
    benchjob *j = data;
    swin *w = j->w;
    if (g_ptr_array_find(g_wins, w, NULL)) {
        srs_out o;
        gctx g;
        const char *name = j->sub == BK_IPI ? "ipi-delay" : j->sub == BK_LAT ? "latency" : bench_mode_id((bench_mode)j->mode);
        w->running = 0;
        gtk_widget_set_sensitive(w->bm_run, TRUE);
        gtk_label_set_text(GTK_LABEL(w->bm_status), "Finished");
        out_begin(w, &o, &g, NULL);
        if (!bench_show_last(name, &o)) so_err(&o, "No result (nothing to run in this mode on this CPU)");
        if (w->plot) gtk_widget_queue_draw(w->plot);
    }
    g_free(j);
    return G_SOURCE_REMOVE;
}

static gpointer bench_worker(gpointer data)
{
    benchjob *j = data;
    if (j->sub == BK_IPI) bench_ipi(j->cpus, j->rounds, NULL);
    else if (j->sub == BK_LAT) bench_latency(j->max, NULL);
    else bench_run((bench_mode)j->mode, j->secs, NULL);
    g_idle_add(bench_done, j);
    return NULL;
}

static void on_bench_run(GtkWidget *x, gpointer data)
{
    swin *w = data;
    benchjob *j;
    GThread *th;
    (void)x;
    if (w->running || burn_active()) { gtk_widget_error_bell(w->win); return; }
    j = g_new0(benchjob, 1);
    j->w = w;
    j->sub = w->sub;
    if (w->sub == BK_BENCH) {
        gtk_spin_button_update(GTK_SPIN_BUTTON(w->bm_secs));
        j->mode = gtk_combo_box_get_active(GTK_COMBO_BOX(w->bm_mode));
        j->secs = gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->bm_secs));
    } else if (w->sub == BK_IPI) {
        gtk_spin_button_update(GTK_SPIN_BUTTON(w->bm_cpus));
        gtk_spin_button_update(GTK_SPIN_BUTTON(w->bm_rounds));
        j->cpus = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w->bm_cpus));
        j->rounds = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w->bm_rounds));
    } else {
        gtk_spin_button_update(GTK_SPIN_BUTTON(w->bm_max));
        j->max = (long)gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->bm_max)) * 1024 * 1024;
    }
    w->running = 1;
    gtk_widget_set_sensitive(w->bm_run, FALSE);
    gtk_label_set_text(GTK_LABEL(w->bm_status), "Running...");
    th = g_thread_try_new("srs-bench", bench_worker, j, NULL);
    if (th) g_thread_unref(th);
    else { w->running = 0; gtk_widget_set_sensitive(w->bm_run, TRUE); g_free(j); }
}

static void build_bench(swin *w, GtkWidget *vb)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    int m;
    if (w->sub == BK_BENCH) {
        gtk_box_pack_start(GTK_BOX(row), lbl("Mode"), FALSE, FALSE, 0);
        w->bm_mode = gtk_combo_box_text_new();
        for (m = 0; m < BM_COUNT; m++) {
            char l[200];
            snprintf(l, sizeof l, "%s - %s", bench_mode_id((bench_mode)m) + 10, bench_mode_desc((bench_mode)m));
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->bm_mode), l);
        }
        gtk_combo_box_set_active(GTK_COMBO_BOX(w->bm_mode), 0);
        gtk_box_pack_start(GTK_BOX(row), w->bm_mode, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(row), lbl("Seconds per measurement"), FALSE, FALSE, 0);
        w->bm_secs = spin(0.1, 10, 0.1, 1);
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(w->bm_secs), 1);
        gtk_box_pack_start(GTK_BOX(row), w->bm_secs, FALSE, FALSE, 0);
    } else if (w->sub == BK_IPI) {
        gtk_box_pack_start(GTK_BOX(row), lbl("CPUs"), FALSE, FALSE, 0);
        w->bm_cpus = spin(2, burn_ncpu(), 1, burn_ncpu());
        gtk_box_pack_start(GTK_BOX(row), w->bm_cpus, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), lbl("Rounds per pair"), FALSE, FALSE, 0);
        w->bm_rounds = spin(1000, 1000000, 1000, 20000);
        gtk_box_pack_start(GTK_BOX(row), w->bm_rounds, FALSE, FALSE, 0);
    } else {
        gtk_box_pack_start(GTK_BOX(row), lbl("Largest block (MB)"), FALSE, FALSE, 0);
        w->bm_max = spin(1, 1024, 1, 256);
        gtk_box_pack_start(GTK_BOX(row), w->bm_max, FALSE, FALSE, 0);
    }
    w->bm_run = sbtn("_Run");
    g_signal_connect(w->bm_run, "clicked", G_CALLBACK(on_bench_run), w);
    gtk_box_pack_start(GTK_BOX(row), w->bm_run, FALSE, FALSE, 0);
    w->bm_status = lbl("Not run - press Run (the CPUs will be busy while it runs)");
    gtk_box_pack_start(GTK_BOX(row), w->bm_status, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vb), row, FALSE, FALSE, 0);
}

/* ======================================================================
 * tool windows
 * ====================================================================== */
static void open_tool(wkind k, int sub)
{
    static const btn row_tool[] = { B("_OK", "!close"), B("_Copy", "!copy"), B("Me_nu", "!menu"), B("_Help", "help"), B(NULL, NULL) };
    static const btn row_find[] = { B("_OK", "!close"), B("_Copy", "!copy"), B("_Next", "!next"), B("Pre_v", "!prev"),
        B("_Highlight", "!hilite"), B("Co_lour", "!colour"), B("_Lookup", "!idlookup"), B("_Help", "help"), B(NULL, NULL) };
    swin *w = find_win(k, "", sub);
    GtkWidget *vb, *row, *b;
    const char *title;
    char t[200];
    if (w) { gtk_window_present(GTK_WINDOW(w->win)); return; }
    w = g_new0(swin, 1);
    w->kind = k;
    w->sub = sub;
    title = k == W_BURN ? (sub ? "CPU Stress" : "CPU Burnin") : k == W_BENCH ? (sub == BK_IPI ? "IPI Delay" : sub == BK_LAT ? "Latency" : "Benchmark")
          : k == W_IDLOOKUP ? "Device ID Lookup" : "Find";
    snprintf(t, sizeof t, "%s - %s", g_image, title);
    make_window(w, t, 1180, k == W_BURN ? 760 : 600);
    vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(vb), 4);
    gtk_container_add(GTK_CONTAINER(w->win), vb);
    if (k == W_IDLOOKUP) {
        row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(row), lbl("ID"), FALSE, FALSE, 0);
        w->l_id = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(w->l_id), "1002:7550");
        gtk_entry_set_width_chars(GTK_ENTRY(w->l_id), 26);
        g_signal_connect(w->l_id, "activate", G_CALLBACK(on_lookup_run), w);
        gtk_box_pack_start(GTK_BOX(row), w->l_id, FALSE, FALSE, 0);
        w->l_mode = gtk_check_button_new();
        w->l_state = MODE_TREE;
        g_signal_connect(w->l_mode, "toggled", G_CALLBACK(on_mode), w);
        gtk_box_pack_start(GTK_BOX(row), w->l_mode, FALSE, FALSE, 0);
        w->l_modelbl = lbl(mode_text(MODE_TREE));
        gtk_widget_set_size_request(w->l_modelbl, 330, -1);
        gtk_label_set_xalign(GTK_LABEL(w->l_modelbl), 0);
        gtk_box_pack_start(GTK_BOX(row), w->l_modelbl, FALSE, FALSE, 0);
        w->l_pci = gtk_radio_button_new_with_label(NULL, "PCI");
        w->l_usb = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(w->l_pci), "USB");
        g_signal_connect(w->l_pci, "toggled", G_CALLBACK(on_bus), w);
        g_signal_connect(w->l_usb, "toggled", G_CALLBACK(on_bus), w);
        gtk_box_pack_start(GTK_BOX(row), w->l_pci, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), w->l_usb, FALSE, FALSE, 0);
        b = sbtn("_Lookup");
        g_signal_connect(b, "clicked", G_CALLBACK(on_lookup_run), w);
        gtk_box_pack_start(GTK_BOX(row), b, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vb), row, FALSE, FALSE, 0);
    } else if (k == W_FIND) {
        char rep[1100] = "";
        GtkWidget *r2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        w->marks = g_ptr_array_new();
        gtk_box_pack_start(GTK_BOX(row), lbl("Match"), FALSE, FALSE, 0);
        w->f_pat = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(w->f_pat), "RX 9070");
        gtk_entry_set_width_chars(GTK_ENTRY(w->f_pat), 40);
        g_signal_connect(w->f_pat, "activate", G_CALLBACK(on_find_run), w);
        gtk_box_pack_start(GTK_BOX(row), w->f_pat, FALSE, FALSE, 0);
        w->f_icase = gtk_check_button_new_with_mnemonic("Ignore _case");
        w->f_fixed = gtk_check_button_new_with_mnemonic("Li_teral");
        w->f_hl = gtk_check_button_new_with_mnemonic("Highlight _Matches");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->f_hl), g_highlight);
        g_signal_connect(w->f_hl, "toggled", G_CALLBACK(on_hl_check), w);
        gtk_box_pack_start(GTK_BOX(row), w->f_icase, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), w->f_fixed, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), w->f_hl, FALSE, FALSE, 0);
        b = sbtn("_Find");
        g_signal_connect(b, "clicked", G_CALLBACK(on_find_run), w);
        gtk_box_pack_start(GTK_BOX(row), b, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vb), row, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(r2), lbl("Max"), FALSE, FALSE, 0);
        w->f_max = spin(1, 100000, 1, 20);
        g_signal_connect(w->f_max, "activate", G_CALLBACK(on_find_run), w);
        gtk_box_pack_start(GTK_BOX(r2), w->f_max, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(r2), lbl("Report"), FALSE, FALSE, 0);
        w->f_report = gtk_entry_new();
        srs_latest_report(rep, sizeof rep);
        gtk_entry_set_text(GTK_ENTRY(w->f_report), rep);
        g_signal_connect(w->f_report, "activate", G_CALLBACK(on_find_run), w);
        gtk_box_pack_start(GTK_BOX(r2), w->f_report, TRUE, TRUE, 0);
        b = sbtn("_Browse...");
        g_signal_connect(b, "clicked", G_CALLBACK(on_browse), w);
        gtk_box_pack_start(GTK_BOX(r2), b, FALSE, FALSE, 0);
        b = sbtn("L_atest");
        g_signal_connect(b, "clicked", G_CALLBACK(on_latest), w);
        gtk_box_pack_start(GTK_BOX(r2), b, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vb), r2, FALSE, FALSE, 0);
    } else if (k == W_BURN) {
        build_burn(w, vb);
    } else {
        build_bench(w, vb);
    }
    gtk_box_pack_start(GTK_BOX(vb), make_view(w), TRUE, TRUE, 0);
    if (k == W_BURN || (k == W_BENCH && sub != BK_BENCH)) {
        w->plot = gtk_drawing_area_new();
        gtk_widget_set_size_request(w->plot, -1, k == W_BURN ? 160 : 200);
        g_signal_connect(w->plot, "draw", G_CALLBACK(draw_plot), w);
        gtk_box_pack_start(GTK_BOX(vb), w->plot, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(vb), button_row(w, k == W_FIND ? row_find : row_tool), FALSE, FALSE, 0);
    gtk_widget_show_all(w->win);
    if (k == W_IDLOOKUP) { set_mode_box(w); render_idlookup(w); gtk_widget_grab_focus(w->l_id); }
    else if (k == W_FIND) { render_find(w); gtk_widget_grab_focus(w->f_pat); }
    else if (k == W_BURN) { burn_timer(w); w->timer = g_timeout_add(1000, burn_timer, w); }
    else {
        srs_out o;
        gctx g;
        const char *name = sub == BK_IPI ? "ipi-delay" : sub == BK_LAT ? "latency" : "benchmark";
        out_begin(w, &o, &g, NULL);
        if (sub == BK_BENCH) srs_section_render(srs_section_find("benchmark"), &o);
        else if (!bench_show_last(name, &o)) so_note(&o, "Not run yet. Press Run.");
    }
}

/* ======================================================================
 * buttons and keys
 * ====================================================================== */
static void do_target(swin *w, const char *t)
{
    if (!t) return;
    if (t[0] != '!') { open_section(t); return; }
    if (!strcmp(t, "!close")) gtk_widget_destroy(w->win);
    else if (!strcmp(t, "!copy")) copy_all(w);
    else if (!strcmp(t, "!refresh")) { if (w->kind == W_SECTION) render(w); }
    else if (!strcmp(t, "!menu")) popup_menu(w->win);
    else if (!strcmp(t, "!main")) { if (g_main) gtk_window_present(GTK_WINDOW(g_main->win)); }
    else if (!strcmp(t, "!zero")) { if (sensors_zero) sensors_zero(); render(w); }
    else if (!strcmp(t, "!hide")) { g_hide_unused = !g_hide_unused; if (sensors_set_hide_unused) sensors_set_hide_unused(g_hide_unused); render(w); }
    else if (!strcmp(t, "!burn")) open_tool(W_BURN, 0);
    else if (!strcmp(t, "!stress")) open_tool(W_BURN, 1);
    else if (!strcmp(t, "!bench")) open_tool(W_BENCH, BK_BENCH);
    else if (!strcmp(t, "!idlookup")) open_tool(W_IDLOOKUP, 0);
    else if (!strcmp(t, "!next")) goto_match(w, +1);
    else if (!strcmp(t, "!prev")) goto_match(w, -1);
    else if (!strcmp(t, "!hilite")) toggle_highlight();
    else if (!strcmp(t, "!colour")) choose_colour(w);
}

static void on_btn(GtkButton *b, gpointer data) { do_target(data, g_object_get_data(G_OBJECT(b), "target")); }

static gboolean on_key(GtkWidget *wid, GdkEventKey *ev, gpointer data)
{
    swin *w = data, *f;
    int ctrl = (ev->state & GDK_CONTROL_MASK) != 0, shift = (ev->state & GDK_SHIFT_MASK) != 0;
    (void)wid;
    switch (ev->keyval) {
    case GDK_KEY_Escape: if (!w->is_main) gtk_widget_destroy(w->win); return TRUE;
    case GDK_KEY_F1: open_section("help"); return TRUE;
    case GDK_KEY_F5: if (w->kind == W_SECTION) render(w); return TRUE;
    case GDK_KEY_F3:
        f = w->kind == W_FIND ? w : find_win(W_FIND, "", 0);
        if (f) goto_match(f, shift ? -1 : +1);
        return TRUE;
    default: break;
    }
    if (ctrl) {
        switch (ev->keyval) {
        case GDK_KEY_f: open_tool(W_FIND, 0); return TRUE;
        case GDK_KEY_l: open_tool(W_IDLOOKUP, 0); return TRUE;
        case GDK_KEY_d: open_section("all-dev"); return TRUE;
        case GDK_KEY_h: toggle_highlight(); return TRUE;
        case GDK_KEY_m: popup_menu(w->win); return TRUE;
        case GDK_KEY_s: save_dialog(0); return TRUE;
        default: break;
        }
    }
    return FALSE;
}


/* ======================================================================
 * System tray (SIV lives in the Icon Tray). StatusNotifierItem through libayatana-appindicator,
 * loaded at run time so it stays optional; KDE/GNOME (with the AppIndicator extension)/XFCE show it.
 * ====================================================================== */
typedef void *(*ai_new_t)(const char *, const char *, int);
typedef void (*ai_status_t)(void *, int);
typedef void (*ai_menu_t)(void *, GtkMenu *);
typedef void (*ai_title_t)(void *, const char *);
typedef void (*ai_target_t)(void *, GtkWidget *);
static struct {
    void *lib, *ind;
    ai_new_t new_; ai_status_t status; ai_menu_t menu; ai_title_t title; ai_target_t target;
    GtkWidget *showitem;
    int hidden, tried;
    guint timer;
} g_tray;

static int tray_available(void)
{
    static const char *const libs[] = { "libayatana-appindicator3.so.1", "libappindicator3.so.1", NULL };
    int i;
    if (g_tray.lib) return 1;
    if (g_tray.tried) return 0;
    g_tray.tried = 1;
    for (i = 0; libs[i] && !g_tray.lib; i++) g_tray.lib = dlopen(libs[i], RTLD_NOW | RTLD_LOCAL);
    if (!g_tray.lib) return 0;
    *(void **)&g_tray.new_ = dlsym(g_tray.lib, "app_indicator_new");
    *(void **)&g_tray.status = dlsym(g_tray.lib, "app_indicator_set_status");
    *(void **)&g_tray.menu = dlsym(g_tray.lib, "app_indicator_set_menu");
    *(void **)&g_tray.title = dlsym(g_tray.lib, "app_indicator_set_title");
    *(void **)&g_tray.target = dlsym(g_tray.lib, "app_indicator_set_secondary_activate_target");
    if (!g_tray.new_ || !g_tray.status || !g_tray.menu) { dlclose(g_tray.lib); g_tray.lib = NULL; return 0; }
    return 1;
}

static int tray_active(void) { return g_tray.ind && g_opt_tray; }

static void srs_hide_all(void)
{
    guint i;
    if (!tray_active()) return;   /* never hide without a way back */
    for (i = 0; i < g_wins->len; i++) gtk_widget_hide(((swin *)g_ptr_array_index(g_wins, i))->win);
    g_tray.hidden = 1;
    if (g_tray.showitem) gtk_menu_item_set_label(GTK_MENU_ITEM(g_tray.showitem), "Show SRS");
}

static void srs_show_all(void)
{
    guint i;
    for (i = 0; i < g_wins->len; i++) gtk_widget_show(((swin *)g_ptr_array_index(g_wins, i))->win);
    if (g_main) gtk_window_present(GTK_WINDOW(g_main->win));
    g_tray.hidden = 0;
    if (g_tray.showitem) gtk_menu_item_set_label(GTK_MENU_ITEM(g_tray.showitem), "Hide to Tray");
}

static void on_tray_toggle(GtkMenuItem *mi, gpointer d) { (void)mi; (void)d; if (g_tray.hidden) srs_show_all(); else srs_hide_all(); }
static void on_tray_open(GtkMenuItem *mi, gpointer d)
{
    const char *t = d;
    (void)mi;
    if (!strcmp(t, "burnin")) open_tool(W_BURN, 0);
    else if (!strcmp(t, "stress")) open_tool(W_BURN, 1);
    else if (!strcmp(t, "bench")) open_tool(W_BENCH, BK_BENCH);
    else if (!strcmp(t, "find")) open_tool(W_FIND, 0);
    else open_section(t);
}
static void on_tray_exit(GtkMenuItem *mi, gpointer d) { (void)mi; (void)d; if (g_main) gtk_widget_destroy(g_main->win); }

static void tray_build_menu(void)
{
    static const struct { const char *label, *target; } quick[] = {
        { "Sensors", "sensors" }, { "Processors", "processors" }, { "CPU Burnin...", "burnin" }, { "CPU Stress...", "stress" },
        { "Benchmark...", "bench" }, { "Find...", "find" }, { NULL, NULL } };
    GtkWidget *m = gtk_menu_new(), *it, *sub;
    int i;
    g_tray.showitem = gtk_menu_item_new_with_label(g_tray.hidden ? "Show SRS" : "Hide to Tray");
    g_signal_connect(g_tray.showitem, "activate", G_CALLBACK(on_tray_toggle), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), g_tray.showitem);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), gtk_separator_menu_item_new());
    for (i = 0; quick[i].label; i++) {
        it = gtk_menu_item_new_with_label(quick[i].label);
        g_signal_connect(it, "activate", G_CALLBACK(on_tray_open), (gpointer)quick[i].target);
        gtk_menu_shell_append(GTK_MENU_SHELL(m), it);
    }
    gtk_menu_shell_append(GTK_MENU_SHELL(m), gtk_separator_menu_item_new());
    sub = gtk_menu_new();
    build_menu(sub);
    it = gtk_menu_item_new_with_label("SRS Menu");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(it), sub);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), it);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), gtk_separator_menu_item_new());
    it = gtk_menu_item_new_with_label("Exit");
    g_signal_connect(it, "activate", G_CALLBACK(on_tray_exit), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), it);
    gtk_widget_show_all(m);
    g_tray.menu(g_tray.ind, GTK_MENU(m));
    if (g_tray.target) g_tray.target(g_tray.ind, g_tray.showitem);   /* middle click = show / hide */
}

static gboolean tray_tick(gpointer d)
{
    burn_sample s;
    char t[200];
    (void)d;
    if (!g_tray.ind || !g_tray.title) return G_SOURCE_CONTINUE;
    burn_sample_now(&s);
    snprintf(t, sizeof t, "SRS  -  CPU %.0f°C %.0f%%  -  GPU %.0f°C %.0f%%", s.cpu_temp, s.cpu_load, s.gpu_temp, s.gpu_load);
    g_tray.title(g_tray.ind, t);
    return G_SOURCE_CONTINUE;
}

static void tray_apply(void)
{
    if (g_opt_tray && tray_available()) {
        if (!g_tray.ind) {
            g_tray.ind = g_tray.new_("srs-sysinfo", "srs", 3 /* APP_INDICATOR_CATEGORY_HARDWARE */);
            if (!g_tray.ind) return;
            g_tray.timer = g_timeout_add_seconds(2, tray_tick, NULL);
        }
        tray_build_menu();
        g_tray.status(g_tray.ind, 1 /* ACTIVE */);
        tray_tick(NULL);
    } else if (g_tray.ind) {
        g_tray.status(g_tray.ind, 0 /* PASSIVE: hidden */);
        if (g_tray.hidden) srs_show_all();
    }
}

/* Start with the desktop: ~/.config/autostart/srs-gui.desktop launching straight into the tray */
static char *autostart_path(void) { return g_build_filename(g_get_user_config_dir(), "autostart", "srs-gui.desktop", NULL); }
static int autostart_on(void) { char *p = autostart_path(); int on = g_file_test(p, G_FILE_TEST_EXISTS); g_free(p); return on; }
static void autostart_set(int on)
{
    char *p = autostart_path();
    if (on) {
        char exe[1024] = "/usr/bin/srs-gui", *dir = g_path_get_dirname(p), *txt;
        ssize_t l = readlink("/proc/self/exe", exe, sizeof exe - 1);
        if (l > 0) exe[l] = 0;
        g_mkdir_with_parents(dir, 0755);
        txt = g_strdup_printf("[Desktop Entry]\nType=Application\nName=SRS System Information (tray)\n"
                              "Comment=Start SRS hidden in the system tray\nExec=%s --gui-tray\nIcon=srs\nTerminal=false\n"
                              "X-GNOME-Autostart-enabled=true\nX-KDE-autostart-after=panel\n", exe);
        g_file_set_contents(p, txt, -1, NULL);
        g_free(txt);
        g_free(dir);
    } else {
        g_unlink(p);
    }
    g_free(p);
}

enum { OPT_TRAY, OPT_CLOSE, OPT_MIN, OPT_START, OPT_AUTOSTART };
static void on_option(GtkCheckMenuItem *mi, gpointer d)
{
    int on = gtk_check_menu_item_get_active(mi);
    switch (GPOINTER_TO_INT(d)) {
    case OPT_TRAY: g_opt_tray = on; break;
    case OPT_CLOSE: g_opt_close_tray = on; break;
    case OPT_MIN: g_opt_min_tray = on; break;
    case OPT_START: g_opt_start_tray = on; break;
    case OPT_AUTOSTART: autostart_set(on); break;
    }
    settings_save();
    tray_apply();   /* also rebuilds the tray menu so its check marks stay in sync */
}

static void build_options(GtkWidget *menu)
{
    static const struct { const char *label; int id; } opts[] = {
        { "System _tray icon", OPT_TRAY }, { "_Close button hides to tray", OPT_CLOSE },
        { "_Minimize hides to tray (X11 / XWayland)", OPT_MIN }, { "_Start hidden in tray", OPT_START },
        { "Start with the _desktop (autostart)", OPT_AUTOSTART }, { NULL, 0 } };
    int i, have = tray_available();
    for (i = 0; opts[i].label; i++) {
        GtkWidget *it = gtk_check_menu_item_new_with_mnemonic(opts[i].label);
        int v = opts[i].id == OPT_TRAY ? g_opt_tray : opts[i].id == OPT_CLOSE ? g_opt_close_tray : opts[i].id == OPT_MIN ? g_opt_min_tray
              : opts[i].id == OPT_START ? g_opt_start_tray : autostart_on();
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(it), v);
        if (!have && opts[i].id != OPT_AUTOSTART) gtk_widget_set_sensitive(it, FALSE);
        g_signal_connect(it, "toggled", G_CALLBACK(on_option), GINT_TO_POINTER(opts[i].id));
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), it);
    }
    if (!have) {
        GtkWidget *it = gtk_menu_item_new_with_label("(tray needs libayatana-appindicator)");
        gtk_widget_set_sensitive(it, FALSE);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), it);
    }
    {
        GtkWidget *it = gtk_menu_item_new_with_mnemonic("_Hide to Tray");
        g_signal_connect(it, "activate", G_CALLBACK(on_tray_toggle), NULL);
        gtk_widget_set_sensitive(it, tray_active());
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), it);
    }
}

/* main window: close / minimise go to the tray when those options are on */
static gboolean on_main_delete(GtkWidget *wid, GdkEvent *ev, gpointer d)
{
    (void)wid; (void)ev; (void)d;
    if (g_opt_close_tray && tray_active()) { srs_hide_all(); return TRUE; }
    return FALSE;
}

static gboolean on_main_state(GtkWidget *wid, GdkEventWindowState *ev, gpointer d)
{
    (void)d;
    if ((ev->changed_mask & GDK_WINDOW_STATE_ICONIFIED) && (ev->new_window_state & GDK_WINDOW_STATE_ICONIFIED) &&
        g_opt_min_tray && tray_active()) {
        gtk_window_deiconify(GTK_WINDOW(wid));
        srs_hide_all();
    }
    return FALSE;
}

/* ======================================================================
 * main window
 * ====================================================================== */
static gboolean main_tick(gpointer data) { render(data); return G_SOURCE_CONTINUE; }

static void open_main(void)
{
    swin *w = g_new0(swin, 1);
    GtkWidget *vb;
    char title[400];
    w->kind = W_SECTION;
    w->is_main = 1;
    snprintf(w->id, sizeof w->id, "overview");
    g_main = w;
    snprintf(title, sizeof title, "%s - System Report Search V%s  %s", g_image, SRS_VERSION, g_host);
    make_window(w, title, 1100, 520);
    vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(vb), 4);
    gtk_container_add(GTK_CONTAINER(w->win), vb);
    gtk_box_pack_start(GTK_BOX(vb), make_view(w), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vb), button_row(w, row_main), FALSE, FALSE, 0);
    render(w);
    w->timer = g_timeout_add(2000, main_tick, w);
    g_signal_connect(w->win, "delete-event", G_CALLBACK(on_main_delete), NULL);
    g_signal_connect(w->win, "window-state-event", G_CALLBACK(on_main_state), NULL);
    gtk_widget_show_all(w->win);
}

static void load_css(void)
{
    GtkCssProvider *c = gtk_css_provider_new();
    gtk_css_provider_load_from_data(c,
        "window, #panel, #panel text { background-color: #F5F5F5; }"
        "#panel, #panel text { font: 8pt Tahoma; color: #000000; }"
        "#panel text selection { background-color: #3399FF; color: #FFFFFF; }"
        "button, entry, label, checkbutton, radiobutton, spinbutton, combobox, menuitem { font: 8pt Tahoma; }"
        "#sivbtn { padding: 1px 4px; min-height: 18px; border-radius: 0; background-image: none;"
        "  background-color: #E1E1E1; color: #000000; border: 1px solid #ADADAD; box-shadow: none; }"
        "#sivbtn:hover { background-color: #E5F1FB; border-color: #0078D7; }"
        "#sivbtn:active { background-color: #CCE4F7; }"
        "#lbl, radiobutton label, checkbutton label { color: #0000CC; }", -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(c), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(c);
}

int main(int argc, char **argv)
{
    const char *autop;
    char *base = g_path_get_basename(argv[0]);
    snprintf(g_image, sizeof g_image, "%s", base);
    g_free(base);
    if (argc > 1 && strncmp(argv[1], "--g", 3)) return srs_cli_main(argc, argv);   /* CLI mode, like SIV64X -save */
    gethostname(g_host, sizeof g_host);
    g_setenv("GTK_THEME", "Adwaita:light", TRUE);   /* this process only: SIV's fixed light look */
    gtk_init(&argc, &argv);
    load_css();
    settings_load();
    {
        char *icon = g_build_filename(g_get_user_data_dir(), "icons", "hicolor", "256x256", "apps", "srs.png", NULL);
        if (g_file_test(icon, G_FILE_TEST_EXISTS)) gtk_window_set_default_icon_from_file(icon, NULL);
        g_free(icon);
    }
    g_wins = g_ptr_array_new();
    {
        int i, start_hidden = g_opt_start_tray;
        for (i = 1; i < argc; i++) if (!strcmp(argv[i], "--gui-tray")) { start_hidden = 1; g_opt_tray = 1; }   /* autostart */
        open_main();
        tray_apply();
        if (start_hidden) srs_hide_all();   /* only hides when a tray icon really exists */
    }
    autop = getenv("SRS_OPEN");   /* comma-separated items or tool names to open at start */
    {
        int i;
        for (i = 1; i < argc; i++) if (!strncmp(argv[i], "--gui-open=", 11)) autop = argv[i] + 11;   /* menu entries / jump list */
    }
    if (autop) {
        char tmp[2048], *t, *save;
        snprintf(tmp, sizeof tmp, "%s", autop);
        for (t = strtok_r(tmp, ",", &save); t; t = strtok_r(NULL, ",", &save)) {
            if (!strcmp(t, "burnin")) open_tool(W_BURN, 0);
            else if (!strcmp(t, "stress")) open_tool(W_BURN, 1);
            else if (!strcmp(t, "bench")) open_tool(W_BENCH, BK_BENCH);
            else if (!strcmp(t, "ipi")) open_tool(W_BENCH, BK_IPI);
            else if (!strcmp(t, "latency")) open_tool(W_BENCH, BK_LAT);
            else if (!strcmp(t, "idlookup")) open_tool(W_IDLOOKUP, 0);
            else if (!strcmp(t, "find")) open_tool(W_FIND, 0);
            else if (!strcmp(t, "menu")) popup_menu(g_main->win);
            else open_section(t);
        }
    }
    gtk_main();
    return 0;
}
