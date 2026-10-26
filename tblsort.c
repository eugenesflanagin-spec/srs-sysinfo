/* tblsort.c - unit-aware ordering of table cells (see tblsort.h). */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE   /* strverscmp */
#endif
#include "tblsort.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *skip_ws(const char *p) { while (*p == ' ' || *p == '\t') p++; return p; }

int ts_missing(const char *s)
{
    if (!s) return 1;
    s = skip_ws(s);
    if (!*s) return 1;
    if ((s[0] == '-' || s[0] == '?') && !s[1 + strspn(s + 1, " ")]) return 1;
    if (!strcmp(s, "n/a") || !strcmp(s, "N/A")) return 1;
    return 0;
}

/* days since 1970-01-01 for a civil date (Howard Hinnant's algorithm) */
static double days_from_civil(int y, int m, int d)
{
    int era, yoe, doy, doe;
    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (double)era * 146097.0 + doe - 719468.0;
}

/* "HH:MM" or "HH:MM:SS(.f)" at p -> seconds; *end after it; 0 if not a time */
static int parse_time(const char *p, double *secs, const char **end)
{
    char *e;
    long h, m;
    double s = 0;
    if (!isdigit((unsigned char)*p)) return 0;
    h = strtol(p, &e, 10);
    if (*e != ':' || !isdigit((unsigned char)e[1]) || !isdigit((unsigned char)e[2])) return 0;
    {
        const char *ms = e + 1;
        m = strtol(ms, &e, 10);
        if (e - ms != 2 || m > 59) return 0;
    }
    if (*e == ':' && isdigit((unsigned char)e[1])) {
        const char *ss = e + 1;
        s = strtod(ss, &e);
        if (s >= 61 || !isdigit((unsigned char)ss[1])) return 0;
    }
    *secs = (double)h * 3600.0 + (double)m * 60.0 + s;
    *end = e;
    return 1;
}

/* unit word after a number -> multiplier to the family's base unit; 0 = not a unit we know */
static double unit_mult(const char *u, size_t n)
{
    static const struct { const char *u; double m; } t[] = {
        /* sizes: SRS prints them base 1024 (fmt_bytes) */
        { "B", 1 }, { "bytes", 1 }, { "Bytes", 1 }, { "byte", 1 },
        { "K", 1024.0 }, { "KB", 1024.0 }, { "kB", 1024.0 }, { "KiB", 1024.0 }, { "k", 1024.0 },
        { "M", 1048576.0 }, { "MB", 1048576.0 }, { "MiB", 1048576.0 },
        { "G", 1073741824.0 }, { "GB", 1073741824.0 }, { "GiB", 1073741824.0 },
        { "T", 1099511627776.0 }, { "TB", 1099511627776.0 }, { "TiB", 1099511627776.0 },
        { "P", 1125899906842624.0 }, { "PB", 1125899906842624.0 }, { "PiB", 1125899906842624.0 },
        /* clocks and rates */
        { "Hz", 1 }, { "KHz", 1e3 }, { "kHz", 1e3 }, { "MHz", 1e6 }, { "GHz", 1e9 },
        { "MT/s", 1e6 }, { "GT/s", 1e9 }, { "Mbps", 1e6 }, { "Gbps", 1e9 }, { "Kbps", 1e3 }, { "kbps", 1e3 },
        { "Mb/s", 1e6 }, { "Gb/s", 1e9 }, { "MB/s", 1048576.0 }, { "GB/s", 1073741824.0 }, { "KB/s", 1024.0 },
        /* time */
        { "ns", 1e-9 }, { "us", 1e-6 }, { "\xc2\xb5s", 1e-6 }, { "ms", 1e-3 }, { "s", 1 }, { "sec", 1 },
        { "min", 60 }, { "h", 3600 }, { "d", 86400 }, { "days", 86400 }, { "day", 86400 },
        /* electrical, thermal, fans, ratios */
        { "mV", 1e-3 }, { "V", 1 }, { "mA", 1e-3 }, { "A", 1 }, { "mW", 1e-3 }, { "W", 1 }, { "kW", 1e3 },
        { "J", 1 }, { "kJ", 1e3 }, { "Wh", 1 }, { "kWh", 1e3 },
        { "\xc2\xb0" "C", 1 }, { "C", 1 }, { "RPM", 1 }, { "rpm", 1 }, { "%", 1 }, { "x", 1 }, { "X", 1 },
        { NULL, 0 } };
    int i;
    for (i = 0; t[i].u; i++) if (strlen(t[i].u) == n && !strncmp(u, t[i].u, n)) return t[i].m;
    return 0;
}

int ts_value(const char *s, double *v)
{
    const char *p, *e;
    char *ep;
    double x, t;
    int y, mo, d, sign = 1;
    if (ts_missing(s)) return 0;
    p = skip_ws(s);
    if (*p == '[' || *p == '(') p = skip_ws(p + 1);   /* "[1234]" PIDs, "(5)" counts */
    /* YYYY-MM-DD [HH:MM[:SS]] */
    if (sscanf(p, "%4d-%2d-%2d", &y, &mo, &d) == 3 && isdigit((unsigned char)p[3]) && p[4] == '-' && mo >= 1 && mo <= 12 && d >= 1 && d <= 31) {
        x = days_from_civil(y, mo, d) * 86400.0;
        e = skip_ws(p + 10);
        if (*e == 'T') e++;
        if (parse_time(e, &t, &e)) x += t;
        *v = x;
        return 1;
    }
    /* MM/DD/YYYY (SMBIOS / BIOS dates) */
    if (sscanf(p, "%2d/%2d/%4d", &mo, &d, &y) == 3 && mo >= 1 && mo <= 12 && d >= 1 && d <= 31 && y > 1900) {
        *v = days_from_civil(y, mo, d) * 86400.0;
        return 1;
    }
    /* "D HH:MM:SS" up-times and plain HH:MM(:SS) */
    {
        long dd;
        char *q;
        dd = strtol(p, &q, 10);
        if (q != p && *q == ' ' && parse_time(skip_ws(q), &t, &e) && !*skip_ws(e)) { *v = dd * 86400.0 + t; return 1; }
        if (parse_time(p, &t, &e) && (!*skip_ws(e) || *e == ' ')) { *v = t; return 1; }
    }
    /* 0x hex */
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && isxdigit((unsigned char)p[2])) {
        *v = (double)strtoull(p + 2, &ep, 16);
        return 1;
    }
    /* number (thousands separators allowed), then an optional unit */
    if (*p == '+' || *p == '-') { if (*p == '-') sign = -1; p++; }
    if (!isdigit((unsigned char)*p) && !(*p == '.' && isdigit((unsigned char)p[1]))) return 0;
    {
        char num[64];
        size_t n = 0;
        const char *q = p;
        while (*q && n < sizeof num - 1) {
            if (isdigit((unsigned char)*q) || *q == '.') num[n++] = *q++;
            else if (*q == ',' && isdigit((unsigned char)q[1]) && isdigit((unsigned char)q[2]) && isdigit((unsigned char)q[3]) &&
                     !isdigit((unsigned char)q[4])) q++;   /* 1,234,567 */
            else break;
        }
        num[n] = 0;
        x = strtod(num, &ep);
        if (ep == num) return 0;
        x *= sign;
        q = skip_ws(q);
        if (*q && *q != ']' && *q != ')') {
            /* the unit: letters, %, °, µ, / up to a space or the end; unknown words: keep the number */
            size_t un = 0;
            double m;
            while (q[un] && q[un] != ' ' && q[un] != ',' && q[un] != ')' && q[un] != ']' && q[un] != '(' && un < 12) un++;
            m = un ? unit_mult(q, un) : 0;
            if (m > 0) x *= m;
            else if (q[-1] != ' ') {
                /* glued to an unknown word: "3rd" is 3, but ids such as "10de", "1a2b" or
                   "1002:7550" are text */
                size_t k;
                for (k = 0; k < un && isxdigit((unsigned char)q[k]); k++) ;
                if (k == un || memchr(q, ':', un) || !isalpha((unsigned char)*q)) return 0;
            }
        }
        *v = x;
        return 1;
    }
}

static int text_cmp(const char *a, const char *b)
{
    char *x = strdup(a), *y = strdup(b), *p;
    int r;
    if (!x || !y) { free(x); free(y); return strcmp(a, b); }
    for (p = x; *p; p++) *p = (char)tolower((unsigned char)*p);
    for (p = y; *p; p++) *p = (char)tolower((unsigned char)*p);
    r = strverscmp(x, y);
    if (!r) r = strcmp(a, b);
    free(x);
    free(y);
    return r;
}

int ts_cmp(const char *a, const char *b)
{
    int ma = ts_missing(a), mb = ts_missing(b);
    double va, vb;
    int na, nb;
    if (ma || mb) return ma - mb;   /* the caller keeps these last in both directions */
    na = ts_value(a, &va);
    nb = ts_value(b, &vb);
    if (na && nb) {
        if (va < vb) return -1;
        if (va > vb) return 1;
        return text_cmp(a, b);
    }
    if (na != nb) return na ? -1 : 1;   /* numbers before words */
    return text_cmp(skip_ws(a), skip_ws(b));
}
