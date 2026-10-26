/* power_model.h - SRS "Power & cost": the arithmetic, with no GTK, SQLite or sysfs in it.
 *
 * Energy counters (unwrap, resets, sanity limits), clocks (sleep / reboot / stall gaps), the
 * whole-PC estimate (measured CPU + GPU, estimated parts, PSU efficiency band), prices (flat,
 * tiered, time-of-use), units and alarms. Kept separate so tests/test_power.c can feed it
 * synthetic counter data.
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#ifndef SRS_POWER_MODEL_H
#define SRS_POWER_MODEL_H

#include <time.h>

/* ---- energy counters (sysfs: microjoules) ---- */
enum { PM_F_WRAP = 1, PM_F_RESET = 2, PM_F_BAD = 4, PM_F_GAP = 8, PM_F_EST = 16 };

typedef struct {
    double prev;        /* last raw reading (uJ) */
    double max_range;   /* counter range in uJ (powercap max_energy_range_uj); 0 = 64-bit / unknown */
    int have;
} pm_counter;

/* One step of a counter read `dt` seconds after the previous one. Returns joules used in the
 * interval, or -1 when the interval cannot be trusted (first read, reset, more than one possible
 * wrap, or an impossible power above max_w); `flags` gets PM_F_* bits. The counter always takes
 * `now` as its new baseline. */
double pm_counter_step(pm_counter *c, double now, double dt, double max_w, int *flags);

/* trapezoid integral of a power-only source (watts) over dt seconds, in joules */
double pm_trapezoid(double w0, double w1, double dt);

/* ---- clocks: monotonic (stops in suspend), boottime (keeps counting), boot_id ---- */
enum { PM_GAP_NONE, PM_GAP_SLEEP, PM_GAP_REBOOT, PM_GAP_STALL };
typedef struct { double mono, boot, wall; char boot_id[40]; } pm_clock;

/* Classify the time between two samples. awake_s = seconds the PC ran, sleep_s = seconds suspended.
 * SLEEP: boottime moved more than monotonic (by > 2 s). REBOOT: boot_id changed. STALL: the
 * program itself did not run for more than max_dt (stopped, frozen). */
int pm_classify(const pm_clock *prev, const pm_clock *now, double max_dt, double *awake_s, double *sleep_s);

/* ---- the PSU ---- */
#define PM_PSU_PTS 12
typedef struct { double load, eff; } pm_pt;   /* load 0..1 of the rating, efficiency 0..1 */
typedef struct {
    char name[96];
    double rating;       /* W DC */
    double band;         /* +- efficiency (0..1) at and above 20 % load */
    int npt;
    pm_pt pt[PM_PSU_PTS];
    char source[300];    /* where the curve comes from (shown in the UI) */
} pm_psu;

/* efficiency at a DC load: mid value and the lo..hi band (widened below 20 % load) */
void pm_psu_eff(const pm_psu *p, double dc_w, double *lo, double *mid, double *hi);
/* presets: 0 = Montech Century II 1200W, then generic 80 PLUS levels at 115 V and 230 V, last = unknown */
int pm_psu_preset_count(void);
const char *pm_psu_preset_name(int i);
void pm_psu_preset(int i, double rating, pm_psu *out);
/* "load%:eff%,..." <-> points (for the expert curve box). Returns number of points parsed. */
int pm_psu_parse_curve(pm_psu *p, const char *s);
void pm_psu_curve_text(const pm_psu *p, char *out, int sz);

/* ---- parts that cannot be measured ---- */
enum {
    PK_FIXED,        /* lo..hi watts each, times count */
    PK_CPU_VRM,      /* lo..hi percent of the measured CPU power (VRM losses) */
    PK_GPU_REST,     /* lo..hi percent of the reported GPU power (rest of the card) */
    PK_CPU_EST,      /* the CPU itself when it cannot be measured (ignored when it is) */
    PK_GPU_EST,      /* the graphics card when it cannot be measured (ignored when it is) */
    PK_OUTSIDE,      /* plugged in beside the PC (monitor): added at the wall, not through the PSU */
    PK_N
};
typedef struct {
    char name[80];
    int kind, count, on;
    double lo, hi;       /* watts each (or percent for the % kinds) */
    char how[120];       /* "datasheet", "typical guess", "your entry", ... */
} pm_part;
const char *pm_kind_name(int kind);

typedef struct { double cpu_w, gpu_w; } pm_meas;   /* NAN = not measured */
typedef struct {
    double dc_lo, dc_mid, dc_hi;
    double eff_lo, eff_mid, eff_hi;
    double wall_lo, wall_mid, wall_hi;
    double meas_w;       /* measured part (CPU + GPU) */
    double outside_lo, outside_hi;
    int cpu_measured, gpu_measured;
} pm_est;

/* part i's contribution (lo / hi watts) with this measurement; 0 when it does not apply */
void pm_part_watts(const pm_part *p, const pm_meas *m, double *lo, double *hi);
void pm_estimate(const pm_meas *m, const pm_part *parts, int n, const pm_psu *psu, pm_est *e);

/* ---- prices ---- */
enum { PT_FLAT, PT_TIERED, PT_TOU };
#define PM_TIERS 6
#define PM_TOU_LEVELS 4
typedef struct {
    int type;
    double flat;                          /* per kWh */
    int ntier;
    double tier_upto[PM_TIERS];           /* tier i applies up to this many kWh in the month (last: no limit) */
    double tier_price[PM_TIERS];
    double household_kwh;                 /* the rest of the home's monthly use, billed before the PC */
    double tou_price[PM_TOU_LEVELS];
    char tou_name[PM_TOU_LEVELS][24];
    unsigned char tou_wd[24], tou_we[24]; /* level per hour, weekdays / weekends */
    double tax_pct;                       /* added to every price */
    double fixed_day;                     /* standing charge per day */
    int fixed_on;                         /* include the standing charge in totals (off by default) */
} pm_tariff;
void pm_tariff_default(pm_tariff *t);
/* price per kWh (tax included) for energy used at local time `t`, when `month_kwh` has already
 * been used by the PC this month (tiered) */
double pm_price(const pm_tariff *t, time_t when, double month_kwh);
/* cost of `kwh` used at `when`, with tiers split exactly when the energy crosses a tier edge */
double pm_cost(const pm_tariff *t, time_t when, double month_kwh, double kwh);

typedef struct { char code[8], sym[8]; int after, dec; } pm_money;
void pm_fmt_money(const pm_money *m, double v, char *out, int sz);
int pm_currency_count(void);
const char *pm_currency_code(int i);
void pm_currency_get(int i, pm_money *m);   /* sensible symbol / placement / decimals */

/* ---- units ---- */
enum { PU_W, PU_KW, PU_BTUH, PU_WH, PU_KWH, PU_MWH, PU_J, PU_KJ, PU_MJ, PU_N };
const char *pm_unit_name(int u);
int pm_unit_is_power(int u);
double pm_to_base(double v, int u);     /* power units -> W, energy units -> Wh */
double pm_from_base(double v, int u);
void pm_fmt_w(double w, char *out, int sz);      /* "160 W", "1.25 kW" */
void pm_fmt_wh(double wh, char *out, int sz);    /* "850 Wh", "3.42 kWh", "1.20 MWh" */

/* ---- minutes: split an interval [t0, t1) of wall-clock seconds over the minutes it covers ---- */
typedef void (*pm_minute_cb)(long minute_start, double frac, void *ctx);
void pm_split_minutes(double t0, double t1, pm_minute_cb cb, void *ctx);

/* ---- alarms ---- */
enum { PA_W_NOW, PA_W_AVG, PA_E_DAY, PA_E_MONTH, PA_COST_DAY, PA_COST_MONTH, PA_FC_E_MONTH, PA_FC_COST_MONTH, PA_N };
typedef struct {
    int on, type, unit;
    double limit;        /* in `unit` (cost types: money) */
    double secs;         /* PA_W_NOW: how long above; PA_W_AVG: averaging window */
    /* state */
    double above_since;  /* PA_W_NOW */
    long fired_period;   /* day / month number it last fired in (re-arms next period) */
    int fired;           /* PA_W_*: fired and not yet re-armed (re-arms 5 % below the limit) */
} pm_alarm;
const char *pm_alarm_name(int type);
int pm_alarm_is_cost(int type);
int pm_alarm_is_power(int type);
typedef struct {
    double t;            /* now (wall seconds) */
    double w_now, w_avg; /* wall W now, average over the alarm's window */
    double wh_day, wh_month, cost_day, cost_month;
    double month_frac;   /* fraction of the month gone (0..1) */
    long day_no, month_no;
} pm_alarm_in;
/* returns 1 when the alarm fires now (once), writing a message into msg */
int pm_alarm_check(pm_alarm *a, const pm_alarm_in *in, const pm_money *money, char *msg, int sz);

#endif
