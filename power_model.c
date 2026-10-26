/* power_model.c - SRS "Power & cost": counters, clocks, estimate, PSU curve, prices, units, alarms.
 * Pure C (libm only); see power_model.h.
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#include "power_model.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ====================================================================== counters */
double pm_counter_step(pm_counter *c, double now, double dt, double max_w, int *flags)
{
    double d;
    int f = 0;
    if (flags) *flags = 0;
    if (!c->have || isnan(c->prev) || dt <= 0) {
        c->prev = now;
        c->have = 1;
        return -1;
    }
    /* a counter of range R can wrap unseen once every R / max_w seconds: past that, a reading
       cannot tell one wrap from two, so the interval is not trusted (the recorder samples every
       second or two, far inside the ~65 s this PC's RAPL range allows at 1000 W) */
    if (c->max_range > 0 && max_w > 0 && dt * max_w * 1e6 >= c->max_range) f |= PM_F_BAD | PM_F_GAP;
    if (now >= c->prev) d = now - c->prev;
    else if (c->max_range > 0) { d = now + c->max_range - c->prev; f |= PM_F_WRAP; }
    else { d = -1; f |= PM_F_RESET; }   /* 64-bit accumulator went down: driver reload or reset */
    c->prev = now;
    if (d >= 0 && max_w > 0 && d / 1e6 / dt > max_w) f |= PM_F_BAD;   /* impossible power: a reset */
    if (flags) *flags = f;
    if (f & (PM_F_BAD | PM_F_RESET)) return -1;
    return d / 1e6;
}

double pm_trapezoid(double w0, double w1, double dt)
{
    if (isnan(w0)) w0 = w1;
    if (isnan(w1)) w1 = w0;
    if (isnan(w0) || dt <= 0) return 0;
    return (w0 + w1) / 2 * dt;
}

/* ====================================================================== clocks */
int pm_classify(const pm_clock *a, const pm_clock *b, double max_dt, double *awake_s, double *sleep_s)
{
    double dm = b->mono - a->mono, db = b->boot - a->boot, sl;
    if (awake_s) *awake_s = 0;
    if (sleep_s) *sleep_s = 0;
    if (a->boot_id[0] && b->boot_id[0] && strcmp(a->boot_id, b->boot_id)) return PM_GAP_REBOOT;
    if (dm < 0 || db < 0) return PM_GAP_REBOOT;
    sl = db - dm;
    if (awake_s) *awake_s = dm;
    if (sl > 2.0) { if (sleep_s) *sleep_s = sl; return PM_GAP_SLEEP; }
    if (dm > max_dt) return PM_GAP_STALL;
    return PM_GAP_NONE;
}

/* ====================================================================== PSU */
/* efficiency between points is interpolated on log(load): PSU curves rise steeply at light load
   and flatten out, which a straight line between 2 % and 20 % would get badly wrong */
static double interp(const pm_psu *p, double load)
{
    int i;
    if (p->npt <= 0) return 0.85;
    if (load <= p->pt[0].load) return p->pt[0].eff;
    for (i = 1; i < p->npt; i++) {
        if (load <= p->pt[i].load) {
            double a = log(p->pt[i - 1].load), b = log(p->pt[i].load), x = log(load);
            double f = b > a ? (x - a) / (b - a) : 0;
            return p->pt[i - 1].eff + f * (p->pt[i].eff - p->pt[i - 1].eff);
        }
    }
    return p->pt[p->npt - 1].eff;
}

void pm_psu_eff(const pm_psu *p, double dc_w, double *lo, double *mid, double *hi)
{
    double load = p->rating > 0 ? dc_w / p->rating : 0.2, m, band = p->band > 0 ? p->band : 0.02;
    if (load < 0.005) load = 0.005;
    m = interp(p, load);
    /* the 80 PLUS tests start at 20 % load (Titanium at 10 %): below that, be less sure */
    if (load < 0.2) band += (0.2 - load) / 0.18 * 0.06;
    *mid = m;
    *lo = m - band;
    *hi = m + band;
    if (*lo < 0.4) *lo = 0.4;
    if (*hi > 0.99) *hi = 0.99;
    if (*mid > *hi) *mid = *hi;
}

typedef struct { const char *name; double rating; int volt230; double e10, e20, e50, e100; } psudef;
/* 80 PLUS internal-redundant minimums, 115 V and 230 V EU (Wikipedia "80 Plus", citing CLEAResult) */
static const psudef g_psus[] = {
    { "80 PLUS (white), 115 V", 0, 0, 0, 0.80, 0.80, 0.80 },
    { "80 PLUS Bronze, 115 V", 0, 0, 0, 0.82, 0.85, 0.82 },
    { "80 PLUS Silver, 115 V", 0, 0, 0, 0.85, 0.88, 0.85 },
    { "80 PLUS Gold, 115 V", 0, 0, 0, 0.87, 0.90, 0.87 },
    { "80 PLUS Platinum, 115 V", 0, 0, 0, 0.90, 0.92, 0.89 },
    { "80 PLUS Titanium, 115 V", 0, 0, 0.90, 0.92, 0.94, 0.90 },
    { "80 PLUS (white), 230 V EU", 0, 1, 0, 0.82, 0.85, 0.82 },
    { "80 PLUS Bronze, 230 V EU", 0, 1, 0, 0.85, 0.88, 0.85 },
    { "80 PLUS Silver, 230 V EU", 0, 1, 0, 0.87, 0.90, 0.87 },
    { "80 PLUS Gold, 230 V EU", 0, 1, 0, 0.90, 0.92, 0.89 },
    { "80 PLUS Platinum, 230 V EU", 0, 1, 0, 0.92, 0.94, 0.90 },
    { "80 PLUS Titanium, 230 V EU", 0, 1, 0.90, 0.93, 0.95, 0.91 },
};
#define NPSU ((int)(sizeof g_psus / sizeof g_psus[0]))

int pm_psu_preset_count(void) { return NPSU + 2; }

const char *pm_psu_preset_name(int i)
{
    if (i == 0) return "Montech CENTURY II 1200W (80 PLUS Gold, Cybenetics Platinum)";
    if (i >= 1 && i <= NPSU) return g_psus[i - 1].name;
    return "I don't know my power supply";
}

static void addpt(pm_psu *p, double load, double eff)
{
    if (p->npt < PM_PSU_PTS && eff > 0) { p->pt[p->npt].load = load; p->pt[p->npt].eff = eff; p->npt++; }
}

void pm_psu_preset(int i, double rating, pm_psu *o)
{
    memset(o, 0, sizeof *o);
    snprintf(o->name, sizeof o->name, "%s", pm_psu_preset_name(i));
    o->band = 0.02;
    if (i == 0) {
        o->rating = 1200;
        /* not a measured curve: the 80 PLUS Gold minimums at 20/50/100 %, lifted to the Cybenetics
           Platinum average (89-91 % at 115 V) that HWbusters' review found, and its note that
           efficiency falls under 70 % at 2 % load (about 24 W) */
        addpt(o, 0.02, 0.68);
        addpt(o, 0.20, 0.89);
        addpt(o, 0.50, 0.91);
        addpt(o, 1.00, 0.88);
        o->band = 0.025;
        snprintf(o->source, sizeof o->source,
                 "Derived, not measured: 80 PLUS Gold minimums, Cybenetics Platinum average (HWbusters review), "
                 "under 70 %% at 2 %% load (HWbusters). Type points from your PSU's Cybenetics report for a better curve.");
        return;
    }
    if (i >= 1 && i <= NPSU) {
        const psudef *d = &g_psus[i - 1];
        o->rating = rating > 0 ? rating : 750;
        /* below the lowest tested point: an assumed fall-off, flagged by the wider band */
        addpt(o, 0.02, (d->e10 > 0 ? d->e10 : d->e20) - 0.17);
        if (d->e10 > 0) addpt(o, 0.10, d->e10);
        addpt(o, 0.20, d->e20);
        addpt(o, 0.50, d->e50);
        addpt(o, 1.00, d->e100);
        snprintf(o->source, sizeof o->source,
                 "80 PLUS minimum efficiency at 20/50/100 %% load (Wikipedia \"80 Plus\", citing CLEAResult). "
                 "Below 20 %% load the curve is an assumption and the range is wider.");
        return;
    }
    o->rating = rating > 0 ? rating : 750;
    addpt(o, 0.02, 0.62);
    addpt(o, 0.20, 0.85);
    addpt(o, 0.50, 0.87);
    addpt(o, 1.00, 0.84);
    o->band = 0.05;
    snprintf(o->source, sizeof o->source, "Unknown power supply: a typical curve with a wide range.");
}

int pm_psu_parse_curve(pm_psu *p, const char *s)
{
    int n = 0;
    const char *q = s;
    pm_pt tmp[PM_PSU_PTS];
    while (q && *q && n < PM_PSU_PTS) {
        double l, e;
        char *end;
        while (*q == ' ' || *q == ',' || *q == ';') q++;
        if (!*q) break;
        l = strtod(q, &end);
        if (end == q || *end != ':') return -1;
        q = end + 1;
        e = strtod(q, &end);
        if (end == q) return -1;
        q = end;
        if (l <= 0 || l > 120 || e <= 20 || e > 100) return -1;
        if (n && l / 100 <= tmp[n - 1].load) return -1;
        tmp[n].load = l / 100;
        tmp[n].eff = e / 100;
        n++;
    }
    if (n < 2) return -1;
    memcpy(p->pt, tmp, sizeof tmp[0] * (size_t)n);
    p->npt = n;
    return n;
}

void pm_psu_curve_text(const pm_psu *p, char *out, int sz)
{
    int i, k = 0;
    out[0] = 0;
    for (i = 0; i < p->npt && k < sz; i++)
        k += snprintf(out + k, (size_t)(sz - k), "%s%g:%g", i ? ", " : "", p->pt[i].load * 100, round(p->pt[i].eff * 1000) / 10);
}

/* ====================================================================== estimate */
const char *pm_kind_name(int k)
{
    static const char *const n[PK_N] = { "Watts each", "% of CPU power", "% of GPU power", "CPU (if not measured)",
                                         "Graphics card (if not measured)", "Beside the PC (at the wall)" };
    return k >= 0 && k < PK_N ? n[k] : "?";
}

void pm_part_watts(const pm_part *p, const pm_meas *m, double *lo, double *hi)
{
    double c = p->count > 0 ? p->count : 1;
    *lo = *hi = 0;
    if (!p->on) return;
    switch (p->kind) {
    case PK_FIXED: case PK_OUTSIDE: *lo = p->lo * c; *hi = p->hi * c; break;
    case PK_CPU_VRM: if (!isnan(m->cpu_w)) { *lo = m->cpu_w * p->lo / 100; *hi = m->cpu_w * p->hi / 100; } break;
    case PK_GPU_REST: if (!isnan(m->gpu_w)) { *lo = m->gpu_w * p->lo / 100; *hi = m->gpu_w * p->hi / 100; } break;
    case PK_CPU_EST: if (isnan(m->cpu_w)) { *lo = p->lo * c; *hi = p->hi * c; } break;
    case PK_GPU_EST: if (isnan(m->gpu_w)) { *lo = p->lo * c; *hi = p->hi * c; } break;
    default: break;
    }
    if (*hi < *lo) { double t = *hi; *hi = *lo; *lo = t; }
}

void pm_estimate(const pm_meas *m, const pm_part *parts, int n, const pm_psu *psu, pm_est *e)
{
    int i;
    double lo = 0, hi = 0, olo = 0, ohi = 0, el, em, eh, x;
    memset(e, 0, sizeof *e);
    e->cpu_measured = !isnan(m->cpu_w);
    e->gpu_measured = !isnan(m->gpu_w);
    e->meas_w = (e->cpu_measured ? m->cpu_w : 0) + (e->gpu_measured ? m->gpu_w : 0);
    for (i = 0; i < n; i++) {
        double a, b;
        pm_part_watts(&parts[i], m, &a, &b);
        if (parts[i].kind == PK_OUTSIDE) { olo += a; ohi += b; }
        else { lo += a; hi += b; }
    }
    e->dc_lo = e->meas_w + lo;
    e->dc_hi = e->meas_w + hi;
    e->dc_mid = (e->dc_lo + e->dc_hi) / 2;
    pm_psu_eff(psu, e->dc_mid, &el, &em, &eh);
    e->eff_lo = el; e->eff_mid = em; e->eff_hi = eh;
    /* the low end: least DC at the best efficiency at that load; the high end: most DC at the worst */
    pm_psu_eff(psu, e->dc_lo, &el, &x, &eh);
    e->wall_lo = e->dc_lo / eh;
    pm_psu_eff(psu, e->dc_hi, &el, &x, &eh);
    e->wall_hi = e->dc_hi / el;
    e->wall_mid = e->dc_mid / em;
    e->outside_lo = olo;
    e->outside_hi = ohi;
    e->wall_lo += olo;
    e->wall_hi += ohi;
    e->wall_mid += (olo + ohi) / 2;
}

/* ====================================================================== prices */
void pm_tariff_default(pm_tariff *t)
{
    int h;
    memset(t, 0, sizeof *t);
    t->type = PT_FLAT;
    t->flat = 0.15;
    t->ntier = 2;
    t->tier_upto[0] = 500; t->tier_price[0] = 0.12;
    t->tier_upto[1] = 0;   t->tier_price[1] = 0.18;
    snprintf(t->tou_name[0], sizeof t->tou_name[0], "Off-peak");
    snprintf(t->tou_name[1], sizeof t->tou_name[1], "Standard");
    snprintf(t->tou_name[2], sizeof t->tou_name[2], "Peak");
    snprintf(t->tou_name[3], sizeof t->tou_name[3], "Super-peak");
    t->tou_price[0] = 0.10; t->tou_price[1] = 0.15; t->tou_price[2] = 0.25; t->tou_price[3] = 0.40;
    for (h = 0; h < 24; h++) {
        t->tou_wd[h] = h < 7 || h >= 23 ? 0 : (h >= 16 && h < 21) ? 2 : 1;
        t->tou_we[h] = h < 7 || h >= 23 ? 0 : 1;
    }
}

static double tax(const pm_tariff *t, double p) { return p * (1 + t->tax_pct / 100); }

/* the tier the month's kWh count is in, and how many kWh are left in it (-1 = no limit) */
static int tier_at(const pm_tariff *t, double pos, double *left)
{
    int i, n = t->ntier < 1 ? 1 : t->ntier > PM_TIERS ? PM_TIERS : t->ntier;
    for (i = 0; i < n - 1; i++)
        if (t->tier_upto[i] > 0 && pos < t->tier_upto[i]) { *left = t->tier_upto[i] - pos; return i; }
    *left = -1;
    return n - 1;
}

double pm_price(const pm_tariff *t, time_t when, double month_kwh)
{
    struct tm tm;
    double left;
    int h;
    switch (t->type) {
    case PT_TIERED:
        return tax(t, t->tier_price[tier_at(t, t->household_kwh + month_kwh, &left)]);
    case PT_TOU:
        localtime_r(&when, &tm);
        h = tm.tm_hour;
        {
            int lv = (tm.tm_wday == 0 || tm.tm_wday == 6) ? t->tou_we[h] : t->tou_wd[h];
            if (lv >= PM_TOU_LEVELS) lv = 0;
            return tax(t, t->tou_price[lv]);
        }
    default:
        return tax(t, t->flat);
    }
}

double pm_cost(const pm_tariff *t, time_t when, double month_kwh, double kwh)
{
    double c = 0, pos, left;
    int guard = 0;
    if (kwh <= 0) return 0;
    if (t->type != PT_TIERED) return pm_price(t, when, month_kwh) * kwh;
    pos = t->household_kwh + month_kwh;
    while (kwh > 1e-12 && guard++ < PM_TIERS + 2) {
        int i = tier_at(t, pos, &left);
        double take = left < 0 || left > kwh ? kwh : left;
        c += tax(t, t->tier_price[i]) * take;
        kwh -= take;
        pos += take;
    }
    return c;
}

typedef struct { const char *code, *sym; int after, dec; } curdef;
static const curdef g_cur[] = {
    { "USD", "$", 0, 2 }, { "EUR", "\xe2\x82\xac", 1, 2 }, { "GBP", "\xc2\xa3", 0, 2 }, { "CAD", "C$", 0, 2 },
    { "AUD", "A$", 0, 2 }, { "NZD", "NZ$", 0, 2 }, { "JPY", "\xc2\xa5", 0, 0 }, { "CHF", "CHF ", 0, 2 },
    { "SEK", " kr", 1, 2 }, { "NOK", " kr", 1, 2 }, { "DKK", " kr", 1, 2 }, { "PLN", " z\xc5\x82", 1, 2 },
    { "CZK", " K\xc4\x8d", 1, 2 }, { "INR", "\xe2\x82\xb9", 0, 2 }, { "BRL", "R$", 0, 2 }, { "MXN", "$", 0, 2 },
    { "ZAR", "R", 0, 2 }, { "CNY", "\xc2\xa5", 0, 2 }, { "KRW", "\xe2\x82\xa9", 0, 0 }, { "Custom", "", 0, 2 },
};
#define NCUR ((int)(sizeof g_cur / sizeof g_cur[0]))
int pm_currency_count(void) { return NCUR; }
const char *pm_currency_code(int i) { return i >= 0 && i < NCUR ? g_cur[i].code : "Custom"; }
void pm_currency_get(int i, pm_money *m)
{
    if (i < 0 || i >= NCUR) i = 0;
    snprintf(m->code, sizeof m->code, "%s", g_cur[i].code);
    snprintf(m->sym, sizeof m->sym, "%s", g_cur[i].sym);
    m->after = g_cur[i].after;
    m->dec = g_cur[i].dec;
}

void pm_fmt_money(const pm_money *m, double v, char *out, int sz)
{
    int dec = m->dec < 0 ? 0 : m->dec > 6 ? 6 : m->dec;
    if (isnan(v)) { snprintf(out, (size_t)sz, "-"); return; }
    /* very small sums (cost per hour) keep two significant digits instead of showing 0.00 */
    if (v != 0 && fabs(v) < pow(10, -dec) * 10) dec = dec + 2 > 6 ? 6 : dec + 2;
    if (m->after) snprintf(out, (size_t)sz, "%s%.*f%s", v < 0 ? "-" : "", dec, fabs(v), m->sym);
    else snprintf(out, (size_t)sz, "%s%s%.*f", v < 0 ? "-" : "", m->sym, dec, fabs(v));
}

/* ====================================================================== units */
static const char *const g_unit[PU_N] = { "W", "kW", "BTU/h", "Wh", "kWh", "MWh", "J", "kJ", "MJ" };
static const double g_ufac[PU_N] = { 1, 1000, 1 / 3.412141633, 1, 1000, 1e6, 1 / 3600.0, 1 / 3.6, 1000 / 3.6 };
const char *pm_unit_name(int u) { return u >= 0 && u < PU_N ? g_unit[u] : "?"; }
int pm_unit_is_power(int u) { return u == PU_W || u == PU_KW || u == PU_BTUH; }
double pm_to_base(double v, int u) { return u >= 0 && u < PU_N ? v * g_ufac[u] : v; }
double pm_from_base(double v, int u) { return u >= 0 && u < PU_N ? v / g_ufac[u] : v; }

void pm_fmt_w(double w, char *out, int sz)
{
    if (isnan(w)) snprintf(out, (size_t)sz, "-");
    else if (fabs(w) >= 10000) snprintf(out, (size_t)sz, "%.1f kW", w / 1000);
    else if (fabs(w) >= 1000) snprintf(out, (size_t)sz, "%.2f kW", w / 1000);
    else if (fabs(w) >= 10) snprintf(out, (size_t)sz, "%.0f W", w);
    else snprintf(out, (size_t)sz, "%.1f W", w);
}

void pm_fmt_wh(double wh, char *out, int sz)
{
    if (isnan(wh)) snprintf(out, (size_t)sz, "-");
    else if (fabs(wh) >= 1e6) snprintf(out, (size_t)sz, "%.2f MWh", wh / 1e6);
    else if (fabs(wh) >= 1000) snprintf(out, (size_t)sz, "%.2f kWh", wh / 1000);
    else if (fabs(wh) >= 10) snprintf(out, (size_t)sz, "%.0f Wh", wh);
    else snprintf(out, (size_t)sz, "%.1f Wh", wh);
}

/* ====================================================================== minutes */
void pm_split_minutes(double t0, double t1, pm_minute_cb cb, void *ctx)
{
    double len = t1 - t0, a = t0;
    if (len <= 0) return;
    while (a < t1 - 1e-9) {
        long m = (long)floor(a / 60) * 60;
        double b = (double)m + 60 < t1 ? (double)m + 60 : t1;
        cb(m, (b - a) / len, ctx);
        a = b;
    }
}

/* ====================================================================== alarms */
const char *pm_alarm_name(int t)
{
    static const char *const n[PA_N] = { "Power right now is over", "Average power is over", "Energy today reaches",
                                         "Energy this month reaches", "Cost today reaches", "Cost this month reaches",
                                         "This month's energy is heading over", "This month's cost is heading over" };
    return t >= 0 && t < PA_N ? n[t] : "?";
}
int pm_alarm_is_cost(int t) { return t == PA_COST_DAY || t == PA_COST_MONTH || t == PA_FC_COST_MONTH; }
int pm_alarm_is_power(int t) { return t == PA_W_NOW || t == PA_W_AVG; }

int pm_alarm_check(pm_alarm *a, const pm_alarm_in *in, const pm_money *money, char *msg, int sz)
{
    double lim, v = 0;
    long period;
    char s1[64], s2[64];
    if (!a->on) return 0;
    lim = pm_alarm_is_cost(a->type) ? a->limit : pm_to_base(a->limit, a->unit);
    if (pm_alarm_is_power(a->type)) {
        v = a->type == PA_W_NOW ? in->w_now : in->w_avg;
        if (isnan(v)) return 0;
        if (v > lim) {
            if (a->type == PA_W_NOW && a->above_since <= 0) a->above_since = in->t;
            if (!a->fired && (a->type == PA_W_AVG || in->t - a->above_since >= a->secs)) {
                a->fired = 1;
                pm_fmt_w(v, s1, sizeof s1);
                pm_fmt_w(lim, s2, sizeof s2);
                if (a->type == PA_W_NOW)
                    snprintf(msg, (size_t)sz, "The PC has drawn about %s for %.0f s (your limit: %s).", s1, in->t - a->above_since, s2);
                else
                    snprintf(msg, (size_t)sz, "The PC's average over the last %.0f min is about %s (your limit: %s).", a->secs / 60, s1, s2);
                return 1;
            }
        } else {
            a->above_since = 0;
            if (v < lim * 0.95) a->fired = 0;   /* re-arm once clearly below */
        }
        return 0;
    }
    switch (a->type) {
    case PA_E_DAY: v = in->wh_day; period = in->day_no; break;
    case PA_E_MONTH: v = in->wh_month; period = in->month_no; break;
    case PA_COST_DAY: v = in->cost_day; period = in->day_no; break;
    case PA_COST_MONTH: v = in->cost_month; period = in->month_no; break;
    case PA_FC_E_MONTH: case PA_FC_COST_MONTH:
        if (in->month_frac < 1.0 / 31) return 0;   /* need a day of data before forecasting */
        v = (a->type == PA_FC_E_MONTH ? in->wh_month : in->cost_month) / in->month_frac;
        period = in->month_no;
        break;
    default: return 0;
    }
    if (isnan(v) || v < lim || a->fired_period == period) return 0;
    a->fired_period = period;
    if (pm_alarm_is_cost(a->type)) { pm_fmt_money(money, v, s1, sizeof s1); pm_fmt_money(money, lim, s2, sizeof s2); }
    else { pm_fmt_wh(v, s1, sizeof s1); pm_fmt_wh(lim, s2, sizeof s2); }
    if (a->type == PA_FC_E_MONTH || a->type == PA_FC_COST_MONTH)
        snprintf(msg, (size_t)sz, "At this rate the month ends at about %s (your limit: %s).", s1, s2);
    else
        snprintf(msg, (size_t)sz, "%s %s (your limit: %s).", pm_alarm_name(a->type), s1, s2);
    return 1;
}
