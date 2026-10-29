/* power_rates.c - SRS "Power & Cost": offline price and grid-CO2 tables. See power_rates.h.
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "power_rates.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct { const char *name, *code; double price, standing; } prt_row;
typedef struct { const char *name; int year; double g; } prt_co2row;

#include "power_rates_data.inc"

#define N(a) ((int)(sizeof a / sizeof a[0]))
enum { C_US, C_EU, C_UK, C_OTHER, C_N };
static const char *const g_countries[C_N] = { "United States (EIA, by state)", "Europe (Eurostat, by country)",
                                              "United Kingdom (Ofgem price cap)", "Somewhere else: type your price" };

int prt_country_count(void) { return C_N; }
const char *prt_country_name(int c) { return c >= 0 && c < C_N ? g_countries[c] : ""; }

int prt_region_count(int c)
{
    switch (c) {
    case C_US: return N(g_us);
    case C_EU: return N(g_eu);
    case C_UK: return N(g_uk);
    default: return 0;
    }
}

const char *prt_region_name(int c, int r)
{
    if (r < 0 || r >= prt_region_count(c)) return "";
    return c == C_US ? g_us[r].name : c == C_EU ? g_eu[r].name : g_uk[r].name;
}

int prt_get(int c, int r, prt_rate *o)
{
    const prt_row *x;
    const char *note, *cur;
    if (r < 0 || r >= prt_region_count(c)) return 0;
    x = c == C_US ? &g_us[r] : c == C_EU ? &g_eu[r] : &g_uk[r];
    note = c == C_US ? g_us_note : c == C_EU ? g_eu_note : g_uk_note;
    cur = c == C_US ? "USD" : c == C_EU ? "EUR" : "GBP";
    memset(o, 0, sizeof *o);
    snprintf(o->name, sizeof o->name, "%s", x->name);
    o->price = x->price;
    o->standing_day = x->standing;
    snprintf(o->currency, sizeof o->currency, "%s", cur);
    snprintf(o->note, sizeof o->note, "%s", note);
    return 1;
}

int prt_has_zip(int c) { return c == C_US; }
const char *prt_tables_date(void) { return g_tables_date; }
const char *prt_zip_source(void) { return "NREL utility rates by ZIP code, 2024 (EIA-861 data), CC BY 4.0"; }

/* the ZIP table file: beside the program (build tree / AppImage), then the system data folders */
static FILE *zip_open(void)
{
    char exe[1024], *dir, *p;
    const char *const *sd;
    const char *env = getenv("SRS_DATA_DIR");
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    FILE *f = NULL;
    if (env && *env) {
        p = g_build_filename(env, "power-zip-rates.tsv", NULL);
        f = fopen(p, "r");
        g_free(p);
        if (f) return f;
    }
    if (n > 0) {
        exe[n] = 0;
        dir = g_path_get_dirname(exe);
        {
            const char *rel[] = { "../../data", "../share/srs-sysinfo", "data", NULL };
            int i;
            for (i = 0; rel[i] && !f; i++) {
                p = g_build_filename(dir, rel[i], "power-zip-rates.tsv", NULL);
                f = fopen(p, "r");
                g_free(p);
            }
        }
        g_free(dir);
        if (f) return f;
    }
    for (sd = g_get_system_data_dirs(); sd && *sd && !f; sd++) {
        p = g_build_filename(*sd, "srs-sysinfo", "power-zip-rates.tsv", NULL);
        f = fopen(p, "r");
        g_free(p);
    }
    return f;
}

int prt_zip_lookup(const char *zip, char *util, int usz, double *price)
{
    FILE *f;
    char line[512], ids[256] = "", state[8] = "";
    int i, best = -1, nids = 0;
    util[0] = 0;
    *price = 0;
    if (!zip || strlen(zip) != 5) return -1;
    for (i = 0; i < 5; i++) if (zip[i] < '0' || zip[i] > '9') return -1;
    if (!(f = zip_open())) return -1;
    while (fgets(line, sizeof line, f))
        if (line[0] == 'Z' && !strncmp(line + 2, zip, 5) && line[7] == '\t') {
            snprintf(ids, sizeof ids, "%s", line + 8);
            ids[strcspn(ids, "\r\n")] = 0;
            break;
        }
    if (!ids[0]) { fclose(f); return -1; }
    rewind(f);
    {
        char **v = g_strsplit(ids, ",", -1);
        nids = (int)g_strv_length(v);
        while (fgets(line, sizeof line, f)) {
            char **c;
            if (line[0] != 'U') continue;
            line[strcspn(line, "\r\n")] = 0;
            c = g_strsplit(line, "\t", 5);
            if (g_strv_length(c) == 5) {
                int k;
                for (k = 0; v[k]; k++)
                    if (!strcmp(c[1], v[k])) {
                        double r = g_ascii_strtod(c[3], NULL);
                        if (!state[0]) snprintf(state, sizeof state, "%s", c[2]);
                        if (r > 0 && (best < 0 || !util[0])) {
                            best = k;
                            *price = r;
                            snprintf(util, (size_t)usz, "%s", c[4]);
                            snprintf(state, sizeof state, "%s", c[2]);
                        }
                    }
            }
            g_strfreev(c);
        }
        g_strfreev(v);
    }
    fclose(f);
    if (util[0] && nids > 1) {
        size_t l = strlen(util);
        if (nids == 2) snprintf(util + l, (size_t)usz - l, " (or 1 other utility here)");
        else snprintf(util + l, (size_t)usz - l, " (or one of %d other utilities here)", nids - 1);
    }
    for (i = 0; i < N(g_us); i++) if (!strcmp(g_us[i].code, state)) return i;
    return 0;
}

int prt_guess_country(void)
{
    char tz[256] = "";
    ssize_t n = readlink("/etc/localtime", tz, sizeof tz - 1);
    const char *z;
    if (n > 0) tz[n] = 0;
    else if (getenv("TZ")) snprintf(tz, sizeof tz, "%s", getenv("TZ"));
    z = strstr(tz, "zoneinfo/") ? strstr(tz, "zoneinfo/") + 9 : tz;
    {
        static const char *const us[] = { "America/New_York", "America/Chicago", "America/Denver", "America/Los_Angeles",
                                          "America/Phoenix", "America/Anchorage", "America/Detroit", "America/Indiana/",
                                          "America/Kentucky/", "America/Boise", "America/Juneau", "America/North_Dakota/",
                                          "America/Menominee", "America/Adak", "America/Nome", "America/Sitka", "US/",
                                          "Pacific/Honolulu", NULL };
        int i;
        for (i = 0; us[i]; i++) if (!strncmp(z, us[i], strlen(us[i]))) return C_US;
    }
    if (!strcmp(z, "Europe/London") || !strcmp(z, "GB")) return C_UK;
    if (!strncmp(z, "Europe/", 7)) return C_EU;
    return C_OTHER;
}

int prt_co2_count(void) { return N(g_co2); }
int prt_co2_get(int i, prt_co2 *o)
{
    if (i < 0 || i >= N(g_co2)) return 0;
    snprintf(o->name, sizeof o->name, "%s", g_co2[i].name);
    o->g_per_kwh = g_co2[i].g;
    snprintf(o->source, sizeof o->source, "Ember yearly electricity data %d, CC BY 4.0", g_co2[i].year);
    return 1;
}
