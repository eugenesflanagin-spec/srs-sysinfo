/* srs.h - sysreport-search shared core (device-ID lookup, report find, devices).
 *
 * One portable C source set, built into several images the way SIV builds
 * SIV64X/SIV32X/...: a native Linux CLI (gcc), Win32 GUI images SRS64X.exe /
 * SRS32X.exe (mingw, system msvcrt.dll), and a winelib native ELF of the GUI.
 * Clean-room: behaviour taken from SIV's documented/observable features only;
 * data comes from hwdata pci.ids/usb.ids, sysfs, systemd-hwdb or SetupAPI.
 */
#ifndef SRS_H
#define SRS_H

#include <stddef.h>

#define SRS_VERSION "1.0.5"

/* qsort/bsearch callbacks: winelib links wine's msvcrt, whose callbacks are ms_abi */
#ifdef _WIN32
#define SRS_CDECL __cdecl
#else
#define SRS_CDECL
#endif

enum srs_bus  { BUS_PCI, BUS_USB };
enum srs_mode { MODE_TREE, MODE_SCAN, MODE_INFO };

/* ---------- device-ID database (flattened .ids tree, sorted for bsearch) ---------- */
typedef struct { char key[24]; char *name; } srs_rec;

typedef struct {
    enum srs_bus bus;
    char path[1024];        /* file actually loaded */
    char version[64];       /* "# Version: ..." header */
    char date[64];          /* "# Date: ..." header */
    srs_rec *recs;
    size_t n;
    double load_ms;
} srs_db;

int  srs_db_load(srs_db *db, enum srs_bus bus);   /* 0 ok, -1 file not found */
void srs_db_free(srs_db *db);
const char *srs_db_tree(const srs_db *db, const char *key);   /* bsearch */
const char *srs_db_scan(const srs_db *db, const char *key);   /* linear file scan; static buffer */
const char *srs_db_find_file(enum srs_bus bus, char *out, size_t outsz);

/* ---------- resolution chain: V:D:S:s:RR -> V:D:S:s -> V:D:RR -> V:D -> V ---------- */
#define SRS_MAXCHAIN 5
typedef struct {
    int nchain;
    char chain[SRS_MAXCHAIN][24];
    char name[SRS_MAXCHAIN][256];   /* "" = no entry */
    int matched;                    /* index into chain, -1 none */
    char vendor[256], subvendor[256], stock[256], resolved[256];
    double lookup_ms;
} srs_result;

/* Returns 0 ok, -1 bad ID (err filled). mode TREE or SCAN. */
int srs_resolve(const srs_db *db, const char *id, enum srs_mode mode,
                srs_result *r, char *err, size_t errsz);
int srs_chain(const char *id, char chain[SRS_MAXCHAIN][24], int *n,
              char *err, size_t errsz);

/* Info mode: ask the OS database (systemd-hwdb on Linux, SetupAPI on Windows). */
typedef struct { char query[256]; char vendor[256]; char model[256]; double ms; char err[256]; } srs_info;
int srs_info_lookup(enum srs_bus bus, const char *id, srs_info *out);

/* ---------- regex (backtracking VM with state memo; Python-re-like syntax subset) ---------- */
typedef struct srs_re srs_re;
srs_re *srs_re_compile(const char *pat, int icase, int literal, char *err, size_t errsz);
/* search s[start..len): returns 1 and sets *so,*eo on match */
int  srs_re_search(srs_re *re, const char *s, int len, int start, int *so, int *eo);
void srs_re_free(srs_re *re);

/* ---------- find across a report ---------- */
typedef struct { int so, eo; } srs_span;
typedef struct { int lineno; char *text; int nspan; srs_span *spans; } srs_hit;
typedef struct { char file[256]; int count; int nshown; srs_hit *hits; } srs_section;
typedef struct {
    char report[1024];
    int nfiles;                 /* sections searched */
    int nsec; srs_section *secs;/* sections with matches */
    int total;
    double ms;
} srs_findres;

/* Returns 0 ok (matches or not), 2 invalid pattern / no report (err filled). */
int  srs_find(const char *report, const char *pattern, int icase, int fixed, int maxshow,
              srs_findres *fr, char *err, size_t errsz);
void srs_find_free(srs_findres *fr);
int  srs_latest_report(char *out, size_t outsz);   /* 0 ok */
const char *srs_reports_root(void);

/* ---------- local devices ---------- */
typedef struct { char slot[64]; char key[40]; } srs_dev;
int srs_devices(enum srs_bus bus, srs_dev **out);   /* returns count, -1 error */
const char *srs_devices_source(void);               /* "sysfs" / "SetupAPI" */

/* ---------- small helpers ---------- */
double srs_now_ms(void);
void srs_thousands(size_t n, char *out, size_t outsz);
const char *srs_bus_name(enum srs_bus b);   /* "pci"/"usb" */
int  srs_utf8_pad(char *out, size_t outsz, const char *s, int width); /* truncate+pad by codepoints */

/* ---------- platform layer (plat_posix.c / plat_win32.c) ---------- */
int  plat_listdir(const char *dir, char ***names);      /* sorted; returns count */
void plat_freelist(char **names, int n);
int  plat_isdir(const char *path);
int  plat_exedir(char *out, size_t outsz);
const char *plat_hwdata_dir(void);                       /* /usr/share/hwdata or Z:\usr\share\hwdata */
const char *plat_sysfs(void);                            /* "/sys" or "Z:\sys" or NULL */
const char *plat_home(void);
char plat_sep(void);
int  plat_run_capture(const char *const argv[], char *out, size_t outsz); /* exit status, -1 can't run */
int  plat_is_wine(void);
int  plat_setupapi_devices(enum srs_bus bus, srs_dev **out);              /* Windows only, else -1 */
int  plat_setupapi_info(enum srs_bus bus, const char *id, srs_info *out); /* Windows only, else -1 */
const char *plat_build_info(void);

/* ---------- CLI (shared by native CLI and the GUI images' console mode) ---------- */
int srs_cli_main(int argc, char **argv);

#endif
