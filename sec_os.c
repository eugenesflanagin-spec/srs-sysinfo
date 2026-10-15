/* sec_os.c - SIV "Windows" items, as their Linux equivalents.
 * [windows]/[system] is the Linux overview panel (os-release, kernel, install date, CPUs, power,
 * time zone, BIOS, displays). Then: processes, services (systemd units / kernel drivers), modules,
 * environment (secrets masked), fonts (fontconfig), locales/code pages (glibc i18n + iconv),
 * users/groups/sessions/capabilities, startup (systemd + XDG autostart), installed software
 * (pacman/flatpak), scheduling (sched_* sysctls, sched_ext), power policy (cpufreq/EPP/profiles),
 * memory pools/paging/file cache, time/timers/zones, crash dumps (systemd-coredump),
 * code integrity (taint/lockdown/Secure Boot), boot mode, shared libraries (/proc/self/maps).
 * Read-only: sysfs/procfs reads and local query commands under `timeout 5` only.
 * Never reads /etc/shadow, keyrings or credential stores; masks secret-looking env values. */
#define _GNU_SOURCE
#include "sec.h"

#include <ctype.h>
#include <gnu/libc-version.h>
#include <grp.h>
#include <iconv.h>
#include <malloc.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

/* ======================================================================
 * helpers
 * ====================================================================== */

/* run a FIXED shell command line (never built from user input) with a 5 s timeout,
 * stdin from /dev/null and stderr discarded. Always returns a malloc'd string. */
static char *sh_rc(const char *cmd, int *rc)
{
    char full[2048], *out = NULL;
    const char *argv[] = { "timeout", "5", "sh", "-c", full, NULL };
    int r;
    snprintf(full, sizeof full, "{ %s ; } </dev/null 2>/dev/null", cmd);
    r = run_out(&out, argv);
    if (rc) *rc = r;
    if (!out) out = strdup("");
    return out ? out : calloc(1, 1);
}
static char *sh(const char *cmd) { return sh_rc(cmd, NULL); }

/* argument from "id:arg" usable as a command argument (no shell involved anyway) */
static int safe_arg(const char *a)
{
    if (!a || !*a || *a == '-') return 0;
    for (; *a; a++)
        if (!isalnum((unsigned char)*a) && !strchr("_.:-+ ", *a)) return 0;
    return 1;
}

static void rtrim(char *s)
{
    size_t l = strlen(s);
    while (l && (s[l - 1] == '\n' || s[l - 1] == ' ' || s[l - 1] == '\t' || s[l - 1] == '\r')) s[--l] = 0;
}

static char *ltrim(char *s) { while (*s == ' ' || *s == '\t') s++; return s; }

/* first line of a command's output */
static void sh_line(const char *cmd, char *buf, size_t sz)
{
    char *t = sh(cmd);
    snprintf(buf, sz, "%.*s", (int)strcspn(t, "\n"), t);
    rtrim(buf);
    free(t);
}

/* print command output as mono lines, with an optional limit */
static int sh_lines(srs_out *o, const char *cmd, int color, int max)
{
    char *t = sh(cmd), *save = NULL, *l;
    int n = 0;
    for (l = strtok_r(t, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        if (max > 0 && n >= max) { so_note(o, "..."); break; }
        so_m(o, color, l);
        so_par(o);
        n++;
    }
    free(t);
    return n;
}

/* strip ANSI escape sequences in place */
static void strip_ansi(char *s)
{
    char *d = s;
    while (*s) {
        if (*s == 0x1b) { s++; if (*s == '[') { s++; while (*s && !isalpha((unsigned char)*s)) s++; if (*s) s++; } continue; }
        *d++ = *s++;
    }
    *d = 0;
}

static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
static const char *base_name(const char *p);

/* key=value file lookup (os-release style, quotes stripped) */
static int kv_file(const char *path, const char *key, char *buf, size_t sz)
{
    char *t, *save = NULL, *l;
    size_t kl = strlen(key);
    int ok = 0;
    *buf = 0;
    if (rd_all(path, &t) < 0) return 0;
    for (l = strtok_r(t, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        l = ltrim(l);
        if (!strncmp(l, key, kl) && l[kl] == '=') {
            char *v = l + kl + 1;
            if (*v == '"' || *v == '\'') { char q = *v++; char *e = strrchr(v, q); if (e) *e = 0; }
            snprintf(buf, sz, "%s", v);
            ok = 1;
            break;
        }
    }
    free(t);
    return ok;
}

/* /proc/meminfo and /proc/vmstat lookups (kB / count), reloaded per section */
static char *g_mi, *g_vm;
static void mem_load(void)
{
    free(g_mi); free(g_vm);
    g_mi = g_vm = NULL;
    if (rd_all("/proc/meminfo", &g_mi) < 0) g_mi = NULL;
    if (rd_all("/proc/vmstat", &g_vm) < 0) g_vm = NULL;
}
static long long find_num(const char *txt, const char *key, int colon)
{
    const char *p = txt;
    size_t kl = strlen(key);
    if (!txt) return -1;
    while (p && *p) {
        if (!strncmp(p, key, kl) && (colon ? p[kl] == ':' : p[kl] == ' ')) return atoll(p + kl + 1);
        p = strchr(p, '\n');
        if (p) p++;
    }
    return -1;
}
static long long mi(const char *k) { return find_num(g_mi, k, 1); }
static long long vm(const char *k) { return find_num(g_vm, k, 0); }
static void kb_str(long long kb, char *b, size_t sz) { if (kb < 0) snprintf(b, sz, "-"); else fmt_bytes((double)kb * 1024.0, b, sz); }

/* uid -> name from /etc/passwd (cached) */
typedef struct { unsigned uid, gid; char name[64], gecos[128], home[256], shell[128]; } pwent;
static pwent *g_pw;
static int g_npw = -1;
static void pw_load(void)
{
    char *t, *save = NULL, *l;
    if (g_npw >= 0) return;
    g_npw = 0;
    if (rd_all("/etc/passwd", &t) < 0) return;
    for (l = strtok_r(t, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        char *f[7] = { 0 }, *p = l;
        int i;
        pwent *n;
        for (i = 0; i < 7 && p; i++) { f[i] = p; p = strchr(p, ':'); if (p) *p++ = 0; }
        if (i < 7) continue;
        n = realloc(g_pw, (size_t)(g_npw + 1) * sizeof *n);
        if (!n) break;
        g_pw = n;
        snprintf(g_pw[g_npw].name, sizeof g_pw[g_npw].name, "%s", f[0]);
        g_pw[g_npw].uid = (unsigned)atol(f[2]);
        g_pw[g_npw].gid = (unsigned)atol(f[3]);
        snprintf(g_pw[g_npw].gecos, sizeof g_pw[g_npw].gecos, "%s", f[4]);
        snprintf(g_pw[g_npw].home, sizeof g_pw[g_npw].home, "%s", f[5]);
        snprintf(g_pw[g_npw].shell, sizeof g_pw[g_npw].shell, "%s", f[6]);
        g_npw++;
    }
    free(t);
}
static const char *uid_name(unsigned uid)
{
    static char b[16];
    int i;
    pw_load();
    for (i = 0; i < g_npw; i++) if (g_pw[i].uid == uid) return g_pw[i].name;
    snprintf(b, sizeof b, "%u", uid);
    return b;
}

static void fmt_epoch(time_t t, char *b, size_t sz)
{
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(b, sz, "%Y-%m-%d %H:%M:%S %A", &tm);
}

static void fmt_uptime(double s, char *b, size_t sz)
{
    long v = (long)s;
    snprintf(b, sz, "%02ld %02ld:%02ld:%02ld", v / 86400, v / 3600 % 24, v / 60 % 60, v % 60);
}

static double uptime_s(void) { char b[64] = ""; rd_str("/proc/uptime", b, sizeof b); return atof(b); }

static time_t boot_time(void)
{
    char *t;
    time_t bt = 0;
    if (rd_all("/proc/stat", &t) >= 0) { char *p = strstr(t, "\nbtime "); if (p) bt = (time_t)atoll(p + 7); free(t); }
    return bt;
}

/* oldest plausible install time + its source */
static time_t install_time(char *src, size_t sz)
{
    char **e, b[64], p[512];
    int n = dir_list("/var/lib/pacman/local", &e), i;
    time_t best = 0;
    struct stat st;
    snprintf(src, sz, "unknown");
    for (i = 0; i < n; i++)
        if (!strncmp(e[i], "filesystem-", 11) || !strncmp(e[i], "base-", 5)) {
            char *t, *q;
            snprintf(p, sizeof p, "/var/lib/pacman/local/%s/desc", e[i]);
            if (rd_all(p, &t) < 0) continue;
            if ((q = strstr(t, "%INSTALLDATE%\n"))) {
                time_t v = (time_t)atoll(q + 14);
                if (v > 0 && (!best || v < best)) { best = v; snprintf(src, sz, "pacman %s %%INSTALLDATE%%", e[i]); }
            }
            free(t);
        }
    dir_free(e, n);
    if (!stat("/etc/machine-id", &st) && st.st_mtime > 0 && (!best || st.st_mtime < best)) { best = st.st_mtime; snprintf(src, sz, "/etc/machine-id mtime"); }
    if (rd_str("/var/log/pacman.log", b, sizeof b) && b[0] == '[') {
        struct tm tm;
        memset(&tm, 0, sizeof tm);
        if (strptime(b + 1, "%Y-%m-%dT%H:%M:%S", &tm)) {
            time_t v;
            tm.tm_isdst = -1;
            v = mktime(&tm);
            if (v > 0 && (!best || v < best)) { best = v; snprintf(src, sz, "/var/log/pacman.log first entry"); }
        }
    }
    return best;
}

static void kernel_cfg_grep(srs_out *o, const char *regex, int color)
{
    char cmd[512];
    snprintf(cmd, sizeof cmd, "zcat /proc/config.gz | grep -E '%s'", regex);
    if (!path_exists("/proc/config.gz")) { so_note(o, "/proc/config.gz not available (CONFIG_IKCONFIG_PROC off)"); return; }
    if (!sh_lines(o, cmd, color, 200)) so_note(o, "no matching kernel config options");
}

static const char *const taint_desc[] = {
    "P  Proprietary module loaded", "F  Module force loaded", "S  Kernel running on out-of-spec system",
    "R  Module force unloaded", "M  Processor reported a Machine Check Exception", "B  Bad page referenced / unexpected page flags",
    "U  Taint requested by userspace", "D  Kernel died recently (OOPS or BUG)", "A  ACPI table overridden by user",
    "W  Kernel issued a warning", "C  Staging driver loaded", "I  Workaround for platform firmware bug applied",
    "O  Externally-built (out-of-tree) module loaded", "E  Unsigned module loaded", "L  Soft lockup occurred",
    "K  Kernel live patched", "X  Auxiliary taint (distro defined)", "T  Kernel built with struct randomization plugin",
    "N  In-kernel test has been run"
};

static void taint_table(srs_out *o)
{
    static const int tabs[] = { 56, 112 };
    long t = 0;
    unsigned i;
    rd_long("/proc/sys/kernel/tainted", &t);
    so_tabs(o, tabs, 2);
    so_kvf(o, "Tainted", t ? C_ORANGE : C_GREEN, "%ld (0x%lX)  /proc/sys/kernel/tainted", t, t);
    for (i = 0; i < sizeof taint_desc / sizeof *taint_desc; i++) {
        char b[16];
        so_t(o, C_DEF, "  ");
        so_check(o, (t >> i) & 1);
        so_tab(o);
        snprintf(b, sizeof b, "bit %u", i);
        so_t(o, C_GRAY, b);
        so_tab(o);
        so_t(o, (t >> i) & 1 ? C_ORANGE : C_VALUE, taint_desc[i]);
        so_par(o);
    }
}

/* ======================================================================
 * [windows] / [system]  - the Linux overview panel
 * ====================================================================== */
static void s_windows(srs_out *o)
{
    static const int tabs[] = { 280 };
    struct utsname u;
    char name[160] = "Linux", ver[64] = "", id[64] = "", like[64] = "", build[64] = "", b[1024], src[128], tz[128] = "", host[128] = "";
    char *txt, *save = NULL, *l;
    time_t it = install_time(src, sizeof src);
    int i;

    uname(&u);
    kv_file("/etc/os-release", "PRETTY_NAME", name, sizeof name);
    kv_file("/etc/os-release", "VERSION_ID", ver, sizeof ver);
    kv_file("/etc/os-release", "ID", id, sizeof id);
    kv_file("/etc/os-release", "ID_LIKE", like, sizeof like);
    kv_file("/etc/os-release", "BUILD_ID", build, sizeof build);
    gethostname(host, sizeof host);
    so_tabs(o, tabs, 1);
    so_par(o);
    so_tab(o);
    so_f(o, C_VALUE, "%s  %s%s  Kernel %s  %s", name, *ver ? "V" : "Build ", *ver ? ver : *build ? build : "rolling", u.release, u.machine);
    so_par(o);
    so_tab(o);
    so_f(o, C_VALUE, "ID %s%s%s  Kernel build %s  Hostname %s", id, *like ? "  ID_LIKE " : "", like, u.version, host);
    so_par(o);
    so_tab(o);
    if (it) { char d[96]; fmt_epoch(it, d, sizeof d); so_f(o, C_VALUE, "InstallDate  %s (%lld)  [%s]", d, (long long)it, src); }
    else so_t(o, C_GRAY, "InstallDate  unknown");
    so_par(o);
    so_tab(o);
    sh_line("systemctl --version", b, sizeof b);
    {
        char ch[16] = "";
        static const char *const chassis[] = { "", "Other", "Unknown", "Desktop", "Low Profile Desktop", "Pizza Box", "Mini Tower", "Tower",
                                               "Portable", "Laptop", "Notebook", "Hand Held", "Docking Station", "All in One", "Sub Notebook" };
        int ct;
        rd_str("/sys/class/dmi/id/chassis_type", ch, sizeof ch);
        ct = atoi(ch);
        so_f(o, C_VALUE, "Product Type Linux  Init %s  Chassis %s", *b ? b : "unknown (not systemd)",
             ct > 0 && ct < 15 ? chassis[ct] : "-");
    }
    so_par(o);
    so_par(o);

    {
        const char *de = getenv("XDG_CURRENT_DESKTOP"), *st = getenv("XDG_SESSION_TYPE"), *wd = getenv("WAYLAND_DISPLAY"), *xd = getenv("DISPLAY");
        snprintf(b, sizeof b, "%s  %s%s%s%s%s", de ? de : "<none>", st ? st : "", wd ? "  WAYLAND_DISPLAY=" : "", wd ? wd : "",
                 xd ? "  DISPLAY=" : "", xd ? xd : "");
        so_kv(o, "Linux Desktop Session", C_VALUE, b);
    }
    so_kv(o, "Root Directory", C_PURPLE, "/");
    so_kv(o, "System Directory", C_PURPLE, "/usr/bin");
    so_kv(o, "Library Directory", C_PURPLE, "/usr/lib");
    so_kv(o, "Boot Directory", C_PURPLE, "/boot");
    rd_str("/proc/cmdline", b, sizeof b);
    so_kv(o, "Kernel Command Line", C_PURPLE, b);
    so_par(o);

    /* per-CPU lines */
    if (rd_all("/proc/cpuinfo", &txt) >= 0) {
        char vend[32] = "", fam[16] = "", mod[16] = "", step[16] = "", mc[32] = "", mn[128] = "";
        int cpu = -1;
        for (l = strtok_r(txt, "\n", &save);; l = strtok_r(NULL, "\n", &save)) {
            char *c = l ? strchr(l, ':') : NULL, *v = c ? ltrim(c + 1) : NULL;
            if (!l || !strncmp(l, "processor", 9)) {
                if (cpu >= 0) {
                    char lab[96], mhz[32] = "";
                    long khz;
                    char p[128];
                    snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", cpu);
                    if (rd_long(p, &khz)) fmt_hz(khz * 1000.0, mhz, sizeof mhz);
                    snprintf(lab, sizeof lab, "[CPU-%d %s %s]", cpu, vend, mc);
                    so_t(o, C_LABEL, lab);
                    so_tab(o);
                    so_f(o, C_BLACK, "%s %s  (Family %s Model %s Stepping %s)", mn, mhz, fam, mod, step);
                    so_par(o);
                }
                if (!l) break;
                cpu = v ? atoi(v) : cpu + 1;
                continue;
            }
            if (!v) continue;
            if (!strncmp(l, "vendor_id", 9)) snprintf(vend, sizeof vend, "%s", v);
            else if (!strncmp(l, "cpu family", 10)) snprintf(fam, sizeof fam, "%s", v);
            else if (!strncmp(l, "model name", 10)) snprintf(mn, sizeof mn, "%s", v);
            else if (!strncmp(l, "model", 5) && (l[5] == '\t' || l[5] == ' ')) snprintf(mod, sizeof mod, "%s", v);
            else if (!strncmp(l, "stepping", 8)) snprintf(step, sizeof step, "%s", v);
            else if (!strncmp(l, "microcode", 9)) snprintf(mc, sizeof mc, "%s", v);
        }
        free(txt);
    }
    so_par(o);

    /* power source */
    {
        char **e;
        int n = dir_list("/sys/class/power_supply", &e), bat = 0, ac = 0, acon = 0;
        for (i = 0; i < n; i++) {
            char t[32] = "", on[8] = "";
            rd_fmt(t, sizeof t, "/sys/class/power_supply/%s/type", e[i]);
            if (!strcmp(t, "Battery")) bat++;
            if (!strcmp(t, "Mains")) { ac++; rd_fmt(on, sizeof on, "/sys/class/power_supply/%s/online", e[i]); if (atoi(on)) acon++; }
        }
        dir_free(e, n > 0 ? n : 0);
        snprintf(b, sizeof b, "%s, %s", ac && !acon ? "Battery Power" : "Mains Power", bat ? "Battery Present" : "No Battery");
        so_t(o, C_LABEL, b);
    }
    so_tab(o);
    {
        char tzl[256] = "", abbr[16];
        time_t now = time(NULL);
        struct tm tm;
        struct timespec res = { 0, 0 };
        ssize_t r = readlink("/etc/localtime", tzl, sizeof tzl - 1);
        if (r > 0) { char *z = strstr(tzl, "zoneinfo/"); tzl[r] = 0; snprintf(tz, sizeof tz, "%s", z ? z + 9 : tzl); }
        else rd_str("/etc/timezone", tz, sizeof tz);
        localtime_r(&now, &tm);
        strftime(abbr, sizeof abbr, "%Z %z", &tm);
        clock_getres(CLOCK_MONOTONIC, &res);
        rd_str("/sys/devices/system/clocksource/clocksource0/current_clocksource", b, sizeof b);
        so_f(o, C_VALUE, "Time Zone  %s  (%s)  Clocksource %s  Resolution %ld ns", *tz ? tz : "UTC", abbr, b, res.tv_nsec);
    }
    so_par(o);
    {
        char up[32], bt[64], la[64] = "";
        time_t btt = boot_time();
        fmt_uptime(uptime_s(), up, sizeof up);
        fmt_epoch(btt, bt, sizeof bt);
        rd_str("/proc/loadavg", la, sizeof la);
        so_tab(o);
        so_f(o, C_VALUE, "Up-time %s  Booted %s  Load %s", up, bt, la);
        so_par(o);
    }
    {
        const char *lang = getenv("LC_ALL");
        if (!lang || !*lang) lang = getenv("LANG");
        so_tab(o);
        so_f(o, C_VALUE, "User %s (uid %u)  Locale %s  Shell %s", uid_name((unsigned)getuid()), (unsigned)getuid(), lang ? lang : "C",
             getenv("SHELL") ? getenv("SHELL") : "-");
        so_par(o);
    }
    so_par(o);
    {
        char bd[64] = "", bv[64] = "", bven[64] = "";
        rd_str("/sys/class/dmi/id/bios_date", bd, sizeof bd);
        rd_str("/sys/class/dmi/id/bios_version", bv, sizeof bv);
        rd_str("/sys/class/dmi/id/bios_vendor", bven, sizeof bven);
        so_t(o, C_LABEL, "System  BIOS Date");
        so_tab(o);
        so_f(o, C_VALUE, "%s  %s %s  Firmware %s", bd, bven, bv, path_exists("/sys/firmware/efi") ? "UEFI" : "Legacy BIOS");
        so_par(o);
    }
    so_par(o);

    /* displays from DRM */
    {
        char **e;
        int n = dir_list("/sys/class/drm", &e), d = 0;
        for (i = 0; i < n; i++) {
            char st[32] = "", en[32] = "", mode[64] = "", p[256], lab[64];
            if (!strchr(e[i], '-') || strncmp(e[i], "card", 4)) continue;
            rd_fmt(st, sizeof st, "/sys/class/drm/%s/status", e[i]);
            if (strcmp(st, "connected")) continue;
            rd_fmt(en, sizeof en, "/sys/class/drm/%s/enabled", e[i]);
            rd_fmt(mode, sizeof mode, "/sys/class/drm/%s/modes", e[i]);
            snprintf(lab, sizeof lab, "%s Display #%d", d ? "Desktop" : "Primary Desktop", d + 1);
            so_t(o, C_LABEL, lab);
            so_tab(o);
            so_f(o, C_VALUE, "%s  %s  preferred mode %s", strchr(e[i], '-') + 1, en, *mode ? mode : "-");
            so_par(o);
            snprintf(p, sizeof p, "/sys/class/drm/%.*s/device/driver", (int)(strchr(e[i], '-') - e[i]), e[i]);
            {
                char drv[256] = "";
                ssize_t r = readlink(p, drv, sizeof drv - 1);
                if (r > 0) { drv[r] = 0; so_t(o, C_GRAY, "  Kernel driver"); so_tab(o); so_t(o, C_RED, strrchr(drv, '/') ? strrchr(drv, '/') + 1 : drv); so_par(o); }
            }
            d++;
        }
        if (!d) so_note(o, "No connected DRM display connectors found");
        dir_free(e, n > 0 ? n : 0);
    }
    so_board_footer(o);
}

/* ======================================================================
 * [processes]
 * ====================================================================== */
typedef struct { int pid, ppid, thr; char state; unsigned uid; long rss; char comm[64], exe[256]; } proc_t;
static int proc_cmp(const void *a, const void *b)
{
    const proc_t *x = a, *y = b;
    int c = strcasecmp(x->comm, y->comm);
    return c ? c : x->pid - y->pid;
}

static int proc_scan(proc_t **out)
{
    char **e;
    int n = dir_list("/proc", &e), i, k = 0;
    proc_t *v = calloc((size_t)(n > 0 ? n : 1), sizeof *v);
    for (i = 0; i < n && v; i++) {
        char p[64], *t, *rp;
        struct stat st;
        if (!isdigit((unsigned char)e[i][0])) continue;
        snprintf(p, sizeof p, "/proc/%s/stat", e[i]);
        if (rd_all(p, &t) < 0) continue;
        rp = strrchr(t, ')');
        if (rp && strchr(t, '(')) {
            proc_t *q = &v[k];
            char *lp = strchr(t, '('), *f[30], *s = rp + 2, *sv = NULL;
            int j = 0;
            q->pid = atoi(e[i]);
            snprintf(q->comm, sizeof q->comm, "%.*s", (int)(rp - lp - 1), lp + 1);
            for (char *tok = strtok_r(s, " ", &sv); tok && j < 30; tok = strtok_r(NULL, " ", &sv)) f[j++] = tok;
            if (j > 21) { q->state = f[0][0]; q->ppid = atoi(f[1]); q->thr = atoi(f[17]); q->rss = atol(f[21]) * (sysconf(_SC_PAGESIZE) / 1024); }
            snprintf(p, sizeof p, "/proc/%s", e[i]);
            if (!stat(p, &st)) q->uid = st.st_uid;
            snprintf(p, sizeof p, "/proc/%s/exe", e[i]);
            {
                ssize_t r = readlink(p, q->exe, sizeof q->exe - 1);
                if (r > 0) q->exe[r] = 0; else snprintf(q->exe, sizeof q->exe, "-");
            }
            k++;
        }
        free(t);
    }
    dir_free(e, n > 0 ? n : 0);
    *out = v;
    return k;
}

static void s_processes(srs_out *o)
{
    static const int tabs[] = { 77, 154, 210, 252, 336, 448, 616 };
    static const char *const h[] = { "Parent", "PID", "Threads", "State", "RSS", "Account Name", "Image Name", "Image Filename" };
    proc_t *v;
    int n = proc_scan(&v), i;
    long pidmax = 0;
    char b[160];
    rd_long("/proc/sys/kernel/pid_max", &pidmax);
    qsort(v, (size_t)n, sizeof *v, proc_cmp);
    so_tabs(o, tabs, 7);
    snprintf(b, sizeof b, "Image Filename - Returned %d of %ld", n, pidmax);
    {
        const char *hh[8];
        memcpy(hh, h, sizeof hh);
        hh[7] = b;
        so_row(o, 8, hh, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    }
    so_par(o);
    for (i = 0; i < n; i++) {
        char c[5][32];
        const char *cells[8];
        snprintf(c[0], 32, "[%d]", v[i].ppid);
        snprintf(c[1], 32, "[%d]", v[i].pid);
        snprintf(c[2], 32, "%d", v[i].thr);
        snprintf(c[3], 32, "%c", v[i].state ? v[i].state : '?');
        kb_str(v[i].rss, c[4], 32);
        cells[0] = c[0]; cells[1] = c[1]; cells[2] = c[2]; cells[3] = c[3]; cells[4] = c[4];
        cells[5] = uid_name(v[i].uid); cells[6] = v[i].exe[0] == '/' ? base_name(v[i].exe) : v[i].comm; cells[7] = v[i].exe;
        so_row(o, 8, cells, (const int[]){ C_GRAY, C_PURPLE, C_VALUE, C_TEAL, C_VALUE, v[i].uid ? C_VALUE : C_RED, C_BLACK, C_GRAY });
    }
    free(v);
}

/* ======================================================================
 * [services-w32]  systemd services   [services-drv]  bound kernel drivers
 * ====================================================================== */
static int unit_table(srs_out *o, const char *cmd)
{
    char *t = sh(cmd), *save = NULL, *l;
    int n = 0, act = 0, fail = 0;
    for (l = strtok_r(t, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        char u[256] = "", ld[32] = "", ac[32] = "", sub[32] = "";
        int off = 0;
        const char *cells[5];
        if (sscanf(l, "%255s %31s %31s %31s %n", u, ld, ac, sub, &off) < 4) continue;
        cells[0] = u; cells[1] = ld; cells[2] = ac; cells[3] = sub; cells[4] = off ? l + off : "";
        so_row(o, 5, cells, (const int[]){ C_BLACK, C_GRAY, !strcmp(ac, "failed") ? C_RED : !strcmp(ac, "active") ? C_GREEN : C_GRAY,
                                           C_TEAL, C_VALUE });
        n++;
        act += !strcmp(ac, "active");
        fail += !strcmp(ac, "failed");
    }
    free(t);
    {
        char b[128];
        snprintf(b, sizeof b, "%d units, %d active, %d failed", n, act, fail);
        so_note(o, b);
    }
    return n;
}

static void s_services_w32(srs_out *o)
{
    static const int tabs[] = { 330, 400, 470, 550 };
    static const char *const h[] = { "Service Name", "Load", "Active", "State", "Description" };
    so_siv_header(o, "Services (systemd)");
    so_tabs(o, tabs, 4);
    so_b(o, C_VALUE, "System services  (systemctl list-units --type=service --all)");
    so_par(o);
    so_row(o, 5, h, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    unit_table(o, "systemctl list-units --type=service --all --no-pager --no-legend --plain");
    so_par(o);
    so_b(o, C_VALUE, "User services  (systemctl --user list-units --type=service --all)");
    so_par(o);
    so_row(o, 5, h, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    unit_table(o, "systemctl --user list-units --type=service --all --no-pager --no-legend --plain");
}

static int is_drv_ctl(const char *n)
{
    static const char *const ctl[] = { "bind", "unbind", "uevent", "module", "new_id", "remove_id", NULL };
    int i;
    for (i = 0; ctl[i]; i++) if (!strcmp(n, ctl[i])) return 1;
    return 0;
}

static void s_services_drv(srs_out *o)
{
    static const int tabs[] = { 112, 350, 420, 500 };
    static const char *const h[] = { "Bus", "Driver Name", "Devices", "State", "Module" };
    char **bus;
    int nb = dir_list("/sys/bus", &bus), i, tot = 0, run = 0;
    so_siv_header(o, "Driver Services (kernel drivers)");
    so_tabs(o, tabs, 4);
    so_row(o, 5, h, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (i = 0; i < nb; i++) {
        char d[256], **dr;
        int nd, j;
        snprintf(d, sizeof d, "/sys/bus/%s/drivers", bus[i]);
        nd = dir_list(d, &dr);
        for (j = 0; j < nd; j++) {
            char p[512], **ent, modl[256] = "", nbuf[16];
            int ne, k, dev = 0;
            ssize_t r;
            const char *cells[5];
            snprintf(p, sizeof p, "/sys/bus/%s/drivers/%s", bus[i], dr[j]);
            ne = dir_list(p, &ent);
            for (k = 0; k < ne; k++) if (!is_drv_ctl(ent[k])) dev++;
            dir_free(ent, ne > 0 ? ne : 0);
            snprintf(p, sizeof p, "/sys/bus/%s/drivers/%s/module", bus[i], dr[j]);
            r = readlink(p, modl, sizeof modl - 1);
            if (r > 0) modl[r] = 0;
            snprintf(nbuf, sizeof nbuf, "%d", dev);
            cells[0] = bus[i]; cells[1] = dr[j]; cells[2] = nbuf; cells[3] = dev ? "Running" : "Stopped";
            cells[4] = r > 0 ? (strrchr(modl, '/') ? strrchr(modl, '/') + 1 : modl) : "(built-in)";
            so_row(o, 5, cells, (const int[]){ C_GRAY, C_BLACK, C_VALUE, dev ? C_GREEN : C_GRAY, C_PURPLE });
            tot++;
            run += dev > 0;
        }
        dir_free(dr, nd > 0 ? nd : 0);
    }
    dir_free(bus, nb > 0 ? nb : 0);
    {
        char b[96];
        snprintf(b, sizeof b, "%d drivers, %d bound to devices (Running)", tot, run);
        so_par(o);
        so_note(o, b);
    }
}

/* ======================================================================
 * [drivers] /proc/modules   [driver-vsn] /sys/module   [signing]
 * ====================================================================== */
typedef struct { char name[64], size[16], used[16], state[16], taint[16], by[512]; } modrow;
static int mod_cmp(const void *a, const void *b) { return strcmp(((const modrow *)a)->name, ((const modrow *)b)->name); }
static int mod_load(modrow **out)
{
    char *t, *save = NULL, *l;
    int n = 0;
    modrow *v = NULL;
    *out = NULL;
    if (rd_all("/proc/modules", &t) < 0) return 0;
    for (l = strtok_r(t, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        modrow *nv = realloc(v, (size_t)(n + 1) * sizeof *v), *m;
        char addr[32];
        if (!nv) break;
        v = nv;
        m = &v[n];
        memset(m, 0, sizeof *m);
        if (sscanf(l, "%63s %15s %15s %511s %15s %31s %15s", m->name, m->size, m->used, m->by, m->state, addr, m->taint) < 5) continue;
        if (!strcmp(m->by, "-")) *m->by = 0;
        else if (*m->by && m->by[strlen(m->by) - 1] == ',') m->by[strlen(m->by) - 1] = 0;
        if (m->taint[0] == '(') { memmove(m->taint, m->taint + 1, strlen(m->taint)); m->taint[strcspn(m->taint, ")")] = 0; }
        n++;
    }
    free(t);
    qsort(v, (size_t)n, sizeof *v, mod_cmp);
    *out = v;
    return n;
}

static void s_drivers(srs_out *o)
{
    static const int tabs[] = { 196, 266, 308, 378, 420 };
    static const char *const h[] = { "Module Name", "Size", "Used", "State", "Taint", "Used By" };
    modrow *m;
    int n = mod_load(&m), i, bi = 0;
    struct utsname u;
    char p[256], *t;
    uname(&u);
    snprintf(p, sizeof p, "/usr/lib/modules/%s/modules.builtin", u.release);
    if (rd_all(p, &t) >= 0) { char *q; for (q = t; *q; q++) bi += *q == '\n'; free(t); }
    so_siv_header(o, "Loaded Drivers (kernel modules)");
    so_tabs(o, tabs, 5);
    so_row(o, 6, h, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (i = 0; i < n; i++) {
        char sz[32];
        const char *cells[6];
        fmt_bytes(atof(m[i].size), sz, sizeof sz);
        cells[0] = m[i].name; cells[1] = sz; cells[2] = m[i].used; cells[3] = m[i].state; cells[4] = m[i].taint; cells[5] = m[i].by;
        so_row(o, 6, cells, (const int[]){ C_BLACK, C_VALUE, C_VALUE, strcmp(m[i].state, "Live") ? C_ORANGE : C_GREEN, C_ORANGE, C_GRAY });
    }
    so_par(o);
    snprintf(p, sizeof p, "%d loadable modules loaded, %d built into kernel %s (modules.builtin)", n, bi, u.release);
    so_note(o, p);
    free(m);
}

static void s_driver_vsn(srs_out *o)
{
    static const int tabs[] = { 196, 280, 448, 630, 680 };
    static const char *const h[] = { "Module", "Type", "Version", "Source Version", "Refs", "Parameters" };
    char **e;
    int n, i;
    if (o->arg && *o->arg) {
        char *out = NULL;
        const char *argv[] = { "timeout", "5", "modinfo", o->arg, NULL };
        so_siv_header(o, "Driver Version");
        if (!safe_arg(o->arg)) { so_err(o, "invalid module name"); return; }
        run_out(&out, argv);
        if (out && *out) so_lines(o, C_VALUE, out); else so_err(o, "modinfo returned nothing");
        free(out);
        return;
    }
    n = dir_list("/sys/module", &e);
    so_siv_header(o, "Driver Versions (/sys/module)");
    so_tabs(o, tabs, 5);
    so_row(o, 6, h, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (i = 0; i < n; i++) {
        char v[64] = "", sv[64] = "", ref[16] = "", p[256], np[16], **pa;
        int npa;
        const char *cells[6];
        int loadable;
        rd_fmt(v, sizeof v, "/sys/module/%s/version", e[i]);
        rd_fmt(sv, sizeof sv, "/sys/module/%s/srcversion", e[i]);
        rd_fmt(ref, sizeof ref, "/sys/module/%s/refcnt", e[i]);
        snprintf(p, sizeof p, "/sys/module/%s/initstate", e[i]);
        loadable = path_exists(p);
        snprintf(p, sizeof p, "/sys/module/%s/parameters", e[i]);
        npa = dir_list(p, &pa);
        dir_free(pa, npa > 0 ? npa : 0);
        snprintf(np, sizeof np, "%d", npa > 0 ? npa : 0);
        cells[0] = e[i]; cells[1] = loadable ? "loadable" : "built-in"; cells[2] = *v ? v : "-"; cells[3] = *sv ? sv : "-";
        cells[4] = *ref ? ref : "-"; cells[5] = np;
        so_row(o, 6, cells, (const int[]){ C_BLACK, loadable ? C_TEAL : C_GRAY, C_PURPLE, C_GRAY, C_VALUE, C_VALUE });
    }
    so_par(o);
    so_note(o, "Open as driver-vsn:<module> for full modinfo output");
    dir_free(e, n > 0 ? n : 0);
}

static void s_signing(srs_out *o)
{
    static const int tabs[] = { 196, 300 };
    modrow *m;
    int n, i, unsig = 0, oot = 0, prop = 0;
    char b[64] = "";
    if (o->arg && *o->arg) {
        char *out = NULL;
        const char *argv[] = { "timeout", "5", "modinfo", "-F", "signer", o->arg, NULL };
        so_siv_header(o, "Driver Signing");
        if (!safe_arg(o->arg)) { so_err(o, "invalid module name"); return; }
        so_tabs(o, tabs, 1);
        run_out(&out, argv);
        if (out) rtrim(out);
        so_kv(o, "Module", C_BLACK, o->arg);
        so_kv(o, "Signer", C_VALUE, out && *out ? out : "(unsigned)");
        free(out);
        out = NULL;
        argv[4] = "sig_hashalgo";
        run_out(&out, argv);
        if (out) rtrim(out);
        so_kv(o, "Hash", C_VALUE, out);
        free(out);
        return;
    }
    n = mod_load(&m);
    so_siv_header(o, "Driver Signing");
    so_tabs(o, tabs, 1);
    rd_str("/sys/module/module/parameters/sig_enforce", b, sizeof b);
    so_kv(o, "module.sig_enforce", !strcmp(b, "Y") ? C_GREEN : C_ORANGE, *b ? b : "- (CONFIG_MODULE_SIG off)");
    for (i = 0; i < n; i++) {
        unsig += strchr(m[i].taint, 'E') != NULL;
        oot += strchr(m[i].taint, 'O') != NULL;
        prop += strchr(m[i].taint, 'P') != NULL;
    }
    so_kvf(o, "Loaded modules", C_VALUE, "%d", n);
    so_kvf(o, "Unsigned (E)", unsig ? C_ORANGE : C_GREEN, "%d", unsig);
    so_kvf(o, "Out-of-tree (O)", oot ? C_ORANGE : C_GREEN, "%d", oot);
    so_kvf(o, "Proprietary (P)", prop ? C_ORANGE : C_GREEN, "%d", prop);
    so_par(o);
    so_b(o, C_VALUE, "Tainting modules");
    so_par(o);
    for (i = 0; i < n; i++)
        if (*m[i].taint) so_kv(o, m[i].name, C_ORANGE, m[i].taint);
    so_par(o);
    so_b(o, C_VALUE, "Kernel signing configuration");
    so_par(o);
    kernel_cfg_grep(o, "^CONFIG_MODULE_SIG|^CONFIG_SYSTEM_TRUSTED_KEYS|^CONFIG_MODULE_HASHES", C_PURPLE);
    so_par(o);
    so_note(o, "Open as signing:<module> for its signer");
    free(m);
}

/* ======================================================================
 * [environment]
 * ====================================================================== */
static int secret_name(const char *n, size_t l)
{
    static const char *const pat[] = { "TOKEN", "KEY", "SECRET", "PASS", "AUTH", "COOKIE", "CREDENTIAL", "SESSION_ID", NULL };
    char up[256];
    size_t i;
    if (l >= sizeof up) l = sizeof up - 1;
    for (i = 0; i < l; i++) up[i] = (char)toupper((unsigned char)n[i]);
    up[l] = 0;
    for (i = 0; pat[i]; i++) if (strstr(up, pat[i])) return 1;
    return 0;
}

static void env_row(srs_out *o, const char *kv)
{
    const char *eq = strchr(kv, '=');
    char name[256], sz[16];
    const char *cells[3];
    size_t nl = eq ? (size_t)(eq - kv) : strlen(kv);
    int sec;
    snprintf(name, sizeof name, "%.*s", (int)nl, kv);
    sec = secret_name(kv, nl);
    snprintf(sz, sizeof sz, "%zu", eq ? strlen(eq + 1) : 0);
    cells[0] = name; cells[1] = sz; cells[2] = sec ? "******** (masked)" : eq ? eq + 1 : "";
    so_row(o, 3, cells, (const int[]){ C_BLACK, C_VALUE, sec ? C_ORANGE : C_VALUE });
}

static void s_environment(srs_out *o)
{
    static const int tabs[] = { 260, 315 };
    static const char *const h[] = { "Environment Variable Name", "Size", "Environment Variable Expansion" };
    extern char **environ;
    char **e;
    char *t, *save = NULL, *l;
    int n = 0, i;
    so_tabs(o, tabs, 2);
    so_row(o, 3, h, (const int[]){ C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (e = environ; e && *e; e++) n++;
    {
        char **v = malloc((size_t)(n ? n : 1) * sizeof *v);
        for (i = 0; v && i < n; i++) v[i] = environ[i];
        if (v) qsort(v, (size_t)n, sizeof *v, cmp_str);
        for (i = 0; v && i < n; i++) env_row(o, v[i]);
        free(v);
    }
    so_par(o);
    so_b(o, C_VALUE, "/etc/environment");
    so_par(o);
    if (rd_all("/etc/environment", &t) >= 0) {
        for (l = strtok_r(t, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
            l = ltrim(l);
            if (*l && *l != '#' && strchr(l, '=')) env_row(o, l);
        }
        free(t);
    }
    so_par(o);
    {
        char b[96];
        snprintf(b, sizeof b, "%d process variables; values of names matching TOKEN/KEY/SECRET/PASS/AUTH/COOKIE are masked", n);
        so_note(o, b);
    }
}

/* ======================================================================
 * [fonts]  font families x character-set coverage (fontconfig)
 * ====================================================================== */
static const struct { const char *abbr, *desc, *lang; } font_sets[] = {
    { "WER", "Western", "en" }, { "CER", "Central European", "pl" }, { "CRK", "Cyrillic", "ru" }, { "GRK", "Greek", "el" },
    { "TRK", "Turkish", "tr" }, { "BTC", "Baltic", "lt" }, { "HBW", "Hebrew", "he" }, { "ARC", "Arabic", "ar" },
    { "VMN", "Vietnamese", "vi" }, { "THI", "Thai", "th" }, { "JAP", "Japanese", "ja" }, { "HGL", "Hangul", "ko" },
    { "CGB", "Chinese GB2312", "zh-cn" }, { "CB5", "Chinese BIG5", "zh-tw" },
};
#define NFSET ((int)(sizeof font_sets / sizeof *font_sets))

static int fam_split(char *txt, char ***out)
{
    char *save = NULL, *l, **v = NULL;
    int n = 0, i, k = 0;
    for (l = strtok_r(txt, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        char **nv;
        char *c = strchr(l, ',');
        if (c) *c = 0;
        while (c && c > l && c[-1] == '\\') { *c = ','; c = strchr(c + 1, ','); if (c) *c = 0; }
        if (!*l) continue;
        nv = realloc(v, (size_t)(n + 1) * sizeof *v);
        if (!nv) break;
        v = nv;
        v[n++] = l;
    }
    qsort(v, (size_t)n, sizeof *v, cmp_str);
    for (i = 0; i < n; i++) if (!k || strcmp(v[i], v[k - 1])) v[k++] = v[i];
    *out = v;
    return k;
}

static void s_fonts(srs_out *o)
{
    static const int tabs[] = { 280, 315, 350, 385, 420, 455, 490, 525, 560, 595, 630, 665, 700, 735 };
    char *all = sh("fc-list : family"), **fam;
    int nf = fam_split(all, &fam), i, j, cnt[NFSET];
    unsigned char *m = calloc((size_t)(nf ? nf : 1), NFSET);
    char b[160];
    for (j = 0; j < NFSET && m; j++) {
        char cmd[96], *t, **lf;
        int nl, k;
        snprintf(cmd, sizeof cmd, "fc-list :lang=%s family", font_sets[j].lang);
        t = sh(cmd);
        nl = fam_split(t, &lf);
        cnt[j] = 0;
        for (k = 0; k < nl; k++) {
            char **hit = bsearch(&lf[k], fam, (size_t)nf, sizeof *fam, cmp_str);
            if (hit) { m[(hit - fam) * NFSET + j] = 1; cnt[j]++; }
        }
        free(lf);
        free(t);
    }
    so_tabs(o, tabs, 14);
    snprintf(b, sizeof b, "Unique %d font families", nf);
    so_t(o, C_LABEL, b);
    so_par(o);
    for (j = 0; j < NFSET; j++) {
        snprintf(b, sizeof b, "%s %s %d (:lang=%s)", font_sets[j].abbr, font_sets[j].desc, m ? cnt[j] : 0, font_sets[j].lang);
        so_t(o, C_GRAY, b);
        so_par(o);
    }
    so_par(o);
    snprintf(b, sizeof b, "Face Name - Character Sets 1 to %d", nf);
    so_t(o, C_LABEL, b);
    for (j = 0; j < NFSET; j++) { so_tab(o); so_t(o, C_LABEL, font_sets[j].abbr); }
    so_par(o);
    so_par(o);
    for (i = 0; i < nf && m; i++) {
        so_t(o, C_BLACK, fam[i]);
        for (j = 0; j < NFSET; j++) { so_tab(o); if (m[i * NFSET + j]) so_t(o, C_TEAL, font_sets[j].abbr); }
        so_par(o);
    }
    free(m);
    free(fam);
    free(all);
}

/* [ranges]  Unicode coverage of a font (default: the monospace font) */
static void s_ranges(srs_out *o)
{
    static const int tabs[] = { 63, 140, 217, 290 };
    static const char *const h[] = { "Range", "Start", "End", "Glyphs", "Characters" };
    const char *pat = o->arg && *o->arg ? o->arg : "monospace";
    const char *argv[] = { "timeout", "5", "fc-match", "-f", "%{family[0]}\n%{file}\n%{charset}\n", pat, NULL };
    char *out = NULL, *fam, *file, *cs, *save = NULL, *tok;
    int n = 0;
    long tot = 0;
    so_siv_header(o, "Font Ranges");
    if (!safe_arg(pat)) { so_err(o, "invalid font pattern"); return; }
    run_out(&out, argv);
    if (!out || !*out) { so_err(o, "fc-match returned nothing"); free(out); return; }
    fam = out;
    file = strchr(fam, '\n');
    if (file) *file++ = 0;
    cs = file ? strchr(file, '\n') : NULL;
    if (cs) *cs++ = 0;
    so_tabs(o, tabs, 4);
    so_kv(o, "Font", C_BLACK, fam);
    so_kv(o, "File", C_PURPLE, file);
    so_kv(o, "Pattern", C_GRAY, pat);
    so_par(o);
    so_row(o, 5, h, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    for (tok = cs ? strtok_r(cs, " \n", &save) : NULL; tok; tok = strtok_r(NULL, " \n", &save)) {
        unsigned long a = strtoul(tok, NULL, 16), e = strchr(tok, '-') ? strtoul(strchr(tok, '-') + 1, NULL, 16) : a;
        char c[4][24], sample[32] = "";
        const char *cells[5];
        snprintf(c[0], 24, "%d", n);
        snprintf(c[1], 24, "U+%04lX", a);
        snprintf(c[2], 24, "U+%04lX", e);
        snprintf(c[3], 24, "%lu", e - a + 1);
        if (a >= 0x20 && a < 0x110000 && !(a >= 0xD800 && a < 0xE000)) {   /* first glyph of the range as UTF-8 */
            unsigned long cp = (a == 0x20 || a == 0xA0) && e > a ? a + 1 : a;
            if (cp < 0x80) snprintf(sample, sizeof sample, "%c", (int)cp);
            else if (cp < 0x800) snprintf(sample, sizeof sample, "%c%c", (int)(0xC0 | cp >> 6), (int)(0x80 | (cp & 0x3F)));
            else if (cp < 0x10000) snprintf(sample, sizeof sample, "%c%c%c", (int)(0xE0 | cp >> 12), (int)(0x80 | (cp >> 6 & 0x3F)), (int)(0x80 | (cp & 0x3F)));
            else snprintf(sample, sizeof sample, "%c%c%c%c", (int)(0xF0 | cp >> 18), (int)(0x80 | (cp >> 12 & 0x3F)), (int)(0x80 | (cp >> 6 & 0x3F)), (int)(0x80 | (cp & 0x3F)));
        }
        cells[0] = c[0]; cells[1] = c[1]; cells[2] = c[2]; cells[3] = c[3]; cells[4] = sample;
        so_row(o, 5, cells, (const int[]){ C_GRAY, C_PURPLE, C_PURPLE, C_VALUE, C_BLACK });
        tot += (long)(e - a + 1);
        n++;
    }
    so_par(o);
    {
        char b[128];
        snprintf(b, sizeof b, "%d ranges, %ld characters supported.  Open as ranges:<font name> for another font", n, tot);
        so_note(o, b);
    }
    free(out);
}

/* ======================================================================
 * code pages: [codepage] [mlang] [nls-info]
 * ====================================================================== */
static const struct { int id; const char *desc, *iconv; } cpages[] = {
    { 37, "IBM EBCDIC US Canada", "IBM037" }, { 437, "OEM United States", NULL }, { 500, "IBM EBCDIC International", "IBM500" },
    { 708, "Arabic ASMO", "ASMO-708" }, { 720, "Arabic (Transparent ASMO)", NULL }, { 737, "OEM Greek 437G", NULL },
    { 775, "OEM Baltic", NULL }, { 850, "OEM Multilingual Latin 1", NULL }, { 852, "OEM Slovak Latin 2", NULL },
    { 855, "OEM Cyrillic", NULL }, { 857, "OEM Turkish", NULL }, { 860, "OEM Portuguese", NULL }, { 861, "OEM Icelandic", NULL },
    { 862, "OEM Hebrew", NULL }, { 863, "OEM Canadian French", NULL }, { 864, "OEM Arabic", NULL }, { 865, "OEM Nordic", NULL },
    { 866, "OEM Russian", NULL }, { 869, "OEM Greek", NULL }, { 874, "ANSI/OEM Thai", NULL }, { 875, "IBM EBCDIC Greek", "IBM875" },
    { 932, "ANSI/OEM Japanese Shift-JIS", NULL }, { 936, "ANSI/OEM Simplified Chinese GBK", NULL },
    { 949, "ANSI/OEM Korean Unified Hangul", NULL }, { 950, "ANSI/OEM Traditional Chinese Big5", NULL },
    { 1026, "IBM EBCDIC Latin 5 Turkish", "IBM1026" }, { 1250, "ANSI Eastern Europe", NULL }, { 1251, "ANSI Cyrillic", NULL },
    { 1252, "ANSI Latin 1", NULL }, { 1253, "ANSI Greek", NULL }, { 1254, "ANSI Turkish", NULL }, { 1255, "ANSI Hebrew", NULL },
    { 1256, "ANSI Arabic", NULL }, { 1257, "ANSI Baltic", NULL }, { 1258, "ANSI/OEM Viet Nam", NULL }, { 1361, "Korean Johab", "JOHAB" },
    { 10000, "Mac Roman", "MACINTOSH" }, { 10001, "Mac Japanese", "" }, { 10002, "Mac Traditional Chinese", "" },
    { 10003, "Mac Korean", "" }, { 10004, "Mac Arabic", "" }, { 10005, "Mac Hebrew", "" }, { 10006, "Mac Greek", "" },
    { 10007, "Mac Cyrillic", "MAC-CYRILLIC" }, { 10008, "Mac Simplified Chinese", "" }, { 10010, "Mac Romanian", "" },
    { 10017, "Mac Ukrainian", "MAC-UK" }, { 10021, "Mac Thai", "" }, { 10029, "Mac Latin 2", "MAC-CENTRALEUROPE" },
    { 10079, "Mac Icelandic", "MAC-IS" }, { 10081, "Mac Turkish", "" }, { 10082, "Mac Croatian", "" },
    { 20127, "US-ASCII (7bit)", "ASCII" }, { 20866, "Russian KOI8", "KOI8-R" }, { 20932, "EUC-JP", "EUC-JP" },
    { 20949, "Korean Wansung", "EUC-KR" }, { 21866, "Ukrainian KOI8", "KOI8-U" }, { 28591, "ISO 8859-1 Latin 1", "ISO-8859-1" },
    { 28592, "ISO 8859-2 Latin 2 (East European)", "ISO-8859-2" }, { 28593, "ISO 8859-3 Latin 3 (South European)", "ISO-8859-3" },
    { 28594, "ISO 8859-4 Latin 4 (Baltic old)", "ISO-8859-4" }, { 28595, "ISO 8859-5 Cyrillic", "ISO-8859-5" },
    { 28596, "ISO 8859-6 Arabic", "ISO-8859-6" }, { 28597, "ISO 8859-7 Greek", "ISO-8859-7" }, { 28598, "ISO 8859-8 Hebrew", "ISO-8859-8" },
    { 28599, "ISO 8859-9 Latin 5 (Turkish)", "ISO-8859-9" }, { 28603, "ISO 8859-13 Latin 7 (Baltic)", "ISO-8859-13" },
    { 28605, "ISO 8859-15 Latin 9 (Euro)", "ISO-8859-15" }, { 65001, "Unicode UTF-8", "UTF-8" }, { 1200, "Unicode UTF-16LE", "UTF-16LE" },
};
#define NCP ((int)(sizeof cpages / sizeof *cpages))

static int iconv_ok(const char *to, const char *from)
{
    iconv_t c = iconv_open(to, from);
    if (c == (iconv_t)-1) return 0;
    iconv_close(c);
    return 1;
}

/* glibc iconv name for a Windows code page id ("" = none) */
static const char *cp_iconv(int i, char *b, size_t sz)
{
    static const char *const fmts[] = { "CP%d", "IBM%d", "WINDOWS-%d", NULL };
    int k;
    if (cpages[i].iconv) return cpages[i].iconv;
    for (k = 0; fmts[k]; k++) {
        snprintf(b, sz, fmts[k], cpages[i].id);
        if (iconv_ok("UTF-8", b)) return b;
    }
    return "";
}

/* charset of the user's locale (from env, without touching the process locale) */
static void locale_charset(char *b, size_t sz)
{
    const char *l = getenv("LC_ALL");
    const char *d;
    if (!l || !*l) l = getenv("LC_CTYPE");
    if (!l || !*l) l = getenv("LANG");
    d = l ? strchr(l, '.') : NULL;
    if (d) { snprintf(b, sz, "%s", d + 1); b[strcspn(b, "@")] = 0; }
    else snprintf(b, sz, "ANSI_X3.4-1968");
    if (!strcasecmp(b, "utf8")) snprintf(b, sz, "UTF-8");
}

static void s_codepage(srs_out *o)
{
    static const int tabs[] = { 63, 336, 476 };
    static const char *const h[] = { "Page ID", "Description", "glibc iconv", "Supported" };
    int i, sup = 0;
    char b[64], cs[64];
    so_siv_header(o, "Code Pages (glibc iconv)");
    so_tabs(o, tabs, 3);
    so_row(o, 4, h, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (i = 0; i < NCP; i++) {
        const char *nm = cp_iconv(i, b, sizeof b);
        int ok = *nm && iconv_ok("UTF-8", nm);
        char id[16];
        snprintf(id, sizeof id, "%d", cpages[i].id);
        so_t(o, C_PURPLE, id);
        so_tab(o);
        so_t(o, C_BLACK, cpages[i].desc);
        so_tab(o);
        so_t(o, C_TEAL, *nm ? nm : "-");
        so_tab(o);
        so_check(o, ok);
        so_par(o);
        sup += ok;
    }
    so_par(o);
    locale_charset(cs, sizeof cs);
    snprintf(b, sizeof b, "%d of %d supported; locale charset %s", sup, NCP, cs);
    so_note(o, b);
    {
        char **e;
        int n = dir_list("/usr/share/i18n/charmaps", &e);
        snprintf(b, sizeof b, "%d glibc charmaps in /usr/share/i18n/charmaps (see [nls-info])", n > 0 ? n : 0);
        so_note(o, b);
        dir_free(e, n > 0 ? n : 0);
    }
}

static void s_mlang(srs_out *o)
{
    static const int tabs[] = { 77, 196, 406 };
    static const char *const h[] = { "Identifier", "UTF-16 UTF-8 ACP", "Codepage Description", "iconv name" };
    char cs[64], b[160];
    int i;
    locale_charset(cs, sizeof cs);
    snprintf(b, sizeof b, "Conversions to UTF-16, UTF-8 and ACP (%s) via glibc iconv", cs);
    so_siv_header(o, b);
    so_tabs(o, tabs, 3);
    so_row(o, 4, h, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (i = 0; i < NCP; i++) {
        char nb[64], id[16];
        const char *nm = cp_iconv(i, nb, sizeof nb);
        int a = *nm && iconv_ok("UTF-16", nm), u = *nm && iconv_ok("UTF-8", nm), c = *nm && iconv_ok(cs, nm);
        snprintf(id, sizeof id, "%d", cpages[i].id);
        so_t(o, C_PURPLE, id);
        so_tab(o);
        so_check(o, a); so_check(o, u); so_check(o, c);
        so_tab(o);
        so_f(o, C_BLACK, "<%s>", cpages[i].desc);
        so_tab(o);
        so_t(o, C_TEAL, *nm ? nm : "-");
        so_par(o);
    }
    so_par(o);
    so_note(o, "Note  [_][_][_] show if the codepage is supported for conversion to UTF-16, UTF-8 or the locale charset (ACP).");
}

static void s_nls_info(srs_out *o)
{
    static const int tabs[] = { 250, 500, 750 };
    char **e, cs[64];
    int n = dir_list("/usr/share/i18n/charmaps", &e), i;
    so_siv_header(o, "NLS Code Pages (glibc charmaps)");
    so_tabs(o, tabs, 3);
    locale_charset(cs, sizeof cs);
    so_kv(o, "ACP (locale charset)", C_PURPLE, cs);
    so_kv(o, "Charmap directory", C_GRAY, "/usr/share/i18n/charmaps");
    so_kvf(o, "Charmaps", C_VALUE, "%d", n > 0 ? n : 0);
    so_kv(o, "Compiled locales", C_GRAY, path_exists("/usr/lib/locale/locale-archive") ? "/usr/lib/locale/locale-archive" : "/usr/lib/locale/*");
    so_par(o);
    for (i = 0; i < n; i++) {
        char nm[128];
        snprintf(nm, sizeof nm, "%s", e[i]);
        if (strlen(nm) > 3 && !strcmp(nm + strlen(nm) - 3, ".gz")) nm[strlen(nm) - 3] = 0;
        if (i % 4) so_tab(o);
        so_check(o, iconv_ok("UTF-8", nm));
        so_t(o, C_BLACK, nm);
        if (i % 4 == 3 || i == n - 1) so_par(o);
    }
    so_par(o);
    so_note(o, "[=] = also usable by iconv(3) under that name");
    dir_free(e, n > 0 ? n : 0);
}

/* ======================================================================
 * [locales] [country] [zones] [key-names]
 * ====================================================================== */
/* "en_US.UTF-8" == "en_US.utf8" */
static void loc_norm(const char *s, char *d, size_t sz)
{
    size_t k = 0;
    for (; *s && k + 1 < sz; s++) if (*s != '-' && *s != '_') d[k++] = (char)tolower((unsigned char)*s);
    d[k] = 0;
}
static int loc_same(const char *a, const char *b)
{
    char x[128], y[128];
    loc_norm(a, x, sizeof x);
    loc_norm(b, y, sizeof y);
    return !strcmp(x, y);
}

/* one block of `locale -a -v` */
typedef struct { char name[64], title[160], lang[64], terr[64], cset[32]; } locinfo;

static void loc_emit(srs_out *o, locinfo *li, const char *cur, int *n)
{
    const char *cells[5];
    char cc[8] = "", ll[16] = "", code[32];
    const char *us = strchr(li->name, '_');
    if (!*li->name) return;
    snprintf(ll, sizeof ll, "%.*s", (int)strcspn(li->name, "_.@"), li->name);
    if (us) snprintf(cc, sizeof cc, "%.*s", (int)strcspn(us + 1, ".@"), us + 1);
    snprintf(code, sizeof code, "%s%s%s", ll, *cc ? "-" : "", cc);
    cells[0] = li->name; cells[1] = *li->title ? li->title : "-"; cells[2] = code; cells[3] = *li->cset ? li->cset : "-";
    cells[4] = *li->terr ? li->terr : *li->lang ? li->lang : "";
    so_row(o, 5, cells, (const int[]){ cur && loc_same(li->name, cur) ? C_GREEN : C_BLACK, C_VALUE, C_PURPLE, C_TEAL, C_GRAY });
    (*n)++;
}

static void s_locales(srs_out *o)
{
    static const int tabs[] = { 210, 640, 714, 840 };
    static const char *const h[] = { "Locale", "Description", "Code", "Code Set", "Country" };
    char *t = sh("locale -a -v"), *save = NULL, *l, b[96];
    const char *cur = getenv("LC_ALL");
    locinfo li;
    int n = 0;
    if (!cur || !*cur) cur = getenv("LANG");
    so_siv_header(o, "System Locales");
    so_tabs(o, tabs, 4);
    so_b(o, C_VALUE, "Current settings (localectl status / environment)");
    so_par(o);
    sh_lines(o, "localectl status --no-pager", C_PURPLE, 20);
    {
        static const char *const lc[] = { "LANG", "LANGUAGE", "LC_ALL", "LC_CTYPE", "LC_NUMERIC", "LC_TIME", "LC_COLLATE", "LC_MONETARY",
                                          "LC_MESSAGES", "LC_PAPER", "LC_MEASUREMENT", NULL };
        int i;
        for (i = 0; lc[i]; i++) if (getenv(lc[i])) so_kv(o, lc[i], C_TEAL, *getenv(lc[i]) ? getenv(lc[i]) : "(empty)");
    }
    so_par(o);
    so_row(o, 5, h, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    memset(&li, 0, sizeof li);
    for (l = strtok_r(t, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        char *bar;
        if (!strncmp(l, "locale: ", 8)) {
            loc_emit(o, &li, cur, &n);
            memset(&li, 0, sizeof li);
            sscanf(l + 8, "%63s", li.name);
            continue;
        }
        if (!(bar = strstr(l, " | "))) continue;
        *bar = 0;
        l = ltrim(l);
        if (!strcmp(l, "title")) snprintf(li.title, sizeof li.title, "%s", bar + 3);
        else if (!strcmp(l, "language")) snprintf(li.lang, sizeof li.lang, "%s", bar + 3);
        else if (!strcmp(l, "territory")) snprintf(li.terr, sizeof li.terr, "%s", bar + 3);
        else if (!strcmp(l, "codeset")) snprintf(li.cset, sizeof li.cset, "%s", bar + 3);
    }
    loc_emit(o, &li, cur, &n);
    so_par(o);
    snprintf(b, sizeof b, "Showing %d compiled locales (locale -a -v)", n);
    so_note(o, b);
    free(t);
}

static void cur_country(char *cc, size_t sz)
{
    const char *l = getenv("LC_ALL");
    const char *u;
    if (!l || !*l) l = getenv("LANG");
    *cc = 0;
    if (l && (u = strchr(l, '_'))) snprintf(cc, sz, "%.2s", u + 1);
}

static void s_country(srs_out *o)
{
    static const int tabs[] = { 49, 350, 420 };
    static const char *const h[] = { "Code", "Country", "Zones", "Representative Time Zone" };
    char *t, *z = NULL, *save = NULL, *l, cc[8], b[96];
    int n = 0;
    cur_country(cc, sizeof cc);
    so_siv_header(o, "Country List (ISO 3166)");
    so_tabs(o, tabs, 3);
    if (rd_all("/usr/share/zoneinfo/iso3166.tab", &t) < 0) { so_err(o, "/usr/share/zoneinfo/iso3166.tab not readable"); return; }
    if (rd_all("/usr/share/zoneinfo/zone.tab", &z) < 0) z = NULL;
    so_row(o, 4, h, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (l = strtok_r(t, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        char code[8], *tab = strchr(l, '\t'), zn[16], first[96] = "";
        const char *cells[4], *p;
        int cnt = 0;
        if (*l == '#' || !tab) continue;
        snprintf(code, sizeof code, "%.*s", (int)(tab - l), l);
        for (p = z; p && *p;) {   /* zone.tab: CC <tab> coords <tab> TZ ... */
            if (!strncmp(p, code, 2) && p[2] == '\t') {
                const char *c2 = strchr(p + 3, '\t');
                cnt++;
                if (c2 && !*first) snprintf(first, sizeof first, "%.*s", (int)strcspn(c2 + 1, "\t\n"), c2 + 1);
            }
            p = strchr(p, '\n');
            if (p) p++;
        }
        snprintf(zn, sizeof zn, "%d", cnt);
        cells[0] = code; cells[1] = tab + 1; cells[2] = zn; cells[3] = first;
        so_row(o, 4, cells, (const int[]){ !strcmp(code, cc) ? C_GREEN : C_PURPLE, C_BLACK, C_VALUE, C_GRAY });
        n++;
    }
    so_par(o);
    snprintf(b, sizeof b, "%d countries; current locale territory %s", n, *cc ? cc : "-");
    so_note(o, b);
    free(t);
    free(z);
}

static void cur_tz(char *tz, size_t sz)
{
    char l[256] = "";
    ssize_t r = readlink("/etc/localtime", l, sizeof l - 1);
    *tz = 0;
    if (r > 0) { char *z; l[r] = 0; z = strstr(l, "zoneinfo/"); snprintf(tz, sz, "%s", z ? z + 9 : l); }
    if (getenv("TZ") && *getenv("TZ")) snprintf(tz, sz, "%s", getenv("TZ") + (getenv("TZ")[0] == ':'));
}

static void s_zones(srs_out *o)
{
    static const int tabs[] = { 210, 340, 600 };
    static const char *const h[] = { "Countries", "Coordinates", "Time Zone", "Comments" };
    char *t, *save = NULL, *l, tz[128], b[160];
    int n = 0;
    cur_tz(tz, sizeof tz);
    so_siv_header(o, "Time Zones (tzdata zone1970.tab)");
    so_tabs(o, tabs, 3);
    so_kv(o, "Current", C_GREEN, *tz ? tz : "UTC");
    sh_line("timedatectl show -p Timezone --value", b, sizeof b);
    so_kv(o, "timedatectl", C_TEAL, b);
    so_par(o);
    if (rd_all("/usr/share/zoneinfo/zone1970.tab", &t) < 0) { so_err(o, "zone1970.tab not readable"); return; }
    so_row(o, 4, h, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (l = strtok_r(t, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        char *f[4] = { 0 }, *p = l;
        int i;
        if (*l == '#') continue;
        for (i = 0; i < 4 && p; i++) { f[i] = p; p = strchr(p, '\t'); if (p) *p++ = 0; }
        if (i < 3) continue;
        so_row(o, 4, (const char *const[]){ f[0], f[1], f[2], f[3] ? f[3] : "" },
               (const int[]){ C_PURPLE, C_GRAY, !strcmp(f[2], tz) ? C_GREEN : C_BLACK, C_VALUE });
        n++;
    }
    so_par(o);
    snprintf(b, sizeof b, "%d time zones", n);
    so_note(o, b);
    free(t);
}

static void s_key_names(srs_out *o)
{
    static const int tabs[] = { 42, 175, 217, 350, 392, 525, 567 };
    char *t, *save = NULL, *l, *names[128] = { 0 }, xkb[128] = "";
    int i;
    so_siv_header(o, "Key Names (Linux input key codes = PC scan codes set 1)");
    if (rd_all("/usr/include/linux/input-event-codes.h", &t) < 0) { so_err(o, "/usr/include/linux/input-event-codes.h not found (linux-api-headers)"); return; }
    for (l = strtok_r(t, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        char nm[64];
        int v;
        if (sscanf(l, "#define KEY_%63s %i", nm, &v) == 2 && v >= 0 && v < 128 && !names[v]) names[v] = strdup(nm);
    }
    free(t);
    sh_line("localectl status --no-pager | grep -E 'X11 Layout|VC Keymap' | tr -s ' ' | paste -sd ';'", xkb, sizeof xkb);
    so_tabs(o, tabs, 7);
    so_row(o, 8, (const char *const[]){ "Scan", "Key Name", "Scan", "Key Name", "Scan", "Key Name", "Scan", "Key Name" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (i = 0; i < 32; i++) {
        int c;
        for (c = 0; c < 4; c++) {
            char sc[8];
            int k = c * 32 + i;
            if (c) so_tab(o);
            snprintf(sc, sizeof sc, "%d", k);
            so_t(o, C_PURPLE, sc);
            so_tab(o);
            so_t(o, C_BLACK, names[k] ? names[k] : "");
        }
        so_par(o);
    }
    so_par(o);
    if (*xkb) so_note(o, ltrim(xkb));
    so_note(o, "Names from linux/input-event-codes.h (KEY_*); keysyms depend on the active XKB layout");
    for (i = 0; i < 128; i++) free(names[i]);
}

/* ======================================================================
 * accounts: [users] [groups] [sessions] [rdp-info] [priv] [passwords] [files]
 * ====================================================================== */
/* "KEY value" files (login.defs, pacman.conf "Key = value") */
static int kv_space(const char *path, const char *key, char *buf, size_t sz)
{
    char *t, *save = NULL, *l;
    size_t kl = strlen(key);
    int ok = 0;
    *buf = 0;
    if (rd_all(path, &t) < 0) return 0;
    for (l = strtok_r(t, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        l = ltrim(l);
        if (!strncmp(l, key, kl) && (l[kl] == ' ' || l[kl] == '\t' || l[kl] == '=')) {
            char *v = ltrim(l + kl);
            if (*v == '=') v = ltrim(v + 1);
            snprintf(buf, sz, "%s", v);
            rtrim(buf);
            ok = 1;
            break;
        }
    }
    free(t);
    return ok;
}

static void s_users(srs_out *o)
{
    static const int tabs[] = { 140, 196, 252, 450, 700 };
    static const char *const h[] = { "User Name", "UID", "GID", "Full Name", "Home", "Shell" };
    char b[128], umin[16] = "1000", list[4096] = "";
    unsigned min;
    int i, sys = 0;
    pw_load();
    kv_space("/etc/login.defs", "UID_MIN", umin, sizeof umin);
    min = (unsigned)atol(umin);
    so_siv_header(o, "Users (/etc/passwd)");
    so_tabs(o, tabs, 5);
    so_row(o, 6, h, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (i = 0; i < g_npw; i++) {
        char u[16], g[16], gec[128];
        if (g_pw[i].uid < min || g_pw[i].uid == 65534) { sys++; continue; }
        snprintf(u, sizeof u, "%u", g_pw[i].uid);
        snprintf(g, sizeof g, "%u", g_pw[i].gid);
        snprintf(gec, sizeof gec, "%.*s", (int)strcspn(g_pw[i].gecos, ","), g_pw[i].gecos);
        so_row(o, 6, (const char *const[]){ g_pw[i].name, u, g, gec, g_pw[i].home, g_pw[i].shell },
               (const int[]){ g_pw[i].uid == getuid() ? C_GREEN : C_BLACK, C_PURPLE, C_PURPLE, C_VALUE, C_GRAY, C_TEAL });
    }
    so_par(o);
    for (i = 0; i < g_npw; i++)
        if (g_pw[i].uid < min || g_pw[i].uid == 65534) {
            if (*list) strncat(list, ", ", sizeof list - strlen(list) - 1);
            strncat(list, g_pw[i].name, sizeof list - strlen(list) - 1);
        }
    snprintf(b, sizeof b, "System accounts (%d, UID < %u or nobody)", sys, min);
    so_b(o, C_VALUE, b);
    so_par(o);
    {   /* wrap the comma list at ~110 chars */
        char *p = list;
        while (*p) {
            size_t l = strlen(p), cut = l;
            if (l > 110) { cut = 110; while (cut > 0 && p[cut] != ' ') cut--; if (!cut) cut = 110; }
            so_f(o, C_GRAY, "%.*s", (int)cut, p);
            so_par(o);
            p += cut;
            while (*p == ' ') p++;
        }
    }
    so_par(o);
    so_b(o, C_VALUE, "Account policy (/etc/login.defs)");
    so_par(o);
    {
        static const char *const k[] = { "UID_MIN", "UID_MAX", "SYS_UID_MAX", "GID_MIN", "PASS_MAX_DAYS", "PASS_MIN_DAYS", "PASS_WARN_AGE",
                                          "ENCRYPT_METHOD", "UMASK", "HOME_MODE", "USERGROUPS_ENAB", NULL };
        for (i = 0; k[i]; i++) if (kv_space("/etc/login.defs", k[i], b, sizeof b)) so_kv(o, k[i], C_PURPLE, b);
    }
    so_note(o, "Password hashes (/etc/shadow) are never read");
}

static void s_groups(srs_out *o)
{
    static const int tabs[] = { 196, 252 };
    static const char *const h[] = { "Group Name", "GID", "Members" };
    gid_t mine[256];
    int nm = getgroups(256, mine), n = 0, i;
    char *t, *save = NULL, *l, b[96];
    so_siv_header(o, "Groups (/etc/group)");
    so_tabs(o, tabs, 2);
    if (rd_all("/etc/group", &t) < 0) { so_err(o, "/etc/group not readable"); return; }
    so_row(o, 3, h, (const int[]){ C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (l = strtok_r(t, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        char *f[4] = { 0 }, *p = l;
        int k, in = 0;
        for (k = 0; k < 4 && p; k++) { f[k] = p; p = strchr(p, ':'); if (p) *p++ = 0; }
        if (k < 3) continue;
        for (i = 0; i < nm; i++) if ((gid_t)atol(f[2]) == mine[i]) in = 1;
        so_row(o, 3, (const char *const[]){ f[0], f[2], f[3] ? f[3] : "" }, (const int[]){ in ? C_GREEN : C_BLACK, C_PURPLE, C_VALUE });
        n++;
    }
    free(t);
    so_par(o);
    snprintf(b, sizeof b, "%d groups; green = groups of the current process (%d)", n, nm);
    so_note(o, b);
}

static void s_sessions(srs_out *o)
{
    static const int tabs[] = { 70, 140, 240, 320, 420, 490 };
    so_siv_header(o, "Sessions (systemd-logind)");
    so_tabs(o, tabs, 6);
    so_b(o, C_VALUE, "Login sessions  (loginctl list-sessions)");
    so_par(o);
    so_row(o, 7, (const char *const[]){ "Session", "UID", "User", "Seat", "Leader", "Class", "TTY / Idle / Since" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    {
        char *t = sh("loginctl list-sessions --no-pager --no-legend"), *save = NULL, *l;
        for (l = strtok_r(t, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
            char f[6][64] = { "" };
            int off = 0;
            if (sscanf(ltrim(l), "%63s %63s %63s %63s %63s %63s %n", f[0], f[1], f[2], f[3], f[4], f[5], &off) < 6) continue;
            so_row(o, 7, (const char *const[]){ f[0], f[1], f[2], f[3], f[4], f[5], ltrim(l) + off },
                   (const int[]){ C_PURPLE, C_VALUE, C_BLACK, C_TEAL, C_GRAY, C_VALUE, C_GRAY });
        }
        free(t);
    }
    so_par(o);
    so_b(o, C_VALUE, "Users with sessions  (loginctl list-users)");
    so_par(o);
    sh_lines(o, "loginctl list-users --no-pager --no-legend", C_VALUE, 50);
    so_par(o);
    so_b(o, C_VALUE, "Inbound SSH connections  (ss state established sport :22)");
    so_par(o);
    if (!sh_lines(o, "ss -Htn state established '( sport = :22 )'", C_VALUE, 50)) so_note(o, "none");
    so_par(o);
    so_b(o, C_VALUE, "SMB sessions  (smbstatus -b)");
    so_par(o);
    if (so_root_file("smbstatus.txt")) { char *t; if (rd_all(so_root_file("smbstatus.txt"), &t) >= 0) { so_lines(o, C_VALUE, t); free(t); } }
    else so_note(o, "Needs root: Menu > Run as root (srs-root)");
}

static void s_rdp_info(srs_out *o)
{
    static const int tabs[] = { 56, 140, 210, 280, 350, 420, 560, 660 };
    static const char *const props[] = { "Name", "Type", "Class", "State", "Remote", "RemoteHost", "Service", "TTY" };
    char *t = sh("loginctl list-sessions --no-pager --no-legend"), *save = NULL, *l;
    int i, n = 0;
    so_siv_header(o, "Session Information (systemd-logind)");
    so_tabs(o, tabs, 8);
    so_row(o, 9, (const char *const[]){ "SID", "User Name", "Type", "Class", "State", "Remote", "Remote Host", "Service", "TTY" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (l = strtok_r(t, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        char sid[32], *out = NULL, v[8][96];
        const char *argv[] = { "timeout", "5", "loginctl", "show-session", sid, "--no-pager", "-p", "Name", "-p", "Type", "-p", "Class", "-p", "State",
                               "-p", "Remote", "-p", "RemoteHost", "-p", "Service", "-p", "TTY", NULL };
        const char *cells[9];
        if (sscanf(ltrim(l), "%31s", sid) != 1 || !safe_arg(sid)) continue;
        run_out(&out, argv);
        for (i = 0; i < 8; i++) {
            char key[32], *p;
            snprintf(key, sizeof key, "%s=", props[i]);
            v[i][0] = 0;
            for (p = out; p && *p; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : NULL)
                if (!strncmp(p, key, strlen(key))) { snprintf(v[i], sizeof v[i], "%.*s", (int)strcspn(p + strlen(key), "\n"), p + strlen(key)); break; }
        }
        free(out);
        cells[0] = sid;
        for (i = 0; i < 8; i++) cells[i + 1] = *v[i] ? v[i] : "-";
        so_row(o, 9, cells, (const int[]){ C_PURPLE, C_BLACK, C_TEAL, C_VALUE, C_GREEN, !strcmp(v[4], "yes") ? C_ORANGE : C_VALUE, C_VALUE, C_GRAY, C_GRAY });
        n++;
    }
    free(t);
    if (!n) so_note(o, "no sessions");
    so_par(o);
    so_b(o, C_VALUE, "Remote desktop / remote login services");
    so_par(o);
    so_tabs(o, (const int[]){ 245 }, 1);
    {
        static const char *const units[] = { "sshd.service", "xrdp.service", "gnome-remote-desktop.service", "wayvnc.service", "x11vnc.service",
                                             "vncserver@:1.service", NULL };
        for (i = 0; units[i]; i++) {
            char *a = NULL, *e = NULL;
            const char *aa[] = { "timeout", "5", "systemctl", "is-active", units[i], NULL };
            const char *ee[] = { "timeout", "5", "systemctl", "is-enabled", units[i], NULL };
            run_out(&a, aa);
            run_out(&e, ee);
            if (a) rtrim(a);
            if (e) rtrim(e);
            so_kvf(o, units[i], a && !strcmp(a, "active") ? C_ORANGE : C_GRAY, "%s / %s", a && *a ? a : "-", e && *e ? e : "not installed");
            free(a);
            free(e);
        }
        {
            char *u = NULL;
            const char *aa[] = { "timeout", "5", "systemctl", "--user", "is-active", "app-org.kde.krdpserver.service", "plasma-krdp_server.service", NULL };
            run_out(&u, aa);
            if (u) { for (char *p = u; *p; p++) if (*p == '\n') *p = ' '; rtrim(u); }
            so_kv(o, "KDE krdp (user)", C_GRAY, u && *u ? u : "-");
            free(u);
        }
    }
}

static const char *const cap_names[] = {
    "CAP_CHOWN", "CAP_DAC_OVERRIDE", "CAP_DAC_READ_SEARCH", "CAP_FOWNER", "CAP_FSETID", "CAP_KILL", "CAP_SETGID", "CAP_SETUID",
    "CAP_SETPCAP", "CAP_LINUX_IMMUTABLE", "CAP_NET_BIND_SERVICE", "CAP_NET_BROADCAST", "CAP_NET_ADMIN", "CAP_NET_RAW", "CAP_IPC_LOCK",
    "CAP_IPC_OWNER", "CAP_SYS_MODULE", "CAP_SYS_RAWIO", "CAP_SYS_CHROOT", "CAP_SYS_PTRACE", "CAP_SYS_PACCT", "CAP_SYS_ADMIN",
    "CAP_SYS_BOOT", "CAP_SYS_NICE", "CAP_SYS_RESOURCE", "CAP_SYS_TIME", "CAP_SYS_TTY_CONFIG", "CAP_MKNOD", "CAP_LEASE",
    "CAP_AUDIT_WRITE", "CAP_AUDIT_CONTROL", "CAP_SETFCAP", "CAP_MAC_OVERRIDE", "CAP_MAC_ADMIN", "CAP_SYSLOG", "CAP_WAKE_ALARM",
    "CAP_BLOCK_SUSPEND", "CAP_AUDIT_READ", "CAP_PERFMON", "CAP_BPF", "CAP_CHECKPOINT_RESTORE",
};

static void s_priv(srs_out *o)
{
    static const int tabs[] = { 245, 301, 357, 413, 469 };
    static const char *const sets[] = { "CapEff", "CapPrm", "CapInh", "CapBnd", "CapAmb" };
    unsigned long long cap[5] = { 0 };
    char *t, exe[512] = "", b[256] = "", nnp[16] = "", sc[16] = "";
    unsigned i;
    ssize_t r = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (r > 0) exe[r] = 0;
    if (rd_all("/proc/self/status", &t) >= 0) {
        int k;
        for (k = 0; k < 5; k++) { char *p = strstr(t, sets[k]); if (p) cap[k] = strtoull(p + strlen(sets[k]) + 1, NULL, 16); }
        { char *p = strstr(t, "NoNewPrivs:"); if (p) snprintf(nnp, sizeof nnp, "%.*s", (int)strcspn(ltrim(p + 11), "\n"), ltrim(p + 11)); }
        { char *p = strstr(t, "Seccomp:"); if (p) snprintf(sc, sizeof sc, "%.*s", (int)strcspn(ltrim(p + 8), "\n"), ltrim(p + 8)); }
        free(t);
    }
    so_tabs(o, tabs, 5);
    snprintf(b, sizeof b, "User %s (uid %u euid %u gid %u egid %u)", uid_name((unsigned)getuid()), (unsigned)getuid(), (unsigned)geteuid(),
             (unsigned)getgid(), (unsigned)getegid());
    so_t(o, C_BLACK, b);
    so_tab(o);
    so_tab(o);
    so_f(o, C_GRAY, "Image %s", exe);
    so_par(o);
    so_f(o, C_GRAY, "NoNewPrivs %s  Seccomp %s  (0=off 1=strict 2=filter)", nnp, sc);
    so_par(o);
    so_par(o);
    snprintf(b, sizeof b, "  Capability - Showing %d", (int)(sizeof cap_names / sizeof *cap_names));
    so_row(o, 6, (const char *const[]){ b, "Effect", "Permit", "Inherit", "Bound", "Ambient" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (i = 0; i < sizeof cap_names / sizeof *cap_names; i++) {
        int k;
        so_t(o, (cap[0] >> i) & 1 ? C_GREEN : C_BLACK, "  ");
        so_t(o, (cap[0] >> i) & 1 ? C_GREEN : C_BLACK, cap_names[i]);
        for (k = 0; k < 5; k++) { so_tab(o); so_check(o, (int)((cap[k] >> i) & 1)); }
        so_par(o);
    }
    so_par(o);
    so_note(o, "Linux capabilities of this process (/proc/self/status Cap*) - the equivalent of Windows privileges");
}

static void s_passwords(srs_out *o)
{
    static const int tabs[] = { 245, 350 };
    static const struct { const char *name, *rel; } stores[] = {
        { "KWallet (KDE)", ".local/share/kwalletd" }, { "GNOME Keyring", ".local/share/keyrings" }, { "pass", ".password-store" },
        { "GnuPG", ".gnupg" }, { "SSH keys", ".ssh" }, { "KeePassXC config", ".config/keepassxc" }, { "Git credentials", ".git-credentials" },
    };
    static const char *const daemons[] = { "kwalletd6", "kwalletd5", "gnome-keyring-d", "keepassxc", "ssh-agent", "gpg-agent", NULL };
    const char *home = getenv("HOME");
    proc_t *v;
    int n, i, k;
    so_siv_header(o, "Credential Stores");
    so_tabs(o, tabs, 2);
    so_b(o, C_VALUE, "Stores present  (presence only - contents are never read)");
    so_par(o);
    for (i = 0; i < (int)(sizeof stores / sizeof *stores); i++) {
        char p[512];
        snprintf(p, sizeof p, "%s/%s", home ? home : "", stores[i].rel);
        so_t(o, C_BLACK, stores[i].name);
        so_tab(o);
        so_check(o, path_exists(p));
        so_tab(o);
        so_t(o, C_GRAY, p);
        so_par(o);
    }
    so_par(o);
    so_b(o, C_VALUE, "Secret service / agent processes");
    so_par(o);
    n = proc_scan(&v);
    for (k = 0; daemons[k]; k++) {
        int pid = 0;
        for (i = 0; i < n; i++) if (!strcmp(v[i].comm, daemons[k]) && v[i].uid == getuid()) { pid = v[i].pid; break; }
        so_t(o, C_BLACK, daemons[k]);
        so_tab(o);
        so_check(o, pid != 0);
        if (pid) { so_tab(o); so_f(o, C_PURPLE, "PID %d", pid); }
        so_par(o);
    }
    free(v);
}

static void s_files(srs_out *o)
{
    so_siv_header(o, "Open Files on Shared Resources / File Locks");
    so_b(o, C_VALUE, "File locks  (lslocks)");
    so_par(o);
    if (!sh_lines(o, "lslocks -o COMMAND,PID,TYPE,MODE,M,PATH", C_VALUE, 400)) so_note(o, "no locks or lslocks unavailable");
    so_par(o);
    so_b(o, C_VALUE, "SMB open files  (smbstatus -L)");
    so_par(o);
    if (so_root_file("smbstatus.txt")) { char *t; if (rd_all(so_root_file("smbstatus.txt"), &t) >= 0) { so_lines(o, C_VALUE, t); free(t); } }
    else so_note(o, "Needs root: Menu > Run as root (srs-root)");
    so_par(o);
    so_b(o, C_VALUE, "NFS exports  (/var/lib/nfs/etab)");
    so_par(o);
    {
        char *t = NULL;
        if (rd_all("/var/lib/nfs/etab", &t) >= 0 && t && *t) so_lines(o, C_VALUE, t);
        else so_note(o, "none / not readable");
        free(t);
    }
}

/* ======================================================================
 * [folders] XDG paths
 * ====================================================================== */
static void folder_row(srs_out *o, const char *name, const char *path)
{
    so_t(o, C_BLACK, name);
    so_tab(o);
    so_check(o, path_exists(path));
    so_t(o, C_PURPLE, path);
    so_par(o);
}

static void s_folders(srs_out *o)
{
    static const int tabs[] = { 245 };
    const char *home = getenv("HOME") ? getenv("HOME") : "";
    char p[1024], *t, *save = NULL, *l, b[160];
    static const struct { const char *name, *env, *def; } base[] = {
        { "PROFILE (HOME)", "HOME", "" }, { "APPDATA (XDG_CONFIG_HOME)", "XDG_CONFIG_HOME", "/.config" },
        { "LOCAL_APPDATA (XDG_DATA_HOME)", "XDG_DATA_HOME", "/.local/share" }, { "STATE (XDG_STATE_HOME)", "XDG_STATE_HOME", "/.local/state" },
        { "INTERNET_CACHE (XDG_CACHE_HOME)", "XDG_CACHE_HOME", "/.cache" }, { "RUNTIME (XDG_RUNTIME_DIR)", "XDG_RUNTIME_DIR", NULL },
    };
    int i;
    snprintf(b, sizeof b, "Folder Paths for %s", uid_name((unsigned)getuid()));
    so_siv_header(o, b);
    so_tabs(o, tabs, 1);
    so_row(o, 2, (const char *const[]){ "Name", "Folder Path" }, (const int[]){ C_LABEL, C_LABEL });
    so_par(o);
    for (i = 0; i < (int)(sizeof base / sizeof *base); i++) {
        const char *e = getenv(base[i].env);
        if (e && *e) folder_row(o, base[i].name, e);
        else if (base[i].def) { snprintf(p, sizeof p, "%s%s", home, base[i].def); folder_row(o, base[i].name, p); }
    }
    snprintf(p, sizeof p, "%s/.config/user-dirs.dirs", home);
    if (rd_all(p, &t) >= 0) {
        for (l = strtok_r(t, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
            char nm[64], val[512], *eq = strchr(l, '='), *v;
            if (*l == '#' || !eq || strncmp(l, "XDG_", 4)) continue;
            snprintf(nm, sizeof nm, "%.*s", (int)(eq - l - 4), l + 4);
            if (strlen(nm) > 4 && !strcmp(nm + strlen(nm) - 4, "_DIR")) nm[strlen(nm) - 4] = 0;
            v = eq + 1;
            if (*v == '"') v++;
            snprintf(val, sizeof val, "%s", v);
            val[strcspn(val, "\"")] = 0;
            if (!strncmp(val, "$HOME", 5)) snprintf(p, sizeof p, "%s%s", home, val + 5); else snprintf(p, sizeof p, "%s", val);
            folder_row(o, nm, p);
        }
        free(t);
    }
    snprintf(p, sizeof p, "%s/.local/share/fonts", home);           folder_row(o, "FONTS", p);
    snprintf(p, sizeof p, "%s/.local/share/applications", home);    folder_row(o, "PROGRAMS", p);
    snprintf(p, sizeof p, "%s/.config/autostart", home);            folder_row(o, "STARTUP", p);
    snprintf(p, sizeof p, "%s/.local/share/Trash", home);           folder_row(o, "TRASH", p);
    snprintf(p, sizeof p, "%s/.local/share/recently-used.xbel", home); folder_row(o, "RECENT", p);
    folder_row(o, "COMMON_PROGRAMS", "/usr/share/applications");
    folder_row(o, "COMMON_STARTUP", "/etc/xdg/autostart");
    folder_row(o, "COMMON_APPDATA", "/etc/xdg");
    folder_row(o, "COMMON_FONTS", "/usr/share/fonts");
    folder_row(o, "PROGRAM_FILES", "/usr/bin");
    folder_row(o, "PROGRAM_FILES_OPT", "/opt");
    folder_row(o, "SYSTEM", "/usr/lib");
    folder_row(o, "TEMP", getenv("TMPDIR") && *getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    {
        const char *x = getenv("XDG_DATA_DIRS"), *c = getenv("XDG_CONFIG_DIRS");
        so_par(o);
        so_kv(o, "XDG_DATA_DIRS", C_GRAY, x ? x : "/usr/local/share:/usr/share");
        so_kv(o, "XDG_CONFIG_DIRS", C_GRAY, c ? c : "/etc/xdg");
    }
}

/* ======================================================================
 * [startup] [software] [setup] [pending] [esu-sts]
 * ====================================================================== */
static void autostart_dir(srs_out *o, const char *dir, int user)
{
    char **e;
    int n = dir_list(dir, &e), i;
    for (i = 0; i < n; i++) {
        char p[1024], nm[160] = "", ex[512] = "", hid[16] = "", gen[16] = "", only[96] = "";
        int on;
        if (!strstr(e[i], ".desktop")) continue;
        snprintf(p, sizeof p, "%s/%s", dir, e[i]);
        kv_file(p, "Name", nm, sizeof nm);
        kv_file(p, "Exec", ex, sizeof ex);
        kv_file(p, "Hidden", hid, sizeof hid);
        kv_file(p, "X-GNOME-Autostart-enabled", gen, sizeof gen);
        kv_file(p, "OnlyShowIn", only, sizeof only);
        on = strcasecmp(hid, "true") && strcasecmp(gen, "false");
        if (*only) {   /* OnlyShowIn=KDE;GNOME; vs XDG_CURRENT_DESKTOP=KDE (colon list) */
            char cd[128], *sv = NULL, *d;
            int match = 0;
            snprintf(cd, sizeof cd, "%s", getenv("XDG_CURRENT_DESKTOP") ? getenv("XDG_CURRENT_DESKTOP") : "");
            for (d = strtok_r(cd, ":", &sv); d; d = strtok_r(NULL, ":", &sv)) {
                char pat[160];
                snprintf(pat, sizeof pat, ";%s;", d);
                {
                    char lst[128];
                    snprintf(lst, sizeof lst, ";%s;", only);
                    if (strstr(lst, pat)) match = 1;
                }
            }
            if (!match) on = 0;
        }
        if (!user) {   /* overridden by a same-named user file? */
            char up[1024];
            snprintf(up, sizeof up, "%s/.config/autostart/%s", getenv("HOME") ? getenv("HOME") : "", e[i]);
            if (path_exists(up)) { on = 0; snprintf(only, sizeof only, "overridden by user file"); }
        }
        so_check(o, on);
        so_t(o, on ? C_BLACK : C_GRAY, e[i]);
        so_tab(o);
        so_t(o, C_VALUE, *nm ? nm : "-");
        so_tab(o);
        so_t(o, C_PURPLE, ex);
        if (*only) { so_t(o, C_GRAY, "  ["); so_t(o, C_GRAY, only); so_t(o, C_GRAY, "]"); }
        so_par(o);
    }
    if (n <= 0) so_note(o, "(empty)");
    dir_free(e, n > 0 ? n : 0);
}

static void s_startup(srs_out *o)
{
    static const int tabs[] = { 336, 560 };
    char p[512];
    so_siv_header(o, "Startup Programs");
    so_tabs(o, tabs, 2);
    so_b(o, C_VALUE, "XDG autostart (user)  ~/.config/autostart");
    so_par(o);
    snprintf(p, sizeof p, "%s/.config/autostart", getenv("HOME") ? getenv("HOME") : "");
    autostart_dir(o, p, 1);
    so_par(o);
    so_b(o, C_VALUE, "XDG autostart (system)  /etc/xdg/autostart");
    so_par(o);
    autostart_dir(o, "/etc/xdg/autostart", 0);
    so_par(o);
    so_b(o, C_VALUE, "Plasma login scripts  ~/.config/plasma-workspace/env");
    so_par(o);
    snprintf(p, sizeof p, "%s/.config/plasma-workspace/env", getenv("HOME") ? getenv("HOME") : "");
    {
        char **e;
        int n = dir_list(p, &e), i;
        for (i = 0; i < n; i++) { so_t(o, C_BLACK, e[i]); so_par(o); }
        if (n <= 0) so_note(o, "(none)");
        dir_free(e, n > 0 ? n : 0);
    }
    so_par(o);
    so_b(o, C_VALUE, "Enabled systemd units (system)");
    so_par(o);
    sh_lines(o, "systemctl list-unit-files --state=enabled --no-pager --no-legend", C_VALUE, 0);
    so_par(o);
    so_b(o, C_VALUE, "Enabled systemd units (user)");
    so_par(o);
    sh_lines(o, "systemctl --user list-unit-files --state=enabled --no-pager --no-legend", C_VALUE, 0);
}

static void s_software(srs_out *o)
{
    static const int tabs[] = { 336, 616 };
    char *all = sh("pacman -Q"), *foreign = sh("pacman -Qqm"), **fv = NULL, *save = NULL, *l, b[160], ne[32] = "";
    int nf = 0, n = 0, nfo = 0;
    for (l = strtok_r(foreign, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        char **x = realloc(fv, (size_t)(nf + 1) * sizeof *fv);
        if (!x) break;
        fv = x;
        fv[nf++] = l;
    }
    qsort(fv, (size_t)nf, sizeof *fv, cmp_str);
    sh_line("pacman -Qqe | wc -l", ne, sizeof ne);
    so_siv_header(o, "Installed Software");
    so_tabs(o, tabs, 2);
    so_row(o, 3, (const char *const[]){ "Package", "Version", "Origin" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    save = NULL;
    for (l = strtok_r(all, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        char *sp = strchr(l, ' ');
        int f;
        if (!sp) continue;
        *sp = 0;
        f = fv && bsearch(&l, fv, (size_t)nf, sizeof *fv, cmp_str) != NULL;
        so_row(o, 3, (const char *const[]){ l, sp + 1, f ? "foreign (AUR/local)" : "repository" },
               (const int[]){ C_BLACK, C_PURPLE, f ? C_ORANGE : C_GRAY });
        n++;
        nfo += f;
    }
    so_par(o);
    snprintf(b, sizeof b, "%d pacman packages (%s explicitly installed, %d foreign)", n, ne, nfo);
    so_note(o, b);
    so_par(o);
    so_b(o, C_VALUE, "Flatpak  (flatpak list)");
    so_par(o);
    if (!sh_lines(o, "flatpak list --columns=application,version,branch,installation", C_VALUE, 0)) so_note(o, "none");
    free(fv);
    free(foreign);
    free(all);
}

static void s_setup(srs_out *o)
{
    static const int tabs[] = { 196 };
    char b[512], **e;
    int n, i;
    so_siv_header(o, "Setup Directories");
    so_tabs(o, tabs, 1);
    so_kv(o, "Boot Dir", C_PURPLE, "/boot");
    n = dir_list("/boot", &e);
    for (i = 0; i < n; i++)
        if (!strncmp(e[i], "vmlinuz", 7) || !strncmp(e[i], "initramfs", 9) || strstr(e[i], "ucode") || !strcmp(e[i], "grub") || !strcmp(e[i], "EFI") || !strcmp(e[i], "loader")) {
            so_t(o, C_DEF, "");
            so_tab(o);
            so_t(o, C_GRAY, e[i]);
            so_par(o);
        }
    dir_free(e, n > 0 ? n : 0);
    {
        static const char *const esp[] = { "/efi", "/boot/efi", "/boot", NULL };
        char *m;
        int found = 0;
        if (rd_all("/proc/mounts", &m) >= 0) {
            for (i = 0; esp[i] && !found; i++) {
                char pat[64];
                snprintf(pat, sizeof pat, " %s vfat ", esp[i]);
                if (strstr(m, pat)) { so_kv(o, "EFI System Partition", C_PURPLE, esp[i]); found = 1; }
            }
            free(m);
        }
        if (!found) so_kv(o, "EFI System Partition", C_GRAY, "not mounted");
    }
    so_kv(o, "Shared Dir", C_PURPLE, "/usr/share");
    so_par(o);
    {
        static const char *const k[] = { "RootDir", "DBPath", "CacheDir", "LogFile", "GPGDir", "HookDir", "Architecture", "ParallelDownloads", NULL };
        for (i = 0; k[i]; i++) if (kv_space("/etc/pacman.conf", k[i], b, sizeof b)) so_kv(o, k[i], C_TEAL, b);
    }
    sh_line("pacman -Qq | wc -l", b, sizeof b);
    so_kv(o, "Installed packages", C_VALUE, b);
    sh_line("grep 'starting full system upgrade' /var/log/pacman.log | tail -1", b, sizeof b);
    so_kv(o, "Last full upgrade", C_VALUE, b);
    so_par(o);
    so_note(o, "/etc/pacman.conf");
}

static void s_pending(srs_out *o)
{
    static const int tabs[] = { 210 };
    char b[512], p[256];
    struct utsname u;
    so_siv_header(o, "Pending Operations");
    so_tabs(o, tabs, 1);
    so_kv(o, "pacman lock", path_exists("/var/lib/pacman/db.lck") ? C_ORANGE : C_GREEN,
          path_exists("/var/lib/pacman/db.lck") ? "/var/lib/pacman/db.lck present (transaction in progress)" : "none");
    uname(&u);
    snprintf(p, sizeof p, "/usr/lib/modules/%s", u.release);
    so_kv(o, "Running kernel modules", path_exists(p) ? C_GREEN : C_RED, path_exists(p) ? p : "missing - kernel was upgraded/removed, reboot pending");
    sh_line("grep 'starting full system upgrade' /var/log/pacman.log | tail -1", b, sizeof b);
    so_kv(o, "Last full upgrade", C_VALUE, b);
    so_par(o);
    so_b(o, C_VALUE, "Last pacman transactions  (/var/log/pacman.log)");
    so_par(o);
    sh_lines(o, "grep -E '\\[ALPM\\] (upgraded|installed|removed)' /var/log/pacman.log | tail -15", C_GRAY, 0);
    so_par(o);
    so_b(o, C_VALUE, "Unmerged config files  (*.pacnew / *.pacsave under /etc)");
    so_par(o);
    if (!sh_lines(o, "find /etc -xdev \\( -name '*.pacnew' -o -name '*.pacsave' \\) -print", C_ORANGE, 200)) so_note(o, "none");
    so_par(o);
    so_note(o, "Updates are not checked (checkupdates needs the network)");
}

static void s_esu_sts(srs_out *o)
{
    static const int tabs[] = { 196 };
    char b[256], **e;
    int n, i;
    so_siv_header(o, "Support / Update Status");
    so_tabs(o, tabs, 1);
    kv_file("/etc/os-release", "PRETTY_NAME", b, sizeof b);
    so_kv(o, "Distribution", C_BLACK, b);
    so_kv(o, "Support end", C_VALUE, kv_file("/etc/os-release", "SUPPORT_END", b, sizeof b) ? b : "none (rolling release)");
    if (kv_file("/etc/os-release", "HOME_URL", b, sizeof b)) so_kv(o, "Home", C_PURPLE, b);
    if (kv_file("/etc/os-release", "SUPPORT_URL", b, sizeof b)) so_kv(o, "Support", C_PURPLE, b);
    so_par(o);
    so_b(o, C_VALUE, "Repository databases (last sync)");
    so_par(o);
    n = dir_list("/var/lib/pacman/sync", &e);
    for (i = 0; i < n; i++) {
        char p[512], d[96];
        struct stat st;
        if (!strstr(e[i], ".db") || strstr(e[i], ".sig")) continue;
        snprintf(p, sizeof p, "/var/lib/pacman/sync/%s", e[i]);
        if (stat(p, &st)) continue;
        fmt_epoch(st.st_mtime, d, sizeof d);
        so_kv(o, e[i], C_VALUE, d);
    }
    dir_free(e, n > 0 ? n : 0);
}

/* ======================================================================
 * WINONLY items with a Linux nearest equivalent
 * ====================================================================== */
static void s_email(srs_out *o)
{
    static const int tabs[] = { 245 };
    static const char *const tb[] = { ".thunderbird/profiles.ini", ".var/app/org.mozilla.Thunderbird/.thunderbird/profiles.ini",
                                      "snap/thunderbird/common/.thunderbird/profiles.ini", NULL };
    const char *home = getenv("HOME") ? getenv("HOME") : "";
    char p[512], b[256];
    int i, any = 0;
    so_siv_header(o, "E-mail Clients (profiles only, no account data)");
    so_tabs(o, tabs, 1);
    for (i = 0; tb[i]; i++) {
        char *t, *save = NULL, *l;
        snprintf(p, sizeof p, "%s/%s", home, tb[i]);
        if (rd_all(p, &t) < 0) continue;
        so_kv(o, "Thunderbird", C_PURPLE, p);
        for (l = strtok_r(t, "\n", &save); l; l = strtok_r(NULL, "\n", &save))
            if (!strncmp(l, "Name=", 5)) { so_kv(o, "  Profile", C_BLACK, l + 5); any = 1; }
        free(t);
    }
    snprintf(p, sizeof p, "%s/.config/evolution/sources", home);
    if (path_exists(p)) { char **e; int n = dir_list(p, &e); so_kvf(o, "Evolution", C_BLACK, "%d source definitions in %s", n, p); dir_free(e, n > 0 ? n : 0); any = 1; }
    snprintf(p, sizeof p, "%s/.local/share/akonadi", home);
    if (path_exists(p)) { so_kv(o, "KMail / Akonadi", C_BLACK, p); any = 1; }
    if (!any) so_note(o, "No Thunderbird/Evolution/KMail profiles found");
    so_par(o);
    sh_line("xdg-mime query default x-scheme-handler/mailto", b, sizeof b);
    so_kv(o, "mailto: handler", C_TEAL, *b ? b : "-");
}

static void s_hives(srs_out *o)
{
    static const int tabs[] = { 336, 420 };
    const char *home = getenv("HOME") ? getenv("HOME") : "";
    char p[512], d[96], **e;
    struct stat st;
    int n, i, rc = 0;
    long long tot = 0;
    so_siv_header(o, "Configuration Databases (registry hive equivalents)");
    so_tabs(o, tabs, 2);
    so_row(o, 3, (const char *const[]){ "Database", "Size", "Modified" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    snprintf(p, sizeof p, "%s/.config/dconf/user", home);
    {
        static const char *fixed[] = { NULL, "/usr/share/glib-2.0/schemas/gschemas.compiled", "/etc/ld.so.cache", "/usr/lib/udev/hwdb.bin",
                                       "/etc/udev/hwdb.bin", NULL };
        fixed[0] = p;
        for (i = 0; fixed[i]; i++) {
            char sz[32];
            if (stat(fixed[i], &st)) continue;
            fmt_bytes((double)st.st_size, sz, sizeof sz);
            fmt_epoch(st.st_mtime, d, sizeof d);
            so_row(o, 3, (const char *const[]){ fixed[i], sz, d }, (const int[]){ C_PURPLE, C_VALUE, C_GRAY });
        }
    }
    n = dir_list("/etc/dconf/db", &e);
    for (i = 0; i < n; i++) {
        char q[512], sz[32];
        snprintf(q, sizeof q, "/etc/dconf/db/%s", e[i]);
        if (stat(q, &st) || !S_ISREG(st.st_mode)) continue;
        fmt_bytes((double)st.st_size, sz, sizeof sz);
        fmt_epoch(st.st_mtime, d, sizeof d);
        so_row(o, 3, (const char *const[]){ q, sz, d }, (const int[]){ C_PURPLE, C_VALUE, C_GRAY });
    }
    dir_free(e, n > 0 ? n : 0);
    snprintf(p, sizeof p, "%s/.config", home);
    n = dir_list(p, &e);
    for (i = 0; i < n; i++) {
        char q[1024];
        size_t l = strlen(e[i]);
        if (l < 3 || strcmp(e[i] + l - 2, "rc")) continue;
        snprintf(q, sizeof q, "%s/%s", p, e[i]);
        if (!stat(q, &st) && S_ISREG(st.st_mode)) { rc++; tot += st.st_size; }
    }
    dir_free(e, n > 0 ? n : 0);
    {
        char sz[32];
        fmt_bytes((double)tot, sz, sizeof sz);
        so_par(o);
        so_kvf(o, "KDE config files (~/.config/*rc)", C_VALUE, "%d files, %s", rc, sz);
    }
}

static void s_clsid(srs_out *o)
{
    static const int tabs[] = { 336 };
    char p[512], *t, *save = NULL, *l;
    int in = 0, n = 0;
    so_siv_header(o, "Default Applications (XDG MIME associations)");
    so_tabs(o, tabs, 1);
    snprintf(p, sizeof p, "%s/.config/mimeapps.list", getenv("HOME") ? getenv("HOME") : "");
    so_row(o, 2, (const char *const[]){ "MIME Type / Scheme", "Default Handler (.desktop)" }, (const int[]){ C_LABEL, C_LABEL });
    so_par(o);
    if (rd_all(p, &t) >= 0) {
        for (l = strtok_r(t, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
            char *eq;
            if (*l == '[') { in = !strcmp(l, "[Default Applications]"); continue; }
            if (!in || !(eq = strchr(l, '='))) continue;
            *eq = 0;
            so_row(o, 2, (const char *const[]){ l, eq + 1 }, (const int[]){ strncmp(l, "x-scheme", 8) ? C_BLACK : C_TEAL, C_PURPLE });
            n++;
        }
        free(t);
    }
    if (!n) so_note(o, "no user defaults in ~/.config/mimeapps.list");
    so_par(o);
    {
        char **e;
        int a = dir_list("/usr/share/applications", &e);
        dir_free(e, a > 0 ? a : 0);
        so_kvf(o, "System .desktop entries", C_VALUE, "%d in /usr/share/applications", a > 0 ? a : 0);
        snprintf(p, sizeof p, "%s/.local/share/applications", getenv("HOME") ? getenv("HOME") : "");
        a = dir_list(p, &e);
        dir_free(e, a > 0 ? a : 0);
        so_kvf(o, "User .desktop entries", C_VALUE, "%d in %s", a > 0 ? a : 0, p);
    }
}

static void s_policies(srs_out *o)
{
    static const int tabs[] = { 336 };
    static const char *const dirs[] = { "/etc/polkit-1/rules.d", "/usr/share/polkit-1/rules.d", "/etc/security/limits.d", NULL };
    char **e, *t;
    int i, n, k;
    so_siv_header(o, "System Policies (polkit / PAM limits)");
    so_tabs(o, tabs, 1);
    for (k = 0; dirs[k]; k++) {
        so_b(o, C_VALUE, dirs[k]);
        so_par(o);
        n = dir_list(dirs[k], &e);
        if (n < 0) so_note(o, "  not readable (root-only directory)");
        else if (!n) so_note(o, "  (empty)");
        for (i = 0; i < n; i++) { so_t(o, C_PURPLE, "  "); so_t(o, C_PURPLE, e[i]); so_par(o); }
        dir_free(e, n > 0 ? n : 0);
        so_par(o);
    }
    n = dir_list("/usr/share/polkit-1/actions", &e);
    so_kvf(o, "polkit action files", C_VALUE, "%d in /usr/share/polkit-1/actions", n > 0 ? n : 0);
    dir_free(e, n > 0 ? n : 0);
    so_par(o);
    so_b(o, C_VALUE, "/etc/security/limits.conf");
    so_par(o);
    if (rd_all("/etc/security/limits.conf", &t) >= 0) {
        char *save = NULL, *l;
        int any = 0;
        for (l = strtok_r(t, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) if (*ltrim(l) && *ltrim(l) != '#') { so_m(o, C_VALUE, l); so_par(o); any = 1; }
        if (!any) so_note(o, "(only comments)");
        free(t);
    }
}

static void s_product_ids(srs_out *o)
{
    static const int tabs[] = { 196 };
    char b[128], m[160];
    so_siv_header(o, "Product / Machine IDs");
    so_tabs(o, tabs, 1);
    if (rd_str("/etc/machine-id", b, sizeof b)) { snprintf(m, sizeof m, "%.8s-XXXXXXXX-XXXXXXXX-XXXXXXXX (masked)", b); so_kv(o, "Machine ID", C_PURPLE, m); }
    if (rd_str("/proc/sys/kernel/random/boot_id", b, sizeof b)) so_kv(o, "Boot ID", C_PURPLE, b);
    if (kv_file("/etc/os-release", "ID", b, sizeof b)) so_kv(o, "OS ID", C_VALUE, b);
    if (kv_file("/etc/os-release", "BUILD_ID", b, sizeof b)) so_kv(o, "Build ID", C_VALUE, b);
    if (kv_file("/etc/os-release", "IMAGE_ID", b, sizeof b)) so_kv(o, "Image ID", C_VALUE, b);
    rd_str("/sys/class/dmi/id/sys_vendor", b, sizeof b);   so_kv(o, "System Vendor", C_VALUE, b);
    rd_str("/sys/class/dmi/id/product_name", b, sizeof b); so_kv(o, "Product Name", C_VALUE, b);
    rd_str("/sys/class/dmi/id/product_sku", b, sizeof b);  so_kv(o, "Product SKU", C_VALUE, b);
    if (rd_str("/sys/class/dmi/id/product_serial", b, sizeof b)) so_kv(o, "Product Serial", C_VALUE, "XXXXXXXX (masked)");
    else so_kv(o, "Product Serial / UUID", C_GRAY, "Needs root: Menu > Run as root (srs-root)");
    so_par(o);
    so_note(o, "Linux has no product activation keys");
}

static void s_reg_srv(srs_out *o)
{
    static const int tabs[] = { 245, 350 };
    static const char *const units[] = { "sshd.service", "sshd.socket", "cockpit.socket", "webmin.service", "smb.service", "nfs-server.service",
                                         "libvirtd.service", NULL };
    int i;
    so_siv_header(o, "Remote Administration Services");
    so_tabs(o, tabs, 2);
    so_row(o, 3, (const char *const[]){ "Unit", "Active", "Enabled" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (i = 0; units[i]; i++) {
        char *a = NULL, *e = NULL;
        const char *aa[] = { "timeout", "5", "systemctl", "is-active", units[i], NULL };
        const char *ee[] = { "timeout", "5", "systemctl", "is-enabled", units[i], NULL };
        run_out(&a, aa);
        run_out(&e, ee);
        if (a) rtrim(a);
        if (e) rtrim(e);
        so_row(o, 3, (const char *const[]){ units[i], a && *a ? a : "-", e && *e ? e : "not installed" },
               (const int[]){ C_BLACK, a && !strcmp(a, "active") ? C_ORANGE : C_GRAY, C_GRAY });
        free(a);
        free(e);
    }
    so_par(o);
    so_note(o, "Nearest Linux equivalent of the Windows RemoteRegistry service: remote login/admin daemons");
}

static void s_setup_api(srs_out *o)
{
    static const int tabs[] = { 245 };
    char b[128];
    so_siv_header(o, "Device Setup Logging (udev)");
    so_tabs(o, tabs, 1);
    so_kv(o, "udev_log (/etc/udev/udev.conf)", C_PURPLE, kv_file("/etc/udev/udev.conf", "udev_log", b, sizeof b) ? b : "default (info)");
    {
        static const char *const d[] = { "/etc/udev/rules.d", "/usr/lib/udev/rules.d", "/run/udev/rules.d", NULL };
        int i;
        for (i = 0; d[i]; i++) { char **e; int n = dir_list(d[i], &e); so_kvf(o, d[i], C_VALUE, "%d rule files", n > 0 ? n : 0); dir_free(e, n > 0 ? n : 0); }
    }
    sh_line("udevadm --version", b, sizeof b);
    so_kv(o, "udevadm version", C_VALUE, b);
}

static void s_vs_paths(srs_out *o)
{
    static const int tabs[] = { 140, 460 };
    static const char *const tools[] = { "cc", "gcc", "g++", "clang", "clang++", "ld", "make", "cmake", "ninja", "meson", "rustc", "cargo", "go",
                                         "python3", "node", "java", "dotnet", "gdb", "lldb", "code", "codium", "x86_64-w64-mingw32-gcc", NULL };
    const char *path = getenv("PATH");
    int i;
    so_siv_header(o, "Development Toolchain Paths");
    so_tabs(o, tabs, 2);
    so_row(o, 3, (const char *const[]){ "Tool", "Path", "Resolves To" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (i = 0; tools[i]; i++) {
        char dirs[8192], *save = NULL, *d, found[1024] = "", real[1024] = "";
        snprintf(dirs, sizeof dirs, "%s", path ? path : "/usr/bin");
        for (d = strtok_r(dirs, ":", &save); d; d = strtok_r(NULL, ":", &save)) {
            char p[1024];
            snprintf(p, sizeof p, "%s/%s", d, tools[i]);
            if (!access(p, X_OK)) { snprintf(found, sizeof found, "%s", p); break; }
        }
        if (*found && !realpath(found, real)) *real = 0;
        so_row(o, 3, (const char *const[]){ tools[i], *found ? found : "<not found>", strcmp(real, found) ? real : "" },
               (const int[]){ C_BLACK, *found ? C_PURPLE : C_GRAY, C_GRAY });
    }
}

static void s_sys_win(srs_out *o)
{
    so_siv_header(o, "Top-level Windows (X11 / XWayland via wmctrl)");
    if (!getenv("DISPLAY")) { so_note(o, "No DISPLAY - wmctrl needs X11/XWayland; native Wayland windows are not enumerable by clients"); return; }
    so_m(o, C_LABEL, "Window ID  Desk  X     Y     W     H     Class                Host  Title");
    so_par(o);
    if (!sh_lines(o, "wmctrl -l -G -x", C_VALUE, 0)) so_note(o, "wmctrl returned nothing");
    so_par(o);
    so_note(o, "Native Wayland clients are not listed (Wayland has no global window list)");
}

static void s_tid_win(srs_out *o)
{
    static const int tabs[] = { 98, 154, 294 };
    char *t, *save = NULL, *l;
    int n = 0;
    so_siv_header(o, "Windows per Process (X11 / XWayland via wmctrl)");
    if (!getenv("DISPLAY")) { so_note(o, "No DISPLAY - wmctrl needs X11/XWayland"); return; }
    so_tabs(o, tabs, 3);
    so_row(o, 4, (const char *const[]){ "Window ID", "PID", "Process", "Title" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    t = sh("wmctrl -l -p");
    for (l = strtok_r(t, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        char wid[32], desk[16], host[128], comm[64] = "-";
        int pid = 0, off = 0;
        if (sscanf(l, "%31s %15s %d %127s %n", wid, desk, &pid, host, &off) < 4) continue;
        if (pid > 0) rd_fmt(comm, sizeof comm, "/proc/%d/comm", pid);
        {
            char ps[16];
            snprintf(ps, sizeof ps, "%d", pid);
            so_row(o, 4, (const char *const[]){ wid, ps, comm, off ? l + off : "" }, (const int[]){ C_PURPLE, C_VALUE, C_BLACK, C_GRAY });
        }
        n++;
    }
    free(t);
    if (!n) so_note(o, "wmctrl returned nothing");
}

static void s_verifier(srs_out *o)
{
    char b[64];
    so_siv_header(o, "Kernel Runtime Checkers (Driver Verifier equivalent)");
    taint_table(o);
    so_par(o);
    so_b(o, C_VALUE, "Debug/checker options built into the running kernel");
    so_par(o);
    kernel_cfg_grep(o, "^CONFIG_(KASAN|UBSAN|KFENCE|DEBUG_PAGEALLOC|PROVE_LOCKING|LOCKDEP|DEBUG_KMEMLEAK|SLUB_DEBUG|DEBUG_LIST|DEBUG_OBJECTS|FAULT_INJECTION|KCSAN|DEBUG_VM|HARDENED_USERCOPY|INIT_ON_ALLOC_DEFAULT_ON|INIT_ON_FREE_DEFAULT_ON)=", C_PURPLE);
    so_par(o);
    if (rd_str("/sys/module/kfence/parameters/sample_interval", b, sizeof b)) so_kv(o, "kfence.sample_interval", C_VALUE, b);
    rd_str("/proc/cmdline", b, sizeof b);
    so_kv(o, "slub_debug on cmdline", C_VALUE, strstr(b, "slub_debug") ? "yes" : "no");
}

static void s_metrics(srs_out *o)
{
    static const int tabs[] = { 245 };
    static const char *const env[] = { "XCURSOR_THEME", "XCURSOR_SIZE", "GDK_SCALE", "GDK_DPI_SCALE", "QT_SCALE_FACTOR", "QT_SCREEN_SCALE_FACTORS",
                                       "QT_AUTO_SCREEN_SCALE_FACTOR", "PLASMA_USE_QT_SCALING", NULL };
    char *t;
    int i;
    so_siv_header(o, "Display Metrics");
    so_tabs(o, tabs, 1);
    for (i = 0; env[i]; i++) if (getenv(env[i])) so_kv(o, env[i], C_TEAL, getenv(env[i]));
    if (rd_all("/proc/bus/input/devices", &t) >= 0) {
        int mice = 0, kbd = 0;
        char *p;
        for (p = t; (p = strstr(p, "H: Handlers=")); p++) { char *e = strchr(p, '\n'); size_t l = e ? (size_t)(e - p) : strlen(p);
            if (memmem(p, l, "mouse", 5)) mice++;
            if (memmem(p, l, "kbd", 3) && memmem(p, l, "leds", 4)) kbd++; }
        so_kvf(o, "Pointer devices", C_VALUE, "%d", mice);
        so_kvf(o, "Keyboards (with LEDs)", C_VALUE, "%d", kbd);
        free(t);
    }
    so_par(o);
    so_b(o, C_VALUE, "Outputs  (kscreen-doctor -o)");
    so_par(o);
    t = sh("kscreen-doctor -o");
    strip_ansi(t);
    if (*t) so_lines(o, C_VALUE, t); else so_note(o, "kscreen-doctor not available / no KScreen session");
    free(t);
}

/* ======================================================================
 * memory: [pools] [paging] [file-cache] [virtual] [heap] [mem-heap] [mem-map]
 * ====================================================================== */
static int count_procs(void)
{
    char **e;
    int n = dir_list("/proc", &e), i, k = 0;
    for (i = 0; i < n; i++) k += isdigit((unsigned char)e[i][0]) != 0;
    dir_free(e, n > 0 ? n : 0);
    return k;
}

static void s_pools(srs_out *o)
{
    static const int tabs[] = { 140, 224, 266, 434, 518, 560, 686, 770 };
    static long long prev[12];
    static double prev_t;
    static int have;
    long long cur[12], fnr = 0;
    char b[256], up[32], dt[32], la[128] = "", c[12][32], d[12][32];
    double now = uptime_s();
    int i;
    mem_load();
    rd_str("/proc/loadavg", la, sizeof la);
    {
        char fn[64] = "";
        rd_str("/proc/sys/fs/file-nr", fn, sizeof fn);
        fnr = atoll(fn);
    }
    cur[0] = mi("Slab") + mi("KernelStack") + mi("PageTables");
    cur[1] = mi("SReclaimable");
    cur[2] = mi("SUnreclaim");
    cur[3] = mi("MemTotal");
    cur[4] = mi("MemAvailable");
    cur[5] = mi("Cached") + mi("Buffers");
    cur[6] = mi("Committed_AS");
    cur[7] = mi("CommitLimit");
    cur[8] = mi("Shmem");
    cur[9] = count_procs();
    cur[10] = strchr(la, '/') ? atoll(strchr(la, '/') + 1) : 0;
    cur[11] = fnr;
    for (i = 0; i < 12; i++) {
        long long dv = have ? cur[i] - prev[i] : 0;
        if (i < 9) { kb_str(cur[i], c[i], 32); if (dv) { kb_str(dv < 0 ? -dv : dv, d[i], 32); if (dv < 0) { memmove(d[i] + 1, d[i], strlen(d[i]) + 1); d[i][0] = '-'; } } else snprintf(d[i], 32, "0"); }
        else { snprintf(c[i], 32, "%lld", cur[i]); snprintf(d[i], 32, "%lld", dv); }
    }
    fmt_uptime(have ? now - prev_t : 0, dt, sizeof dt);
    fmt_uptime(now, up, sizeof up);
    snprintf(b, sizeof b, "Δ-time %s System Up-time %s", dt, up);
    so_siv_header(o, b);
    so_tabs(o, tabs, 8);
    so_row(o, 9, (const char *const[]){ "Description", "Current", "Δ", "Description", "Current", "Δ", "Description", "Current", "Δ" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    {
        static const char *const nm[] = { "Kernel Total", "Physical Total", "Commit Total", "Kernel Reclaimable", "Physical Available", "Commit Limit",
                                          "Kernel Unreclaim", "System Cache", "Shared (shmem)" };
        static const int idx[] = { 0, 3, 6, 1, 4, 7, 2, 5, 8 };
        int r;
        for (r = 0; r < 3; r++) {
            const char *cells[9];
            int k;
            for (k = 0; k < 3; k++) { int j = idx[r * 3 + k]; cells[k * 3] = nm[r * 3 + k]; cells[k * 3 + 1] = c[j]; cells[k * 3 + 2] = d[j]; }
            so_row(o, 9, cells, (const int[]){ C_LABEL, C_BLACK, C_GRAY, C_LABEL, C_BLACK, C_GRAY, C_LABEL, C_BLACK, C_GRAY });
        }
    }
    so_par(o);
    so_row(o, 9, (const char *const[]){ "Process Count", c[9], d[9], "Thread Count", c[10], d[10], "Handle Count", c[11], d[11] },
           (const int[]){ C_LABEL, C_BLACK, C_GRAY, C_LABEL, C_BLACK, C_GRAY, C_LABEL, C_BLACK, C_GRAY });
    so_par(o);
    so_note(o, "Kernel Total = Slab + KernelStack + PageTables; Handle Count = allocated file handles (/proc/sys/fs/file-nr)");
    memcpy(prev, cur, sizeof prev);
    prev_t = now;
    have = 1;
}

static void s_paging(srs_out *o)
{
    static const int tabs[] = { 280, 364, 448, 532 };
    char *t, b[128], **e;
    int n, i;
    mem_load();
    so_siv_header(o, "Paging Files (swap)");
    so_tabs(o, tabs, 4);
    so_row(o, 5, (const char *const[]){ "Swap Name", "Type", "Size", "Used", "Priority" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    if (rd_all("/proc/swaps", &t) >= 0) {
        char *save = NULL, *l = strtok_r(t, "\n", &save);   /* skip header */
        int k = 0;
        for (l = strtok_r(NULL, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
            char f[128], ty[32], s1[32], s2[32], pr[16];
            long long sz, us;
            if (sscanf(l, "%127s %31s %lld %lld %15s", f, ty, &sz, &us, pr) != 5) continue;
            kb_str(sz, s1, sizeof s1);
            kb_str(us, s2, sizeof s2);
            so_row(o, 5, (const char *const[]){ f, ty, s1, s2, pr }, (const int[]){ C_PURPLE, C_VALUE, C_BLACK, C_BLACK, C_VALUE });
            k++;
        }
        if (!k) so_note(o, "no swap active");
        free(t);
    }
    so_par(o);
    so_tabs(o, tabs, 1);
    kb_str(mi("SwapTotal"), b, sizeof b);  so_kv(o, "Swap Total", C_VALUE, b);
    kb_str(mi("SwapFree"), b, sizeof b);   so_kv(o, "Swap Free", C_VALUE, b);
    kb_str(mi("SwapCached"), b, sizeof b); so_kv(o, "Swap Cached", C_VALUE, b);
    kb_str(mi("Zswap"), b, sizeof b);      so_kv(o, "Zswap pool", C_VALUE, b);
    so_kvf(o, "Pages swapped in / out", C_VALUE, "%lld / %lld", vm("pswpin"), vm("pswpout"));
    {
        static const char *const sc[] = { "swappiness", "page-cluster", "watermark_scale_factor", "min_free_kbytes", NULL };
        for (i = 0; sc[i]; i++) { char p[128]; snprintf(p, sizeof p, "/proc/sys/vm/%s", sc[i]); if (rd_str(p, b, sizeof b)) so_kv(o, sc[i], C_PURPLE, b); }
    }
    so_par(o);
    n = dir_list("/sys/block", &e);
    for (i = 0; i < n; i++) {
        char alg[128] = "", ds[32] = "", mm[256] = "", sz[32], o1[32], o2[32], o3[32];
        long long a = 0, c = 0, u = 0;
        if (strncmp(e[i], "zram", 4)) continue;
        rd_fmt(alg, sizeof alg, "/sys/block/%s/comp_algorithm", e[i]);
        if (strchr(alg, '[') && strchr(alg, ']')) { char *q = strchr(alg, '[') + 1; memmove(alg, q, strlen(q) + 1); *strchr(alg, ']') = 0; }
        rd_fmt(ds, sizeof ds, "/sys/block/%s/disksize", e[i]);
        rd_fmt(mm, sizeof mm, "/sys/block/%s/mm_stat", e[i]);
        sscanf(mm, "%lld %lld %lld", &a, &c, &u);
        fmt_bytes(atof(ds), sz, sizeof sz);
        fmt_bytes((double)a, o1, sizeof o1);
        fmt_bytes((double)c, o2, sizeof o2);
        fmt_bytes((double)u, o3, sizeof o3);
        so_kvf(o, e[i], C_TEAL, "disksize %s  algorithm %s  data %s  compressed %s  memory used %s  ratio %.2f", sz, alg, o1, o2, o3,
               c ? (double)a / (double)c : 0.0);
    }
    dir_free(e, n > 0 ? n : 0);
    n = dir_list("/sys/module/zswap/parameters", &e);
    for (i = 0; i < n; i++) { char v[64] = ""; char k[96]; rd_fmt(v, sizeof v, "/sys/module/zswap/parameters/%s", e[i]); snprintf(k, sizeof k, "zswap.%s", e[i]); so_kv(o, k, C_GRAY, v); }
    dir_free(e, n > 0 ? n : 0);
}

static void s_file_cache(srs_out *o)
{
    static const int tabs[] = { 280, 400 };
    static const char *const k[] = { "Cached", "Buffers", "Active(file)", "Inactive(file)", "Dirty", "Writeback", "Mapped", "Shmem", "Unevictable",
                                     "FileHugePages", NULL };
    static const char *const sc[] = { "dirty_ratio", "dirty_background_ratio", "dirty_bytes", "dirty_background_bytes", "dirty_expire_centisecs",
                                      "dirty_writeback_centisecs", "vfs_cache_pressure", NULL };
    static const char *const vs[] = { "pgpgin", "pgpgout", "pgfault", "pgmajfault", "workingset_refault_file", "workingset_activate_file", "pgsteal_kswapd",
                                      "pgsteal_direct", "drop_pagecache", NULL };
    char b[64];
    int i;
    mem_load();
    so_siv_header(o, "System File Cache (page cache)");
    so_tabs(o, tabs, 2);
    so_row(o, 3, (const char *const[]){ "Page cache (/proc/meminfo)", "Size", "kB" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL });
    for (i = 0; k[i]; i++) {
        char raw[32];
        kb_str(mi(k[i]), b, sizeof b);
        snprintf(raw, sizeof raw, "%lld", mi(k[i]));
        so_row(o, 3, (const char *const[]){ k[i], b, raw }, (const int[]){ C_LABEL, C_BLACK, C_GRAY });
    }
    so_par(o);
    so_row(o, 2, (const char *const[]){ "Counters (/proc/vmstat)", "Total Count" }, (const int[]){ C_LABEL, C_LABEL });
    for (i = 0; vs[i]; i++) { char v[32]; snprintf(v, sizeof v, "%lld", vm(vs[i])); so_row(o, 2, (const char *const[]){ vs[i], v }, (const int[]){ C_LABEL, C_BLACK }); }
    so_par(o);
    so_row(o, 2, (const char *const[]){ "Writeback policy (/proc/sys/vm)", "Value" }, (const int[]){ C_LABEL, C_LABEL });
    for (i = 0; sc[i]; i++) { char p[128]; snprintf(p, sizeof p, "/proc/sys/vm/%s", sc[i]); if (rd_str(p, b, sizeof b)) so_row(o, 2, (const char *const[]){ sc[i], b }, (const int[]){ C_LABEL, C_PURPLE }); }
    so_par(o);
    so_kvf(o, "Page Size", C_VALUE, "%ld", sysconf(_SC_PAGESIZE));
}

static void status_vm(srs_out *o, int all)
{
    char *t, *save = NULL, *l;
    if (rd_all("/proc/self/status", &t) < 0) return;
    for (l = strtok_r(t, "\n", &save); l; l = strtok_r(NULL, "\n", &save))
        if (!strncmp(l, "Vm", 2) || !strncmp(l, "Rss", 3) || (all && (!strncmp(l, "Hugetlb", 7) || !strncmp(l, "Threads", 7)))) {
            char *c = strchr(l, ':');
            if (!c) continue;
            *c = 0;
            so_kv(o, l, C_BLACK, ltrim(c + 1));
        }
    free(t);
}

static void s_virtual(srs_out *o)
{
    static const int tabs[] = { 245 };
    static const char *const sc[] = { "overcommit_memory", "overcommit_ratio", "overcommit_kbytes", "max_map_count", "mmap_min_addr", NULL };
    static const struct { int r; const char *n; } lim[] = { { RLIMIT_AS, "RLIMIT_AS" }, { RLIMIT_DATA, "RLIMIT_DATA" }, { RLIMIT_STACK, "RLIMIT_STACK" },
                                                           { RLIMIT_MEMLOCK, "RLIMIT_MEMLOCK" } };
    char b[64];
    int i;
    mem_load();
    so_siv_header(o, "Virtual Memory");
    so_tabs(o, tabs, 1);
    so_b(o, C_VALUE, "System");
    so_par(o);
    kb_str(mi("CommitLimit"), b, sizeof b);   so_kv(o, "Commit Limit", C_BLACK, b);
    kb_str(mi("Committed_AS"), b, sizeof b);  so_kv(o, "Committed", C_BLACK, b);
    kb_str(mi("VmallocTotal"), b, sizeof b);  so_kv(o, "Vmalloc Total", C_BLACK, b);
    kb_str(mi("VmallocUsed"), b, sizeof b);   so_kv(o, "Vmalloc Used", C_BLACK, b);
    kb_str(mi("AnonPages"), b, sizeof b);     so_kv(o, "Anonymous Pages", C_BLACK, b);
    kb_str(mi("AnonHugePages"), b, sizeof b); so_kv(o, "Anon Huge Pages", C_BLACK, b);
    so_kvf(o, "HugePages Total / Free", C_BLACK, "%lld / %lld (size %lld kB)", mi("HugePages_Total"), mi("HugePages_Free"), mi("Hugepagesize"));
    for (i = 0; sc[i]; i++) { char p[96]; snprintf(p, sizeof p, "/proc/sys/vm/%s", sc[i]); if (rd_str(p, b, sizeof b)) so_kv(o, sc[i], C_PURPLE, b); }
    if (rd_str("/proc/sys/kernel/randomize_va_space", b, sizeof b)) so_kv(o, "randomize_va_space", C_PURPLE, b);
    if (rd_str("/sys/kernel/mm/transparent_hugepage/enabled", b, sizeof b)) so_kv(o, "THP enabled", C_PURPLE, b);
    so_par(o);
    so_b(o, C_VALUE, "This process");
    so_par(o);
    status_vm(o, 0);
    for (i = 0; i < 4; i++) {
        struct rlimit rl;
        char cu[32], mx[32];
        if (getrlimit(lim[i].r, &rl)) continue;
        if (rl.rlim_cur == RLIM_INFINITY) snprintf(cu, sizeof cu, "unlimited"); else fmt_bytes((double)rl.rlim_cur, cu, sizeof cu);
        if (rl.rlim_max == RLIM_INFINITY) snprintf(mx, sizeof mx, "unlimited"); else fmt_bytes((double)rl.rlim_max, mx, sizeof mx);
        so_kvf(o, lim[i].n, C_TEAL, "%s (hard %s)", cu, mx);
    }
}

static void smaps_region(srs_out *o, const char *tag)
{
    char *t, *save = NULL, *l;
    int in = 0;
    if (rd_all("/proc/self/smaps", &t) < 0) return;
    for (l = strtok_r(t, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        unsigned long a, b;
        char sp;
        if (sscanf(l, "%lx-%lx%c", &a, &b, &sp) == 3 && sp == ' ') {
            in = strstr(l, tag) != NULL;
            if (in) { so_m(o, C_PURPLE, l); so_par(o); }
            continue;
        }
        if (in && (!strncmp(l, "Size", 4) || !strncmp(l, "Rss", 3) || !strncmp(l, "Pss", 3) || !strncmp(l, "Private", 7) || !strncmp(l, "Anonymous", 9) || !strncmp(l, "Swap:", 5))) {
            char *c = strchr(l, ':');
            if (c) { *c = 0; so_kv(o, l, C_BLACK, ltrim(c + 1)); }
        }
    }
    free(t);
}

static void s_heap(srs_out *o)
{
    static const int tabs[] = { 280 };
    struct mallinfo2 m = mallinfo2();
    char b[32];
    so_siv_header(o, "Process Heap (glibc malloc, this process)");
    so_tabs(o, tabs, 1);
    fmt_bytes((double)m.arena, b, sizeof b);    so_kvf(o, "Arena (sbrk heap)", C_BLACK, "%s (%zu)", b, m.arena);
    fmt_bytes((double)m.hblkhd, b, sizeof b);   so_kvf(o, "mmap'd blocks", C_BLACK, "%s in %zu blocks", b, m.hblks);
    fmt_bytes((double)m.uordblks, b, sizeof b); so_kvf(o, "In use (allocated)", C_BLACK, "%s (%zu)", b, m.uordblks);
    fmt_bytes((double)m.fordblks, b, sizeof b); so_kvf(o, "Free (in arena)", C_BLACK, "%s in %zu chunks", b, m.ordblks);
    fmt_bytes((double)m.fsmblks, b, sizeof b);  so_kvf(o, "Fastbin free", C_BLACK, "%s in %zu blocks", b, m.smblks);
    fmt_bytes((double)m.keepcost, b, sizeof b); so_kvf(o, "Releasable (top)", C_BLACK, "%s", b);
    so_par(o);
    so_b(o, C_VALUE, "[heap] mapping  (/proc/self/smaps)");
    so_par(o);
    smaps_region(o, "[heap]");
}

static void s_mem_heap(srs_out *o)
{
    static const int tabs[] = { 280 };
    char *t;
    so_siv_header(o, "Memory Heap / Usage (this process)");
    so_tabs(o, tabs, 1);
    so_b(o, C_VALUE, "/proc/self/status");
    so_par(o);
    status_vm(o, 1);
    so_par(o);
    so_b(o, C_VALUE, "/proc/self/smaps_rollup");
    so_par(o);
    if (rd_all("/proc/self/smaps_rollup", &t) >= 0) {
        char *save = NULL, *l;
        for (l = strtok_r(t, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
            char *c = strchr(l, ':');
            if (!c || strchr(l, '[')) continue;
            *c = 0;
            so_kv(o, l, C_BLACK, ltrim(c + 1));
        }
        free(t);
    }
    so_par(o);
    so_b(o, C_VALUE, "Thread stacks / anonymous [stack] mapping");
    so_par(o);
    smaps_region(o, "[stack]");
}

static void s_mem_map(srs_out *o)
{
    char *t;
    so_siv_header(o, "Memory Map (/proc/self/maps)");
    so_m(o, C_LABEL, "Address Range                     Perm Offset   Dev   Inode      Path");
    so_par(o);
    if (rd_all("/proc/self/maps", &t) >= 0) { so_lines(o, C_VALUE, t); free(t); }
}

/* ======================================================================
 * shared libraries: [dll] [dll-addr] [dll-name] [dll-known]
 * ====================================================================== */
typedef struct { unsigned long lo, hi; char path[512]; } dllrow;
static int dll_load(dllrow **out)
{
    char *t, *save = NULL, *l;
    dllrow *v = NULL;
    int n = 0, i;
    *out = NULL;
    if (rd_all("/proc/self/maps", &t) < 0) return 0;
    for (l = strtok_r(t, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        unsigned long a, b;
        char *p = strchr(l, '/');
        if (!p || sscanf(l, "%lx-%lx", &a, &b) != 2) continue;
        for (i = 0; i < n; i++) if (!strcmp(v[i].path, p)) break;
        if (i < n) { if (a < v[i].lo) v[i].lo = a; if (b > v[i].hi) v[i].hi = b; continue; }
        {
            dllrow *nv = realloc(v, (size_t)(n + 1) * sizeof *v);
            if (!nv) break;
            v = nv;
            v[n].lo = a;
            v[n].hi = b;
            snprintf(v[n].path, sizeof v[n].path, "%s", p);
            n++;
        }
    }
    free(t);
    *out = v;
    return n;
}
static const char *base_name(const char *p) { const char *s = strrchr(p, '/'); return s ? s + 1 : p; }
static int dll_cmp_name(const void *a, const void *b) { return strcasecmp(base_name(((const dllrow *)a)->path), base_name(((const dllrow *)b)->path)); }

static void dll_table(srs_out *o, int mode)
{
    static const int tabs[] = { 140, 280, 364, 600 };
    dllrow *v;
    int n = dll_load(&v), i;
    char b[96];
    if (mode == 2) qsort(v, (size_t)n, sizeof *v, dll_cmp_name);
    so_tabs(o, tabs, 4);
    so_row(o, 5, (const char *const[]){ "Base", "End", "Size", "Module", "Version / Path" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (i = 0; i < n; i++) {
        char lo[24], hi[24], sz[24], ver[64] = "";
        const char *bn = base_name(v[i].path), *so = strstr(bn, ".so.");
        snprintf(lo, sizeof lo, "%012lX", v[i].lo);
        snprintf(hi, sizeof hi, "%012lX", v[i].hi);
        fmt_bytes((double)(v[i].hi - v[i].lo), sz, sizeof sz);
        if (so && isdigit((unsigned char)so[4])) snprintf(ver, sizeof ver, "V%s  ", so + 4);
        snprintf(b, sizeof b, "%s", ver);
        {
            char vp[640];
            snprintf(vp, sizeof vp, "%s%s", b, v[i].path);
            so_row(o, 5, (const char *const[]){ lo, hi, sz, bn, vp }, (const int[]){ C_PURPLE, C_GRAY, C_VALUE, C_BLACK, C_GRAY });
        }
    }
    so_par(o);
    snprintf(b, sizeof b, "%d mapped files (executable, shared libraries, locale/data files)", n);
    so_note(o, b);
    free(v);
}

static void s_dll(srs_out *o)
{
    static const int tabs[] = { 245 };
    char exe[512] = "", interp[256] = "";
    ssize_t r = readlink("/proc/self/exe", exe, sizeof exe - 1);
    dllrow *v;
    int n = dll_load(&v), i;
    if (r > 0) exe[r] = 0;
    for (i = 0; i < n; i++) if (strstr(base_name(v[i].path), "ld-linux")) snprintf(interp, sizeof interp, "%s", v[i].path);
    free(v);
    so_siv_header(o, "Base Modules (shared objects of this process)");
    so_tabs(o, tabs, 1);
    so_kv(o, "Image", C_BLACK, exe);
    so_kv(o, "Dynamic loader", C_PURPLE, interp);
    so_kv(o, "glibc", C_VALUE, gnu_get_libc_version());
    so_par(o);
    dll_table(o, 0);
}

static void s_dll_addr(srs_out *o) { so_siv_header(o, "Modules by Address"); dll_table(o, 1); }
static void s_dll_name(srs_out *o) { so_siv_header(o, "Modules by Name"); dll_table(o, 2); }

static void s_dll_known(srs_out *o)
{
    static const int tabs[] = { 245 };
    char b[256], *t, **e;
    int n, i;
    so_siv_header(o, "Known Libraries (ld.so cache)");
    so_tabs(o, tabs, 1);
    sh_line("ldconfig -p | head -1", b, sizeof b);
    so_kv(o, "ld.so.cache", C_BLACK, b);
    so_kv(o, "LD_LIBRARY_PATH", C_TEAL, getenv("LD_LIBRARY_PATH") ? getenv("LD_LIBRARY_PATH") : "<not set>");
    so_kv(o, "LD_PRELOAD", C_TEAL, getenv("LD_PRELOAD") ? getenv("LD_PRELOAD") : "<not set>");
    if (rd_all("/etc/ld.so.preload", &t) >= 0) { rtrim(t); so_kv(o, "/etc/ld.so.preload", C_ORANGE, *t ? t : "(empty)"); free(t); }
    so_par(o);
    so_b(o, C_VALUE, "Search directories  (/etc/ld.so.conf + ld.so.conf.d)");
    so_par(o);
    if (rd_all("/etc/ld.so.conf", &t) >= 0) {
        char *save = NULL, *l;
        for (l = strtok_r(t, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) if (*l != '#' && strncmp(l, "include", 7)) { so_m(o, C_PURPLE, l); so_par(o); }
        free(t);
    }
    n = dir_list("/etc/ld.so.conf.d", &e);
    for (i = 0; i < n; i++) {
        char p[512];
        snprintf(p, sizeof p, "/etc/ld.so.conf.d/%s", e[i]);
        if (rd_all(p, &t) >= 0) {
            char *save = NULL, *l;
            for (l = strtok_r(t, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) if (*ltrim(l) && *ltrim(l) != '#') { so_t(o, C_PURPLE, ltrim(l)); so_tab(o); so_t(o, C_GRAY, e[i]); so_par(o); }
            free(t);
        }
    }
    dir_free(e, n > 0 ? n : 0);
    so_par(o);
    so_b(o, C_VALUE, "Core system libraries  (ldconfig -p)");
    so_par(o);
    sh_lines(o, "ldconfig -p | grep -E '^[[:space:]]lib(c|m|dl|pthread|rt|stdc\\+\\+|gcc_s|z|ssl|crypto|systemd|udev|dbus-1|drm|EGL|GL|vulkan|X11|wayland-client|asound|pulse)\\.so\\.'", C_VALUE, 80);
}

/* ======================================================================
 * [handle]
 * ====================================================================== */
typedef struct { int pid, n; char comm[64]; } fdcnt;
static int fdcnt_cmp(const void *a, const void *b) { return ((const fdcnt *)b)->n - ((const fdcnt *)a)->n; }

static void s_handle(srs_out *o)
{
    static const int tabs[] = { 245, 315 };
    char b[128], **e;
    int n = dir_list("/proc/self/fd", &e), i, np, k = 0;
    fdcnt *v;
    proc_t *ps;
    so_siv_header(o, "Handles (file descriptors)");
    so_tabs(o, tabs, 2);
    if (rd_str("/proc/sys/fs/file-nr", b, sizeof b)) { char *q; for (q = b; *q; q++) if (*q == '\t') *q = ' '; so_kv(o, "System file-nr (alloc free max)", C_BLACK, b); }
    if (rd_str("/proc/sys/fs/file-max", b, sizeof b)) so_kv(o, "file-max", C_PURPLE, b);
    if (rd_str("/proc/sys/fs/nr_open", b, sizeof b)) so_kv(o, "nr_open (per process max)", C_PURPLE, b);
    if (rd_str("/proc/sys/fs/inode-nr", b, sizeof b)) { char *q; for (q = b; *q; q++) if (*q == '\t') *q = ' '; so_kv(o, "inode-nr (alloc free)", C_VALUE, b); }
    {
        struct rlimit rl;
        if (!getrlimit(RLIMIT_NOFILE, &rl)) so_kvf(o, "RLIMIT_NOFILE (soft hard)", C_TEAL, "%llu %llu", (unsigned long long)rl.rlim_cur, (unsigned long long)rl.rlim_max);
    }
    so_par(o);
    so_b(o, C_VALUE, "This process  (/proc/self/fd)");
    so_par(o);
    for (i = 0; i < n; i++) {
        char p[64], tg[512] = "";
        ssize_t r;
        snprintf(p, sizeof p, "/proc/self/fd/%s", e[i]);
        r = readlink(p, tg, sizeof tg - 1);
        if (r > 0) tg[r] = 0;
        so_kv(o, e[i], C_VALUE, tg);
    }
    dir_free(e, n > 0 ? n : 0);
    so_par(o);
    np = proc_scan(&ps);
    v = calloc((size_t)(np ? np : 1), sizeof *v);
    for (i = 0; i < np && v; i++) {
        char p[64];
        int c;
        snprintf(p, sizeof p, "/proc/%d/fd", ps[i].pid);
        c = dir_list(p, &e);
        if (c < 0) continue;
        dir_free(e, c);
        v[k].pid = ps[i].pid;
        v[k].n = c;
        snprintf(v[k].comm, sizeof v[k].comm, "%s", ps[i].comm);
        k++;
    }
    free(ps);
    if (v) qsort(v, (size_t)k, sizeof *v, fdcnt_cmp);
    snprintf(b, sizeof b, "Open handles per process - top 30 of %d readable", k);
    so_b(o, C_VALUE, b);
    so_par(o);
    for (i = 0; i < k && i < 30; i++) {
        char pid[16], c[16];
        snprintf(pid, sizeof pid, "%d", v[i].pid);
        snprintf(c, sizeof c, "%d", v[i].n);
        so_row(o, 3, (const char *const[]){ v[i].comm, pid, c }, (const int[]){ C_BLACK, C_PURPLE, C_VALUE });
    }
    free(v);
}

/* ======================================================================
 * [scheduling] [power-policy] [power-scheme]
 * ====================================================================== */
static void s_scheduling(srs_out *o)
{
    static const int tabs[] = { 336 };
    char **e, b[256];
    int n, i, pol = sched_getscheduler(0);
    struct timespec ts = { 0, 0 };
    so_siv_header(o, "Linux Scheduling Parameters");
    so_tabs(o, tabs, 1);
    {
        struct utsname u;
        uname(&u);
        so_kv(o, "Kernel build", C_BLACK, u.version);
    }
    if (rd_str("/sys/kernel/sched_ext/state", b, sizeof b)) {
        char ops[128] = "";
        rd_str("/sys/kernel/sched_ext/root/ops", ops, sizeof ops);
        so_kvf(o, "sched_ext state", !strcmp(b, "enabled") ? C_GREEN : C_GRAY, "%s%s%s", b, *ops ? "  scheduler " : "", ops);
        if (rd_str("/sys/kernel/sched_ext/switch_all", b, sizeof b)) so_kv(o, "sched_ext switch_all", C_VALUE, b);
        if (rd_str("/sys/kernel/sched_ext/enable_seq", b, sizeof b)) so_kv(o, "sched_ext enable_seq", C_VALUE, b);
        if (rd_str("/sys/kernel/sched_ext/nr_rejected", b, sizeof b)) so_kv(o, "sched_ext nr_rejected", C_VALUE, b);
    } else so_kv(o, "sched_ext", C_GRAY, "not available (CONFIG_SCHED_CLASS_EXT off)");
    sh_line("systemctl is-active scx_loader.service scx.service | paste -sd ' '", b, sizeof b);
    so_kv(o, "scx_loader.service / scx.service", C_GRAY, b);
    so_par(o);
    so_b(o, C_VALUE, "Scheduler sysctls  (/proc/sys/kernel/sched_*)");
    so_par(o);
    n = dir_list("/proc/sys/kernel", &e);
    for (i = 0; i < n; i++) {
        char p[256];
        if (strncmp(e[i], "sched_", 6) && strcmp(e[i], "timer_migration") && strcmp(e[i], "numa_balancing")) continue;
        snprintf(p, sizeof p, "/proc/sys/kernel/%s", e[i]);
        if (rd_str(p, b, sizeof b)) so_kv(o, e[i], C_PURPLE, b);
    }
    dir_free(e, n > 0 ? n : 0);
    so_par(o);
    so_b(o, C_VALUE, "This process");
    so_par(o);
    so_kv(o, "Policy", C_TEAL, pol == SCHED_OTHER ? "SCHED_OTHER" : pol == SCHED_FIFO ? "SCHED_FIFO" : pol == SCHED_RR ? "SCHED_RR" : pol == SCHED_BATCH ? "SCHED_BATCH"
                               : pol == SCHED_IDLE ? "SCHED_IDLE" : pol == 7 ? "SCHED_EXT" : "other");
    so_kvf(o, "Nice", C_VALUE, "%d", getpriority(PRIO_PROCESS, 0));
    if (!sched_rr_get_interval(0, &ts)) so_kvf(o, "Time slice (sched_rr_get_interval)", C_VALUE, "%.3f ms", ts.tv_sec * 1e3 + ts.tv_nsec / 1e6);
    rd_str("/proc/self/autogroup", b, sizeof b);
    so_kv(o, "Autogroup", C_VALUE, b);
    rd_str("/proc/cmdline", b, sizeof b);
    {
        char *p = strstr(b, "preempt=");
        if (p) { char *sp = strchr(p, ' '); if (sp) *sp = 0; }
        so_kv(o, "preempt= (cmdline)", C_VALUE, p ? p : "<not set>");
    }
}

static void cpu0_kv(srs_out *o, const char *label, const char *file)
{
    char p[256], b[512];
    snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu0/cpufreq/%s", file);
    if (rd_str(p, b, sizeof b)) so_kv(o, label, C_PURPLE, b);
}

static void s_power_policy(srs_out *o)
{
    static const int tabs[] = { 336 };
    static const struct { const char *l, *p; } f[] = {
        { "amd_pstate status", "/sys/devices/system/cpu/amd_pstate/status" }, { "amd_pstate prefcore", "/sys/devices/system/cpu/amd_pstate/prefcore" },
        { "Boost", "/sys/devices/system/cpu/cpufreq/boost" }, { "intel_pstate status", "/sys/devices/system/cpu/intel_pstate/status" },
        { "cpuidle driver", "/sys/devices/system/cpu/cpuidle/current_driver" }, { "cpuidle governor", "/sys/devices/system/cpu/cpuidle/current_governor" },
        { "ACPI platform_profile", "/sys/firmware/acpi/platform_profile" }, { "platform_profile choices", "/sys/firmware/acpi/platform_profile_choices" },
        { "Sleep states (/sys/power/state)", "/sys/power/state" }, { "mem_sleep", "/sys/power/mem_sleep" }, { "Hibernate (disk)", "/sys/power/disk" },
        { "pm_async", "/sys/power/pm_async" }, { "PCIe ASPM policy", "/sys/module/pcie_aspm/parameters/policy" },
        { "USB autosuspend (s)", "/sys/module/usbcore/parameters/autosuspend" }, { "NMI watchdog", "/proc/sys/kernel/nmi_watchdog" },
        { "laptop_mode", "/proc/sys/vm/laptop_mode" }, { "Wakeup sources armed", "/sys/power/wakeup_count" },
    };
    char b[256], **e;
    int i, n;
    so_siv_header(o, "System Power Policy");
    so_tabs(o, tabs, 1);
    so_b(o, C_VALUE, "CPU frequency (cpu0)");
    so_par(o);
    cpu0_kv(o, "Scaling driver", "scaling_driver");
    cpu0_kv(o, "Governor", "scaling_governor");
    cpu0_kv(o, "Available governors", "scaling_available_governors");
    cpu0_kv(o, "Energy performance preference", "energy_performance_preference");
    cpu0_kv(o, "Available EPP", "energy_performance_available_preferences");
    cpu0_kv(o, "Scaling min (kHz)", "scaling_min_freq");
    cpu0_kv(o, "Scaling max (kHz)", "scaling_max_freq");
    cpu0_kv(o, "Hardware max (kHz)", "cpuinfo_max_freq");
    so_par(o);
    so_b(o, C_VALUE, "Platform");
    so_par(o);
    for (i = 0; i < (int)(sizeof f / sizeof *f); i++) if (rd_str(f[i].p, b, sizeof b)) so_kv(o, f[i].l, C_PURPLE, b);
    so_par(o);
    so_b(o, C_VALUE, "Power supplies  (/sys/class/power_supply)");
    so_par(o);
    n = dir_list("/sys/class/power_supply", &e);
    for (i = 0; i < n; i++) {
        char t[32] = "", st[32] = "", cap[16] = "";
        rd_fmt(t, sizeof t, "/sys/class/power_supply/%s/type", e[i]);
        rd_fmt(st, sizeof st, "/sys/class/power_supply/%s/status", e[i]);
        rd_fmt(cap, sizeof cap, "/sys/class/power_supply/%s/capacity", e[i]);
        so_kvf(o, e[i], C_VALUE, "%s %s%s%s", t, st, *cap ? " " : "", *cap ? cap : "");
    }
    if (n <= 0) so_note(o, "none (desktop on mains power, no battery)");
    dir_free(e, n > 0 ? n : 0);
}

static void s_power_scheme(srs_out *o)
{
    static const int tabs[] = { 336 };
    char b[256], **e;
    int n, i, k = 0;
    struct { char v[64]; int n; } govs[8], epps[8];
    int ng = 0, ne = 0;
    so_siv_header(o, "Active Power Scheme");
    so_tabs(o, tabs, 1);
    sh_line("powerprofilesctl get", b, sizeof b);
    so_kv(o, "power-profiles-daemon", *b ? C_GREEN : C_GRAY, *b ? b : "not running");
    sh_line("tuned-adm active", b, sizeof b);
    so_kv(o, "tuned", *b ? C_GREEN : C_GRAY, *b ? b : "not running");
    if (rd_str("/sys/firmware/acpi/platform_profile", b, sizeof b)) so_kv(o, "ACPI platform_profile", C_PURPLE, b);
    n = dir_list("/sys/devices/system/cpu", &e);
    for (i = 0; i < n; i++) {
        char g[64] = "", p[64] = "";
        int j;
        if (strncmp(e[i], "cpu", 3) || !isdigit((unsigned char)e[i][3])) continue;
        k++;
        rd_fmt(g, sizeof g, "/sys/devices/system/cpu/%s/cpufreq/scaling_governor", e[i]);
        rd_fmt(p, sizeof p, "/sys/devices/system/cpu/%s/cpufreq/energy_performance_preference", e[i]);
        for (j = 0; j < ng && strcmp(govs[j].v, g); j++) ;
        if (j == ng && ng < 8) { snprintf(govs[ng].v, sizeof govs[ng].v, "%s", g); govs[ng++].n = 0; }
        if (j < ng) govs[j].n++;
        for (j = 0; j < ne && strcmp(epps[j].v, p); j++) ;
        if (j == ne && ne < 8) { snprintf(epps[ne].v, sizeof epps[ne].v, "%s", p); epps[ne++].n = 0; }
        if (j < ne) epps[j].n++;
    }
    dir_free(e, n > 0 ? n : 0);
    for (i = 0; i < ng; i++) so_kvf(o, "Governor", C_TEAL, "%s on %d of %d CPUs", *govs[i].v ? govs[i].v : "-", govs[i].n, k);
    for (i = 0; i < ne; i++) so_kvf(o, "EPP", C_TEAL, "%s on %d of %d CPUs", *epps[i].v ? epps[i].v : "-", epps[i].n, k);
    so_par(o);
    so_b(o, C_VALUE, "Available profiles  (powerprofilesctl list)");
    so_par(o);
    if (!sh_lines(o, "powerprofilesctl list", C_VALUE, 40)) so_note(o, "power-profiles-daemon not running");
}

/* ======================================================================
 * time: [time] [times] [sys-tim]
 * ====================================================================== */
static void s_time(srs_out *o)
{
    static const int tabs[] = { 245 };
    time_t now = time(NULL);
    struct tm lt, ut;
    char b[128], tz[128];
    localtime_r(&now, &lt);
    gmtime_r(&now, &ut);
    so_siv_header(o, "Time");
    so_tabs(o, tabs, 1);
    strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S %A", &lt); so_kv(o, "Local Time", C_BLACK, b);
    strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S", &ut);    so_kv(o, "UTC", C_BLACK, b);
    so_kvf(o, "Epoch", C_VALUE, "%lld", (long long)now);
    cur_tz(tz, sizeof tz);
    so_kv(o, "Time Zone", C_PURPLE, *tz ? tz : "UTC");
    strftime(b, sizeof b, "%Z  UTC%z", &lt);
    so_kvf(o, "Abbreviation / Offset", C_PURPLE, "%s  %s", b, lt.tm_isdst > 0 ? "daylight saving time" : "standard time");
    so_kvf(o, "Day of year / ISO week", C_VALUE, "%d / %d", lt.tm_yday + 1, (strftime(b, sizeof b, "%V", &lt), atoi(b)));
    so_par(o);
    so_b(o, C_VALUE, "timedatectl show");
    so_par(o);
    {
        char *t = sh("timedatectl show --no-pager"), *save = NULL, *l;
        for (l = strtok_r(t, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) { char *eq = strchr(l, '='); if (eq) { *eq = 0; so_kv(o, l, C_TEAL, eq + 1); } }
        free(t);
    }
    so_par(o);
    so_b(o, C_VALUE, "Real-time clock  (/sys/class/rtc/rtc0)");
    so_par(o);
    {
        static const char *const r[] = { "name", "date", "time", "since_epoch", "hctosys", "max_user_freq", NULL };
        int i;
        for (i = 0; r[i]; i++) if (rd_fmt(b, sizeof b, "/sys/class/rtc/rtc0/%s", r[i])) so_kv(o, r[i], C_VALUE, b);
    }
    so_par(o);
    {
        char up[32], bt[64];
        fmt_uptime(uptime_s(), up, sizeof up);
        fmt_epoch(boot_time(), bt, sizeof bt);
        so_kv(o, "System Up-time", C_GREEN, up);
        so_kv(o, "Boot Time", C_GREEN, bt);
    }
}

static void s_times(srs_out *o)
{
    static const int tabs[] = { 280 };
    static const struct { clockid_t id; const char *n; } clk[] = {
        { CLOCK_REALTIME, "CLOCK_REALTIME" }, { CLOCK_MONOTONIC, "CLOCK_MONOTONIC" }, { CLOCK_MONOTONIC_RAW, "CLOCK_MONOTONIC_RAW" },
        { CLOCK_BOOTTIME, "CLOCK_BOOTTIME" }, { CLOCK_TAI, "CLOCK_TAI" }, { CLOCK_PROCESS_CPUTIME_ID, "CLOCK_PROCESS_CPUTIME_ID" },
        { CLOCK_THREAD_CPUTIME_ID, "CLOCK_THREAD_CPUTIME_ID" }, { CLOCK_REALTIME_COARSE, "CLOCK_REALTIME_COARSE" }, { CLOCK_MONOTONIC_COARSE, "CLOCK_MONOTONIC_COARSE" },
    };
    struct timespec mono, boot;
    char up[32], b[64];
    int i;
    so_siv_header(o, "Times");
    so_tabs(o, tabs, 1);
    so_row(o, 2, (const char *const[]){ "Clock", "Value (s)  /  Resolution" }, (const int[]){ C_LABEL, C_LABEL });
    for (i = 0; i < (int)(sizeof clk / sizeof *clk); i++) {
        struct timespec t, r;
        if (clock_gettime(clk[i].id, &t) || clock_getres(clk[i].id, &r)) continue;
        so_kvf(o, clk[i].n, C_BLACK, "%lld.%09ld  /  %ld ns", (long long)t.tv_sec, t.tv_nsec, r.tv_nsec + r.tv_sec * 1000000000L);
    }
    clock_gettime(CLOCK_MONOTONIC, &mono);
    clock_gettime(CLOCK_BOOTTIME, &boot);
    fmt_uptime((double)(boot.tv_sec - mono.tv_sec), up, sizeof up);
    so_kv(o, "Time spent suspended", C_TEAL, up);
    rd_str("/proc/uptime", b, sizeof b);
    so_kv(o, "/proc/uptime (up idle)", C_VALUE, b);
    {
        struct rusage ru;
        if (!getrusage(RUSAGE_SELF, &ru))
            so_kvf(o, "This process user / system", C_VALUE, "%ld.%06ld / %ld.%06ld s", (long)ru.ru_utime.tv_sec, (long)ru.ru_utime.tv_usec, (long)ru.ru_stime.tv_sec,
                   (long)ru.ru_stime.tv_usec);
    }
    so_par(o);
    so_b(o, C_VALUE, "Time synchronisation  (timedatectl show-timesync / timesync-status)");
    so_par(o);
    {
        char *t = sh("timedatectl show-timesync --all --no-pager"), *save = NULL, *l;
        int n = 0;
        for (l = strtok_r(t, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) { char *eq = strchr(l, '='); if (eq) { *eq = 0; so_kv(o, l, C_TEAL, eq + 1); n++; } }
        free(t);
        if (!n) so_note(o, "systemd-timesyncd not running");
    }
    so_par(o);
    sh_lines(o, "timedatectl timesync-status --no-pager", C_VALUE, 30);
}

static void s_sys_tim(srs_out *o)
{
    static const int tabs[] = { 280 };
    char b[256], **e;
    int n, i;
    so_siv_header(o, "System Timers");
    so_tabs(o, tabs, 1);
    if (rd_str("/sys/devices/system/clocksource/clocksource0/current_clocksource", b, sizeof b)) so_kv(o, "Current clocksource", C_GREEN, b);
    if (rd_str("/sys/devices/system/clocksource/clocksource0/available_clocksource", b, sizeof b)) so_kv(o, "Available clocksources", C_PURPLE, b);
    {
        struct timespec r;
        clock_getres(CLOCK_MONOTONIC, &r);
        so_kvf(o, "hrtimer resolution", C_VALUE, "%ld ns", r.tv_nsec);
        clock_getres(CLOCK_MONOTONIC_COARSE, &r);
        so_kvf(o, "Coarse (tick) resolution", C_VALUE, "%.3f ms", r.tv_nsec / 1e6);
    }
    if (rd_str("/proc/sys/kernel/timer_migration", b, sizeof b)) so_kv(o, "timer_migration", C_VALUE, b);
    {
        char *t;
        if (rd_all("/proc/cpuinfo", &t) >= 0) {
            char *fl = strstr(t, "\nflags");
            char *end = fl ? strchr(fl + 1, '\n') : NULL;
            if (end) *end = 0;
            so_kvf(o, "TSC flags", C_VALUE, "%s%s%s%s", fl && strstr(fl, " tsc ") ? "tsc " : "", fl && strstr(fl, " constant_tsc") ? "constant_tsc " : "",
                   fl && strstr(fl, " nonstop_tsc") ? "nonstop_tsc " : "", fl && strstr(fl, " tsc_deadline_timer") ? "tsc_deadline_timer" : "");
            free(t);
        }
    }
    so_par(o);
    so_b(o, C_VALUE, "Clock event devices  (/sys/devices/system/clockevents)");
    so_par(o);
    n = dir_list("/sys/devices/system/clockevents", &e);
    {
        char last[64] = "";
        int run = 0;
        for (i = 0; i <= n; i++) {
            char d[64] = "";
            if (i < n && !strncmp(e[i], "clockevent", 10) && isdigit((unsigned char)e[i][10])) rd_fmt(d, sizeof d, "/sys/devices/system/clockevents/%s/current_device", e[i]);
            else if (i < n) continue;
            if (i == n || strcmp(d, last)) {
                if (run) so_kvf(o, last, C_TEAL, "%d CPU clockevent device%s", run, run == 1 ? "" : "s");
                snprintf(last, sizeof last, "%s", d);
                run = 0;
            }
            run++;
        }
        if (rd_str("/sys/devices/system/clockevents/broadcast/current_device", b, sizeof b)) so_kv(o, "broadcast", C_TEAL, b);
    }
    dir_free(e, n > 0 ? n : 0);
    so_par(o);
    so_b(o, C_VALUE, "Kernel timer configuration");
    so_par(o);
    kernel_cfg_grep(o, "^CONFIG_(HZ|HZ_[0-9]+|NO_HZ[A-Z_]*|HIGH_RES_TIMERS|TICK_[A-Z_]*|HPET[A-Z_]*)=", C_PURPLE);
    rd_str("/proc/cmdline", b, sizeof b);
    so_kv(o, "nohz / tsc on cmdline", C_VALUE, strstr(b, "nohz") || strstr(b, "tsc=") ? b : "<none>");
}

/* ======================================================================
 * crash reporting: [watson] [wer]
 * ====================================================================== */
static void conf_lines(srs_out *o, const char *path)
{
    char *t, *save = NULL, *l;
    int any = 0;
    if (rd_all(path, &t) < 0) return;
    for (l = strtok_r(t, "\n", &save); l; l = strtok_r(NULL, "\n", &save))
        if (*ltrim(l) && *ltrim(l) != '#' && *ltrim(l) != ';') { so_t(o, C_PURPLE, l); so_tab(o); so_t(o, C_GRAY, path); so_par(o); any = 1; }
    if (!any) { so_t(o, C_GRAY, "(defaults only)"); so_tab(o); so_t(o, C_GRAY, path); so_par(o); }
    free(t);
}

static void s_watson(srs_out *o)
{
    static const int tabs[] = { 280 };
    static const char *const k[] = { "core_pattern", "core_pipe_limit", "core_uses_pid", "print-fatal-signals", "panic_on_oops", "panic", NULL };
    char b[512], **e;
    int i, n;
    struct rlimit rl;
    so_siv_header(o, "Crash Handler (core dumps)");
    so_tabs(o, tabs, 1);
    for (i = 0; k[i]; i++) { char p[96]; snprintf(p, sizeof p, "/proc/sys/kernel/%s", k[i]); if (rd_str(p, b, sizeof b)) so_kv(o, k[i], C_PURPLE, b); }
    if (rd_str("/proc/sys/fs/suid_dumpable", b, sizeof b)) so_kv(o, "fs.suid_dumpable", C_PURPLE, b);
    if (!getrlimit(RLIMIT_CORE, &rl)) {
        if (rl.rlim_cur == RLIM_INFINITY) so_kv(o, "RLIMIT_CORE (this process)", C_TEAL, "unlimited");
        else so_kvf(o, "RLIMIT_CORE (this process)", C_TEAL, "%llu", (unsigned long long)rl.rlim_cur);
    }
    sh_line("systemctl is-active systemd-coredump.socket", b, sizeof b);
    so_kv(o, "systemd-coredump.socket", !strcmp(b, "active") ? C_GREEN : C_GRAY, b);
    so_par(o);
    so_b(o, C_VALUE, "coredump.conf");
    so_par(o);
    conf_lines(o, "/etc/systemd/coredump.conf");
    n = dir_list("/etc/systemd/coredump.conf.d", &e);
    for (i = 0; i < n; i++) { char p[512]; snprintf(p, sizeof p, "/etc/systemd/coredump.conf.d/%s", e[i]); conf_lines(o, p); }
    dir_free(e, n > 0 ? n : 0);
    n = dir_list("/usr/lib/systemd/coredump.conf.d", &e);
    for (i = 0; i < n; i++) { char p[512]; snprintf(p, sizeof p, "/usr/lib/systemd/coredump.conf.d/%s", e[i]); conf_lines(o, p); }
    dir_free(e, n > 0 ? n : 0);
}

static void s_wer(srs_out *o)
{
    static const int tabs[] = { 280 };
    char **e, b[64];
    int n, i;
    long long tot = 0;
    so_siv_header(o, "Error Reporting (systemd-coredump)");
    so_tabs(o, tabs, 1);
    n = dir_list("/var/lib/systemd/coredump", &e);
    for (i = 0; i < n; i++) {
        char p[600];
        struct stat st;
        snprintf(p, sizeof p, "/var/lib/systemd/coredump/%s", e[i]);
        if (!stat(p, &st)) tot += st.st_size;
    }
    fmt_bytes((double)tot, b, sizeof b);
    so_kvf(o, "Stored core dumps", C_VALUE, "%d files, %s in /var/lib/systemd/coredump", n > 0 ? n : 0, b);
    dir_free(e, n > 0 ? n : 0);
    so_par(o);
    so_b(o, C_VALUE, "Most recent crashes  (coredumpctl list -r -n 30)");
    so_par(o);
    if (!sh_lines(o, "coredumpctl list --no-pager -r -n 30", C_VALUE, 0)) so_note(o, "no core dumps recorded (or journal not readable)");
}

/* ======================================================================
 * [integrity] [boot-mode] [ms-dos]
 * ====================================================================== */
static void s_integrity(srs_out *o)
{
    static const int tabs[] = { 336 };
    static const struct { const char *l, *p; } f[] = {
        { "module.sig_enforce", "/sys/module/module/parameters/sig_enforce" }, { "kernel.modules_disabled", "/proc/sys/kernel/modules_disabled" },
        { "kernel.kexec_load_disabled", "/proc/sys/kernel/kexec_load_disabled" }, { "kernel.kptr_restrict", "/proc/sys/kernel/kptr_restrict" },
        { "kernel.dmesg_restrict", "/proc/sys/kernel/dmesg_restrict" }, { "kernel.perf_event_paranoid", "/proc/sys/kernel/perf_event_paranoid" },
        { "kernel.unprivileged_bpf_disabled", "/proc/sys/kernel/unprivileged_bpf_disabled" }, { "kernel.yama.ptrace_scope", "/proc/sys/kernel/yama/ptrace_scope" },
        { "kernel.randomize_va_space", "/proc/sys/kernel/randomize_va_space" }, { "Active LSMs", "/sys/kernel/security/lsm" },
        { "TPM version", "/sys/class/tpm/tpm0/tpm_version_major" },
    };
    char b[256];
    int i;
    so_siv_header(o, "Code Integrity Information");
    so_tabs(o, tabs, 1);
    if (rd_str("/sys/kernel/security/lockdown", b, sizeof b)) so_kv(o, "Kernel lockdown", C_PURPLE, b);
    else so_kv(o, "Kernel lockdown", C_GRAY, "lockdown LSM not built/active");
    sh_line("mokutil --sb-state", b, sizeof b);
    so_kv(o, "Secure Boot (mokutil)", strstr(b, "enabled") ? C_GREEN : C_ORANGE, *b ? b : "unknown");
    for (i = 0; i < (int)(sizeof f / sizeof *f); i++) if (rd_str(f[i].p, b, sizeof b)) so_kv(o, f[i].l, C_PURPLE, b);
    so_kv(o, "IMA", C_VALUE, path_exists("/sys/kernel/security/ima") ? "present" : "not active");
    so_par(o);
    taint_table(o);
}

static void s_boot_mode(srs_out *o)
{
    static const int tabs[] = { 280 };
    char b[1024];
    so_siv_header(o, "Boot Mode");
    so_tabs(o, tabs, 1);
    so_kv(o, "Firmware", C_GREEN, path_exists("/sys/firmware/efi") ? "UEFI" : "Legacy BIOS (CSM)");
    if (rd_str("/sys/firmware/efi/fw_platform_size", b, sizeof b)) so_kvf(o, "UEFI platform size", C_VALUE, "%s-bit", b);
    sh_line("mokutil --sb-state", b, sizeof b);
    so_kv(o, "Secure Boot", C_VALUE, *b ? b : "unknown");
    sh_line("systemctl get-default", b, sizeof b);
    so_kv(o, "Default target", C_PURPLE, b);
    rd_str("/proc/cmdline", b, sizeof b);
    so_kv(o, "Kernel command line", C_PURPLE, b);
    so_par(o);
    so_b(o, C_VALUE, "Boot loader  (bootctl status)");
    so_par(o);
    sh_lines(o, "bootctl status --no-pager | sed -n '/^System:/,/^Current Boot Loader:/p;/Product:/p' | grep -v '^Current Boot Loader:' | head -15", C_VALUE, 20);
    so_par(o);
    so_b(o, C_VALUE, "Safe-mode equivalents (systemd)");
    so_par(o);
    so_kv(o, "systemd.unit=rescue.target", C_GRAY, "single-user, local filesystems mounted (Safe Mode)");
    so_kv(o, "systemd.unit=emergency.target", C_GRAY, "root shell, root filesystem read-only (Safe Mode minimal)");
    so_kv(o, "systemd.unit=multi-user.target", C_GRAY, "no graphical session, networking up (Safe Mode with Networking)");
}

static void s_ms_dos(srs_out *o)
{
    static const int tabs[] = { 560 };
    static const char *const dirs[] = { "by-label", "by-uuid", "by-partlabel", "by-partuuid", "by-id", "by-path", NULL };
    int k;
    so_siv_header(o, "Device Name Mappings (/dev/disk)");
    so_tabs(o, tabs, 1);
    for (k = 0; dirs[k]; k++) {
        char d[64], **e;
        int n, i;
        snprintf(d, sizeof d, "/dev/disk/%s", dirs[k]);
        n = dir_list(d, &e);
        if (n <= 0) { dir_free(e, n > 0 ? n : 0); continue; }
        so_b(o, C_VALUE, d);
        so_par(o);
        for (i = 0; i < n; i++) {
            char p[512], t[256] = "";
            ssize_t r;
            snprintf(p, sizeof p, "%s/%s", d, e[i]);
            r = readlink(p, t, sizeof t - 1);
            if (r > 0) t[r] = 0;
            so_t(o, C_BLACK, e[i]);
            so_tab(o);
            so_f(o, C_PURPLE, "/dev/%s", base_name(t));
            so_par(o);
        }
        so_par(o);
        dir_free(e, n);
    }
}

/* ======================================================================
 * vendor service analogues: [acs-srv] [cl4-srv] [cue-srv] [rst-srv]
 * ====================================================================== */
static void svc_probe(srs_out *o, const char *title, const char *const *units, const char *const *mods, const char *const *procs, const char *extra_file)
{
    static const int tabs[] = { 336, 420 };
    int i;
    so_siv_header(o, title);
    so_tabs(o, tabs, 2);
    so_kv(o, "Windows service", C_GRAY, "not applicable on Linux - showing the Linux analogue");
    so_par(o);
    for (i = 0; units && units[i]; i++) {
        char *a = NULL, *e = NULL;
        const char *aa[] = { "timeout", "5", "systemctl", "is-active", units[i], NULL };
        const char *ee[] = { "timeout", "5", "systemctl", "is-enabled", units[i], NULL };
        run_out(&a, aa);
        run_out(&e, ee);
        if (a) rtrim(a);
        if (e) rtrim(e);
        so_t(o, C_BLACK, units[i]);
        so_tab(o);
        so_t(o, a && !strcmp(a, "active") ? C_GREEN : C_GRAY, a && *a ? a : "-");
        so_tab(o);
        so_t(o, C_GRAY, e && *e ? e : "not installed");
        so_par(o);
        free(a);
        free(e);
    }
    for (i = 0; mods && mods[i]; i++) {
        char p[128];
        snprintf(p, sizeof p, "/sys/module/%s", mods[i]);
        so_t(o, C_BLACK, "module ");
        so_t(o, C_BLACK, mods[i]);
        so_tab(o);
        so_t(o, path_exists(p) ? C_GREEN : C_GRAY, path_exists(p) ? "loaded" : "not loaded");
        so_par(o);
    }
    if (procs) {
        proc_t *v;
        int n = proc_scan(&v), k;
        for (i = 0; procs[i]; i++) {
            int pid = 0;
            for (k = 0; k < n; k++) if (!strcmp(v[k].comm, procs[i])) { pid = v[k].pid; break; }
            so_t(o, C_BLACK, "process ");
            so_t(o, C_BLACK, procs[i]);
            so_tab(o);
            if (pid) so_f(o, C_GREEN, "running PID %d", pid); else so_t(o, C_GRAY, "not running");
            so_par(o);
        }
        free(v);
    }
    if (extra_file) {
        char *t;
        so_par(o);
        so_b(o, C_VALUE, extra_file);
        so_par(o);
        if (rd_all(extra_file, &t) >= 0) { so_lines(o, C_VALUE, t); free(t); } else so_note(o, "not present");
    }
}

static void s_acs_srv(srs_out *o)
{
    static const char *const u[] = { "asusd.service", "supergfxd.service", NULL };
    static const char *const m[] = { "asus_wmi", "asus_nb_wmi", "asus_ec_sensors", "asus_wmi_sensors", "asus_armoury", NULL };
    svc_probe(o, "ASUS Control Service (Linux: asusd / asus WMI drivers)", u, m, NULL, NULL);
}
static void s_corsair_srv(srs_out *o)
{
    static const char *const u[] = { "coolercontrold.service", "coolercontrol-liqctld.service", "liquidcfg.service", NULL };
    static const char *const m[] = { "corsair_cpro", "corsair_psu", "nzxt_kraken3", "nzxt_smart2", NULL };
    static const char *const p[] = { "coolercontrold", "coolercontrol-l", "liquidctl", NULL };
    svc_probe(o, "Corsair Service (Linux: CoolerControl / liquidctl / hwmon drivers)", u, m, p, NULL);
}
static void s_rst_srv(srs_out *o)
{
    static const char *const u[] = { "mdmonitor.service", "mdadm.service", NULL };
    static const char *const m[] = { "md_mod", "raid0", "raid1", "raid456", "dm_raid", "vmd", NULL };
    svc_probe(o, "Intel RST Service (Linux: md RAID / IMSM / VMD)", u, m, NULL, "/proc/mdstat");
}

/* ======================================================================
 * table (CHECKLIST.tsv group "os")
 * ====================================================================== */
const srs_item sec_os[] = {
    { "acs-srv", "windows", "ASUS control service", B_ABSENT, 0, "systemctl list-units (no such service)", s_acs_srv, 0, "os" },
    { "boot-mode", "windows", "Boot mode options", B_LINUX, 0, "/proc/cmdline; systemctl get-default; /sys/firmware/efi", s_boot_mode, 0, "os" },
    { "cl4-srv", "windows", "Corsair Link 4 service", B_ABSENT, 0, "n/a (Windows service); via liquidctl/coolercontrol analogue", s_corsair_srv, 0, "os" },
    { "clsid", "windows", "Shell CLSIDs", B_WINONLY, 0, "none; nearest xdg-mime; /usr/share/applications", s_clsid, 0, "os" },
    { "codepage", "windows", "Code pages", B_LINUX, 0, "/usr/share/i18n/charmaps; iconv -l", s_codepage, 0, "os" },
    { "country", "windows", "TAPI country list", B_LINUX, 0, "/usr/share/zoneinfo/iso3166.tab; locale", s_country, 0, "os" },
    { "cue-srv", "windows", "Corsair service", B_ABSENT, 0, "n/a (Windows service); via liquidctl/coolercontrol analogue", s_corsair_srv, 0, "os" },
    { "dll", "windows", "Base DLLs", B_LINUX, 0, "/proc/self/maps; ldd", s_dll, 0, "os" },
    { "dll-addr", "windows", "DLLs by address", B_LINUX, 0, "/proc/self/maps", s_dll_addr, 0, "os" },
    { "dll-known", "windows", "Known DLLs", B_LINUX, 0, "/etc/ld.so.cache; ldconfig -p", s_dll_known, 0, "os" },
    { "dll-name", "windows", "DLLs by name", B_LINUX, 0, "/proc/self/maps", s_dll_name, 0, "os" },
    { "drivers", "windows", "Drivers", B_LINUX, 0, "/proc/modules; /sys/module", s_drivers, 0, "os" },
    { "driver-vsn", "windows", "Driver versions", B_LINUX, 0, "modinfo; /sys/module", s_driver_vsn, 0, "os" },
    { "e-mail", "windows", "E-mail accounts", B_WINONLY, 0, "none", s_email, 0, "os" },
    { "environment", "windows", "Environment", B_LINUX, 0, "/proc/self/environ; /etc/environment", s_environment, 0, "os" },
    { "esu-sts", "windows", "ESU status", B_WINONLY, 0, "none", s_esu_sts, 0, "os" },
    { "file-cache", "windows", "System file cache", B_LINUX, 0, "/proc/meminfo; /proc/vmstat", s_file_cache, 1000, "os" },
    { "files", "windows", "Open server files", B_LINUX, 1, "smbstatus -L; lslocks", s_files, 0, "os" },
    { "fmo-sts", "windows", "Feature management overrides", B_WINONLY, 0, "none", NULL, 0, "os" },
    { "folders", "windows", "Folder paths", B_LINUX, 0, "~/.config/user-dirs.dirs; xdg-user-dir", s_folders, 0, "os" },
    { "fonts", "windows", "Font families", B_LINUX, 0, "fc-list", s_fonts, 0, "os" },
    { "groups", "windows", "Groups", B_LINUX, 0, "/etc/group; getent group", s_groups, 0, "os" },
    { "handle", "windows", "Handles", B_LINUX, 0, "/proc/self/fd; lsof", s_handle, 0, "os" },
    { "heap", "windows", "Process heap", B_LINUX, 0, "/proc/self/smaps", s_heap, 0, "os" },
    { "hives", "windows", "Registry hives", B_WINONLY, 0, "none", s_hives, 0, "os" },
    { "integrity", "windows", "Code integrity", B_LINUX, 0, "/sys/module/module/parameters/sig_enforce; /proc/sys/kernel/tainted; mokutil --sb-state", s_integrity, 0, "os" },
    { "key-names", "windows", "Key names", B_LINUX, 0, "xkbcli; /usr/share/X11/xkb/rules/evdev.lst; dumpkeys", s_key_names, 0, "os" },
    { "locales", "windows", "Locales", B_LINUX, 0, "locale -a; localectl list-locales", s_locales, 0, "os" },
    { "mem-heap", "windows", "Memory heap", B_LINUX, 0, "/proc/self/smaps; /proc/self/status", s_mem_heap, 0, "os" },
    { "mem-map", "windows", "Memory mapping", B_LINUX, 0, "/proc/self/maps", s_mem_map, 0, "os" },
    { "metrics", "windows", "System metrics", B_WINONLY, 0, "none; nearest kscreen-doctor -o", s_metrics, 0, "os" },
    { "mlang", "windows", "MLang conversions", B_WINONLY, 0, "none; nearest iconv -l", s_mlang, 0, "os" },
    { "ms-dos", "windows", "DOS device names", B_LINUX, 0, "/dev/disk/by-id; /dev/disk/by-uuid", s_ms_dos, 0, "os" },
    { "n2k-srv", "windows", "N2K service", B_ABSENT, 0, "n/a (Windows vendor service)", NULL, 0, "os" },
    { "nls-info", "windows", "NLS code pages", B_LINUX, 0, "/usr/share/i18n/charmaps", s_nls_info, 0, "os" },
    { "paging", "windows", "Paging files", B_LINUX, 0, "/proc/swaps; /proc/sys/vm/swappiness", s_paging, 2000, "os" },
    { "passwords", "windows", "Credentials", B_WINONLY, 0, "none; nearest secret-tool", s_passwords, 0, "os" },
    { "pending", "windows", "Pending file renames", B_WINONLY, 0, "none", s_pending, 0, "os" },
    { "plm-srv", "windows", "ProLiant monitor service", B_ABSENT, 0, "n/a (HP ProLiant)", NULL, 0, "os" },
    { "policies", "windows", "Windows policies", B_WINONLY, 0, "none; nearest /etc/polkit-1/rules.d", s_policies, 0, "os" },
    { "pools", "windows", "Memory pools", B_LINUX, 0, "/proc/meminfo; /proc/vmstat", s_pools, 1000, "os" },
    { "power-policy", "windows", "Power policy", B_LINUX, 0, "cpupower frequency-info; /sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference; powerprofilesctl", s_power_policy, 0, "os" },
    { "power-scheme", "windows", "Power scheme", B_LINUX, 0, "powerprofilesctl; tuned-adm active", s_power_scheme, 0, "os" },
    { "priv", "windows", "Privileges", B_LINUX, 0, "/proc/self/status; capsh --print", s_priv, 0, "os" },
    { "processes", "windows", "Processes", B_LINUX, 0, "/proc", s_processes, 0, "os" },
    { "product-ids", "windows", "Product IDs", B_WINONLY, 0, "none; nearest /etc/machine-id", s_product_ids, 0, "os" },
    { "products", "windows", "Product keys", B_WINONLY, 0, "none", NULL, 0, "os" },
    { "ranges", "windows", "Font ranges", B_LINUX, 0, "fc-query; fc-list", s_ranges, 0, "os" },
    { "rdp-info", "windows", "RDP session info", B_LINUX, 0, "loginctl list-sessions", s_rdp_info, 0, "os" },
    { "reg-path", "tuning", "Remote registry paths", B_WINONLY, 0, "none", NULL, 0, "os" },
    { "reg-srv", "windows", "Remote registry service", B_WINONLY, 0, "none; nearest systemctl status sshd", s_reg_srv, 0, "os" },
    { "render-format", "windows", "Text converter render format", B_WINONLY, 0, "none", NULL, 0, "os" },
    { "rst-srv", "windows", "Intel RST service", B_ABSENT, 0, "n/a (Intel RST)", s_rst_srv, 0, "os" },
    { "scheduling", "windows", "Scheduling", B_LINUX, 0, "/proc/sys/kernel/sched_autogroup_enabled; /sys/kernel/sched_ext; chrt", s_scheduling, 0, "os" },
    { "services-drv", "windows", "Driver services", B_LINUX, 0, "/proc/modules; /sys/module", s_services_drv, 0, "os" },
    { "services-w32", "windows", "Win32 services", B_LINUX, 0, "systemctl list-units --type=service", s_services_w32, 0, "os" },
    { "sessions", "windows", "Sessions", B_LINUX, 0, "loginctl; smbstatus -b", s_sessions, 0, "os" },
    { "setup", "windows", "Setup directories", B_LINUX, 0, "/etc/os-release; /boot", s_setup, 0, "os" },
    { "setup-api", "windows", "Setup API logging", B_WINONLY, 0, "none; nearest udevadm control", s_setup_api, 0, "os" },
    { "signing", "windows", "Driver signing", B_LINUX, 0, "modinfo; /sys/module/module/parameters/sig_enforce", s_signing, 0, "os" },
    { "software", "windows", "Installed software", B_LINUX, 0, "pacman -Q; flatpak list", s_software, 0, "os" },
    { "startup", "windows", "Startup programs", B_LINUX, 0, "/etc/xdg/autostart; ~/.config/autostart; systemctl list-unit-files", s_startup, 0, "os" },
    { "system", "hardware", "System", B_LINUX, 0, "/etc/os-release; uname", s_windows, 0, "os" },
    { "sys-tim", "windows", "System timers", B_LINUX, 0, "/sys/devices/system/clocksource/clocksource0; /sys/devices/system/clockevents", s_sys_tim, 0, "os" },
    { "sys-win", "windows", "System windows", B_WINONLY, 0, "none; nearest wmctrl -l", s_sys_win, 0, "os" },
    { "tid-win", "windows", "Thread windows", B_WINONLY, 0, "none; nearest wmctrl -lp", s_tid_win, 0, "os" },
    { "time", "windows", "Time", B_LINUX, 0, "timedatectl; /etc/localtime", s_time, 0, "os" },
    { "times", "windows", "Times", B_LINUX, 0, "timedatectl show-timesync", s_times, 0, "os" },
    { "users", "windows", "Users", B_LINUX, 0, "/etc/passwd; getent passwd", s_users, 0, "os" },
    { "verifier", "windows", "Driver verifier", B_WINONLY, 0, "none; nearest /proc/sys/kernel/tainted", s_verifier, 0, "os" },
    { "virtual", "windows", "Virtual memory", B_LINUX, 0, "/proc/meminfo; /proc/self/status", s_virtual, 0, "os" },
    { "vs-paths", "windows", "Visual Studio paths", B_WINONLY, 0, "none", s_vs_paths, 0, "os" },
    { "watson", "windows", "Dr Watson / crash debugger", B_LINUX, 0, "/proc/sys/kernel/core_pattern; coredumpctl", s_watson, 0, "os" },
    { "wer", "windows", "Error reporting", B_LINUX, 0, "/etc/systemd/coredump.conf; coredumpctl", s_wer, 0, "os" },
    { "win-ce", "windows", "Windows CE devices", B_WINONLY, 0, "none", NULL, 0, "os" },
    { "windows", "windows", "Windows (OS) panel", B_LINUX, 0, "/etc/os-release; uname; hostnamectl", s_windows, 0, "os" },
    { "winsat", "windows", "WinSAT", B_WINONLY, 0, "none", NULL, 0, "os" },
    { "zones", "windows", "Time zones", B_LINUX, 0, "timedatectl list-timezones; /usr/share/zoneinfo", s_zones, 0, "os" },
    { NULL, NULL, NULL, B_LINUX, 0, NULL, NULL, 0, NULL }
};
