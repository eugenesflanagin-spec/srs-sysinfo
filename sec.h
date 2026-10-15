/* sec.h - SIV-style "sections": every SIV item is a section that renders coloured,
 * tab-aligned rows into an abstract sink. The same producer feeds the GUI panels,
 * the Menu, and "-save" (SIV's text dump format: "[item]  [category]" + content).
 *
 * Writing a section:
 *     static void s_cpus(srs_out *o) {
 *         static const int tabs[] = { 120, 300 };
 *         so_tabs(o, tabs, 2);
 *         so_kv(o, "Cores", C_VALUE, "16");
 *     }
 *     const srs_item sec_cpu[] = {
 *         { "cpus", "hardware", "CPUs", B_LINUX, 0, "/proc/cpuinfo", s_cpus, 0 },
 *         { NULL }
 *     };
 * Safety rules for producers: read-only. Never write sysfs, never open i2c/DDC or
 * hidraw, never run benchmarks/stress from a producer, never call sudo/pkexec
 * (root data comes from the srs-root helper's cache, see so_root_file()).
 */
#ifndef SEC_H
#define SEC_H

#include <stddef.h>

/* colours - same palette as SIV's panels */
enum { C_DEF = 0, C_LABEL = 1, C_VALUE, C_PURPLE, C_TEAL, C_ORANGE, C_GREEN, C_RED, C_BLACK, C_GRAY, C_NCOL };

typedef enum { B_LINUX, B_ABSENT, B_WINONLY } srs_bucket;

typedef struct srs_out srs_out;
struct srs_out {
    void (*text)(srs_out *o, int color, int bold, int mono, const char *s);
    void (*tab)(srs_out *o);
    void (*par)(srs_out *o);
    void (*tabs)(srs_out *o, const int *px, int n);
    void *ctx;
    int col;                 /* text backend: current column */
    int ntabs, tabpx[32];    /* text backend: current tab stops in px */
    int tabn;                /* text backend: tabs used on the current line */
    const char *arg;         /* section argument: "processor:8" -> "8" (NULL if none) */
    int gui;                 /* 1 when rendering into the GUI (live sections may show more) */
};

typedef struct srs_item {
    const char *id;          /* SIV item name, e.g. "cpuid" */
    const char *category;    /* SIV category: hardware / windows / tuning / ... */
    const char *title;       /* human title */
    srs_bucket bucket;
    int needs_root;
    const char *linux_src;   /* where the data comes from (shown in stubs and Help) */
    void (*fn)(srs_out *o);  /* NULL = render the bucket text */
    int live_ms;             /* GUI auto-refresh period, 0 = static */
    const char *group;       /* owning group: cpu machine sensors storage network os gpu bus tools siv */
} srs_item;

/* ---- registry ---- */
const srs_item *srs_section_find(const char *id);  /* "id" or "id:arg" */
int  srs_section_count(void);
const srs_item *srs_section_at(int i);          /* SIV order */
void srs_section_render(const srs_item *s, srs_out *o);
int  srs_save(const char *items, const char *file); /* "-save" ; items like "[cpuid][spd]" or NULL = default set */

/* ---- output helpers ---- */
void so_tabs(srs_out *o, const int *px, int n);
void so_t(srs_out *o, int color, const char *s);        /* plain text */
void so_b(srs_out *o, int color, const char *s);        /* bold */
void so_m(srs_out *o, int color, const char *s);        /* monospace */
void so_tab(srs_out *o);
void so_par(srs_out *o);
void so_f(srs_out *o, int color, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
void so_kv(srs_out *o, const char *k, int color, const char *v);
void so_kvf(srs_out *o, const char *k, int color, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
/* a row of cells separated by tabs; colors[i] per cell (NULL = C_VALUE) */
void so_row(srs_out *o, int n, const char *const *cells, const int *colors);
void so_head(srs_out *o, const char *title);             /* bold title line + blank line */
void so_note(srs_out *o, const char *s);                 /* gray note line */
void so_err(srs_out *o, const char *s);                  /* red line */
void so_lines(srs_out *o, int color, const char *text);  /* multi-line text, mono, one row per line */

void so_check(srs_out *o, int on);                     /* SIV "[=]" / "[_]" (GUI: check boxes) */
void so_siv_header(srs_out *o, const char *what);        /* "srs-gui - <what> on \\HOST - <OS> <kernel>" + blank */
void so_board_footer(srs_out *o);                        /* blank + "<board vendor> <board> BIOS <ver> <date>" */
const char *sys_os_name(void);                           /* "CachyOS Linux 7.3.0-rc7-..." */
const char *sys_board(void);                             /* "ASUSTeK ... TUF GAMING X870-PRO WIFI7 BIOS 1805 09/18/2026" */
const char *sys_cpu_name(void);                          /* /proc/cpuinfo model name */

/* ---- data helpers (read-only) ---- */
int  rd_str(const char *path, char *buf, size_t sz);     /* first line, trimmed; 1 ok */
int  rd_all(const char *path, char **out);               /* whole file (malloc), returns length or -1 */
int  rd_long(const char *path, long *v);
int  rd_fmt(char *buf, size_t sz, const char *fmt, ...) __attribute__((format(printf, 3, 4))); /* rd_str of formatted path */
int  run_out(char **out, const char *const argv[]);      /* run command, capture stdout (malloc), returns exit or -1 */
int  dir_list(const char *dir, char ***names);           /* sorted; returns count or -1; free with dir_free */
void dir_free(char **names, int n);
int  path_exists(const char *path);
const char *so_root_file(const char *name);              /* cached root data from srs-root, NULL if absent */
void fmt_bytes(double v, char *buf, size_t sz);          /* 1.23GB style (base 1024) */
void fmt_hz(double hz, char *buf, size_t sz);            /* 4.41GHz style */

/* ---- text backend (used by -save and the CLI) ---- */
void so_text_init(srs_out *o, void *FILE_ptr);

/* group tables (sec_<group>.c) */
extern const srs_item sec_cpu[], sec_machine[], sec_sensors[], sec_storage[], sec_network[],
                         sec_os[], sec_gpu[], sec_bus[], sec_tools[], sec_siv[], sec_stubs[];

#endif
