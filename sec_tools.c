/* sec_tools.c - SIV tools: [benchmark*] [ipi-*] [latency] [lookup] [ping] [reg-find] and the
 * srs-gui tools [id-lookup] [find] [all-dev] [burnin] [cpu-stress].
 * Benchmarks never run from -save: sections show the last saved result (bench.c caches it);
 * runs start only from the GUI Run buttons or the explicit CLI commands. */
#define _GNU_SOURCE
#include "sec.h"
#include "srs.h"
#include "bench.h"
#include "burner.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void not_run(srs_out *o, const char *what, const char *cli)
{
    static const int tabs[] = { 160 };
    so_tabs(o, tabs, 1);
    so_kv(o, "Status", C_ORANGE, "Not run yet");
    so_kv(o, "Run it", C_PURPLE, what);
    so_kv(o, "Command line", C_PURPLE, cli);
    so_note(o, "Benchmarks are never started by -save; this section shows the last result.");
}

static void s_bench_mode(srs_out *o, bench_mode m)
{
    char cli[96];
    if (bench_show_last(bench_mode_id(m), o)) return;
    so_siv_header(o, "CPU Benchmark");
    so_kv(o, "Mode", C_TEAL, bench_mode_desc(m));
    if (m == BM_EC || m == BM_EP) {
        unsigned char is_e[1024];
        long nc = sysconf(_SC_NPROCESSORS_CONF);
        if (!bench_ecores(is_e, nc > 1024 ? 1024 : (int)nc)) {
            so_kv(o, "Status", C_ORANGE, "No efficiency cores detected on this CPU");
            so_kv(o, "Checked", C_PURPLE, "/sys/devices/cpu_atom/cpus (Intel hybrid CPUs)");
            return;
        }
    }
    snprintf(cli, sizeof cli, "srs-gui bench %s", bench_mode_id(m) + 10);
    not_run(o, "Menu > Tools > Benchmark, then Run", cli);
}
#define BM(fn, m) static void fn(srs_out *o) { s_bench_mode(o, m); }
BM(s_b_ac, BM_AC) BM(s_b_as, BM_AS) BM(s_b_dd, BM_DD) BM(s_b_dw, BM_DW) BM(s_b_ec, BM_EC)
BM(s_b_ep, BM_EP) BM(s_b_lc, BM_LC) BM(s_b_lp, BM_LP) BM(s_b_pc, BM_PC) BM(s_b_pp, BM_PP)

static void s_benchmark(srs_out *o)
{
    static const int tabs[] = { 140, 820 };
    int m;
    so_siv_header(o, "CPU Benchmarks (Dhrystone 2.1 DMIPS / Whetstone MWIPS)");
    so_tabs(o, tabs, 2);
    so_row(o, 3, (const char *const[]){ "Mode", "Description", "Last result" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL });
    for (m = 0; m < BM_COUNT; m++) {
        char path[1100], *txt, last[96] = "not run";
        const char *h = getenv("HOME");
        snprintf(path, sizeof path, "%s/.cache/srs/bench-%s.txt", h ? h : "", bench_mode_id((bench_mode)m));
        if (rd_all(path, &txt) >= 0) {
            char *t = strstr(txt, "Total");
            if (t) { snprintf(last, sizeof last, "%.80s", t + 5); last[strcspn(last, "\n")] = 0; }
            free(txt);
        }
        so_row(o, 3, (const char *const[]){ bench_mode_id((bench_mode)m) + 10, bench_mode_desc((bench_mode)m), last },
               (const int[]){ C_PURPLE, C_VALUE, C_BLACK });
    }
}

static void s_ipi(srs_out *o)
{
    if (bench_show_last("ipi-delay", o)) return;
    so_siv_header(o, "CPU IPI Delay");
    not_run(o, "Menu > Tools > IPI Delay, then Run", "srs-gui ipi [--cpus N] [--rounds R]");
}

static void s_latency(srs_out *o)
{
    if (bench_show_last("latency", o)) return;
    so_siv_header(o, "Memory Latency");
    not_run(o, "Menu > Tools > Latency, then Run", "srs-gui latency [--max MB]");
}

static void s_pi(srs_out *o)
{
    if (bench_show_last("pi", o)) return;
    so_siv_header(o, "SRS Pi");
    not_run(o, "Benchmarks & stress > SRS Pi, then Run", "srs-gui pi [--digits 1m] [--threads N] [--method chudnovsky|agm]");
}

static void s_crypto(srs_out *o)
{
    if (bench_show_last("crypto", o)) return;
    so_siv_header(o, "Crypto benchmark");
    not_run(o, "Benchmarks & stress > Crypto, then Run", "srs-gui crypto [--threads N] [--seconds S]");
}

static void s_prime(srs_out *o)
{
    if (bench_show_last("prime", o)) return;
    so_siv_header(o, "Prime sieve");
    not_run(o, "Benchmarks & stress > Prime Sieve, then Run", "srs-gui prime [--limit 1e10] [--threads N]");
}

/* [bench-history] - the run history of SRS Pi, Crypto and Prime sieve, newest first */
static void s_bench_history(srs_out *o)
{
    static const int tabs[] = { 150, 220, 520, 980 };
    char path[1100], *txt, *lines[400];
    const char *h = getenv("HOME");
    int n = 0, i;
    so_siv_header(o, "Benchmark history (newest first)");
    snprintf(path, sizeof path, "%s/.cache/srs/bench-history.tsv", h ? h : "");
    if (rd_all(path, &txt) < 0) {
        so_kv(o, "Status", C_ORANGE, "No runs yet");
        so_note(o, "Every run of SRS Pi, Crypto and Prime Sieve adds a line to ~/.cache/srs/bench-history.tsv.");
        return;
    }
    {
        char *save = NULL, *l;
        for (l = strtok_r(txt, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
            if (!strncmp(l, "time\t", 5)) continue;
            if (n == 400) { memmove(lines, lines + 1, sizeof lines - sizeof *lines); n--; }
            lines[n++] = l;
        }
    }
    so_tabs(o, tabs, 4);
    so_row(o, 5, (const char *const[]){ "When", "Test", "Settings", "Result", "Build" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    for (i = n - 1; i >= 0; i--) {
        char *c[5] = { "", "", "", "", "" }, *save = NULL, *f;
        int k = 0;
        for (f = strtok_r(lines[i], "\t", &save); f && k < 5; f = strtok_r(NULL, "\t", &save)) c[k++] = f;
        so_row(o, 5, (const char *const *)c, (const int[]){ C_GRAY, C_PURPLE, C_VALUE, strstr(c[3], "WRONG") ? C_RED : C_BLACK, C_GRAY });
    }
    so_par(o);
    so_note(o, "Compare results only between runs with the same settings and the same Build: a package built for any x86-64 CPU "
               "scores lower than one built for this CPU (-march=native).");
    free(txt);
}

/* [lookup] - SIV's Lookup panel: the name-database files and program files */
static void file_row(srs_out *o, const char *desc, const char *path, const char *ident, const char *url)
{
    so_row(o, 5, (const char *const[]){ desc, path, ident, "UTF-8", url }, (const int[]){ C_LABEL, C_PURPLE, C_TEAL, C_ORANGE, C_VALUE });
}

static void ids_ident(const char *path, char *out, size_t sz)
{
    char *txt, ver[64] = "?", date[64] = "?", *p;
    long n = 0;
    struct stat st;
    snprintf(out, sz, "not found");
    if (rd_all(path, &txt) < 0) return;
    for (p = txt; *p; ) {
        char *e = strchr(p, '\n');
        if (*p == '#') {
            const char *q = p + 1;
            while (*q == ' ' || *q == '\t') q++;
            if (!strncmp(q, "Version:", 8)) sscanf(q + 8, " %63s", ver);
            else if (!strncmp(q, "Date:", 5)) sscanf(q + 5, " %63[^\n]", date);
        } else if (*p != '\n') n++;   /* every record line (vendors, devices, subsystems) */
        if (!e) break;
        p = e + 1;
    }
    free(txt);
    if (stat(path, &st) == 0 && !strcmp(ver, "?")) snprintf(ver, sizeof ver, "%ld bytes", (long)st.st_size);
    snprintf(out, sz, "Date %s  Version %s  (%ld)", date, ver, n);
}

static void s_lookup(srs_out *o)
{
    static const int tabs[] = { 150, 520, 840, 900 };
    char id[256], exe[1024] = "";
    ssize_t l;
    so_siv_header(o, "Lookup (name databases and program files)");
    so_tabs(o, tabs, 4);
    so_row(o, 5, (const char *const[]){ "Description", "File Specification", "Current Identification", "Code Page", "WEB Download Location" },
           (const int[]){ C_LABEL, C_PURPLE, C_TEAL, C_ORANGE, C_VALUE });
    ids_ident("/usr/share/hwdata/pci.ids", id, sizeof id);
    file_row(o, "PCI Device Names", "/usr/share/hwdata/pci.ids", id, "https://pci-ids.ucw.cz/v2.2/pci.ids");
    ids_ident("/usr/share/hwdata/usb.ids", id, sizeof id);
    file_row(o, "USB Device Names", "/usr/share/hwdata/usb.ids", id, "http://www.linux-usb.org/usb.ids");
    ids_ident("/usr/share/hwdata/pnp.ids", id, sizeof id);
    file_row(o, "PNP / Monitor Names", "/usr/share/hwdata/pnp.ids", id, "https://uefi.org/pnp_id_list");
    ids_ident("/usr/share/hwdata/oui.txt", id, sizeof id);
    file_row(o, "MAC Vendor Names", "/usr/share/hwdata/oui.txt", id, "https://standards-oui.ieee.org/oui/oui.txt");
    if ((l = readlink("/proc/self/exe", exe, sizeof exe - 1)) > 0) exe[l] = 0;
    file_row(o, "SRS Program", exe, "V" SRS_VERSION, "-");
    file_row(o, "SRS Kernel Driver", "none", "srs-gui uses no kernel driver; root data via srs-root", "-");
    so_par(o);
    so_note(o, "No network access: srs-gui shows the upstream URLs but never downloads (SIV's WEB UPD / Latest are not reproduced).");
}

/* [ping] - default gateway only */
static void s_ping(srs_out *o)
{
    char *route, gw[64] = "", *out = NULL, *p;
    if (rd_all("/proc/net/route", &route) >= 0) {
        for (p = strtok(route, "\n"); p; p = strtok(NULL, "\n")) {
            char ifn[32];
            unsigned dst, g, flags;
            if (sscanf(p, "%31s %x %x %x", ifn, &dst, &g, &flags) == 4 && dst == 0 && g) {
                snprintf(gw, sizeof gw, "%u.%u.%u.%u", g & 255, (g >> 8) & 255, (g >> 16) & 255, g >> 24);
                break;
            }
        }
        free(route);
    }
    so_siv_header(o, "Ping");
    if (!*gw) { so_err(o, "No IPv4 default gateway"); return; }
    so_kv(o, "Target", C_PURPLE, gw);
    so_par(o);
    {
        const char *argv[] = { "ping", "-c", "3", "-i", "0.2", "-W", "1", gw, NULL };
        int rc = run_out(&out, argv);
        if (rc < 0) so_err(o, "ping not available");
        else so_lines(o, rc == 0 ? C_BLACK : C_RED, out);
    }
    free(out);
}

/* [reg-find] - SIV searches the registry; the Linux equivalent is the text configuration in /etc
   and ~/.config. Bounded walk (depth 4, files <= 256 KB). arg = pattern (regex). */
static int rf_hits;
static void rf_walk(srs_out *o, srs_re *re, const char *dir, int depth)
{
    DIR *d;
    struct dirent *e;
    if (depth > 4 || rf_hits >= 200 || !(d = opendir(dir))) return;
    while ((e = readdir(d)) && rf_hits < 200) {
        char path[2048];
        struct stat st;
        if (e->d_name[0] == '.' && (!e->d_name[1] || (e->d_name[1] == '.' && !e->d_name[2]))) continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        if (lstat(path, &st)) continue;
        if (S_ISDIR(st.st_mode)) { rf_walk(o, re, path, depth + 1); continue; }
        if (!S_ISREG(st.st_mode) || st.st_size > 256 * 1024 || strstr(path, "shadow") || strstr(path, "gshadow")) continue;
        {
            FILE *f = fopen(path, "rb");
            char line[1024];
            int ln = 0;
            if (!f) continue;
            while (fgets(line, sizeof line, f) && rf_hits < 200) {
                int so_, eo, len;
                ln++;
                if (memchr(line, 0, strlen(line) + 1) != line + strlen(line)) break;   /* binary */
                line[strcspn(line, "\n")] = 0;
                len = (int)strlen(line);
                if (srs_re_search(re, line, len, 0, &so_, &eo)) {
                    so_f(o, C_PURPLE, "%s:%d", path, ln);
                    so_tab(o);
                    so_m(o, C_BLACK, line);
                    so_par(o);
                    rf_hits++;
                }
            }
            fclose(f);
        }
    }
    closedir(d);
}

static void s_reg_find(srs_out *o)
{
    static const int tabs[] = { 420 };
    char err[256], cfg[1024];
    srs_re *re;
    const char *h = getenv("HOME");
    so_siv_header(o, "Configuration Find (Linux equivalent of SIV's Registry Find)");
    so_tabs(o, tabs, 1);
    if (!o->arg || !*o->arg) {
        so_kv(o, "Usage", C_PURPLE, "reg-find:PATTERN  (regex; searches /etc and ~/.config text files)");
        return;
    }
    if (!(re = srs_re_compile(o->arg, 1, 0, err, sizeof err))) { so_f(o, C_RED, "Match String [%s] is Invalid: %s", o->arg, err); so_par(o); return; }
    rf_hits = 0;
    rf_walk(o, re, "/etc", 0);
    snprintf(cfg, sizeof cfg, "%s/.config", h ? h : "");
    rf_walk(o, re, cfg, 0);
    srs_re_free(re);
    so_par(o);
    if (!rf_hits) so_f(o, C_RED, "No Matches for %s", o->arg);
    else so_f(o, C_GREEN, "%d Matches%s", rf_hits, rf_hits >= 200 ? " (limit 200)" : "");
    so_par(o);
}

/* [id-lookup] - device-ID lookup (arg "1002:7550[:SSSS:ssss][:RR]" or "usb:1d6b:0002") */
static void s_id_lookup(srs_out *o)
{
    static const int tabs[] = { 24, 220, 700 };
    const char *a = o->arg;
    enum srs_bus bus = BUS_PCI;
    srs_db db;
    srs_result r;
    char err[256];
    int i;
    so_siv_header(o, "Device ID Lookup");
    if (!a || !*a) { so_kv(o, "Usage", C_PURPLE, "id-lookup:VVVV:DDDD[:SSSS:ssss][:RR]   or   id-lookup:usb:VVVV:PPPP"); return; }
    if (!strncmp(a, "usb:", 4)) { bus = BUS_USB; a += 4; }
    if (srs_db_load(&db, bus)) { so_err(o, "cannot find the .ids file"); return; }
    if (srs_resolve(&db, a, MODE_TREE, &r, err, sizeof err)) { so_err(o, err); srs_db_free(&db); return; }
    so_tabs(o, tabs, 3);
    for (i = 0; i < r.nchain; i++) {
        so_t(o, C_ORANGE, i == r.matched ? "<" : ""); so_tab(o);
        so_t(o, i == r.matched ? C_PURPLE : C_GRAY, r.chain[i]); so_tab(o);
        so_t(o, r.name[i][0] ? C_BLACK : C_GRAY, r.name[i][0] ? r.name[i] : "(no entry)"); so_par(o);
    }
    so_par(o);
    so_tabs(o, (const int[]){ 110 }, 1);
    so_kv(o, "Vendor", C_VALUE, r.vendor);
    if (r.subvendor[0]) so_kv(o, "Subsystem", C_GREEN, r.subvendor);
    so_kv(o, "Stock", C_TEAL, r.stock);
    so_kv(o, "Resolved", C_BLACK, r.resolved[0] ? r.resolved : "No Matches");
    srs_db_free(&db);
}

/* [find] - search the latest sysreport (arg = pattern) */
static void s_find(srs_out *o)
{
    srs_findres fr;
    char err[1400];
    int i, j, k;
    so_siv_header(o, "Find");
    if (!o->arg || !*o->arg) { so_kv(o, "Usage", C_PURPLE, "find:PATTERN   (regex over the newest ~/sysreports report)"); return; }
    if (srs_find(NULL, o->arg, 0, 0, 20, &fr, err, sizeof err)) { so_err(o, err); return; }
    so_f(o, C_TEAL, "Searching %s for \"%s\"", fr.report, o->arg); so_par(o);
    for (i = 0; i < fr.nsec; i++) {
        so_par(o);
        so_f(o, C_VALUE, "== %s - %d Match%s", fr.secs[i].file, fr.secs[i].count, fr.secs[i].count == 1 ? "" : "es"); so_par(o);
        for (j = 0; j < fr.secs[i].nshown; j++) {
            const srs_hit *h = &fr.secs[i].hits[j];
            int at = 0;
            so_f(o, C_GRAY, "%7d: ", h->lineno);
            for (k = 0; k < h->nspan; k++) {
                char seg[2048];
                snprintf(seg, sizeof seg, "%.*s", h->spans[k].so - at, h->text + at); so_m(o, C_BLACK, seg);
                snprintf(seg, sizeof seg, "%.*s", h->spans[k].eo - h->spans[k].so, h->text + h->spans[k].so); so_m(o, C_RED, seg);
                at = h->spans[k].eo;
            }
            so_m(o, C_BLACK, h->text + at);
            so_par(o);
        }
    }
    so_par(o);
    if (fr.total) so_f(o, C_GREEN, "%d Matches in %d of %d sections", fr.total, fr.nsec, fr.nfiles);
    else so_f(o, C_RED, "No Matches for %s", o->arg);
    so_par(o);
    srs_find_free(&fr);
}

/* [all-dev] - every PCI and USB device resolved (SIV's "ALL Dev") */
static void s_all_dev(srs_out *o)
{
    static const int tabs[] = { 110, 300, 450, 660 };
    int b, unresolved = 0;
    so_siv_header(o, "ALL Devices");
    so_tabs(o, tabs, 4);
    for (b = 0; b < 2; b++) {
        srs_db db;
        srs_dev *d;
        int n, i;
        char recs[32];
        enum srs_bus bus = b ? BUS_USB : BUS_PCI;
        if (o->arg && *o->arg && strcmp(o->arg, b ? "usb" : "pci")) continue;
        if (srs_db_load(&db, bus)) continue;
        n = srs_devices(bus, &d);
        if (n < 0) n = 0;
        srs_thousands(db.n, recs, sizeof recs);
        so_f(o, C_VALUE, "[%s] %d devices, %s records ", b ? "USB" : "PCI", n, recs); so_t(o, C_PURPLE, db.path); so_par(o);
        so_row(o, 5, (const char *const[]){ "Slot", "ID", "Matched", "Vendor", "Resolved" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
        for (i = 0; i < n; i++) {
            srs_result r;
            char e[128], name[600];
            int vo;
            if (srs_resolve(&db, d[i].key, MODE_TREE, &r, e, sizeof e)) { memset(&r, 0, sizeof r); r.matched = -1; }
            vo = r.matched < 0 || !strchr(r.chain[r.matched], ':');
            unresolved += vo;
            snprintf(name, sizeof name, "%s", vo ? "Unknown Device" : r.resolved);
            int colons = 0;
            const char *m;
            for (m = r.matched >= 0 ? r.chain[r.matched] : ""; *m; m++) colons += *m == ':';
            if (!vo && r.subvendor[0] && colons < 3) {   /* subsystem key had no entry: name the board vendor */
                size_t l = strlen(name);
                snprintf(name + l, sizeof name - l, "  [subsystem: %s]", r.subvendor);
            }
            so_row(o, 5, (const char *const[]){ d[i].slot, d[i].key, r.matched >= 0 ? r.chain[r.matched] : "-", r.vendor[0] ? r.vendor : "Unknown Vendor", name },
                   (const int[]){ C_LABEL, C_PURPLE, C_TEAL, C_VALUE, vo ? C_RED : C_BLACK });
        }
        so_par(o);
        free(d);
        srs_db_free(&db);
    }
    so_f(o, C_ORANGE, "%d device(s) resolved only to vendor or not at all", unresolved);
    so_par(o);
}

static void s_burnin(srs_out *o)
{
    static const int tabs[] = { 200 };
    burn_sample s;
    burn_sample_now(&s);
    so_siv_header(o, "CPU Burnin");
    so_tabs(o, tabs, 1);
    so_kv(o, "State", C_ORANGE, burn_active() ? "Running" : "Idle (start it from Menu > Tools > CPU Burnin)");
    so_kvf(o, "Worker threads", C_BLACK, "%d CPUs available", burn_ncpu());
    so_kvf(o, "Hottest CPU sensor", C_BLACK, "%s %.1f°C", s.cpu_temp_name, s.cpu_temp);
    so_kvf(o, "Hottest SIO sensor", C_BLACK, "%s %.1f°C", s.sio_name, s.sio_temp);
    so_kvf(o, "GPU", C_BLACK, "%.0f%% load  %.1f°C  %.0f MHz", s.gpu_load, s.gpu_temp, s.gpu_mhz);
    so_kv(o, "Command line", C_PURPLE, "srs-gui burn [--threads N | --cpus 0,2] [--seconds S] [--mode same|swap] [--limit C] [--cool S] [--cycles N]");
    so_note(o, "Safety: never auto-starts; stops at the temperature limit (default 90 C) and the time cap.");
}

const srs_item sec_tools[] = {
    { "benchmark", "hardware", "CPU Benchmarks", B_LINUX, 0, "built-in Dhrystone 2.1 / Whetstone", s_benchmark, 0, "tools" },
    { "benchmark-ac", "hardware", "Benchmark: all CPUs concurrent", B_LINUX, 0, "built-in", s_b_ac, 0, "tools" },
    { "benchmark-as", "hardware", "Benchmark: all CPUs sequential", B_LINUX, 0, "built-in", s_b_as, 0, "tools" },
    { "benchmark-dd", "hardware", "Benchmark: D+D then W+W", B_LINUX, 0, "built-in", s_b_dd, 0, "tools" },
    { "benchmark-dw", "hardware", "Benchmark: D+W then W+D", B_LINUX, 0, "built-in", s_b_dw, 0, "tools" },
    { "benchmark-ec", "hardware", "Benchmark: E-cores concurrent", B_ABSENT, 0, "no efficiency cores", s_b_ec, 0, "tools" },
    { "benchmark-ep", "hardware", "Benchmark: E-cores one at a time", B_ABSENT, 0, "no efficiency cores", s_b_ep, 0, "tools" },
    { "benchmark-lc", "hardware", "Benchmark: logical threads concurrent", B_LINUX, 0, "built-in", s_b_lc, 0, "tools" },
    { "benchmark-lp", "hardware", "Benchmark: logical threads one at a time", B_LINUX, 0, "built-in", s_b_lp, 0, "tools" },
    { "benchmark-pc", "hardware", "Benchmark: physical cores concurrent", B_LINUX, 0, "built-in", s_b_pc, 0, "tools" },
    { "benchmark-pp", "hardware", "Benchmark: physical cores one at a time", B_LINUX, 0, "built-in", s_b_pp, 0, "tools" },
    { "ipi-delay", "hardware", "CPU IPI Delay", B_LINUX, 0, "built-in core-to-core ping-pong", s_ipi, 0, "tools" },
    { "ipi-dmp", "hardware", "IPI Dump", B_LINUX, 0, "built-in core-to-core ping-pong", s_ipi, 0, "tools" },
    { "ipi-plots", "hardware", "IPI Plots", B_LINUX, 0, "built-in core-to-core ping-pong", s_ipi, 0, "tools" },
    { "latency", "hardware", "Memory Latency", B_LINUX, 0, "built-in pointer chase", s_latency, 0, "tools" },
    { "srs-pi", "tools", "SRS Pi", B_LINUX, 0, "built-in (Chudnovsky / Gauss-Legendre, checked digits)", s_pi, 0, "tools" },
    { "crypto-bench", "tools", "Crypto benchmark", B_LINUX, 0, "built-in AES-128 / SHA-256 (AES-NI, SHA-NI)", s_crypto, 0, "tools" },
    { "prime-sieve", "tools", "Prime sieve", B_LINUX, 0, "built-in segmented sieve of Eratosthenes", s_prime, 0, "tools" },
    { "bench-history", "tools", "Benchmark history", B_LINUX, 0, "~/.cache/srs/bench-history.tsv", s_bench_history, 0, "tools" },
    { "lookup", "hardware", "Lookup", B_LINUX, 0, "/usr/share/hwdata/*.ids", s_lookup, 0, "tools" },
    { "ping", "windows", "Ping", B_LINUX, 0, "ping (default gateway)", s_ping, 0, "tools" },
    { "reg-find", "windows", "Configuration Find", B_LINUX, 0, "/etc, ~/.config", s_reg_find, 0, "tools" },
    { "id-lookup", "tools", "Device ID Lookup", B_LINUX, 0, "hwdata pci.ids / usb.ids", s_id_lookup, 0, "tools" },
    { "find", "tools", "Find", B_LINUX, 0, "~/sysreports newest report", s_find, 0, "tools" },
    { "all-dev", "hardware", "ALL Devices", B_LINUX, 0, "sysfs + hwdata", s_all_dev, 0, "tools" },
    { "burnin", "tools", "CPU Burnin", B_LINUX, 0, "built-in burner", s_burnin, 2000, "tools" },
    { "cpu-stress", "tools", "CPU Stress", B_LINUX, 0, "built-in burner", s_burnin, 2000, "tools" },
    { NULL, NULL, NULL, B_LINUX, 0, NULL, NULL, 0, NULL }
};
