/* tests/test_power.c - unit tests for SRS "Power & Cost" (power_model.c, power_db.c) with synthetic
 * counter data. Build and run: make test-power
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#include "../power_model.h"
#include "../power_db.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_fail, g_n;
#define CHECK(cond, ...) do { g_n++; if (!(cond)) { g_fail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define NEAR(a, b, tol) (fabs((a) - (b)) <= (tol))

#define RAPL_RANGE 65532610987.0   /* this PC's max_energy_range_uj */

static void t_counter_basic(void)
{
    pm_counter c = { 0 };
    int f, i;
    double e, sum = 0, v = 1000;
    e = pm_counter_step(&c, v, 1, 1000, &f);
    CHECK(e < 0, "first read gives no interval (%g)", e);
    for (i = 0; i < 10; i++) {
        v += 47e6;   /* 47 W for 1 s */
        e = pm_counter_step(&c, v, 1, 1000, &f);
        CHECK(f == 0, "no flags on a plain step (%d)", f);
        sum += e;
    }
    CHECK(NEAR(sum, 470, 1e-6), "47 W x 10 s = 470 J (got %g)", sum);
    e = pm_counter_step(&c, v, 1, 1000, &f);
    CHECK(e == 0 && f == 0, "zero delta = 0 J (got %g, flags %d)", e, f);
}

static void t_counter_wrap(void)
{
    pm_counter c = { 0 };
    int f;
    double e;
    c.max_range = RAPL_RANGE;
    pm_counter_step(&c, RAPL_RANGE - 20e6, 1, 1000, &f);   /* 20 J before the top */
    e = pm_counter_step(&c, 30e6, 1, 1000, &f);           /* wrapped: 20 + 30 J */
    CHECK(f & PM_F_WRAP, "wrap flagged (%d)", f);
    CHECK(NEAR(e, 50, 1e-6), "unwrapped delta 50 J (got %g)", e);
    /* a wrap after a long gap could hide a second wrap: flagged and not used */
    e = pm_counter_step(&c, 10e6, 120, 1000, &f);
    CHECK(e < 0 && (f & PM_F_BAD), "120 s gap on a 65 kJ counter is not trusted (e %g flags %d)", e, f);
    /* and the counter has a new baseline afterwards */
    e = pm_counter_step(&c, 10e6 + 60e6, 1, 1000, &f);
    CHECK(NEAR(e, 60, 1e-6) && f == 0, "after the gap: 60 J (got %g flags %d)", e, f);
}

static void t_counter_reset(void)
{
    pm_counter c = { 0 };   /* zenergy: 64-bit accumulator, max_range 0 */
    int f;
    double e;
    pm_counter_step(&c, 5e12, 1, 1000, &f);
    e = pm_counter_step(&c, 3e6, 1, 1000, &f);           /* went down: driver reload / reboot */
    CHECK(e < 0 && (f & PM_F_RESET), "64-bit counter going down is a reset (e %g flags %d)", e, f);
    e = pm_counter_step(&c, 3e6 + 50e6, 1, 1000, &f);
    CHECK(NEAR(e, 50, 1e-6), "new baseline after reset (got %g)", e);
    e = pm_counter_step(&c, 3e6 + 50e6 + 5e9, 1, 1000, &f);   /* 5000 W: impossible for a CPU */
    CHECK(e < 0 && (f & PM_F_BAD), "impossible power is rejected (e %g flags %d)", e, f);
}

/* a whole synthetic run: power changes every second, the 65.5 kJ counter wraps many times */
static void t_counter_long_run(void)
{
    pm_counter c = { 0 };
    double v = 12345678, truth = 0, got = 0, e;
    int i, f, wraps = 0;
    c.max_range = RAPL_RANGE;
    pm_counter_step(&c, v, 1, 1000, &f);
    for (i = 0; i < 3600; i++) {
        double w = 40 + 120 * (0.5 + 0.5 * sin(i / 60.0)) + (i % 7);   /* 40..167 W */
        truth += w;
        v += w * 1e6;
        if (v >= RAPL_RANGE) { v -= RAPL_RANGE; wraps++; }
        e = pm_counter_step(&c, v, 1, 1000, &f);
        if (e >= 0) got += e;
    }
    CHECK(wraps >= 3, "the run wrapped the counter (%d times)", wraps);
    CHECK(NEAR(got, truth, 1e-3), "1 h with %d wraps: %.3f J measured vs %.3f J true", wraps, got, truth);
}

static void t_trapezoid(void)
{
    double sum = 0, w0 = 0;
    int i;
    CHECK(NEAR(pm_trapezoid(0, 100, 10), 500, 1e-9), "ramp 0 -> 100 W over 10 s = 500 J");
    for (i = 1; i <= 10; i++) { double w1 = 10.0 * i; sum += pm_trapezoid(w0, w1, 1); w0 = w1; }
    CHECK(NEAR(sum, 500, 1e-9), "same ramp in 1 s steps = 500 J (got %g)", sum);
    CHECK(NEAR(pm_trapezoid(NAN, 30, 2), 60, 1e-9), "missing first reading holds the second");
}

static void t_classify(void)
{
    pm_clock a = { 100, 100, 1000, "aaaa" }, b;
    double aw, sl;
    b = a; b.mono += 1; b.boot += 1;
    CHECK(pm_classify(&a, &b, 30, &aw, &sl) == PM_GAP_NONE && NEAR(aw, 1, 1e-9), "normal 1 s step");
    b = a; b.mono += 1; b.boot += 3601;
    CHECK(pm_classify(&a, &b, 30, &aw, &sl) == PM_GAP_SLEEP && NEAR(sl, 3600, 1e-9) && NEAR(aw, 1, 1e-9),
          "suspend: boottime jumped 3600 s more than monotonic (sleep %g awake %g)", sl, aw);
    b = a; b.mono += 100; b.boot += 100;
    CHECK(pm_classify(&a, &b, 30, &aw, &sl) == PM_GAP_STALL, "100 s without a sample = stall");
    b = a; snprintf(b.boot_id, sizeof b.boot_id, "bbbb"); b.mono = 5; b.boot = 5;
    CHECK(pm_classify(&a, &b, 30, &aw, &sl) == PM_GAP_REBOOT, "new boot_id = reboot");
}

typedef struct { double frac[8]; long m[8]; int n; } splitctx;
static void split_cb(long m, double frac, void *ctx) { splitctx *s = ctx; if (s->n < 8) { s->m[s->n] = m; s->frac[s->n] = frac; s->n++; } }

static void t_split(void)
{
    splitctx s = { { 0 }, { 0 }, 0 };
    pm_split_minutes(50, 130, split_cb, &s);
    CHECK(s.n == 3, "50..130 s covers 3 minutes (%d)", s.n);
    CHECK(s.m[0] == 0 && s.m[1] == 60 && s.m[2] == 120, "minute starts 0/60/120");
    CHECK(NEAR(s.frac[0], 10.0 / 80, 1e-9) && NEAR(s.frac[1], 60.0 / 80, 1e-9) && NEAR(s.frac[2], 10.0 / 80, 1e-9), "fractions 10/60/10 of 80");
    s.n = 0;
    pm_split_minutes(61, 62, split_cb, &s);
    CHECK(s.n == 1 && s.m[0] == 60 && NEAR(s.frac[0], 1, 1e-9), "1 s inside one minute");
}

static void t_psu(void)
{
    pm_psu p;
    double lo, mid, hi, lo2, mid2, hi2;
    pm_psu_preset(0, 0, &p);
    CHECK(p.rating == 1200, "Montech preset is 1200 W");
    pm_psu_eff(&p, 160, &lo, &mid, &hi);
    CHECK(lo < mid && mid < hi && mid > 0.8 && mid < 0.9, "160 W on 1200 W: lo %.3f mid %.3f hi %.3f", lo, mid, hi);
    pm_psu_eff(&p, 600, &lo2, &mid2, &hi2);
    CHECK(NEAR(mid2, 0.91, 1e-9), "50 %% load = 91 %% (got %.3f)", mid2);
    CHECK(hi - lo > hi2 - lo2, "the band is wider at light load (%.3f vs %.3f)", hi - lo, hi2 - lo2);
    pm_psu_eff(&p, 24, &lo, &mid, &hi);
    CHECK(mid < 0.70, "2 %% load (24 W) is under 70 %% (got %.3f)", mid);
    CHECK(pm_psu_parse_curve(&p, "2:65, 10:84, 20:88, 50:91, 100:89") == 5, "parse a typed Cybenetics curve");
    CHECK(pm_psu_parse_curve(&p, "20:88, 10:84") < 0, "a curve must go up in load");
    pm_psu_preset(4, 850, &p);   /* 80 PLUS Gold 115 V */
    pm_psu_eff(&p, 425, &lo, &mid, &hi);
    CHECK(NEAR(mid, 0.90, 1e-9) && p.rating == 850, "Gold 850 W at 50 %% = 90 %% (got %.3f)", mid);
}

static void t_estimate(void)
{
    pm_part parts[4];
    pm_psu p;
    pm_est e;
    pm_meas m = { 47, 32 };
    memset(parts, 0, sizeof parts);
    parts[0] = (pm_part){ "Fans", PK_FIXED, 9, 1, 1, 2.5, "" };
    parts[1] = (pm_part){ "CPU VRM", PK_CPU_VRM, 1, 1, 8, 12, "" };
    parts[2] = (pm_part){ "CPU if not measured", PK_CPU_EST, 1, 1, 20, 100, "" };
    parts[3] = (pm_part){ "Monitor", PK_OUTSIDE, 1, 1, 20, 30, "" };
    pm_psu_preset(0, 0, &p);
    pm_estimate(&m, parts, 4, &p, &e);
    CHECK(NEAR(e.dc_lo, 47 + 32 + 9 + 3.76, 1e-9), "dc_lo = measured + fans lo + VRM 8 %% (got %g)", e.dc_lo);
    CHECK(NEAR(e.dc_hi, 47 + 32 + 22.5 + 5.64, 1e-9), "dc_hi (got %g)", e.dc_hi);
    CHECK(e.wall_lo < e.wall_mid && e.wall_mid < e.wall_hi, "wall lo < mid < hi (%g %g %g)", e.wall_lo, e.wall_mid, e.wall_hi);
    CHECK(e.wall_lo > e.dc_lo + 20, "the monitor is added at the wall (%g)", e.wall_lo);
    m.cpu_w = NAN;
    pm_estimate(&m, parts, 4, &p, &e);
    CHECK(!e.cpu_measured && NEAR(e.dc_lo, 32 + 9 + 20, 1e-9), "unmeasured CPU uses its estimate row, no VRM %% (got %g)", e.dc_lo);
}

static time_t local_time(int y, int mo, int d, int h)
{
    struct tm tm;
    memset(&tm, 0, sizeof tm);
    tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d; tm.tm_hour = h; tm.tm_min = 30; tm.tm_isdst = -1;
    return mktime(&tm);
}

static void t_prices(void)
{
    pm_tariff t;
    pm_money m;
    char s[64];
    pm_tariff_default(&t);
    CHECK(NEAR(pm_cost(&t, 0, 0, 2), 0.30, 1e-9), "flat 0.15 x 2 kWh = 0.30");
    t.tax_pct = 10;
    CHECK(NEAR(pm_cost(&t, 0, 0, 2), 0.33, 1e-9), "plus 10 %% tax = 0.33");
    t.tax_pct = 0;
    t.type = PT_TIERED;
    t.ntier = 2; t.tier_upto[0] = 500; t.tier_price[0] = 0.10; t.tier_price[1] = 0.20;
    CHECK(NEAR(pm_cost(&t, 0, 499.5, 1), 0.05 + 0.10, 1e-9), "crossing a tier edge splits the kWh (got %g)", pm_cost(&t, 0, 499.5, 1));
    t.household_kwh = 600;
    CHECK(NEAR(pm_price(&t, 0, 0), 0.20, 1e-9), "household use first: the PC is in the top tier");
    t.type = PT_TOU;
    /* 2026-10-28 is a Wednesday, 2026-10-31 a Saturday (local time) */
    CHECK(NEAR(pm_price(&t, local_time(2026, 10, 28, 17), 0), 0.25, 1e-9), "weekday 17:30 = peak");
    CHECK(NEAR(pm_price(&t, local_time(2026, 10, 28, 3), 0), 0.10, 1e-9), "weekday 03:30 = off-peak");
    CHECK(NEAR(pm_price(&t, local_time(2026, 10, 31, 17), 0), 0.15, 1e-9), "Saturday 17:30 = standard");
    pm_currency_get(1, &m);
    pm_fmt_money(&m, 12.5, s, sizeof s);
    CHECK(!strcmp(s, "12.50\xe2\x82\xac"), "EUR after the number (%s)", s);
    pm_currency_get(0, &m);
    pm_fmt_money(&m, 0.0042, s, sizeof s);
    CHECK(!strcmp(s, "$0.0042"), "small sums keep digits (%s)", s);
}

static void t_units(void)
{
    char s[32];
    CHECK(NEAR(pm_to_base(1, PU_KWH), 1000, 1e-9), "1 kWh = 1000 Wh");
    CHECK(NEAR(pm_from_base(1, PU_KJ), 3.6, 1e-9), "1 Wh = 3.6 kJ");
    CHECK(NEAR(pm_from_base(1, PU_J), 3600, 1e-6), "1 Wh = 3600 J");
    CHECK(NEAR(pm_from_base(100, PU_BTUH), 341.2141633, 1e-6), "100 W = 341.2 BTU/h");
    pm_fmt_wh(3420, s, sizeof s);
    CHECK(!strcmp(s, "3.42 kWh"), "format 3420 Wh (%s)", s);
    pm_fmt_w(160.4, s, sizeof s);
    CHECK(!strcmp(s, "160 W"), "format 160.4 W (%s)", s);
}

static void t_alarms(void)
{
    pm_alarm a;
    pm_alarm_in in;
    pm_money m;
    char msg[300];
    int fired = 0, i;
    pm_currency_get(0, &m);
    memset(&a, 0, sizeof a);
    memset(&in, 0, sizeof in);
    a.on = 1; a.type = PA_W_NOW; a.unit = PU_W; a.limit = 400; a.secs = 10;
    for (i = 0; i < 30; i++) { in.t = i; in.w_now = 450; fired += pm_alarm_check(&a, &in, &m, msg, sizeof msg); }
    CHECK(fired == 1, "over 400 W for 30 s fires once (%d)", fired);
    in.t = 31; in.w_now = 370; pm_alarm_check(&a, &in, &m, msg, sizeof msg);
    for (i = 40; i < 60; i++) { in.t = i; in.w_now = 450; fired += pm_alarm_check(&a, &in, &m, msg, sizeof msg); }
    CHECK(fired == 2, "re-arms after dropping 5 %% below (%d)", fired);
    memset(&a, 0, sizeof a);
    a.on = 1; a.type = PA_E_DAY; a.unit = PU_KWH; a.limit = 3; a.fired_period = -1;
    in.day_no = 100; in.wh_day = 2999; fired = pm_alarm_check(&a, &in, &m, msg, sizeof msg);
    in.wh_day = 3001; fired += pm_alarm_check(&a, &in, &m, msg, sizeof msg);
    in.wh_day = 3500; fired += pm_alarm_check(&a, &in, &m, msg, sizeof msg);
    CHECK(fired == 1, "3 kWh/day fires once a day (%d)", fired);
    in.day_no = 101; in.wh_day = 3100; fired += pm_alarm_check(&a, &in, &m, msg, sizeof msg);
    CHECK(fired == 2, "and again the next day (%d)", fired);
    memset(&a, 0, sizeof a);
    a.on = 1; a.type = PA_FC_COST_MONTH; a.limit = 20; a.fired_period = -1;
    in.month_no = 7; in.cost_month = 8; in.month_frac = 0.33;
    CHECK(pm_alarm_check(&a, &in, &m, msg, sizeof msg) == 1 && strstr(msg, "$24"), "forecast 8 / 0.33 = 24 > 20 (%s)", msg);
}

static void t_sessions(void)
{
    pm_sess ss;
    pm_sess_done d;
    int t, n = 0;
    double wh = 400.0 / 3600;   /* 400 W for 1 s */
    memset(&ss, 0, sizeof ss);
    ss.thr = 50; ss.start_s = 120; ss.end_s = 180;
    for (t = 1; t <= 1020; t++) {
        double busy = (t > 60 && t <= 360) || (t > 420 && t <= 720) ? 90 : (t > 360 && t <= 420) ? 10 : 5;
        if (pm_sess_step(&ss, t, busy, 1, wh, wh * 0.15 / 1000, 400, &d)) n++;
    }
    CHECK(n == 1, "one session with a 60 s pause inside (%d)", n);
    CHECK(NEAR(d.t0, 60, 1e-9) && NEAR(d.t1, 720, 1e-9), "session 60..720 s (got %g..%g)", d.t0, d.t1);
    CHECK(NEAR(d.wh, 660 * wh, 1e-6), "energy of the session incl. the pause = %.3f Wh (got %.3f)", 660 * wh, d.wh);
    memset(&ss, 0, sizeof ss);
    ss.thr = 50; ss.start_s = 120; ss.end_s = 180;
    n = 0;
    for (t = 1; t <= 600; t++) if (pm_sess_step(&ss, t, t > 100 && t <= 200 ? 95 : 0, 1, wh, 0, 400, &d)) n++;
    CHECK(n == 0 && !ss.active, "a 100 s burst is not a session");
    memset(&ss, 0, sizeof ss);
    ss.thr = 50; ss.start_s = 120; ss.end_s = 180;
    for (t = 1; t <= 300; t++) pm_sess_step(&ss, t, 80, 1, wh, 0, 400, &d);
    CHECK(pm_sess_flush(&ss, 300, &d) == 1 && NEAR(d.t1, 300, 1e-9) && NEAR(d.t0, 0, 1e-9), "flush ends a running session now");
    CHECK(pm_sess_flush(&ss, 301, &d) == 0, "nothing left to flush");
}

/* ---- the database: minutes add up, groups, gaps, re-open, CSV ---- */
static void t_db(void)
{
    char path[] = "/tmp/srs-power-test-XXXXXX";
    char db[300], csv[320];
    int fd = mkstemp(path);
    pdb_row r;
    pdb_sum sums[64];
    int n, i;
    double total = 0;
    FILE *f;
    char line[512];
    long t0 = 1793000000 / 60 * 60;   /* a fixed minute (2026-10) */
    if (fd >= 0) close(fd);
    unlink(path);
    snprintf(db, sizeof db, "%s.db", path);
    CHECK(pdb_open(db) == 0, "open a new database at %s (%s)", db, pdb_error());
    memset(&r, 0, sizeof r);
    r.secs_on = 30; r.wh_mid = 1.0; r.wh_lo = 0.8; r.wh_hi = 1.3; r.wh_cpu = 0.4; r.wh_gpu = 0.3; r.cost = 0.00015; r.w_max = 150;
    r.secs_busy = 10; r.wh_busy = 0.5;
    for (i = 0; i < 180; i++) {             /* 3 hours of minutes, each written in two halves */
        CHECK(pdb_add(t0 + i * 60, &r) == 0 || i, "add (%s)", pdb_error());
        pdb_add(t0 + i * 60, &r);
    }
    CHECK(pdb_flush() == 0, "commit");
    n = pdb_group(PDB_HOUR, t0, t0 + 180 * 60, sums, 64);
    for (i = 0; i < n; i++) total += sums[i].wh_mid;
    CHECK(n >= 3 && n <= 4, "3 hours of minutes give 3-4 hour rows (%d)", n);
    CHECK(NEAR(total, 360, 1e-6), "2 x 1 Wh x 180 minutes = 360 Wh (got %g)", total);
    total = 0;
    for (i = 0; i < n; i++) total += sums[i].wh_busy;
    CHECK(NEAR(total, 180, 1e-6), "busy energy adds up too: 180 Wh (got %g)", total);
    {   /* a 2 s sample straddling a minute edge, split the way the recorder does it */
        pdb_row y, z = r;
        double tw = 0, tb = 0, ts = 0;
        splitctx sp = { { 0 }, { 0 }, 0 };
        int k;
        long base = t0 + 500 * 60;
        z.secs_on = 2; z.wh_mid = 0.09; z.secs_busy = 2; z.wh_busy = 0.09; z.cost = 0.0000135;
        pm_split_minutes((double)base - 0.5, (double)base + 1.5, split_cb, &sp);
        for (k = 0; k < sp.n; k++) { y = z; pdb_row_scale(&y, sp.frac[k]); pdb_add(sp.m[k], &y); }
        pdb_flush();
        n = pdb_group(PDB_HOUR, base - 3600, base + 3600, sums, 64);
        for (k = 0; k < n; k++) { tw += sums[k].wh_mid; tb += sums[k].wh_busy; ts += sums[k].secs_busy; }
        CHECK(sp.n == 2 && NEAR(tw, 0.09, 1e-12) && NEAR(tb, 0.09, 1e-12) && NEAR(ts, 2, 1e-12),
              "a sample split over two minutes keeps its energy and busy time (%g Wh, %g busy Wh, %g busy s)", tw, tb, ts);
    }
    CHECK(pdb_session(t0 + 60, t0 + 3600, 250, 0.04, 480) == 0, "store a session");
    {
        pdb_sess ss[4];
        CHECK(pdb_sessions(t0, t0 + 7200, ss, 4) == 1 && NEAR(ss[0].wh, 250, 1e-9) && ss[0].t1 - ss[0].t0 == 3540, "read it back");
    }
    CHECK(NEAR(pdb_sum_range(t0, t0 + 60 * 60, NULL), 120, 1e-6), "first hour = 120 Wh");
    pdb_gap(t0 + 200 * 60, t0 + 260 * 60, PDB_GAP_ASLEEP);
    pdb_meta_set("boot_id", "abc");
    pdb_close();
    CHECK(pdb_open(db) == 0, "re-open");
    {
        char v[64];
        CHECK(pdb_meta_get("boot_id", v, sizeof v) && !strcmp(v, "abc"), "meta survives a re-open");
    }
    CHECK(pdb_count_gaps(t0, t0 + 1000 * 60) == 1, "one gap stored");
    snprintf(csv, sizeof csv, "%s.csv", path);
    CHECK(pdb_export_csv(csv, PDB_MINUTE, t0, t0 + 10 * 60, "# test note", NULL, NULL) == 10, "10 minute rows exported");
    f = fopen(csv, "r");
    n = 0;
    while (f && fgets(line, sizeof line, f)) { if (line[0] != '#') n++; }
    if (f) fclose(f);
    CHECK(n == 11, "CSV has a header and 10 rows (%d lines)", n);
    pdb_close();
    unlink(db); unlink(csv);
    snprintf(line, sizeof line, "%s-wal", db); unlink(line);
    snprintf(line, sizeof line, "%s-shm", db); unlink(line);
}

int main(void)
{
    setenv("TZ", "UTC", 1);
    tzset();
    t_counter_basic();
    t_counter_wrap();
    t_counter_reset();
    t_counter_long_run();
    t_trapezoid();
    t_classify();
    t_split();
    t_psu();
    t_estimate();
    t_prices();
    t_units();
    t_alarms();
    t_sessions();
    t_db();
    printf("%d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
