/* power_db.c - SRS "Power & cost" history in SQLite. See power_db.h.
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "power_db.h"

#include <errno.h>
#include <math.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static sqlite3 *g_db;
static sqlite3_stmt *g_add;
static int g_tx;
static char g_err[400], g_path[1024];

const char *pdb_error(void) { return g_err; }
const char *pdb_path(void) { return g_path; }
int pdb_is_open(void) { return g_db != NULL; }

static int fail(const char *what)
{
    snprintf(g_err, sizeof g_err, "%s: %s", what, g_db ? sqlite3_errmsg(g_db) : "no database");
    return -1;
}

static int exec(const char *sql)
{
    char *e = NULL;
    if (sqlite3_exec(g_db, sql, NULL, NULL, &e) != SQLITE_OK) {
        snprintf(g_err, sizeof g_err, "%s", e ? e : "sqlite error");
        sqlite3_free(e);
        return -1;
    }
    return 0;
}

static void mkdirs(const char *file)
{
    char d[1024], *p;
    snprintf(d, sizeof d, "%s", file);
    p = strrchr(d, '/');
    if (!p || p == d) return;
    *p = 0;
    for (p = d + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(d, 0700); *p = '/'; }
    mkdir(d, 0700);
}

int pdb_open(const char *path)
{
    static const char *const schema =
        "PRAGMA journal_mode=WAL;"
        "PRAGMA synchronous=NORMAL;"
        "CREATE TABLE IF NOT EXISTS minutes(t INTEGER PRIMARY KEY, secs_on REAL, secs_sleep REAL, secs_off REAL,"
        " wh_mid REAL, wh_lo REAL, wh_hi REAL, wh_cpu REAL, wh_gpu REAL, cost REAL, w_max REAL, flags INTEGER);"
        "CREATE TABLE IF NOT EXISTS gaps(t0 INTEGER, t1 INTEGER, kind INTEGER);"
        "CREATE INDEX IF NOT EXISTS gaps_t0 ON gaps(t0);"
        "CREATE TABLE IF NOT EXISTS meta(k TEXT PRIMARY KEY, v TEXT);"
        "PRAGMA user_version=1;";
    if (g_db) pdb_close();
    g_err[0] = 0;
    snprintf(g_path, sizeof g_path, "%s", path);
    mkdirs(path);
    if (sqlite3_open_v2(path, &g_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, NULL) != SQLITE_OK) {
        fail("open");
        sqlite3_close(g_db);
        g_db = NULL;
        return -1;
    }
    chmod(path, 0600);   /* a usage history says when someone is at the PC: keep it private */
    sqlite3_busy_timeout(g_db, 3000);
    if (exec(schema)) { sqlite3_close(g_db); g_db = NULL; return -1; }
    if (sqlite3_prepare_v2(g_db,
            "INSERT INTO minutes VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12) ON CONFLICT(t) DO UPDATE SET"
            " secs_on=secs_on+excluded.secs_on, secs_sleep=secs_sleep+excluded.secs_sleep, secs_off=secs_off+excluded.secs_off,"
            " wh_mid=wh_mid+excluded.wh_mid, wh_lo=wh_lo+excluded.wh_lo, wh_hi=wh_hi+excluded.wh_hi,"
            " wh_cpu=wh_cpu+excluded.wh_cpu, wh_gpu=wh_gpu+excluded.wh_gpu, cost=cost+excluded.cost,"
            " w_max=max(w_max,excluded.w_max), flags=flags|excluded.flags", -1, &g_add, NULL) != SQLITE_OK) {
        fail("prepare");
        sqlite3_close(g_db);
        g_db = NULL;
        return -1;
    }
    return 0;
}

void pdb_close(void)
{
    if (!g_db) return;
    pdb_flush();
    sqlite3_finalize(g_add);
    g_add = NULL;
    sqlite3_close(g_db);
    g_db = NULL;
}

int pdb_flush(void)
{
    if (!g_db || !g_tx) return 0;
    g_tx = 0;
    return exec("COMMIT") ? -1 : 0;
}

static int begin(void)
{
    if (g_tx) return 0;
    if (exec("BEGIN")) return -1;
    g_tx = 1;
    return 0;
}

int pdb_add(long minute, const pdb_row *r)
{
    int rc;
    if (!g_db) return fail("add");
    if (begin()) return -1;
    sqlite3_bind_int64(g_add, 1, minute);
    sqlite3_bind_double(g_add, 2, r->secs_on);
    sqlite3_bind_double(g_add, 3, r->secs_sleep);
    sqlite3_bind_double(g_add, 4, r->secs_off);
    sqlite3_bind_double(g_add, 5, r->wh_mid);
    sqlite3_bind_double(g_add, 6, r->wh_lo);
    sqlite3_bind_double(g_add, 7, r->wh_hi);
    sqlite3_bind_double(g_add, 8, r->wh_cpu);
    sqlite3_bind_double(g_add, 9, r->wh_gpu);
    sqlite3_bind_double(g_add, 10, r->cost);
    sqlite3_bind_double(g_add, 11, r->w_max);
    sqlite3_bind_int(g_add, 12, r->flags);
    rc = sqlite3_step(g_add);
    sqlite3_reset(g_add);
    return rc == SQLITE_DONE ? 0 : fail("add");
}

int pdb_gap(long t0, long t1, int kind)
{
    sqlite3_stmt *s;
    int rc;
    if (!g_db || t1 <= t0) return -1;
    if (begin()) return -1;
    if (sqlite3_prepare_v2(g_db, "INSERT INTO gaps VALUES(?1,?2,?3)", -1, &s, NULL) != SQLITE_OK) return fail("gap");
    sqlite3_bind_int64(s, 1, t0);
    sqlite3_bind_int64(s, 2, t1);
    sqlite3_bind_int(s, 3, kind);
    rc = sqlite3_step(s);
    sqlite3_finalize(s);
    return rc == SQLITE_DONE ? 0 : fail("gap");
}

int pdb_gaps(long from, long to, long *t0, long *t1, int *kind, int max)
{
    sqlite3_stmt *s;
    int n = 0;
    if (!g_db) return 0;
    if (sqlite3_prepare_v2(g_db, "SELECT t0,t1,kind FROM gaps WHERE t1>?1 AND t0<?2 ORDER BY t0", -1, &s, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int64(s, 1, from);
    sqlite3_bind_int64(s, 2, to);
    while (sqlite3_step(s) == SQLITE_ROW) {
        if (n < max) {
            if (t0) t0[n] = (long)sqlite3_column_int64(s, 0);
            if (t1) t1[n] = (long)sqlite3_column_int64(s, 1);
            if (kind) kind[n] = sqlite3_column_int(s, 2);
        }
        n++;
    }
    sqlite3_finalize(s);
    return n;
}

int pdb_count_gaps(long from, long to) { return pdb_gaps(from, to, NULL, NULL, NULL, 0); }

static const char *group_fmt(int g)
{
    switch (g) {
    case PDB_MINUTE: return "%Y-%m-%d %H:%M";
    case PDB_HOUR: return "%Y-%m-%d %H:00";
    case PDB_DAY: return "%Y-%m-%d";
    case PDB_WEEK: return "%Y week %W";
    case PDB_MONTH: return "%Y-%m";
    default: return "%Y";
    }
}

typedef int (*row_cb)(const pdb_sum *r, void *ctx);

/* step through the grouped rows one at a time (no limit on how many) */
static int group_iter(int group, long from, long to, row_cb cb, void *ctx)
{
    sqlite3_stmt *s;
    char sql[700];
    int n = 0;
    if (!g_db) return 0;
    snprintf(sql, sizeof sql,
             "SELECT min(t), strftime('%s', t, 'unixepoch', 'localtime') AS k, sum(wh_mid), sum(wh_lo), sum(wh_hi), sum(wh_cpu),"
             " sum(wh_gpu), sum(cost), sum(secs_on), sum(secs_sleep), sum(secs_off), max(w_max)"
             " FROM minutes WHERE t>=?1 AND t<?2 GROUP BY k ORDER BY min(t)", group_fmt(group));
    if (sqlite3_prepare_v2(g_db, sql, -1, &s, NULL) != SQLITE_OK) { fail("group"); return 0; }
    sqlite3_bind_int64(s, 1, from);
    sqlite3_bind_int64(s, 2, to);
    while (sqlite3_step(s) == SQLITE_ROW) {
        pdb_sum o;
        const unsigned char *k = sqlite3_column_text(s, 1);
        o.t = (long)sqlite3_column_int64(s, 0);
        snprintf(o.label, sizeof o.label, "%s", k ? (const char *)k : "");
        o.wh_mid = sqlite3_column_double(s, 2);
        o.wh_lo = sqlite3_column_double(s, 3);
        o.wh_hi = sqlite3_column_double(s, 4);
        o.wh_cpu = sqlite3_column_double(s, 5);
        o.wh_gpu = sqlite3_column_double(s, 6);
        o.cost = sqlite3_column_double(s, 7);
        o.secs_on = sqlite3_column_double(s, 8);
        o.secs_sleep = sqlite3_column_double(s, 9);
        o.secs_off = sqlite3_column_double(s, 10);
        o.w_max = sqlite3_column_double(s, 11);
        n++;
        if (cb(&o, ctx)) break;
    }
    sqlite3_finalize(s);
    return n;
}

typedef struct { pdb_sum *out; int max, n; } arrctx;
static int to_array(const pdb_sum *r, void *ctx)
{
    arrctx *a = ctx;
    if (a->n >= a->max) return 1;
    a->out[a->n++] = *r;
    return 0;
}

int pdb_group(int group, long from, long to, pdb_sum *out, int max)
{
    arrctx a = { out, max, 0 };
    group_iter(group, from, to, to_array, &a);
    return a.n;
}

double pdb_sum_range(long from, long to, double *cost)
{
    sqlite3_stmt *s;
    double wh = 0;
    if (cost) *cost = 0;
    if (!g_db) return 0;
    if (sqlite3_prepare_v2(g_db, "SELECT total(wh_mid), total(cost) FROM minutes WHERE t>=?1 AND t<?2", -1, &s, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int64(s, 1, from);
    sqlite3_bind_int64(s, 2, to);
    if (sqlite3_step(s) == SQLITE_ROW) {
        wh = sqlite3_column_double(s, 0);
        if (cost) *cost = sqlite3_column_double(s, 1);
    }
    sqlite3_finalize(s);
    return wh;
}

static long scalar(const char *sql)
{
    sqlite3_stmt *s;
    long v = 0;
    if (!g_db || sqlite3_prepare_v2(g_db, sql, -1, &s, NULL) != SQLITE_OK) return 0;
    if (sqlite3_step(s) == SQLITE_ROW) v = (long)sqlite3_column_int64(s, 0);
    sqlite3_finalize(s);
    return v;
}
long pdb_first_minute(void) { return scalar("SELECT ifnull(min(t),0) FROM minutes"); }
long pdb_minutes_count(void) { return scalar("SELECT count(*) FROM minutes"); }

int pdb_meta_get(const char *key, char *out, int sz)
{
    sqlite3_stmt *s;
    int ok = 0;
    out[0] = 0;
    if (!g_db || sqlite3_prepare_v2(g_db, "SELECT v FROM meta WHERE k=?1", -1, &s, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(s, 1, key, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(s) == SQLITE_ROW) {
        const unsigned char *v = sqlite3_column_text(s, 0);
        snprintf(out, (size_t)sz, "%s", v ? (const char *)v : "");
        ok = 1;
    }
    sqlite3_finalize(s);
    return ok;
}

int pdb_meta_set(const char *key, const char *val)
{
    sqlite3_stmt *s;
    int rc;
    if (!g_db) return -1;
    if (begin()) return -1;
    if (sqlite3_prepare_v2(g_db, "INSERT OR REPLACE INTO meta VALUES(?1,?2)", -1, &s, NULL) != SQLITE_OK) return fail("meta");
    sqlite3_bind_text(s, 1, key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 2, val, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(s);
    sqlite3_finalize(s);
    return rc == SQLITE_DONE ? 0 : fail("meta");
}

/* a number with the chosen decimal mark (a decimal comma for spreadsheets set up that way) */
static void num(FILE *f, double v, int prec, const char *dec)
{
    char b[64], *p;
    snprintf(b, sizeof b, "%.*f", prec, v);
    if (dec && *dec == ',' && (p = strchr(b, '.'))) *p = ',';
    fputs(b, f);
}

typedef struct { FILE *f; const char *sep, *dec; } csvctx;
static int csv_row(const pdb_sum *r, void *ctx)
{
    csvctx *c = ctx;
    char ts[40];
    time_t t = r->t;
    struct tm tm;
    double hrs = (r->secs_on + r->secs_sleep) / 3600;
    double v[] = { r->wh_mid / 1000, r->wh_lo / 1000, r->wh_hi / 1000, r->wh_cpu / 1000, r->wh_gpu / 1000, r->cost };
    int k;
    gmtime_r(&t, &tm);
    strftime(ts, sizeof ts, "%Y-%m-%dT%H:%M:%SZ", &tm);
    fprintf(c->f, "%s%s%s%s", r->label, c->sep, ts, c->sep);
    for (k = 0; k < 6; k++) { num(c->f, v[k], 5, c->dec); fputs(c->sep, c->f); }
    num(c->f, hrs > 0 ? r->wh_mid / hrs : 0, 1, c->dec); fputs(c->sep, c->f);
    num(c->f, r->w_max, 1, c->dec); fputs(c->sep, c->f);
    num(c->f, r->secs_on / 3600, 4, c->dec); fputs(c->sep, c->f);
    num(c->f, r->secs_sleep / 3600, 4, c->dec);
    fputc('\n', c->f);
    return 0;
}

int pdb_export_csv(const char *file, int group, long from, long to, const char *note, const char *currency, const char *decimal)
{
    csvctx c;
    int n;
    const char *sep = decimal && *decimal == ',' ? ";" : ",";
    if (!g_db) return fail("export");
    pdb_flush();
    c.f = fopen(file, "w");
    if (!c.f) { snprintf(g_err, sizeof g_err, "%s: %s", file, strerror(errno)); return -1; }
    c.sep = sep;
    c.dec = decimal;
    if (note && *note) {
        const char *p = note;
        while (*p) {
            const char *e = strchr(p, '\n');
            int len = e ? (int)(e - p) : (int)strlen(p);
            fprintf(c.f, "# %.*s\n", len, p);
            p += len + (e ? 1 : 0);
        }
    }
    fprintf(c.f, "period%sstart_utc%skwh_estimate%skwh_low%skwh_high%scpu_kwh_measured%sgpu_kwh_measured%scost_%s%savg_w%smax_w%shours_recorded%shours_asleep\n",
            sep, sep, sep, sep, sep, sep, sep, currency && *currency ? currency : "money", sep, sep, sep, sep);
    n = group_iter(group, from, to, csv_row, &c);
    if (fclose(c.f)) { snprintf(g_err, sizeof g_err, "%s: %s", file, strerror(errno)); return -1; }
    return n;
}

long pdb_reprice(const pm_tariff *t, long from)
{
    sqlite3_stmt *s, *u;
    long n = 0, month_key = -1;
    double month_kwh = 0;
    if (!g_db) return -1;
    pdb_flush();
    /* tiers count from the start of the month: begin there even if repricing from later */
    {
        time_t f = from;
        struct tm tm;
        localtime_r(&f, &tm);
        tm.tm_mday = 1; tm.tm_hour = 0; tm.tm_min = 0; tm.tm_sec = 0; tm.tm_isdst = -1;
        from = (long)mktime(&tm);
    }
    if (exec("BEGIN")) return -1;
    if (sqlite3_prepare_v2(g_db, "SELECT t, wh_mid FROM minutes WHERE t>=?1 ORDER BY t", -1, &s, NULL) != SQLITE_OK) { exec("ROLLBACK"); return fail("reprice"); }
    if (sqlite3_prepare_v2(g_db, "UPDATE minutes SET cost=?2 WHERE t=?1", -1, &u, NULL) != SQLITE_OK) { sqlite3_finalize(s); exec("ROLLBACK"); return fail("reprice"); }
    sqlite3_bind_int64(s, 1, from);
    while (sqlite3_step(s) == SQLITE_ROW) {
        time_t tt = (time_t)sqlite3_column_int64(s, 0);
        double kwh = sqlite3_column_double(s, 1) / 1000;
        struct tm tm;
        long mk;
        localtime_r(&tt, &tm);
        mk = tm.tm_year * 12L + tm.tm_mon;
        if (mk != month_key) { month_key = mk; month_kwh = 0; }
        sqlite3_bind_int64(u, 1, (sqlite3_int64)tt);
        sqlite3_bind_double(u, 2, pm_cost(t, tt, month_kwh, kwh));
        sqlite3_step(u);
        sqlite3_reset(u);
        month_kwh += kwh;
        n++;
    }
    sqlite3_finalize(s);
    sqlite3_finalize(u);
    if (exec("COMMIT")) return -1;
    return n;
}

int pdb_delete_all(void)
{
    if (!g_db) return -1;
    pdb_flush();
    if (exec("DELETE FROM minutes; DELETE FROM gaps; DELETE FROM meta WHERE k<>'schema'")) return -1;
    exec("VACUUM");
    return 0;
}
