/* power.c - SRS "Power & cost": the page in srs-gui's main window, the recorder, and the
 * headless background recorder (`srs-gui --power-recorder`).
 *
 * Tabs: Now (live estimate with its range, cost at this draw, recording), History (graphs by
 * hour / day / week / month, table, CSV export, re-pricing), Price (currency, flat / tiered /
 * time-of-use, offline price tables), Setup (power supply, parts, sleep / off draw, recording,
 * background recorder), Alarms (desktop notifications) and Sources (what is measured and how).
 *
 * Only one process records at a time: the one holding the lock file next to the database. The
 * other one shows the same history read from the database.
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "power.h"
#include "power_db.h"
#include "power_model.h"
#include "power_src.h"
#include "power_rates.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define MAXPARTS 48
#define MAXALARM 16
#define RING 3600
#define HROWS 400

static const char *const g_disclaimer_short =
    "Estimates only. Most of these numbers are educated guesses: they are not a meter reading and not a bill.";

static const char *const g_disclaimer =
    "<b>Estimates only. Please read.</b>\n\n"
    "SRS shows how much power your computer <b>probably</b> uses and what it <b>might</b> cost. These numbers are "
    "<b>estimates</b>, not a meter reading and not a bill.\n\n"
    "\xe2\x80\xa2 Only some parts of a PC report their own power (usually the processor and the graphics card), and even "
    "those reports can be off. Everything else (motherboard, memory, drives, fans, lights, the power supply's own "
    "losses, and your screens) is <b>guessed</b> from typical values. That is why SRS shows a range.\n"
    "\xe2\x80\xa2 A plug-in power meter or smart plug is more accurate, but it can also be wrong.\n"
    "\xe2\x80\xa2 Electricity prices come from what you type in or from public tables that may be <b>out of date</b> or "
    "not match <b>your</b> plan. Your real price can include tiers, time-of-day rates, standing charges, taxes, "
    "discounts and fees that SRS does not know about.\n"
    "\xe2\x80\xa2 CO\xe2\x82\x82 figures use grid averages. Your supplier's actual mix may be very different.\n"
    "\xe2\x80\xa2 Alarms can be late, missed, or wrong (for example if the computer sleeps, crashes, or a sensor stops "
    "reporting).\n\n"
    "<b>Do not use SRS for billing, for safety decisions, or as proof of energy use.</b> Always check your electricity "
    "bill and your supplier.\n\n"
    "SRS is free software provided \"as is\", without any warranty (GNU General Public License v2.0 or later, "
    "sections 11-12). Eugene Flanagin and the SRS contributors are not responsible for any errors, losses, costs or "
    "damage that come from using these estimates.\n\n"
    "Price and emissions data: U.S. EIA, Eurostat, Ofgem, AER, Ember and others (see the Price tab). These "
    "organisations are not responsible for SRS's use or changes of their data.";

typedef struct {
    /* ---- settings (power.ini) ---- */
    pm_part parts[MAXPARTS];
    int nparts;
    pm_psu psu;
    int psu_preset;
    pm_tariff tar;
    pm_money money;
    int cur_idx;
    char price_src[400];
    double co2_g;                 /* grams CO2 per kWh, 0 = off */
    char co2_src[200];
    int mode;                     /* 0 off, 1 for a set time, 2 always */
    double until;
    int until_n, until_unit;      /* unit: 0 minutes, 1 hours, 2 days */
    int interval;                 /* seconds between samples */
    double sleep_w, off_w;
    int disclaimer_ok, notify, dec_comma;
    double busy_thr, sess_start_min, sess_end_min;
    pm_alarm alarm[MAXALARM];
    int nalarm;
    /* ---- runtime ---- */
    int headless;
    psrc src;
    pm_clock last;
    pm_meas meas;
    pm_est est, prev_est;
    int have_est, flags;
    double load_cpu, load_gpu, busy;
    pm_sess sess;
    double ring_t[RING], ring_lo[RING], ring_mid[RING], ring_hi[RING];
    int ring_n, ring_i;
    int lockfd, locked, other_rec;
    char db_path[1024], ini_path[1024];
    long month_key;
    double month_wh;
    double last_flush, last_slow;
    char last_alarm[600];
    guint timer;
    void (*open_item)(const char *);
    int loading;
    /* ---- widgets ---- */
    GtkWidget *page, *nb, *big, *range, *badges, *cost[4], *pricelbl, *rec_combo, *rec_n, *rec_unit, *rec_start, *rec_status,
              *today, *live, *bd_view;
    GtkListStore *bd_store;
    GtkWidget *h_range, *h_what, *h_area, *h_sum, *h_view;
    GtkListStore *h_store;
    pdb_sum h_rows[HROWS];
    int h_n, h_group, h_cost;
    GtkWidget *c_cur, *c_sym, *c_after, *c_dec, *c_type[3], *c_stack, *c_flat, *c_tup[PM_TIERS], *c_tpr[PM_TIERS], *c_house,
              *c_tname[PM_TOU_LEVELS], *c_tprice[PM_TOU_LEVELS], *c_tou[2][24], *c_tax, *c_fixed, *c_fixon, *c_src, *c_proj,
              *c_co2, *c_co2src, *c_rate_country, *c_rate_region, *c_rate_zip, *c_rate_info;
    GtkWidget *s_psu, *s_rating, *s_curve, *s_psusrc, *s_psunow, *s_pview, *s_sleep, *s_off, *s_int, *s_bg, *s_bgstat, *s_db,
              *s_curveerr;
    GtkListStore *s_pstore, *s_kinds;
    GtkWidget *a_view, *a_type, *a_limit, *a_unit, *a_secs, *a_conv, *a_notify, *a_last, *a_form;
    GtkListStore *a_store;
    GtkWidget *src_lbl, *src_raw;
    GtkWidget *x_range, *x_thr, *x_start, *x_end, *x_sum, *x_view, *x_now;
    GtkListStore *x_store;
} pwstate;
static pwstate G;

static void ui_now(void);
static void ui_slow(void);
static void hist_reload(void);
static void cfg_save(void);
static void parts_fill(void);
static void alarms_fill(void);
static void sess_fill(void);
static void sess_now_text(void);

/* ====================================================================== paths and settings */
static void paths(void)
{
    const char *e;
    if (G.db_path[0]) return;
    if ((e = getenv("SRS_POWER_DB")) && *e) snprintf(G.db_path, sizeof G.db_path, "%s", e);
    else {
        char *p = g_build_filename(g_get_user_data_dir(), "srs", "power.db", NULL);
        snprintf(G.db_path, sizeof G.db_path, "%s", p);
        g_free(p);
    }
    if ((e = getenv("SRS_POWER_INI")) && *e) snprintf(G.ini_path, sizeof G.ini_path, "%s", e);
    else {
        char *p = g_build_filename(g_get_user_config_dir(), "srs", "power.ini", NULL);
        snprintf(G.ini_path, sizeof G.ini_path, "%s", p);
        g_free(p);
    }
}

static void defaults(void)
{
    pm_tariff_default(&G.tar);
    pm_currency_get(0, &G.money);
    G.cur_idx = 0;
    snprintf(G.price_src, sizeof G.price_src, "SRS's example price: type yours from your bill.");
    G.psu_preset = pm_psu_preset_count() - 1;
    pm_psu_preset(G.psu_preset, 750, &G.psu);
    G.nparts = psrc_default_parts(&G.src, G.parts, MAXPARTS);
    G.interval = 2;
    G.until_n = 2;
    G.until_unit = 1;
    G.sleep_w = 3;
    G.off_w = 1;
    G.notify = 1;
    G.busy_thr = 50;
    G.sess_start_min = 2;
    G.sess_end_min = 3;
}

static double kf_dbl(GKeyFile *k, const char *g, const char *key, double def)
{
    GError *e = NULL;
    double v = g_key_file_get_double(k, g, key, &e);
    if (e) { g_error_free(e); return def; }
    return v;
}
static int kf_int(GKeyFile *k, const char *g, const char *key, int def)
{
    GError *e = NULL;
    int v = g_key_file_get_integer(k, g, key, &e);
    if (e) { g_error_free(e); return def; }
    return v;
}
static void kf_str(GKeyFile *k, const char *g, const char *key, char *out, int sz)
{
    char *v = g_key_file_get_string(k, g, key, NULL);
    if (v) { snprintf(out, (size_t)sz, "%s", v); g_free(v); }
}

static void cfg_load(void)
{
    GKeyFile *k = g_key_file_new();
    int i, n;
    char key[64], buf[512];
    paths();
    defaults();
    if (!g_key_file_load_from_file(k, G.ini_path, G_KEY_FILE_NONE, NULL)) { g_key_file_free(k); return; }
    G.disclaimer_ok = kf_int(k, "Power", "Disclaimer Accepted", 0);
    G.mode = kf_int(k, "Power", "Recording", 0);
    G.until = kf_dbl(k, "Power", "Recording Until", 0);
    G.until_n = kf_int(k, "Power", "Recording For", 2);
    G.until_unit = kf_int(k, "Power", "Recording For Unit", 1);
    G.interval = kf_int(k, "Power", "Sample Seconds", 2);
    if (G.interval < 1 || G.interval > 60) G.interval = 2;
    G.sleep_w = kf_dbl(k, "Power", "Asleep Watts", 3);
    G.off_w = kf_dbl(k, "Power", "Off Watts", 1);
    G.notify = kf_int(k, "Power", "Desktop Notifications", 1);
    G.dec_comma = kf_int(k, "Power", "CSV Decimal Comma", 0);
    G.busy_thr = kf_dbl(k, "Power", "Busy Percent", 50);
    G.sess_start_min = kf_dbl(k, "Power", "Session Start Minutes", 2);
    G.sess_end_min = kf_dbl(k, "Power", "Session End Minutes", 3);
    /* money */
    G.cur_idx = kf_int(k, "Price", "Currency", 0);
    pm_currency_get(G.cur_idx, &G.money);
    kf_str(k, "Price", "Symbol", G.money.sym, sizeof G.money.sym);
    G.money.after = kf_int(k, "Price", "Symbol After", G.money.after);
    G.money.dec = kf_int(k, "Price", "Decimals", G.money.dec);
    G.tar.type = kf_int(k, "Price", "Type", PT_FLAT);
    G.tar.flat = kf_dbl(k, "Price", "Flat", G.tar.flat);
    G.tar.household_kwh = kf_dbl(k, "Price", "Household kWh", 0);
    G.tar.tax_pct = kf_dbl(k, "Price", "Tax Percent", 0);
    G.tar.fixed_day = kf_dbl(k, "Price", "Standing Charge Per Day", 0);
    G.tar.fixed_on = kf_int(k, "Price", "Include Standing Charge", 0);
    kf_str(k, "Price", "Source", G.price_src, sizeof G.price_src);
    G.co2_g = kf_dbl(k, "Price", "CO2 g per kWh", 0);
    kf_str(k, "Price", "CO2 Source", G.co2_src, sizeof G.co2_src);
    n = kf_int(k, "Price", "Tiers", G.tar.ntier);
    if (n >= 1 && n <= PM_TIERS) G.tar.ntier = n;
    for (i = 0; i < PM_TIERS; i++) {
        snprintf(key, sizeof key, "Tier %d Up To", i + 1);
        G.tar.tier_upto[i] = kf_dbl(k, "Price", key, G.tar.tier_upto[i]);
        snprintf(key, sizeof key, "Tier %d Price", i + 1);
        G.tar.tier_price[i] = kf_dbl(k, "Price", key, G.tar.tier_price[i]);
    }
    for (i = 0; i < PM_TOU_LEVELS; i++) {
        snprintf(key, sizeof key, "TOU %d Name", i + 1);
        kf_str(k, "Price", key, G.tar.tou_name[i], sizeof G.tar.tou_name[i]);
        snprintf(key, sizeof key, "TOU %d Price", i + 1);
        G.tar.tou_price[i] = kf_dbl(k, "Price", key, G.tar.tou_price[i]);
    }
    buf[0] = 0;
    kf_str(k, "Price", "TOU Weekdays", buf, sizeof buf);
    if (strlen(buf) == 24) for (i = 0; i < 24; i++) G.tar.tou_wd[i] = (unsigned char)((buf[i] - '0') & 3);
    buf[0] = 0;
    kf_str(k, "Price", "TOU Weekends", buf, sizeof buf);
    if (strlen(buf) == 24) for (i = 0; i < 24; i++) G.tar.tou_we[i] = (unsigned char)((buf[i] - '0') & 3);
    /* PSU */
    G.psu_preset = kf_int(k, "PSU", "Preset", G.psu_preset);
    if (G.psu_preset < 0 || G.psu_preset >= pm_psu_preset_count()) G.psu_preset = pm_psu_preset_count() - 1;
    pm_psu_preset(G.psu_preset, kf_dbl(k, "PSU", "Rating", 750), &G.psu);
    G.psu.rating = kf_dbl(k, "PSU", "Rating", G.psu.rating);
    buf[0] = 0;
    kf_str(k, "PSU", "Curve", buf, sizeof buf);
    if (buf[0] && pm_psu_parse_curve(&G.psu, buf) > 0 && kf_int(k, "PSU", "Curve Typed", 0))
        snprintf(G.psu.source, sizeof G.psu.source, "Your typed curve (load %%:efficiency %%).");
    /* parts */
    n = kf_int(k, "Parts", "Count", -1);
    if (n >= 0) {
        G.nparts = 0;
        for (i = 0; i < n && G.nparts < MAXPARTS; i++) {
            char **v;
            snprintf(key, sizeof key, "Part %d", i + 1);
            buf[0] = 0;
            kf_str(k, "Parts", key, buf, sizeof buf);
            v = g_strsplit(buf, "|", 7);
            if (g_strv_length(v) >= 6) {
                pm_part *p = &G.parts[G.nparts++];
                memset(p, 0, sizeof *p);
                snprintf(p->name, sizeof p->name, "%s", v[0]);
                p->kind = atoi(v[1]);
                if (p->kind < 0 || p->kind >= PK_N) p->kind = PK_FIXED;
                p->count = atoi(v[2]);
                p->on = atoi(v[3]);
                p->lo = g_ascii_strtod(v[4], NULL);
                p->hi = g_ascii_strtod(v[5], NULL);
                if (v[6]) snprintf(p->how, sizeof p->how, "%s", v[6]);
            }
            g_strfreev(v);
        }
    }
    /* alarms */
    n = kf_int(k, "Alarms", "Count", 0);
    G.nalarm = 0;
    for (i = 0; i < n && G.nalarm < MAXALARM; i++) {
        char **v;
        snprintf(key, sizeof key, "Alarm %d", i + 1);
        buf[0] = 0;
        kf_str(k, "Alarms", key, buf, sizeof buf);
        v = g_strsplit(buf, "|", 6);
        if (g_strv_length(v) >= 5) {
            pm_alarm *a = &G.alarm[G.nalarm++];
            memset(a, 0, sizeof *a);
            a->on = atoi(v[0]);
            a->type = atoi(v[1]);
            a->unit = atoi(v[2]);
            a->limit = g_ascii_strtod(v[3], NULL);
            a->secs = g_ascii_strtod(v[4], NULL);
            a->fired_period = v[5] ? atol(v[5]) : -1;
            if (a->type < 0 || a->type >= PA_N) a->type = PA_W_NOW;
        }
        g_strfreev(v);
    }
    g_key_file_free(k);
}

static void kf_set_dbl(GKeyFile *k, const char *g, const char *key, double v)
{
    char b[G_ASCII_DTOSTR_BUF_SIZE];
    g_key_file_set_value(k, g, key, g_ascii_dtostr(b, sizeof b, v));
}

static void cfg_save(void)
{
    GKeyFile *k;
    char key[64], buf[700], *dir;
    int i;
    if (G.headless || G.loading) return;   /* the background recorder never writes settings */
    paths();
    k = g_key_file_new();
    g_key_file_load_from_file(k, G.ini_path, G_KEY_FILE_KEEP_COMMENTS, NULL);
    g_key_file_set_integer(k, "Power", "Disclaimer Accepted", G.disclaimer_ok);
    g_key_file_set_integer(k, "Power", "Recording", G.mode);
    kf_set_dbl(k, "Power", "Recording Until", G.until);
    g_key_file_set_integer(k, "Power", "Recording For", G.until_n);
    g_key_file_set_integer(k, "Power", "Recording For Unit", G.until_unit);
    g_key_file_set_integer(k, "Power", "Sample Seconds", G.interval);
    kf_set_dbl(k, "Power", "Asleep Watts", G.sleep_w);
    kf_set_dbl(k, "Power", "Off Watts", G.off_w);
    g_key_file_set_integer(k, "Power", "Desktop Notifications", G.notify);
    g_key_file_set_integer(k, "Power", "CSV Decimal Comma", G.dec_comma);
    kf_set_dbl(k, "Power", "Busy Percent", G.busy_thr);
    kf_set_dbl(k, "Power", "Session Start Minutes", G.sess_start_min);
    kf_set_dbl(k, "Power", "Session End Minutes", G.sess_end_min);
    g_key_file_set_integer(k, "Price", "Currency", G.cur_idx);
    g_key_file_set_string(k, "Price", "Symbol", G.money.sym);
    g_key_file_set_integer(k, "Price", "Symbol After", G.money.after);
    g_key_file_set_integer(k, "Price", "Decimals", G.money.dec);
    g_key_file_set_integer(k, "Price", "Type", G.tar.type);
    kf_set_dbl(k, "Price", "Flat", G.tar.flat);
    kf_set_dbl(k, "Price", "Household kWh", G.tar.household_kwh);
    kf_set_dbl(k, "Price", "Tax Percent", G.tar.tax_pct);
    kf_set_dbl(k, "Price", "Standing Charge Per Day", G.tar.fixed_day);
    g_key_file_set_integer(k, "Price", "Include Standing Charge", G.tar.fixed_on);
    g_key_file_set_string(k, "Price", "Source", G.price_src);
    kf_set_dbl(k, "Price", "CO2 g per kWh", G.co2_g);
    g_key_file_set_string(k, "Price", "CO2 Source", G.co2_src);
    g_key_file_set_integer(k, "Price", "Tiers", G.tar.ntier);
    for (i = 0; i < PM_TIERS; i++) {
        snprintf(key, sizeof key, "Tier %d Up To", i + 1);
        kf_set_dbl(k, "Price", key, G.tar.tier_upto[i]);
        snprintf(key, sizeof key, "Tier %d Price", i + 1);
        kf_set_dbl(k, "Price", key, G.tar.tier_price[i]);
    }
    for (i = 0; i < PM_TOU_LEVELS; i++) {
        snprintf(key, sizeof key, "TOU %d Name", i + 1);
        g_key_file_set_string(k, "Price", key, G.tar.tou_name[i]);
        snprintf(key, sizeof key, "TOU %d Price", i + 1);
        kf_set_dbl(k, "Price", key, G.tar.tou_price[i]);
    }
    for (i = 0; i < 24; i++) buf[i] = (char)('0' + G.tar.tou_wd[i]);
    buf[24] = 0;
    g_key_file_set_string(k, "Price", "TOU Weekdays", buf);
    for (i = 0; i < 24; i++) buf[i] = (char)('0' + G.tar.tou_we[i]);
    g_key_file_set_string(k, "Price", "TOU Weekends", buf);
    g_key_file_set_integer(k, "PSU", "Preset", G.psu_preset);
    kf_set_dbl(k, "PSU", "Rating", G.psu.rating);
    pm_psu_curve_text(&G.psu, buf, sizeof buf);
    g_key_file_set_string(k, "PSU", "Curve", buf);
    g_key_file_set_integer(k, "PSU", "Curve Typed", !strncmp(G.psu.source, "Your typed", 10));
    g_key_file_remove_group(k, "Parts", NULL);
    g_key_file_set_integer(k, "Parts", "Count", G.nparts);
    for (i = 0; i < G.nparts; i++) {
        pm_part *p = &G.parts[i];
        char lo[G_ASCII_DTOSTR_BUF_SIZE], hi[G_ASCII_DTOSTR_BUF_SIZE], nm[96], how[140];
        snprintf(nm, sizeof nm, "%s", p->name);
        snprintf(how, sizeof how, "%s", p->how);
        g_strdelimit(nm, "|", '/');
        g_strdelimit(how, "|", '/');
        snprintf(key, sizeof key, "Part %d", i + 1);
        snprintf(buf, sizeof buf, "%s|%d|%d|%d|%s|%s|%s", nm, p->kind, p->count, p->on, g_ascii_dtostr(lo, sizeof lo, p->lo),
                 g_ascii_dtostr(hi, sizeof hi, p->hi), how);
        g_key_file_set_string(k, "Parts", key, buf);
    }
    g_key_file_remove_group(k, "Alarms", NULL);
    g_key_file_set_integer(k, "Alarms", "Count", G.nalarm);
    for (i = 0; i < G.nalarm; i++) {
        pm_alarm *a = &G.alarm[i];
        char lim[G_ASCII_DTOSTR_BUF_SIZE], secs[G_ASCII_DTOSTR_BUF_SIZE];
        snprintf(key, sizeof key, "Alarm %d", i + 1);
        snprintf(buf, sizeof buf, "%d|%d|%d|%s|%s|%ld", a->on, a->type, a->unit, g_ascii_dtostr(lim, sizeof lim, a->limit),
                 g_ascii_dtostr(secs, sizeof secs, a->secs), a->fired_period);
        g_key_file_set_string(k, "Alarms", key, buf);
    }
    dir = g_path_get_dirname(G.ini_path);
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);
    g_key_file_save_to_file(k, G.ini_path, NULL);
    g_key_file_free(k);
}

/* ====================================================================== recording */
static int rec_wanted(double wall)
{
    if (G.mode == 2) return 1;
    return G.mode == 1 && wall < G.until;
}

static int db_ensure(int create)
{
    paths();
    if (pdb_is_open()) return 1;
    if (!create && !g_file_test(G.db_path, G_FILE_TEST_EXISTS)) return 0;
    return pdb_open(G.db_path) == 0;
}

static int lock_take(void)
{
    char p[1100];
    if (G.locked) return 1;
    paths();
    snprintf(p, sizeof p, "%s.lock", G.db_path);
    {
        char *dir = g_path_get_dirname(p);
        g_mkdir_with_parents(dir, 0700);
        g_free(dir);
    }
    if (G.lockfd <= 0) G.lockfd = open(p, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (G.lockfd < 0) { G.lockfd = 0; return 0; }
    if (flock(G.lockfd, LOCK_EX | LOCK_NB) == 0) { G.locked = 1; G.other_rec = 0; return 1; }
    G.other_rec = 1;
    return 0;
}

static void lock_drop(void)
{
    if (G.locked) flock(G.lockfd, LOCK_UN);
    G.locked = 0;
}

/* someone else (the background recorder or another SRS) holds the lock */
static int lock_probe_other(void)
{
    char p[1100];
    int fd, busy = 0;
    if (G.locked) return 0;
    paths();
    snprintf(p, sizeof p, "%s.lock", G.db_path);
    fd = open(p, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    if (flock(fd, LOCK_SH | LOCK_NB) != 0) busy = 1;
    else flock(fd, LOCK_UN);
    close(fd);
    return busy;
}

typedef struct { pdb_row r; } addctx;
static void add_minute(long m, double frac, void *ctx)
{
    addctx *a = ctx;
    pdb_row x = a->r;
    x.secs_on *= frac; x.secs_sleep *= frac; x.secs_off *= frac;
    x.wh_mid *= frac; x.wh_lo *= frac; x.wh_hi *= frac; x.wh_cpu *= frac; x.wh_gpu *= frac; x.cost *= frac;
    pdb_add(m, &x);
}

static long month_of(double wall)
{
    time_t t = (time_t)wall;
    struct tm tm;
    localtime_r(&t, &tm);
    return tm.tm_year * 12L + tm.tm_mon;
}

static double month_start(double wall)
{
    time_t t = (time_t)wall;
    struct tm tm;
    localtime_r(&t, &tm);
    tm.tm_mday = 1; tm.tm_hour = 0; tm.tm_min = 0; tm.tm_sec = 0; tm.tm_isdst = -1;
    return (double)mktime(&tm);
}

static double day_start(double wall)
{
    time_t t = (time_t)wall;
    struct tm tm;
    localtime_r(&t, &tm);
    tm.tm_hour = 0; tm.tm_min = 0; tm.tm_sec = 0; tm.tm_isdst = -1;
    return (double)mktime(&tm);
}

/* write [t0, t1) to the minutes table; cost priced at the end of the interval */
static void rec_interval(double t0, double t1, pdb_row *r)
{
    addctx a;
    long mk = month_of(t1);
    if (mk != G.month_key) { G.month_key = mk; G.month_wh = pdb_sum_range((long)month_start(t1), (long)t0, NULL); }
    r->cost = pm_cost(&G.tar, (time_t)t1, G.month_wh / 1000, r->wh_mid / 1000);
    G.month_wh += r->wh_mid;
    a.r = *r;
    pm_split_minutes(t0, t1, add_minute, &a);
}

/* energy over a gap at a fixed draw (asleep / off), one row per minute, at most 31 days */
static void rec_fixed(double t0, double t1, double w, int asleep)
{
    pdb_row r;
    if (t1 - t0 > 31 * 86400.0) t0 = t1 - 31 * 86400.0;
    if (t1 <= t0) return;
    memset(&r, 0, sizeof r);
    if (asleep) r.secs_sleep = t1 - t0; else r.secs_off = t1 - t0;
    r.wh_mid = r.wh_lo = r.wh_hi = w * (t1 - t0) / 3600;
    r.w_max = w;
    r.flags = PM_F_EST;
    rec_interval(t0, t1, &r);
}

static void rec_meta(void)
{
    char b[64];
    snprintf(b, sizeof b, "%.0f", G.last.wall);
    pdb_meta_set("last_sample", b);
    pdb_meta_set("boot_id", G.last.boot_id);
}

/* recording starts: account for the time since the last sample (off, or not recorded) */
static void rec_begin(void)
{
    char b[64], boot[64];
    double last, bootw, now = G.last.wall;
    G.month_key = -1;
    if (!pdb_meta_get("last_sample", b, sizeof b)) { rec_meta(); pdb_flush(); return; }
    last = atof(b);
    pdb_meta_get("boot_id", boot, sizeof boot);
    bootw = psrc_boot_wall();
    if (last > 0 && now - last > 90) {
        if (boot[0] && strcmp(boot, G.last.boot_id) && bootw > last && bootw <= now) {
            pdb_gap((long)last, (long)bootw, PDB_GAP_OFF);
            if (G.off_w > 0) rec_fixed(last, bootw, G.off_w, 0);
            if (now - bootw > 90) pdb_gap((long)bootw, (long)now, PDB_GAP_NOREC);
        } else
            pdb_gap((long)last, (long)now, PDB_GAP_NOREC);
    }
    rec_meta();
    pdb_flush();
}

static void sess_end(double wall)
{
    pm_sess_done d;
    if (pm_sess_flush(&G.sess, wall, &d) && pdb_is_open() && G.locked && d.t1 - d.t0 >= 60) pdb_session(d.t0, d.t1, d.wh, d.cost, d.peak);
}

static void rec_end(void)
{
    if (!G.locked) return;
    sess_end(G.last.wall);
    if (pdb_is_open()) { rec_meta(); pdb_flush(); }
    lock_drop();
}

/* desktop notification over D-Bus (org.freedesktop.Notifications): no extra library */
static void notify_desktop(const char *title, const char *body)
{
    GDBusConnection *c = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
    GVariantBuilder acts, hints;
    if (!c) return;
    g_variant_builder_init(&acts, G_VARIANT_TYPE("as"));
    g_variant_builder_init(&hints, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&hints, "{sv}", "desktop-entry", g_variant_new_string("srs-gui"));
    g_dbus_connection_call(c, "org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications",
                           "Notify", g_variant_new("(susssasa{sv}i)", "SRS", 0u, "srs", title, body, &acts, &hints, -1),
                           NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL, NULL, NULL);
    g_object_unref(c);
}

static double ring_avg(double secs)
{
    int i, n = 0;
    double s = 0, now = G.ring_n ? G.ring_t[(G.ring_i + RING - 1) % RING] : 0;
    for (i = 0; i < G.ring_n; i++) {
        int k = (G.ring_i + RING - 1 - i) % RING;
        if (now - G.ring_t[k] > secs) break;
        s += G.ring_mid[k];
        n++;
    }
    return n ? s / n : NAN;
}

static void alarms_run(double wall)
{
    pm_alarm_in in;
    int i, changed = 0;
    double ds = day_start(wall), ms = month_start(wall);
    time_t t = (time_t)wall;
    struct tm tm;
    if (!G.nalarm) return;
    localtime_r(&t, &tm);
    memset(&in, 0, sizeof in);
    in.t = wall;
    in.w_now = G.have_est ? G.est.wall_mid : NAN;
    in.day_no = (long)(ds / 86400);
    in.month_no = month_of(wall);
    if (db_ensure(0)) {
        in.wh_day = pdb_sum_range((long)ds, (long)wall + 60, &in.cost_day);
        in.wh_month = pdb_sum_range((long)ms, (long)wall + 60, &in.cost_month);
    }
    {
        int dim = 31;
        struct tm n2 = tm;
        n2.tm_mon++; n2.tm_mday = 0; n2.tm_isdst = -1; n2.tm_hour = 12;
        mktime(&n2);
        dim = n2.tm_mday;
        in.month_frac = (wall - ms) / (dim * 86400.0);
    }
    for (i = 0; i < G.nalarm; i++) {
        char msg[400];
        pm_alarm *a = &G.alarm[i];
        long fp = a->fired_period;
        if (a->type == PA_W_AVG) in.w_avg = ring_avg(a->secs > 0 ? a->secs : 1800);
        if (pm_alarm_check(a, &in, &G.money, msg, sizeof msg)) {
            char when[32];
            strftime(when, sizeof when, "%H:%M", &tm);
            snprintf(G.last_alarm, sizeof G.last_alarm, "%s  %s", when, msg);
            if (G.notify) notify_desktop("SRS power alarm (estimate)", msg);
        }
        if (fp != a->fired_period) changed = 1;
    }
    if (changed) cfg_save();
}

/* one sample: read, estimate, record */
static void rec_tick(void)
{
    pm_clock now;
    pm_meas m;
    double awake = 0, sl = 0, dt;
    int gap, fl = 0, want;
    psrc_clock(&now);
    gap = pm_classify(&G.last, &now, G.interval * 5.0 > 30 ? G.interval * 5.0 : 30, &awake, &sl);
    dt = now.mono - G.last.mono;
    if (gap == PM_GAP_NONE) psrc_read(&G.src, dt, &m, &fl);
    else { psrc_read(&G.src, 0, &m, &fl); m = G.meas; fl |= PM_F_GAP; }   /* new baseline after a gap */
    G.meas = m;
    G.flags = fl;
    psrc_load(&G.src, &G.load_cpu, &G.load_gpu);
    G.busy = isnan(G.load_gpu) ? G.load_cpu : isnan(G.load_cpu) ? G.load_gpu : fmax(G.load_cpu, G.load_gpu);
    if (G.have_est) G.prev_est = G.est;
    pm_estimate(&m, G.parts, G.nparts, &G.psu, &G.est);
    if (!G.have_est) G.prev_est = G.est;
    G.have_est = 1;
    G.ring_t[G.ring_i] = now.wall;
    G.ring_lo[G.ring_i] = G.est.wall_lo;
    G.ring_mid[G.ring_i] = G.est.wall_mid;
    G.ring_hi[G.ring_i] = G.est.wall_hi;
    G.ring_i = (G.ring_i + 1) % RING;
    if (G.ring_n < RING) G.ring_n++;

    if (G.mode == 1 && now.wall >= G.until && !G.headless) { G.mode = 0; cfg_save(); }
    want = rec_wanted(now.wall);
    if (want && !G.locked && lock_take() && db_ensure(1)) { G.last = now; rec_begin(); }
    else if (!want && G.locked) rec_end();
    if (G.locked && pdb_is_open() && G.last.mono > 0) {
        if (gap == PM_GAP_NONE) {
            pdb_row r;
            memset(&r, 0, sizeof r);
            r.secs_on = dt;
            r.wh_mid = (G.prev_est.wall_mid + G.est.wall_mid) / 2 * dt / 3600;
            r.wh_lo = (G.prev_est.wall_lo + G.est.wall_lo) / 2 * dt / 3600;
            r.wh_hi = (G.prev_est.wall_hi + G.est.wall_hi) / 2 * dt / 3600;
            r.wh_cpu = isnan(m.cpu_w) ? 0 : m.cpu_w * dt / 3600;
            r.wh_gpu = isnan(m.gpu_w) ? 0 : m.gpu_w * dt / 3600;
            r.w_max = G.est.wall_mid;
            r.flags = fl;
            if (!isnan(G.busy) && G.busy >= G.busy_thr) { r.secs_busy = dt; r.wh_busy = r.wh_mid; }
            rec_interval(now.wall - dt, now.wall, &r);
            {
                pm_sess_done d;
                G.sess.thr = G.busy_thr;
                G.sess.start_s = G.sess_start_min * 60;
                G.sess.end_s = G.sess_end_min * 60;
                if (pm_sess_step(&G.sess, now.wall, G.busy, dt, r.wh_mid, r.cost, G.est.wall_mid, &d)) pdb_session(d.t0, d.t1, d.wh, d.cost, d.peak);
            }
        } else if (gap == PM_GAP_SLEEP) {
            sess_end(G.last.wall);
            double s1 = now.wall - awake, s0 = s1 - sl;
            pdb_gap((long)s0, (long)s1, PDB_GAP_ASLEEP);
            if (G.sleep_w > 0) rec_fixed(s0, s1, G.sleep_w, 1);
        } else {
            sess_end(G.last.wall);
            pdb_gap((long)G.last.wall, (long)now.wall, gap == PM_GAP_STALL ? PDB_GAP_STALL : PDB_GAP_NOREC);
        }
        if (now.mono - G.last_flush > 30) { G.last = now; rec_meta(); pdb_flush(); G.last_flush = now.mono; }
    }
    G.last = now;
    /* alarms: in the recording process, or here when nobody records */
    if (now.mono - G.last_slow >= 10 || G.last_slow == 0) {
        G.other_rec = !G.locked && lock_probe_other();
        if (G.locked || !G.other_rec) alarms_run(now.wall);
    }
}

static gboolean on_timer(gpointer d)
{
    pm_clock c;
    (void)d;
    rec_tick();
    psrc_clock(&c);
    if (!G.headless && G.page && gtk_widget_get_mapped(G.page)) {
        ui_now();
        if (c.mono - G.last_slow >= 10) ui_slow();
    }
    if (c.mono - G.last_slow >= 10) G.last_slow = c.mono;
    return G_SOURCE_CONTINUE;
}

static void timer_restart(void)
{
    if (G.timer) g_source_remove(G.timer);
    G.timer = g_timeout_add_seconds((guint)G.interval, on_timer, NULL);
}

static void rec_start_runtime(void)
{
    cfg_load();
    psrc_detect(&G.src);
    if (G.nparts == 0) G.nparts = psrc_default_parts(&G.src, G.parts, MAXPARTS);
    psrc_clock(&G.last);
    G.month_key = -1;
    timer_restart();
}

/* ====================================================================== the background recorder */
static GMainLoop *g_loop;
static gboolean on_quit_signal(gpointer d) { (void)d; if (g_loop) g_main_loop_quit(g_loop); return G_SOURCE_REMOVE; }

#include <glib-unix.h>
static time_t ini_mtime(void)
{
    struct stat st;
    return stat(G.ini_path, &st) == 0 ? st.st_mtime : 0;
}
static time_t g_ini_seen;
static gboolean on_ini_check(gpointer d)
{
    (void)d;
    if (ini_mtime() != g_ini_seen) {
        int iv = G.interval;
        g_ini_seen = ini_mtime();
        G.loading = 1;
        {
            pm_alarm keep[MAXALARM];
            int nk = G.nalarm;
            memcpy(keep, G.alarm, sizeof keep);
            cfg_load();
            if (nk == G.nalarm) {   /* keep the alarm states this process is tracking */
                int i;
                for (i = 0; i < nk; i++) { G.alarm[i].above_since = keep[i].above_since; G.alarm[i].fired = keep[i].fired; }
            }
        }
        G.loading = 0;
        if (iv != G.interval) timer_restart();
    }
    return G_SOURCE_CONTINUE;
}

int power_recorder_main(int argc, char **argv)
{
    (void)argc; (void)argv;
    G.headless = 1;
    paths();
    rec_start_runtime();
    g_ini_seen = ini_mtime();
    g_timeout_add_seconds(5, on_ini_check, NULL);
    g_unix_signal_add(SIGTERM, on_quit_signal, NULL);
    g_unix_signal_add(SIGINT, on_quit_signal, NULL);
    fprintf(stderr, "srs power recorder: database %s, settings %s\n", G.db_path, G.ini_path);
    g_loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(g_loop);
    rec_end();
    pdb_close();
    return 0;
}

/* ====================================================================== drawing helpers */
static void fg_rgba(GtkWidget *w, GdkRGBA *c)
{
    gtk_style_context_get_color(gtk_widget_get_style_context(w), gtk_widget_get_state_flags(w), c);
}

static double nice_step(double range, int ticks)
{
    double raw = range / ticks, mag = pow(10, floor(log10(raw > 0 ? raw : 1))), f = raw / mag;
    return (f <= 1 ? 1 : f <= 2 ? 2 : f <= 5 ? 5 : 10) * mag;
}

static void draw_text(cairo_t *cr, double x, double y, const char *s, int align)   /* align: 0 left, 1 centre, 2 right */
{
    cairo_text_extents_t te;
    cairo_text_extents(cr, s, &te);
    cairo_move_to(cr, x - (align == 1 ? te.width / 2 : align == 2 ? te.width : 0), y);
    cairo_show_text(cr, s);
}

static void axes(cairo_t *cr, GdkRGBA *fg, double l, double t, double r, double b, double ymax, const char *unit)
{
    double step = nice_step(ymax, 4), v;
    char s[48];
    cairo_set_line_width(cr, 1);
    for (v = 0; v <= ymax + 1e-9; v += step) {
        double y = b - (b - t) * v / ymax;
        cairo_set_source_rgba(cr, fg->red, fg->green, fg->blue, 0.15);
        cairo_move_to(cr, l, floor(y) + 0.5);
        cairo_line_to(cr, r, floor(y) + 0.5);
        cairo_stroke(cr);
        cairo_set_source_rgba(cr, fg->red, fg->green, fg->blue, 0.75);
        if (step < 0.01) snprintf(s, sizeof s, "%.3f", v);
        else if (step < 0.1) snprintf(s, sizeof s, "%.2f", v);
        else if (step < 1) snprintf(s, sizeof s, "%.1f", v);
        else snprintf(s, sizeof s, "%.0f", v);
        draw_text(cr, l - 6, y + 4, s, 2);
    }
    cairo_save(cr);
    cairo_translate(cr, 12, (t + b) / 2);
    cairo_rotate(cr, -M_PI / 2);
    draw_text(cr, 0, 0, unit, 1);
    cairo_restore(cr);
}

/* the last 10 minutes at the wall: centre line and the range as a band */
static gboolean draw_live(GtkWidget *w, cairo_t *cr, gpointer d)
{
    int W = gtk_widget_get_allocated_width(w), H = gtk_widget_get_allocated_height(w), i, first = 1;
    double l = 56, t = 10, r = W - 12, b = H - 24, span = 600, now, ymax = 50;
    GdkRGBA fg;
    (void)d;
    fg_rgba(w, &fg);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 11);
    if (G.ring_n < 2) {
        cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.7);
        draw_text(cr, W / 2.0, H / 2.0, "Measuring... the graph starts after a few seconds.", 1);
        return FALSE;
    }
    now = G.ring_t[(G.ring_i + RING - 1) % RING];
    for (i = 0; i < G.ring_n; i++) {
        int k = (G.ring_i + RING - 1 - i) % RING;
        if (now - G.ring_t[k] > span) break;
        if (G.ring_hi[k] > ymax) ymax = G.ring_hi[k];
    }
    ymax = nice_step(ymax * 1.1, 4) * ceil(ymax * 1.1 / nice_step(ymax * 1.1, 4));
    axes(cr, &fg, l, t, r, b, ymax, "W");
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.75);
    draw_text(cr, l, H - 6, "10 min ago", 0);
    draw_text(cr, (l + r) / 2, H - 6, "5 min", 1);
    draw_text(cr, r, H - 6, "now", 2);
#define PX(tt) (r - (now - (tt)) / span * (r - l))
#define PY(v) (b - (b - t) * (v) / ymax)
    /* band: along the high values, back along the low ones */
    for (i = 0; i < G.ring_n; i++) {
        int k = (G.ring_i + RING - 1 - i) % RING;
        if (now - G.ring_t[k] > span) break;
        if (first) { cairo_move_to(cr, PX(G.ring_t[k]), PY(G.ring_hi[k])); first = 0; }
        else cairo_line_to(cr, PX(G.ring_t[k]), PY(G.ring_hi[k]));
    }
    for (i--; i >= 0; i--) {
        int k = (G.ring_i + RING - 1 - i) % RING;
        cairo_line_to(cr, PX(G.ring_t[k]), PY(G.ring_lo[k]));
    }
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, 0.21, 0.52, 0.89, 0.22);
    cairo_fill(cr);
    first = 1;
    for (i = 0; i < G.ring_n; i++) {
        int k = (G.ring_i + RING - 1 - i) % RING;
        if (now - G.ring_t[k] > span) break;
        if (first) { cairo_move_to(cr, PX(G.ring_t[k]), PY(G.ring_mid[k])); first = 0; }
        else cairo_line_to(cr, PX(G.ring_t[k]), PY(G.ring_mid[k]));
    }
    cairo_set_source_rgba(cr, 0.21, 0.52, 0.89, 1);
    cairo_set_line_width(cr, 2);
    cairo_stroke(cr);
#undef PX
#undef PY
    return FALSE;
}

/* ====================================================================== Now tab */
static GtkWidget *lbl(const char *text, const char *cls, float xalign)
{
    GtkWidget *l = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(l), xalign);
    if (cls) {
        char **v = g_strsplit(cls, " ", -1), **p;
        for (p = v; *p; p++) gtk_style_context_add_class(gtk_widget_get_style_context(l), *p);
        g_strfreev(v);
    }
    return l;
}

static GtkWidget *wrap_lbl(const char *text, const char *cls)
{
    GtkWidget *l = lbl(text, cls, 0);
    gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(l), 110);
    return l;
}

static GtkWidget *frame(const char *title, GtkWidget *child)
{
    GtkWidget *f = gtk_frame_new(NULL), *l = gtk_label_new(NULL);
    char *m = g_markup_printf_escaped("<b>%s</b>", title);
    gtk_label_set_markup(GTK_LABEL(l), m);
    g_free(m);
    gtk_frame_set_label_widget(GTK_FRAME(f), l);
    if (GTK_IS_CONTAINER(child)) gtk_container_set_border_width(GTK_CONTAINER(child), 10);
    else {
        gtk_widget_set_margin_start(child, 10);
        gtk_widget_set_margin_end(child, 10);
        gtk_widget_set_margin_top(child, 10);
        gtk_widget_set_margin_bottom(child, 10);
    }
    gtk_container_add(GTK_CONTAINER(f), child);
    return f;
}

static GtkWidget *scrolled(GtkWidget *child)
{
    GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sw), child);
    return sw;
}

static const char *price_kind(void)
{
    return G.tar.type == PT_TIERED ? "tiered" : G.tar.type == PT_TOU ? "time of use" : "flat";
}

static void money(double v, char *out, int sz) { pm_fmt_money(&G.money, v, out, sz); }

static void rec_status_text(char *s, int sz)
{
    double now = (double)time(NULL);
    if (G.other_rec && !G.locked)
        snprintf(s, (size_t)sz, "Recording is done by SRS's background recorder (or another SRS window): the history below comes from it.");
    else if (G.mode == 2)
        snprintf(s, (size_t)sz, "Recording always: whenever SRS runs (also from the tray), one row per minute.");
    else if (G.mode == 1 && now < G.until) {
        char t[32];
        time_t u = (time_t)G.until;
        struct tm tm;
        double left = (G.until - now) / 60;
        localtime_r(&u, &tm);
        strftime(t, sizeof t, "%a %H:%M", &tm);
        if (left >= 120) snprintf(s, (size_t)sz, "Recording until %s (%.0f h %.0f min left).", t, floor(left / 60), fmod(left, 60));
        else snprintf(s, (size_t)sz, "Recording until %s (%.0f min left).", t, ceil(left));
    } else
        snprintf(s, (size_t)sz, "Not recording. The live estimate above still works; nothing is saved.");
}

static void bd_add(const char *part, const char *w, const char *how, int bold)
{
    GtkTreeIter it;
    gtk_list_store_append(G.bd_store, &it);
    gtk_list_store_set(G.bd_store, &it, 0, part, 1, w, 2, how, 3, bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL, -1);
}

static void range_txt(double lo, double hi, char *out, int sz)
{
    char a[32], b[32];
    if (fabs(hi - lo) < 0.05) { pm_fmt_w(lo, out, sz); return; }
    pm_fmt_w(lo, a, sizeof a);
    pm_fmt_w(hi, b, sizeof b);
    snprintf(out, (size_t)sz, "%s - %s", a, b);
}

static void ui_now(void)
{
    char s[600], a[64], b[64], c[64];
    double price, w;
    int i;
    if (!G.page || !G.have_est) return;
    w = G.est.wall_mid;
    pm_fmt_w(w, a, sizeof a);
    snprintf(s, sizeof s, "about %s", a);
    gtk_label_set_text(GTK_LABEL(G.big), s);
    pm_fmt_w(G.est.wall_lo, a, sizeof a);
    pm_fmt_w(G.est.wall_hi, b, sizeof b);
    snprintf(s, sizeof s, "at the wall, likely between %s and %s (\xc2\xb1%.0f %%)", a, b,
             w > 0 ? (G.est.wall_hi - G.est.wall_lo) / 2 / w * 100 : 0);
    gtk_label_set_text(GTK_LABEL(G.range), s);
    snprintf(s, sizeof s, "Processor: %s  \xc2\xb7  Graphics: %s  \xc2\xb7  Everything else: estimated%s",
             G.src.cpu_how, G.src.gpu_how, G.flags & (PM_F_GAP | PM_F_EST) ? "  \xc2\xb7  (a reading was skipped)" : "");
    gtk_label_set_text(GTK_LABEL(G.badges), s);
    price = pm_price(&G.tar, time(NULL), G.month_wh / 1000);
    {
        static const double hours[4] = { 1, 24, 24 * 30.4375, 24 * 365.25 };
        static const char *const names[4] = { "an hour", "a day", "a month", "a year" };
        for (i = 0; i < 4; i++) {
            money(w / 1000 * hours[i] * price + (G.tar.fixed_on ? G.tar.fixed_day * hours[i] / 24 : 0), a, sizeof a);
            snprintf(s, sizeof s, "%s %s", a, names[i]);
            gtk_label_set_text(GTK_LABEL(G.cost[i]), s);
        }
    }
    money(price, a, sizeof a);
    snprintf(s, sizeof s, "If it kept drawing this much: price %s per kWh (%s%s%s). Change it on the Price tab.", a, price_kind(),
             G.tar.tax_pct > 0 ? ", tax included" : "", G.tar.fixed_on ? ", standing charge included" : "");
    if (G.money.dec + 2 > 2) {   /* show the per-kWh price with more digits */
        pm_money m4 = G.money;
        m4.dec = 4;
        pm_fmt_money(&m4, price, a, sizeof a);
        snprintf(s, sizeof s, "If it kept drawing this much: price %s per kWh (%s%s%s). Change it on the Price tab.", a, price_kind(),
                 G.tar.tax_pct > 0 ? ", tax included" : "", G.tar.fixed_on ? ", standing charge included" : "");
    }
    gtk_label_set_text(GTK_LABEL(G.pricelbl), s);
    rec_status_text(s, sizeof s);
    gtk_label_set_text(GTK_LABEL(G.rec_status), s);
    gtk_widget_queue_draw(G.live);
    /* breakdown */
    gtk_list_store_clear(G.bd_store);
    if (G.est.cpu_measured) { pm_fmt_w(G.meas.cpu_w, a, sizeof a); bd_add("Processor (CPU package)", a, G.src.cpu_how, 0); }
    if (G.est.gpu_measured) { pm_fmt_w(G.meas.gpu_w, a, sizeof a); bd_add("Graphics (as the driver reports it)", a, G.src.gpu_how, 0); }
    for (i = 0; i < G.nparts; i++) {
        double lo, hi;
        pm_part_watts(&G.parts[i], &G.meas, &lo, &hi);
        if (!G.parts[i].on || (lo == 0 && hi == 0)) continue;
        range_txt(lo, hi, a, sizeof a);
        if (G.parts[i].count > 1 && (G.parts[i].kind == PK_FIXED || G.parts[i].kind == PK_OUTSIDE))
            snprintf(s, sizeof s, "%s (x%d)", G.parts[i].name, G.parts[i].count);
        else
            snprintf(s, sizeof s, "%s", G.parts[i].name);
        bd_add(s, a, G.parts[i].how, 0);
    }
    range_txt(G.est.dc_lo / G.est.eff_hi - G.est.dc_lo, G.est.dc_hi / G.est.eff_lo - G.est.dc_hi, a, sizeof a);
    snprintf(s, sizeof s, "efficiency %.0f-%.0f %% at %.0f %% of %.0f W (%s)", G.est.eff_lo * 100, G.est.eff_hi * 100,
             G.psu.rating > 0 ? G.est.dc_mid / G.psu.rating * 100 : 0, G.psu.rating, G.psu.name);
    bd_add("Power supply losses", a, s, 0);
    range_txt(G.est.wall_lo, G.est.wall_hi, a, sizeof a);
    pm_fmt_w(G.est.wall_mid, b, sizeof b);
    pm_fmt_w(G.est.dc_mid, c, sizeof c);
    snprintf(s, sizeof s, "about %s; inside the PC about %s", b, c);
    bd_add("At the wall", a, s, 1);
}

static void ui_slow(void)
{
    char s[700], a[48], b[48], c[48], d[48];
    double now = (double)time(NULL), cd = 0, cm = 0, wd, wm;
    if (!G.page) return;
    if (db_ensure(0)) {
        wd = pdb_sum_range((long)day_start(now), (long)now + 60, &cd);
        wm = pdb_sum_range((long)month_start(now), (long)now + 60, &cm);
        pm_fmt_wh(wd, a, sizeof a);
        money(cd, b, sizeof b);
        pm_fmt_wh(wm, c, sizeof c);
        money(cm, d, sizeof d);
        snprintf(s, sizeof s, "Recorded today: %s (about %s)   \xc2\xb7   This month: %s (about %s)", a, b, c, d);
    } else
        snprintf(s, sizeof s, "Nothing recorded yet.");
    gtk_label_set_text(GTK_LABEL(G.today), s);
    if (G.a_last) gtk_label_set_text(GTK_LABEL(G.a_last), G.last_alarm[0] ? G.last_alarm : "No alarm so far in this session.");
    sess_now_text();
    if (G.s_psunow && G.have_est) {
        snprintf(s, sizeof s, "Right now: about %.0f W inside the PC = %.0f %% of %.0f W, efficiency %.0f %% (range %.0f-%.0f %%).",
                 G.est.dc_mid, G.psu.rating > 0 ? G.est.dc_mid / G.psu.rating * 100 : 0, G.psu.rating, G.est.eff_mid * 100,
                 G.est.eff_lo * 100, G.est.eff_hi * 100);
        gtk_label_set_text(GTK_LABEL(G.s_psunow), s);
    }
    if (G.src_raw) {
        GString *g = g_string_new(NULL);
        int i;
        for (i = 0; i < G.src.ncpu + G.src.ngpu; i++) {
            psrc_chan *ch = i < G.src.ncpu ? &G.src.cpu[i] : &G.src.gpu[i - G.src.ncpu];
            g_string_append_printf(g, "%s\t%s\t%s\n", ch->label, ch->path, isnan(ch->w) ? "-" : (snprintf(a, sizeof a, "%.2f W", ch->w), a));
        }
        if (!g->len) g_string_append(g, "No power or energy source is readable on this PC.");
        gtk_label_set_text(GTK_LABEL(G.src_raw), g->str);
        g_string_free(g, TRUE);
    }
    if (G.c_proj && G.have_est) {
        double price = pm_price(&G.tar, time(NULL), G.month_wh / 1000), avgw = NAN, wmonth = 0, cmonth = 0, frac, days;
        if (db_ensure(0)) {
            pdb_sum r[1];
            long ms = (long)month_start(now);
            wmonth = pdb_sum_range(ms, (long)now + 60, &cmonth);
            if (pdb_group(PDB_YEAR, ms, (long)now + 60, r, 1) == 1 && r[0].secs_on > 600) avgw = r[0].wh_mid / (r[0].secs_on / 3600);
        }
        days = (now - month_start(now)) / 86400;
        frac = days / 30.4375;
        pm_fmt_w(G.est.wall_mid, a, sizeof a);
        money(G.est.wall_mid / 1000 * 24 * 365.25 * price, b, sizeof b);
        {
            GString *g = g_string_new(NULL);
            g_string_append_printf(g, "Left on 24/7 at today's draw (%s): about %s a year", a, b);
            if (!isnan(avgw)) {
                pm_fmt_w(avgw, c, sizeof c);
                money(avgw / 1000 * 24 * 365.25 * price, d, sizeof d);
                g_string_append_printf(g, "; at this month's recorded average (%s): about %s a year", c, d);
            }
            g_string_append(g, ".\n");
            if (wmonth > 0 && frac > 0.03) {
                money(cmonth / frac, c, sizeof c);
                pm_fmt_wh(wmonth / frac, d, sizeof d);
                g_string_append_printf(g, "This month at the rate recorded so far: about %s, %s (recorded hours only).\n", d, c);
            }
            if (G.co2_g > 0) {
                g_string_append_printf(g, "CO\xe2\x82\x82: about %.0f g an hour at today's draw, %.0f kg a year if left on 24/7 (%.0f g/kWh, %s).",
                                       G.est.wall_mid / 1000 * G.co2_g, G.est.wall_mid / 1000 * 24 * 365.25 * G.co2_g / 1000, G.co2_g,
                                       G.co2_src[0] ? G.co2_src : "your figure");
                if (wmonth > 0) g_string_append_printf(g, " Recorded this month: about %.1f kg.", wmonth / 1000 * G.co2_g / 1000);
            } else
                g_string_append(g, "CO\xe2\x82\x82: off (pick a grid average below).");
            gtk_label_set_text(GTK_LABEL(G.c_proj), g->str);
            g_string_free(g, TRUE);
        }
    }
}

static void on_rec_combo(GtkComboBox *cb, gpointer d)
{
    int m = gtk_combo_box_get_active(cb);
    (void)d;
    if (G.loading) return;
    gtk_widget_set_sensitive(G.rec_n, m == 1);
    gtk_widget_set_sensitive(G.rec_unit, m == 1);
    gtk_widget_set_sensitive(G.rec_start, m == 1);
    if (m == 1) return;   /* "For a set time" starts with the Start button */
    G.mode = m;
    cfg_save();
    rec_tick();
    ui_now();
}

static void on_rec_start(GtkButton *b, gpointer d)
{
    static const double unit[3] = { 60, 3600, 86400 };
    (void)b; (void)d;
    G.until_n = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(G.rec_n));
    G.until_unit = gtk_combo_box_get_active(GTK_COMBO_BOX(G.rec_unit));
    if (G.until_unit < 0 || G.until_unit > 2) G.until_unit = 1;
    G.mode = 1;
    G.until = (double)time(NULL) + G.until_n * unit[G.until_unit];
    cfg_save();
    rec_tick();
    ui_now();
}

static GtkWidget *build_now(void)
{
    GtkWidget *vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10), *hb, *g, *tv, *sw;
    GtkCellRenderer *rr;
    int i;
    gtk_container_set_border_width(GTK_CONTAINER(vb), 14);
    hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
    {
        GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        G.big = lbl("about - W", "pw-big", 0);
        G.range = lbl("at the wall", "pw-sub", 0);
        gtk_box_pack_start(GTK_BOX(v), G.big, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(v), G.range, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(hb), v, FALSE, FALSE, 0);
    }
    g = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(g), 28);
    gtk_grid_set_row_spacing(GTK_GRID(g), 2);
    for (i = 0; i < 4; i++) {
        G.cost[i] = lbl("-", "pw-cost", 0);
        gtk_grid_attach(GTK_GRID(g), G.cost[i], i % 2, i / 2, 1, 1);
    }
    gtk_widget_set_valign(g, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(hb), g, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vb), hb, FALSE, FALSE, 0);
    G.badges = wrap_lbl("", "dim-label");
    gtk_box_pack_start(GTK_BOX(vb), G.badges, FALSE, FALSE, 0);
    G.pricelbl = wrap_lbl("", "dim-label");
    gtk_box_pack_start(GTK_BOX(vb), G.pricelbl, FALSE, FALSE, 0);

    /* recording */
    hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(hb), lbl("Recording:", NULL, 0), FALSE, FALSE, 0);
    G.rec_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(G.rec_combo), "Off");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(G.rec_combo), "For a set time");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(G.rec_combo), "Always (forever)");
    gtk_widget_set_tooltip_text(G.rec_combo, "Off: nothing is saved. For a set time: record, then stop by itself. "
                                             "Always: record whenever SRS (or its background recorder, Setup tab) runs.");
    gtk_box_pack_start(GTK_BOX(hb), G.rec_combo, FALSE, FALSE, 0);
    G.rec_n = gtk_spin_button_new_with_range(1, 999, 1);
    gtk_box_pack_start(GTK_BOX(hb), G.rec_n, FALSE, FALSE, 0);
    G.rec_unit = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(G.rec_unit), "minutes");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(G.rec_unit), "hours");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(G.rec_unit), "days");
    gtk_box_pack_start(GTK_BOX(hb), G.rec_unit, FALSE, FALSE, 0);
    G.rec_start = gtk_button_new_with_mnemonic("_Start");
    gtk_widget_set_tooltip_text(G.rec_start, "Start recording for this long");
    g_signal_connect(G.rec_start, "clicked", G_CALLBACK(on_rec_start), NULL);
    gtk_box_pack_start(GTK_BOX(hb), G.rec_start, FALSE, FALSE, 0);
    g_signal_connect(G.rec_combo, "changed", G_CALLBACK(on_rec_combo), NULL);
    gtk_box_pack_start(GTK_BOX(vb), hb, FALSE, FALSE, 0);
    G.rec_status = wrap_lbl("", NULL);
    gtk_box_pack_start(GTK_BOX(vb), G.rec_status, FALSE, FALSE, 0);
    G.today = wrap_lbl("", "pw-today");
    gtk_box_pack_start(GTK_BOX(vb), G.today, FALSE, FALSE, 0);

    G.live = gtk_drawing_area_new();
    gtk_widget_set_size_request(G.live, -1, 170);
    g_signal_connect(G.live, "draw", G_CALLBACK(draw_live), NULL);
    gtk_box_pack_start(GTK_BOX(vb), frame("The last 10 minutes at the wall (line: estimate, band: likely range)", G.live), FALSE, FALSE, 0);

    G.bd_store = gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);
    tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(G.bd_store));
    G.bd_view = tv;
    rr = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(tv), gtk_tree_view_column_new_with_attributes("Part", rr, "text", 0, "weight", 3, NULL));
    rr = gtk_cell_renderer_text_new();
    g_object_set(rr, "xalign", 1.0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tv), gtk_tree_view_column_new_with_attributes("Watts", rr, "text", 1, "weight", 3, NULL));
    rr = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(tv), gtk_tree_view_column_new_with_attributes("How SRS knows", rr, "text", 2, NULL));
    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(sw), tv);
    gtk_widget_set_size_request(sw, -1, 230);
    gtk_box_pack_start(GTK_BOX(vb), frame("Where the watts go", sw), TRUE, TRUE, 0);
    return scrolled(vb);
}

/* ====================================================================== History tab */
enum { HR_24H, HR_7D, HR_30D, HR_12W, HR_12M, HR_ALL, HR_N };
static const char *const g_hr_names[HR_N] = { "Last 24 hours, by hour", "Last 7 days, by day", "Last 30 days, by day",
                                               "Last 12 weeks, by week", "Last 12 months, by month", "Everything, by month" };

static void hist_span(int r, long *from, long *to, int *group)
{
    double now = (double)time(NULL);
    time_t t = (time_t)now;
    struct tm tm;
    localtime_r(&t, &tm);
    *to = (long)now + 120;
    tm.tm_min = 0; tm.tm_sec = 0; tm.tm_isdst = -1;
    switch (r) {
    case HR_24H: tm.tm_hour -= 23; *group = PDB_HOUR; break;
    case HR_7D: tm.tm_hour = 0; tm.tm_mday -= 6; *group = PDB_DAY; break;
    case HR_30D: tm.tm_hour = 0; tm.tm_mday -= 29; *group = PDB_DAY; break;
    case HR_12W: tm.tm_hour = 0; tm.tm_mday -= ((tm.tm_wday + 6) % 7) + 7 * 11; *group = PDB_WEEK; break;
    case HR_12M: tm.tm_hour = 0; tm.tm_mday = 1; tm.tm_mon -= 11; *group = PDB_MONTH; break;
    default:
        *from = db_ensure(0) ? pdb_first_minute() : 0;
        *group = PDB_MONTH;
        return;
    }
    *from = (long)mktime(&tm);
}

/* every period in the range, so empty hours / days show as gaps on the graph */
static int hist_slots(int group, long from, long to, pdb_sum *out, int max)
{
    static const char *const fmt[] = { "%Y-%m-%d %H:%M", "%Y-%m-%d %H:00", "%Y-%m-%d", "%Y week %W", "%Y-%m", "%Y" };
    time_t t = from;
    struct tm tm;
    int n = 0, guard = 0;
    char lab[40];
    if (group == PDB_MINUTE) return 0;
    while (t < to && n < max && guard++ < 5000) {
        localtime_r(&t, &tm);
        strftime(lab, sizeof lab, fmt[group], &tm);
        if (!n || strcmp(out[n - 1].label, lab)) {
            memset(&out[n], 0, sizeof out[n]);
            out[n].t = (long)t;
            snprintf(out[n].label, sizeof out[n].label, "%s", lab);
            n++;
        }
        tm.tm_isdst = -1;
        if (group == PDB_HOUR) tm.tm_hour++;
        else if (group == PDB_DAY) tm.tm_mday++;
        else if (group == PDB_WEEK) tm.tm_mday += 7;
        else if (group == PDB_MONTH) tm.tm_mon++;
        else tm.tm_year++;
        if (group != PDB_HOUR) tm.tm_hour = 12;   /* stay clear of DST edges */
        t = mktime(&tm);
    }
    return n;
}

static void hist_reload(void)
{
    long from, to;
    int r = gtk_combo_box_get_active(GTK_COMBO_BOX(G.h_range)), i, j, n;
    static pdb_sum got[HROWS];
    double wh = 0, lo = 0, hi = 0, cost = 0, on = 0, sl = 0, bs = 0, bwh = 0;
    char s[900], a[48], b[48], c[48], d[48];
    GtkTreeIter it;
    if (r < 0) r = HR_7D;
    G.h_cost = gtk_combo_box_get_active(GTK_COMBO_BOX(G.h_what)) == 1;
    hist_span(r, &from, &to, &G.h_group);
    gtk_list_store_clear(G.h_store);
    G.h_n = 0;
    if (!db_ensure(0) || (r == HR_ALL && from == 0)) {
        gtk_label_set_text(GTK_LABEL(G.h_sum), "No history yet. Choose \"For a set time\" or \"Always\" next to Recording on the Now tab.");
        gtk_widget_queue_draw(G.h_area);
        return;
    }
    pdb_flush();
    n = pdb_group(G.h_group, from, to, got, HROWS);
    G.h_n = hist_slots(G.h_group, from, to, G.h_rows, HROWS);
    for (i = 0; i < n; i++) {
        for (j = 0; j < G.h_n; j++) if (!strcmp(G.h_rows[j].label, got[i].label)) { G.h_rows[j] = got[i]; break; }
        if (j == G.h_n && G.h_n < HROWS) G.h_rows[G.h_n++] = got[i];
    }
    for (i = 0; i < G.h_n; i++) {
        pdb_sum *x = &G.h_rows[i];
        wh += x->wh_mid; lo += x->wh_lo; hi += x->wh_hi; cost += x->cost; on += x->secs_on; sl += x->secs_sleep;
        bs += x->secs_busy; bwh += x->wh_busy;
        if (x->secs_on + x->secs_sleep + x->secs_off <= 0) continue;
        pm_fmt_wh(x->wh_mid, a, sizeof a);
        {
            char l1[32], h1[32];
            pm_fmt_wh(x->wh_lo, l1, sizeof l1);
            pm_fmt_wh(x->wh_hi, h1, sizeof h1);
            snprintf(b, sizeof b, "%s - %s", l1, h1);
        }
        money(x->cost, c, sizeof c);
        snprintf(d, sizeof d, "%.1f h", x->secs_on / 3600);
        {
            char mx[32], sleeps[32];
            pm_fmt_w(x->w_max, mx, sizeof mx);
            snprintf(sleeps, sizeof sleeps, "%.1f h", x->secs_sleep / 3600);
            gtk_list_store_append(G.h_store, &it);
            gtk_list_store_set(G.h_store, &it, 0, x->label, 1, a, 2, b, 3, c, 4, d, 5, sleeps, 6, mx, -1);
        }
    }
    pm_fmt_wh(wh, a, sizeof a);
    pm_fmt_wh(lo, b, sizeof b);
    pm_fmt_wh(hi, c, sizeof c);
    money(cost, d, sizeof d);
    snprintf(s, sizeof s, "Total: %s (likely %s - %s), about %s  \xc2\xb7  %.1f h recorded awake, %.1f h asleep  \xc2\xb7  %d gap%s not recorded",
             a, b, c, d, on / 3600, sl / 3600, pdb_count_gaps(from, to), pdb_count_gaps(from, to) == 1 ? "" : "s");
    if (G.tar.fixed_on && G.tar.fixed_day > 0) {
        char f[48];
        money(G.tar.fixed_day, f, sizeof f);
        snprintf(s + strlen(s), sizeof s - strlen(s), "  \xc2\xb7  standing charge %s a day not in these totals", f);
    }
    if (G.co2_g > 0) snprintf(s + strlen(s), sizeof s - strlen(s), "  \xc2\xb7  about %.1f kg CO\xe2\x82\x82", wh / 1000 * G.co2_g / 1000);
    if (wh > 0 && on > 0) {
        char e1[32], e2[32];
        pm_fmt_wh(bwh, e1, sizeof e1);
        pm_fmt_wh(wh - bwh, e2, sizeof e2);
        snprintf(s + strlen(s), sizeof s - strlen(s), "\nBusy (CPU or graphics at %.0f %% or more): %s in %.1f h (%.0f %% of the energy)  \xc2\xb7  "
                 "idle, light use and sleep: %s in %.1f h", G.busy_thr, e1, bs / 3600, bwh / wh * 100, e2, (on + sl - bs) / 3600);
    }
    gtk_label_set_text(GTK_LABEL(G.h_sum), s);
    gtk_widget_queue_draw(G.h_area);
}

static void short_label(const pdb_sum *r, int group, char *out, int sz)
{
    const char *l = r->label;
    if (group == PDB_HOUR && strlen(l) >= 13) snprintf(out, (size_t)sz, "%.2s", l + 11);
    else if (group == PDB_DAY && strlen(l) >= 10) snprintf(out, (size_t)sz, "%.5s", l + 5);
    else if (group == PDB_WEEK && strstr(l, "week ")) snprintf(out, (size_t)sz, "W%s", strstr(l, "week ") + 5);
    else snprintf(out, (size_t)sz, "%s", l);
}

static gboolean draw_hist(GtkWidget *w, cairo_t *cr, gpointer d)
{
    int W = gtk_widget_get_allocated_width(w), H = gtk_widget_get_allocated_height(w), i, every;
    double l = 64, t = 12, r = W - 12, b = H - 26, ymax = 0, bw;
    GdkRGBA fg;
    (void)d;
    fg_rgba(w, &fg);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 11);
    if (!G.h_n) {
        cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.7);
        draw_text(cr, W / 2.0, H / 2.0, "No recorded data in this range.", 1);
        return FALSE;
    }
    for (i = 0; i < G.h_n; i++) {
        double v = G.h_cost ? G.h_rows[i].cost : G.h_rows[i].wh_hi / 1000;
        if (v > ymax) ymax = v;
    }
    if (ymax <= 0) ymax = G.h_cost ? 0.01 : 0.01;
    ymax = nice_step(ymax * 1.1, 4) * ceil(ymax * 1.1 / nice_step(ymax * 1.1, 4));
    {
        char unit[40];
        if (G.h_cost) snprintf(unit, sizeof unit, "%s", G.money.code[0] ? G.money.code : "cost");
        else snprintf(unit, sizeof unit, "kWh");
        axes(cr, &fg, l, t, r, b, ymax, unit);
    }
    bw = (r - l) / G.h_n;
    every = (int)ceil(G.h_n / ((r - l) / 46.0));
    if (every < 1) every = 1;
    for (i = 0; i < G.h_n; i++) {
        pdb_sum *x = &G.h_rows[i];
        double v = G.h_cost ? x->cost : x->wh_mid / 1000, x0 = l + i * bw + bw * 0.15, x1 = l + (i + 1) * bw - bw * 0.15;
        double y = b - (b - t) * v / ymax;
        char lab[40];
        if (v > 0) {
            cairo_set_source_rgba(cr, 0.21, 0.52, 0.89, 0.85);
            cairo_rectangle(cr, x0, y, x1 - x0, b - y);
            cairo_fill(cr);
            if (!G.h_cost && x->wh_hi > x->wh_lo) {   /* the likely range */
                double ylo = b - (b - t) * x->wh_lo / 1000 / ymax, yhi = b - (b - t) * x->wh_hi / 1000 / ymax, xm = (x0 + x1) / 2;
                cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.8);
                cairo_set_line_width(cr, 1.2);
                cairo_move_to(cr, xm, ylo); cairo_line_to(cr, xm, yhi);
                cairo_move_to(cr, xm - 3, ylo); cairo_line_to(cr, xm + 3, ylo);
                cairo_move_to(cr, xm - 3, yhi); cairo_line_to(cr, xm + 3, yhi);
                cairo_stroke(cr);
            }
        }
        if (i % every == 0) {
            short_label(x, G.h_group, lab, sizeof lab);
            cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.75);
            draw_text(cr, (x0 + x1) / 2, H - 8, lab, 1);
        }
    }
    return FALSE;
}

static gboolean hist_tip(GtkWidget *w, gint x, gint y, gboolean kb, GtkTooltip *tip, gpointer d)
{
    int W = gtk_widget_get_allocated_width(w), i;
    double l = 64, r = W - 12;
    char s[400], a[48], b[48], c[48], m[48];
    (void)y; (void)kb; (void)d;
    if (!G.h_n || x < l || x > r) return FALSE;
    i = (int)((x - l) / ((r - l) / G.h_n));
    if (i < 0 || i >= G.h_n) return FALSE;
    pm_fmt_wh(G.h_rows[i].wh_mid, a, sizeof a);
    pm_fmt_wh(G.h_rows[i].wh_lo, b, sizeof b);
    pm_fmt_wh(G.h_rows[i].wh_hi, c, sizeof c);
    money(G.h_rows[i].cost, m, sizeof m);
    snprintf(s, sizeof s, "%s\n%s (likely %s - %s)\nabout %s\n%.1f h recorded", G.h_rows[i].label, a, b, c, m, G.h_rows[i].secs_on / 3600);
    gtk_tooltip_set_text(tip, s);
    return TRUE;
}

static void on_hist_changed(GtkWidget *w, gpointer d) { (void)w; (void)d; hist_reload(); }

static char *export_note(void)
{
    GString *g = g_string_new(NULL);
    char t[64], curve[200];
    time_t now = time(NULL);
    struct tm tm;
    int i;
    localtime_r(&now, &tm);
    strftime(t, sizeof t, "%Y-%m-%d %H:%M %Z", &tm);
    g_string_append_printf(g, "SRS Power & cost history, exported %s\n", t);
    g_string_append_printf(g, "%s Do not use it for billing, safety decisions or as proof of energy use.\n", g_disclaimer_short);
    g_string_append(g, "kwh_estimate is SRS's centre estimate at the wall; kwh_low / kwh_high are the likely range. CPU and GPU are the "
                       "measured parts (inside the PC); everything else is estimated from the parts list below.\n");
    pm_psu_curve_text(&G.psu, curve, sizeof curve);
    g_string_append_printf(g, "Power supply: %s, %.0f W, curve (load %%:efficiency %%) %s\n", G.psu.name, G.psu.rating, curve);
    for (i = 0; i < G.nparts; i++)
        if (G.parts[i].on)
            g_string_append_printf(g, "Part: %s, x%d, %g-%g (%s)\n", G.parts[i].name, G.parts[i].count, G.parts[i].lo, G.parts[i].hi,
                                   pm_kind_name(G.parts[i].kind));
    g_string_append_printf(g, "Cost: priced when recorded (%s price now, %s). Price source: %s\n", price_kind(), G.money.code, G.price_src);
    return g_string_free(g, FALSE);
}

static void on_export(GtkButton *bt, gpointer d)
{
    GtkWidget *dlg, *extra, *res, *dec, *all;
    GtkWindow *top = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(bt)));
    (void)d;
    if (!db_ensure(0)) return;
    dlg = gtk_file_chooser_dialog_new("Export power history as CSV", top, GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancel",
                                      GTK_RESPONSE_CANCEL, "_Export", GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dlg), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dlg), "srs-power-history.csv");
    extra = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(extra), lbl("One row per:", NULL, 0), FALSE, FALSE, 0);
    res = gtk_combo_box_text_new();
    {
        static const char *const r[] = { "minute", "hour", "day", "week", "month", "year" };
        int i;
        for (i = 0; i < 6; i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(res), r[i]);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(res), G.h_group);
    gtk_box_pack_start(GTK_BOX(extra), res, FALSE, FALSE, 0);
    all = gtk_check_button_new_with_mnemonic("_Everything (not just the range shown)");
    gtk_box_pack_start(GTK_BOX(extra), all, FALSE, FALSE, 0);
    dec = gtk_check_button_new_with_mnemonic("Decimal _comma (1,5; columns split by ;)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dec), G.dec_comma);
    gtk_box_pack_start(GTK_BOX(extra), dec, FALSE, FALSE, 0);
    gtk_widget_show_all(extra);
    gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER(dlg), extra);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char *fn = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg)), *note = export_note(), msg[600];
        long from, to;
        int group, n;
        hist_span(gtk_combo_box_get_active(GTK_COMBO_BOX(G.h_range)), &from, &to, &group);
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(all))) { from = 0; to = (long)time(NULL) + 120; }
        G.dec_comma = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dec));
        cfg_save();
        n = pdb_export_csv(fn, gtk_combo_box_get_active(GTK_COMBO_BOX(res)), from, to, note, G.money.code, G.dec_comma ? "," : ".");
        if (n < 0) snprintf(msg, sizeof msg, "Export failed: %s", pdb_error());
        else snprintf(msg, sizeof msg, "Exported %d rows to %s", n, fn);
        gtk_label_set_text(GTK_LABEL(G.h_sum), msg);
        g_free(fn);
        g_free(note);
    }
    gtk_widget_destroy(dlg);
}

static int confirm(GtkWidget *from, const char *title, const char *text, const char *yes)
{
    GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(gtk_widget_get_toplevel(from)), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
                                            GTK_BUTTONS_NONE, "%s", title);
    int r;
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dlg), "%s", text);
    gtk_dialog_add_buttons(GTK_DIALOG(dlg), "_Cancel", GTK_RESPONSE_CANCEL, yes, GTK_RESPONSE_ACCEPT, NULL);
    r = gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
    return r == GTK_RESPONSE_ACCEPT;
}

static void on_reprice(GtkButton *b, gpointer d)
{
    long n;
    char s[200];
    (void)d;
    if (!db_ensure(0)) return;
    if (!confirm(GTK_WIDGET(b), "Price the whole history again?",
                 "Every recorded minute gets the cost from the current price settings (Price tab), as if they had always "
                 "applied. Use this after correcting a price. It cannot be undone.", "_Re-price"))
        return;
    n = pdb_reprice(&G.tar, 0);
    G.month_key = -1;
    if (n < 0) snprintf(s, sizeof s, "Re-pricing failed: %s", pdb_error());
    else snprintf(s, sizeof s, "Re-priced %ld minutes with the current price settings.", n);
    hist_reload();
    gtk_label_set_text(GTK_LABEL(G.h_sum), s);
}

static GtkWidget *build_history(void)
{
    GtkWidget *vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10), *hb, *b, *tv, *sw;
    int i;
    static const char *const cols[] = { "Period", "Energy", "Likely range", "Cost", "Recorded", "Asleep", "Highest" };
    gtk_container_set_border_width(GTK_CONTAINER(vb), 14);
    hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    G.h_range = gtk_combo_box_text_new();
    for (i = 0; i < HR_N; i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(G.h_range), g_hr_names[i]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(G.h_range), HR_7D);
    gtk_box_pack_start(GTK_BOX(hb), G.h_range, FALSE, FALSE, 0);
    G.h_what = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(G.h_what), "Energy (kWh)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(G.h_what), "Cost");
    gtk_combo_box_set_active(GTK_COMBO_BOX(G.h_what), 0);
    gtk_box_pack_start(GTK_BOX(hb), G.h_what, FALSE, FALSE, 0);
    b = gtk_button_new_from_icon_name("view-refresh-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(b, "Read the history again");
    g_signal_connect(b, "clicked", G_CALLBACK(on_hist_changed), NULL);
    gtk_box_pack_start(GTK_BOX(hb), b, FALSE, FALSE, 0);
    b = gtk_button_new_with_mnemonic("_Re-price...");
    gtk_widget_set_tooltip_text(b, "Cost every recorded minute again with the current price settings");
    g_signal_connect(b, "clicked", G_CALLBACK(on_reprice), NULL);
    gtk_box_pack_end(GTK_BOX(hb), b, FALSE, FALSE, 0);
    b = gtk_button_new_with_mnemonic("_Export CSV...");
    gtk_widget_set_tooltip_text(b, "Save the history as a CSV file for a spreadsheet (by minute, hour, day, week or month)");
    g_signal_connect(b, "clicked", G_CALLBACK(on_export), NULL);
    gtk_box_pack_end(GTK_BOX(hb), b, FALSE, FALSE, 0);
    g_signal_connect(G.h_range, "changed", G_CALLBACK(on_hist_changed), NULL);
    g_signal_connect(G.h_what, "changed", G_CALLBACK(on_hist_changed), NULL);
    gtk_box_pack_start(GTK_BOX(vb), hb, FALSE, FALSE, 0);
    G.h_sum = wrap_lbl("", NULL);
    gtk_box_pack_start(GTK_BOX(vb), G.h_sum, FALSE, FALSE, 0);
    G.h_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(G.h_area, -1, 240);
    gtk_widget_set_has_tooltip(G.h_area, TRUE);
    g_signal_connect(G.h_area, "draw", G_CALLBACK(draw_hist), NULL);
    g_signal_connect(G.h_area, "query-tooltip", G_CALLBACK(hist_tip), NULL);
    gtk_box_pack_start(GTK_BOX(vb), frame("Energy at the wall (bar: estimate, whisker: likely range)", G.h_area), FALSE, FALSE, 0);
    G.h_store = gtk_list_store_new(7, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(G.h_store));
    G.h_view = tv;
    for (i = 0; i < 7; i++) {
        GtkCellRenderer *rr = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes(cols[i], rr, "text", i, NULL);
        if (i) g_object_set(rr, "xalign", 1.0, NULL);
        if (!i) gtk_tree_view_column_set_expand(c, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tv), c);
    }
    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(sw), tv);
    gtk_widget_set_size_request(sw, -1, 200);
    gtk_box_pack_start(GTK_BOX(vb), sw, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vb), wrap_lbl("Costs are priced at the rate in force when each minute was recorded. Hours when SRS "
                                             "was not recording are gaps, not zero use. While the PC sleeps or is off, SRS counts "
                                             "the watts set on the Setup tab.", "dim-label"), FALSE, FALSE, 0);
    return vb;
}

/* ====================================================================== Sessions tab */
static void sess_reload(void)
{
    static pdb_sess ss[500];
    int r = gtk_combo_box_get_active(GTK_COMBO_BOX(G.x_range)), n, i;
    long now = (long)time(NULL), from = r == 0 ? now - 7 * 86400L : r == 1 ? now - 30 * 86400L : 0;
    double wh = 0, cost = 0, secs = 0;
    char s[600];
    gtk_list_store_clear(G.x_store);
    if (!db_ensure(0)) { gtk_label_set_text(GTK_LABEL(G.x_sum), "No history yet: sessions are found while Recording is on."); return; }
    pdb_flush();
    n = pdb_sessions(from, now + 60, ss, 500);
    for (i = 0; i < n; i++) {
        GtkTreeIter it;
        char a[48], b[32], c[48], d[48], e[32], f[48];
        time_t t0 = ss[i].t0;
        struct tm tm;
        double len = (double)(ss[i].t1 - ss[i].t0);
        localtime_r(&t0, &tm);
        strftime(a, sizeof a, "%a %Y-%m-%d %H:%M", &tm);
        if (len >= 3600) snprintf(b, sizeof b, "%.0f h %02.0f min", floor(len / 3600), fmod(len / 60, 60));
        else snprintf(b, sizeof b, "%.0f min", len / 60);
        pm_fmt_wh(ss[i].wh, c, sizeof c);
        money(ss[i].cost, d, sizeof d);
        pm_fmt_w(ss[i].peak, e, sizeof e);
        if (G.co2_g > 0) snprintf(f, sizeof f, "%.0f g", ss[i].wh / 1000 * G.co2_g); else snprintf(f, sizeof f, "-");
        gtk_list_store_append(G.x_store, &it);
        gtk_list_store_set(G.x_store, &it, 0, a, 1, b, 2, c, 3, d, 4, e, 5, f, -1);
        wh += ss[i].wh; cost += ss[i].cost; secs += len;
    }
    {
        char a[48], b[48];
        pm_fmt_wh(wh, a, sizeof a);
        money(cost, b, sizeof b);
        if (n) snprintf(s, sizeof s, "%d session%s, %.1f h, %s, about %s (the whole PC while busy: SRS cannot split power by program).",
                        n, n == 1 ? "" : "s", secs / 3600, a, b);
        else snprintf(s, sizeof s, "No busy sessions in this range.");
    }
    gtk_label_set_text(GTK_LABEL(G.x_sum), s);
}

static void sess_now_text(void)
{
    char s[300], g[16], c[16], e[32];
    if (!G.x_now) return;
    if (isnan(G.load_gpu)) snprintf(g, sizeof g, "-"); else snprintf(g, sizeof g, "%.0f %%", G.load_gpu);
    if (isnan(G.load_cpu)) snprintf(c, sizeof c, "-"); else snprintf(c, sizeof c, "%.0f %%", G.load_cpu);
    if (G.sess.active) {
        pm_fmt_wh(G.sess.wh, e, sizeof e);
        snprintf(s, sizeof s, "Load now: CPU %s, graphics %s  \xc2\xb7  a session is running (%.0f min, %s so far).", c, g,
                 (G.last.wall - G.sess.t0) / 60, e);
    } else
        snprintf(s, sizeof s, "Load now: CPU %s, graphics %s  \xc2\xb7  no session running.", c, g);
    gtk_label_set_text(GTK_LABEL(G.x_now), s);
}

static void on_sess_set(GtkWidget *w, gpointer d)
{
    (void)w; (void)d;
    if (G.loading) return;
    G.busy_thr = gtk_spin_button_get_value(GTK_SPIN_BUTTON(G.x_thr));
    G.sess_start_min = gtk_spin_button_get_value(GTK_SPIN_BUTTON(G.x_start));
    G.sess_end_min = gtk_spin_button_get_value(GTK_SPIN_BUTTON(G.x_end));
    cfg_save();
}

static void on_sess_range(GtkWidget *w, gpointer d) { (void)w; (void)d; sess_reload(); }

static GtkWidget *build_sessions(void)
{
    GtkWidget *vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10), *hb, *tv, *sw;
    static const char *const cols[] = { "Started", "Length", "Energy", "Cost", "Highest", "CO\xe2\x82\x82" };
    int i;
    gtk_container_set_border_width(GTK_CONTAINER(vb), 14);
    gtk_box_pack_start(GTK_BOX(vb), wrap_lbl("A session is a stretch of heavy use, such as a game or a long render: the processor or the "
                                             "graphics card busy at or above the level below for a few minutes. Its energy and cost are the "
                                             "whole PC's while it lasted. Found while Recording is on.", "dim-label"), FALSE, FALSE, 0);
    hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(hb), lbl("Busy means at least", NULL, 0), FALSE, FALSE, 0);
    G.x_thr = gtk_spin_button_new_with_range(5, 100, 5);
    gtk_box_pack_start(GTK_BOX(hb), G.x_thr, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hb), lbl("% CPU or graphics load.", NULL, 0), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vb), hb, FALSE, FALSE, 0);
    hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(hb), lbl("A session starts after", NULL, 0), FALSE, FALSE, 0);
    G.x_start = gtk_spin_button_new_with_range(1, 60, 1);
    gtk_box_pack_start(GTK_BOX(hb), G.x_start, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hb), lbl("min busy and ends after", NULL, 0), FALSE, FALSE, 0);
    G.x_end = gtk_spin_button_new_with_range(1, 60, 1);
    gtk_box_pack_start(GTK_BOX(hb), G.x_end, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hb), lbl("min below it.", NULL, 0), FALSE, FALSE, 0);
    g_signal_connect(G.x_thr, "value-changed", G_CALLBACK(on_sess_set), NULL);
    g_signal_connect(G.x_start, "value-changed", G_CALLBACK(on_sess_set), NULL);
    g_signal_connect(G.x_end, "value-changed", G_CALLBACK(on_sess_set), NULL);
    gtk_box_pack_start(GTK_BOX(vb), hb, FALSE, FALSE, 0);
    G.x_now = lbl("", NULL, 0);
    gtk_box_pack_start(GTK_BOX(vb), G.x_now, FALSE, FALSE, 0);
    hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    G.x_range = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(G.x_range), "Last 7 days");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(G.x_range), "Last 30 days");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(G.x_range), "Everything");
    gtk_combo_box_set_active(GTK_COMBO_BOX(G.x_range), 1);
    g_signal_connect(G.x_range, "changed", G_CALLBACK(on_sess_range), NULL);
    gtk_box_pack_start(GTK_BOX(hb), G.x_range, FALSE, FALSE, 0);
    G.x_sum = wrap_lbl("", NULL);
    gtk_box_pack_start(GTK_BOX(hb), G.x_sum, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vb), hb, FALSE, FALSE, 0);
    G.x_store = gtk_list_store_new(6, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(G.x_store));
    G.x_view = tv;
    for (i = 0; i < 6; i++) {
        GtkCellRenderer *rr = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *col;
        if (i) g_object_set(rr, "xalign", 1.0, NULL);
        col = gtk_tree_view_column_new_with_attributes(cols[i], rr, "text", i, NULL);
        if (!i) gtk_tree_view_column_set_expand(col, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tv), col);
    }
    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(sw), tv);
    gtk_widget_set_size_request(sw, -1, 320);
    gtk_box_pack_start(GTK_BOX(vb), sw, TRUE, TRUE, 0);
    return vb;
}

static void sess_fill(void)
{
    G.loading = 1;
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(G.x_thr), G.busy_thr);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(G.x_start), G.sess_start_min);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(G.x_end), G.sess_end_min);
    G.loading = 0;
}

/* ====================================================================== Price tab */
static void price_fill(void);

static void on_price_changed(GtkWidget *w, gpointer d)
{
    int i, h;
    (void)w; (void)d;
    if (G.loading) return;
    G.tar.flat = gtk_spin_button_get_value(GTK_SPIN_BUTTON(G.c_flat));
    for (i = 0; i < PM_TIERS; i++) {
        G.tar.tier_upto[i] = gtk_spin_button_get_value(GTK_SPIN_BUTTON(G.c_tup[i]));
        G.tar.tier_price[i] = gtk_spin_button_get_value(GTK_SPIN_BUTTON(G.c_tpr[i]));
    }
    /* tiers in use: up to the first one without a limit */
    G.tar.ntier = PM_TIERS;
    for (i = 0; i < PM_TIERS - 1; i++) if (G.tar.tier_upto[i] <= 0) { G.tar.ntier = i + 1; break; }
    for (i = 0; i < PM_TIERS; i++) {
        gtk_widget_set_sensitive(G.c_tup[i], i < G.tar.ntier - 1 || (i == G.tar.ntier - 1 && i < PM_TIERS - 1));
        gtk_widget_set_sensitive(G.c_tpr[i], i < G.tar.ntier);
    }
    G.tar.household_kwh = gtk_spin_button_get_value(GTK_SPIN_BUTTON(G.c_house));
    for (i = 0; i < PM_TOU_LEVELS; i++) {
        snprintf(G.tar.tou_name[i], sizeof G.tar.tou_name[i], "%s", gtk_entry_get_text(GTK_ENTRY(G.c_tname[i])));
        G.tar.tou_price[i] = gtk_spin_button_get_value(GTK_SPIN_BUTTON(G.c_tprice[i]));
    }
    for (i = 0; i < 3; i++) if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(G.c_type[i]))) G.tar.type = i;
    gtk_stack_set_visible_child_name(GTK_STACK(G.c_stack), G.tar.type == PT_TIERED ? "tiered" : G.tar.type == PT_TOU ? "tou" : "flat");
    G.tar.tax_pct = gtk_spin_button_get_value(GTK_SPIN_BUTTON(G.c_tax));
    G.tar.fixed_day = gtk_spin_button_get_value(GTK_SPIN_BUTTON(G.c_fixed));
    G.tar.fixed_on = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(G.c_fixon));
    {
        int ci = gtk_combo_box_get_active(GTK_COMBO_BOX(G.c_cur));
        if (ci >= 0 && ci != G.cur_idx) {   /* a new currency: its usual symbol and decimals */
            G.cur_idx = ci;
            pm_currency_get(ci, &G.money);
            G.loading = 1;
            gtk_entry_set_text(GTK_ENTRY(G.c_sym), G.money.sym);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(G.c_after), G.money.after);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(G.c_dec), G.money.dec);
            G.loading = 0;
        }
        snprintf(G.money.sym, sizeof G.money.sym, "%s", gtk_entry_get_text(GTK_ENTRY(G.c_sym)));
        G.money.after = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(G.c_after));
        G.money.dec = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(G.c_dec));
    }
    for (h = 0; h < 48; h++) {
        GtkWidget *bt = G.c_tou[h / 24][h % 24];
        int lv = h < 24 ? G.tar.tou_wd[h] : G.tar.tou_we[h - 24], k;
        GtkStyleContext *sc = gtk_widget_get_style_context(bt);
        char cls[16];
        for (k = 0; k < PM_TOU_LEVELS; k++) { snprintf(cls, sizeof cls, "pw-tou%d", k); gtk_style_context_remove_class(sc, cls); }
        snprintf(cls, sizeof cls, "pw-tou%d", lv);
        gtk_style_context_add_class(sc, cls);
        {
            char tip[120];
            snprintf(tip, sizeof tip, "%s %02d:00-%02d:00: %s (click to change)", h < 24 ? "Weekdays" : "Weekends", h % 24, (h % 24) + 1,
                     G.tar.tou_name[lv]);
            gtk_widget_set_tooltip_text(bt, tip);
        }
    }
    if (strncmp(G.price_src, "Your price", 10) && w && w != G.c_cur && w != G.c_sym && w != G.c_after && w != G.c_dec)
        snprintf(G.price_src, sizeof G.price_src, "Your price (typed in).");
    gtk_label_set_text(GTK_LABEL(G.c_src), G.price_src);
    cfg_save();
    if (G.have_est) { ui_now(); ui_slow(); }
}

static void on_tou_click(GtkButton *b, gpointer d)
{
    int h = GPOINTER_TO_INT(d);
    unsigned char *lv = h < 24 ? &G.tar.tou_wd[h] : &G.tar.tou_we[h - 24];
    *lv = (unsigned char)((*lv + 1) % PM_TOU_LEVELS);
    on_price_changed(GTK_WIDGET(b), NULL);
}

static GtkWidget *pspin(double lo, double hi, double step, int digits)
{
    GtkWidget *s = gtk_spin_button_new_with_range(lo, hi, step);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(s), (guint)digits);
    g_signal_connect(s, "value-changed", G_CALLBACK(on_price_changed), NULL);
    return s;
}

/* ---- offline price tables ---- */
static void rate_regions_fill(void)
{
    int c = gtk_combo_box_get_active(GTK_COMBO_BOX(G.c_rate_country)), i, n;
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(G.c_rate_region));
    n = prt_region_count(c);
    for (i = 0; i < n; i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(G.c_rate_region), prt_region_name(c, i));
    if (n) gtk_combo_box_set_active(GTK_COMBO_BOX(G.c_rate_region), 0);
    gtk_widget_set_sensitive(G.c_rate_zip, prt_has_zip(c));
}

static void rate_show(void)
{
    int c = gtk_combo_box_get_active(GTK_COMBO_BOX(G.c_rate_country)), r = gtk_combo_box_get_active(GTK_COMBO_BOX(G.c_rate_region));
    prt_rate x;
    char s[700];
    if (c < 0 || r < 0 || !prt_get(c, r, &x)) { gtk_label_set_text(GTK_LABEL(G.c_rate_info), ""); return; }
    if (x.standing_day > 0)
        snprintf(s, sizeof s, "%s: %.4f %s per kWh, standing charge %.4f %s a day. %s", x.name, x.price, x.currency, x.standing_day, x.currency, x.note);
    else
        snprintf(s, sizeof s, "%s: %.4f %s per kWh. %s", x.name, x.price, x.currency, x.note);
    gtk_label_set_text(GTK_LABEL(G.c_rate_info), s);
}

static void on_rate_country(GtkComboBox *cb, gpointer d) { (void)cb; (void)d; rate_regions_fill(); rate_show(); }
static void on_rate_region(GtkComboBox *cb, gpointer d) { (void)cb; (void)d; rate_show(); }

static void on_rate_zip(GtkEntry *e, gpointer d)
{
    int c = gtk_combo_box_get_active(GTK_COMBO_BOX(G.c_rate_country)), r;
    char util[200], s[600];
    double price;
    (void)d;
    r = prt_zip_lookup(gtk_entry_get_text(e), util, sizeof util, &price);
    if (r < 0) { gtk_label_set_text(GTK_LABEL(G.c_rate_info), "That ZIP code is not in the bundled table (US five-digit ZIP codes only)."); return; }
    if (c >= 0 && r < prt_region_count(c)) gtk_combo_box_set_active(GTK_COMBO_BOX(G.c_rate_region), r);
    if (util[0] && price > 0) {
        snprintf(s, sizeof s, "%s\nThis ZIP code is probably served by %s, average residential price %.4f USD per kWh (%s). "
                 "\"Use this price\" takes the utility's average when one is shown.", gtk_label_get_text(GTK_LABEL(G.c_rate_info)), util, price,
                 prt_zip_source());
        gtk_label_set_text(GTK_LABEL(G.c_rate_info), s);
        g_object_set_data(G_OBJECT(G.c_rate_info), "zip-price", GINT_TO_POINTER((int)(price * 1e6)));
        g_object_set_data_full(G_OBJECT(G.c_rate_info), "zip-util", g_strdup(util), g_free);
    }
}

static void on_rate_use(GtkButton *b, gpointer d)
{
    int c = gtk_combo_box_get_active(GTK_COMBO_BOX(G.c_rate_country)), r = gtk_combo_box_get_active(GTK_COMBO_BOX(G.c_rate_region)), i;
    int zp = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(G.c_rate_info), "zip-price"));
    const char *zu = g_object_get_data(G_OBJECT(G.c_rate_info), "zip-util");
    prt_rate x;
    (void)b; (void)d;
    if (c < 0 || r < 0 || !prt_get(c, r, &x)) return;
    if (zp > 0 && zu) { x.price = zp / 1e6; snprintf(x.name, sizeof x.name, "%s", zu); snprintf(x.note, sizeof x.note, "%s", prt_zip_source()); }
    G.loading = 1;
    for (i = 0; i < pm_currency_count(); i++) if (!strcmp(pm_currency_code(i), x.currency)) gtk_combo_box_set_active(GTK_COMBO_BOX(G.c_cur), i);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(G.c_type[PT_FLAT]), TRUE);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(G.c_flat), x.price);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(G.c_tax), 0);
    if (x.standing_day > 0) gtk_spin_button_set_value(GTK_SPIN_BUTTON(G.c_fixed), x.standing_day);
    G.loading = 0;
    on_price_changed(NULL, NULL);
    snprintf(G.price_src, sizeof G.price_src, "Area average from SRS's offline table: %s. %s Your own bill's price is better.", x.name, x.note);
    gtk_label_set_text(GTK_LABEL(G.c_src), G.price_src);
    cfg_save();
    g_object_set_data(G_OBJECT(G.c_rate_info), "zip-price", NULL);
}

static void on_co2(GtkComboBox *cb, gpointer d)
{
    int i = gtk_combo_box_get_active(cb);
    (void)d;
    if (G.loading) return;
    if (i <= 0) { G.co2_g = 0; G.co2_src[0] = 0; }
    else {
        prt_co2 c;
        if (prt_co2_get(i - 1, &c)) { G.co2_g = c.g_per_kwh; snprintf(G.co2_src, sizeof G.co2_src, "%s, %s", c.name, c.source); }
    }
    gtk_label_set_text(GTK_LABEL(G.c_co2src), G.co2_g > 0 ? G.co2_src : "");
    cfg_save();
    if (G.have_est) ui_slow();
}

static GtkWidget *build_price(void)
{
    GtkWidget *vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12), *g, *hb, *st, *f;
    int i, h;
    gtk_container_set_border_width(GTK_CONTAINER(vb), 14);
    /* currency */
    hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(hb), lbl("Currency:", NULL, 0), FALSE, FALSE, 0);
    G.c_cur = gtk_combo_box_text_new();
    for (i = 0; i < pm_currency_count(); i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(G.c_cur), pm_currency_code(i));
    gtk_box_pack_start(GTK_BOX(hb), G.c_cur, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hb), lbl("Symbol:", NULL, 0), FALSE, FALSE, 0);
    G.c_sym = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(G.c_sym), 6);
    gtk_entry_set_max_length(GTK_ENTRY(G.c_sym), 7);
    gtk_box_pack_start(GTK_BOX(hb), G.c_sym, FALSE, FALSE, 0);
    G.c_after = gtk_check_button_new_with_label("after the number");
    gtk_box_pack_start(GTK_BOX(hb), G.c_after, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hb), lbl("Decimals:", NULL, 0), FALSE, FALSE, 0);
    G.c_dec = gtk_spin_button_new_with_range(0, 4, 1);
    gtk_box_pack_start(GTK_BOX(hb), G.c_dec, FALSE, FALSE, 0);
    g_signal_connect(G.c_cur, "changed", G_CALLBACK(on_price_changed), NULL);
    g_signal_connect(G.c_sym, "changed", G_CALLBACK(on_price_changed), NULL);
    g_signal_connect(G.c_after, "toggled", G_CALLBACK(on_price_changed), NULL);
    g_signal_connect(G.c_dec, "value-changed", G_CALLBACK(on_price_changed), NULL);
    gtk_box_pack_start(GTK_BOX(vb), hb, FALSE, FALSE, 0);

    /* structure */
    {
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        G.c_type[0] = gtk_radio_button_new_with_mnemonic(NULL, "_Flat: one price per kWh");
        G.c_type[1] = gtk_radio_button_new_with_mnemonic_from_widget(GTK_RADIO_BUTTON(G.c_type[0]), "_Tiered: the price changes after so many kWh a month");
        G.c_type[2] = gtk_radio_button_new_with_mnemonic_from_widget(GTK_RADIO_BUTTON(G.c_type[0]), "Time of _use: by hour and day");
        for (i = 0; i < 3; i++) { gtk_box_pack_start(GTK_BOX(hb), G.c_type[i], FALSE, FALSE, 0); g_signal_connect(G.c_type[i], "toggled", G_CALLBACK(on_price_changed), NULL); }
        gtk_box_pack_start(GTK_BOX(box), hb, FALSE, FALSE, 0);
        st = gtk_stack_new();
        G.c_stack = st;
        gtk_stack_set_homogeneous(GTK_STACK(st), FALSE);
        /* flat */
        hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_box_pack_start(GTK_BOX(hb), lbl("Price per kWh (from your bill):", NULL, 0), FALSE, FALSE, 0);
        G.c_flat = pspin(0, 100, 0.001, 4);
        gtk_box_pack_start(GTK_BOX(hb), G.c_flat, FALSE, FALSE, 0);
        gtk_stack_add_named(GTK_STACK(st), hb, "flat");
        /* tiered */
        g = gtk_grid_new();
        gtk_grid_set_column_spacing(GTK_GRID(g), 8);
        gtk_grid_set_row_spacing(GTK_GRID(g), 4);
        for (i = 0; i < PM_TIERS; i++) {
            char s[64];
            snprintf(s, sizeof s, "Tier %d: up to", i + 1);
            gtk_grid_attach(GTK_GRID(g), lbl(s, NULL, 1), 0, i, 1, 1);
            G.c_tup[i] = pspin(0, 1e6, 10, 0);
            gtk_widget_set_tooltip_text(G.c_tup[i], "kWh a month in this tier (counting from the start of the month). 0 = no limit: the last tier.");
            gtk_grid_attach(GTK_GRID(g), G.c_tup[i], 1, i, 1, 1);
            gtk_grid_attach(GTK_GRID(g), lbl("kWh a month, price per kWh", NULL, 0), 2, i, 1, 1);
            G.c_tpr[i] = pspin(0, 100, 0.001, 4);
            gtk_grid_attach(GTK_GRID(g), G.c_tpr[i], 3, i, 1, 1);
        }
        gtk_grid_attach(GTK_GRID(g), lbl("The rest of your home uses about", NULL, 1), 0, PM_TIERS, 1, 1);
        G.c_house = pspin(0, 1e6, 10, 0);
        gtk_grid_attach(GTK_GRID(g), G.c_house, 1, PM_TIERS, 1, 1);
        gtk_grid_attach(GTK_GRID(g), lbl("kWh a month (billed before the PC, so the PC lands in a higher tier; 0 = the PC first)", NULL, 0), 2, PM_TIERS, 2, 1);
        gtk_stack_add_named(GTK_STACK(st), g, "tiered");
        /* time of use */
        g = gtk_grid_new();
        gtk_grid_set_column_spacing(GTK_GRID(g), 2);
        gtk_grid_set_row_spacing(GTK_GRID(g), 4);
        for (i = 0; i < PM_TOU_LEVELS; i++) {
            GtkWidget *sw = gtk_button_new_with_label(" ");
            char cls[16];
            snprintf(cls, sizeof cls, "pw-tou%d", i);
            gtk_style_context_add_class(gtk_widget_get_style_context(sw), cls);
            gtk_widget_set_sensitive(sw, FALSE);
            gtk_grid_attach(GTK_GRID(g), sw, 0, i, 1, 1);
            G.c_tname[i] = gtk_entry_new();
            gtk_entry_set_width_chars(GTK_ENTRY(G.c_tname[i]), 12);
            g_signal_connect(G.c_tname[i], "changed", G_CALLBACK(on_price_changed), NULL);
            gtk_grid_attach(GTK_GRID(g), G.c_tname[i], 1, i, 4, 1);
            G.c_tprice[i] = pspin(0, 100, 0.001, 4);
            gtk_grid_attach(GTK_GRID(g), G.c_tprice[i], 5, i, 4, 1);
        }
        gtk_grid_attach(GTK_GRID(g), lbl("Click an hour to change its price level:", NULL, 0), 0, PM_TOU_LEVELS, 12, 1);
        for (h = 0; h < 24; h++) {
            char s[8];
            snprintf(s, sizeof s, "%d", h);
            gtk_grid_attach(GTK_GRID(g), lbl(s, "dim-label", 0.5), h + 2, PM_TOU_LEVELS + 1, 1, 1);
        }
        for (i = 0; i < 2; i++) {
            gtk_grid_attach(GTK_GRID(g), lbl(i ? "Sat, Sun" : "Mon-Fri", NULL, 0), 0, PM_TOU_LEVELS + 2 + i, 2, 1);
            for (h = 0; h < 24; h++) {
                GtkWidget *bt = gtk_button_new();
                gtk_widget_set_size_request(bt, 26, 24);
                gtk_style_context_add_class(gtk_widget_get_style_context(bt), "pw-hour");
                g_signal_connect(bt, "clicked", G_CALLBACK(on_tou_click), GINT_TO_POINTER(i * 24 + h));
                G.c_tou[i][h] = bt;
                gtk_grid_attach(GTK_GRID(g), bt, h + 2, PM_TOU_LEVELS + 2 + i, 1, 1);
            }
        }
        gtk_stack_add_named(GTK_STACK(st), g, "tou");
        gtk_widget_show_all(st);   /* a stack only switches to a child that is already visible */
        gtk_box_pack_start(GTK_BOX(box), st, FALSE, FALSE, 0);
        g = gtk_grid_new();
        gtk_grid_set_column_spacing(GTK_GRID(g), 8);
        gtk_grid_set_row_spacing(GTK_GRID(g), 4);
        gtk_grid_attach(GTK_GRID(g), lbl("Taxes and levies not in the price above (%):", NULL, 1), 0, 0, 1, 1);
        G.c_tax = pspin(0, 100, 0.5, 2);
        gtk_grid_attach(GTK_GRID(g), G.c_tax, 1, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(g), lbl("Standing charge per day:", NULL, 1), 0, 1, 1, 1);
        G.c_fixed = pspin(0, 1000, 0.01, 4);
        gtk_grid_attach(GTK_GRID(g), G.c_fixed, 1, 1, 1, 1);
        G.c_fixon = gtk_check_button_new_with_label("include it in the cost per hour/day/month/year (the PC does not cause it)");
        g_signal_connect(G.c_fixon, "toggled", G_CALLBACK(on_price_changed), NULL);
        gtk_grid_attach(GTK_GRID(g), G.c_fixon, 2, 1, 1, 1);
        gtk_box_pack_start(GTK_BOX(box), g, FALSE, FALSE, 0);
        G.c_src = wrap_lbl("", "dim-label");
        gtk_box_pack_start(GTK_BOX(box), G.c_src, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vb), frame("Your price", box), FALSE, FALSE, 0);
    }

    /* offline tables */
    {
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6), *b;
        char s[600];
        hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        G.c_rate_country = gtk_combo_box_text_new();
        for (i = 0; i < prt_country_count(); i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(G.c_rate_country), prt_country_name(i));
        gtk_box_pack_start(GTK_BOX(hb), G.c_rate_country, FALSE, FALSE, 0);
        G.c_rate_region = gtk_combo_box_text_new();
        gtk_box_pack_start(GTK_BOX(hb), G.c_rate_region, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(hb), lbl("US ZIP:", NULL, 0), FALSE, FALSE, 0);
        G.c_rate_zip = gtk_entry_new();
        gtk_entry_set_width_chars(GTK_ENTRY(G.c_rate_zip), 7);
        gtk_entry_set_max_length(GTK_ENTRY(G.c_rate_zip), 5);
        gtk_widget_set_tooltip_text(G.c_rate_zip, "Type a US ZIP code and press Enter: picks the state, and the utility's average price where the table has it");
        gtk_box_pack_start(GTK_BOX(hb), G.c_rate_zip, FALSE, FALSE, 0);
        b = gtk_button_new_with_mnemonic("Use this _price");
        g_signal_connect(b, "clicked", G_CALLBACK(on_rate_use), NULL);
        gtk_box_pack_start(GTK_BOX(hb), b, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), hb, FALSE, FALSE, 0);
        G.c_rate_info = wrap_lbl("", NULL);
        gtk_box_pack_start(GTK_BOX(box), G.c_rate_info, FALSE, FALSE, 0);
        snprintf(s, sizeof s, "Built into SRS, nothing is sent anywhere. Averages for an area, not your plan, and they may be out of date: "
                 "%s. Sources and licences: Guide, \"Power & cost\".", prt_tables_date());
        gtk_box_pack_start(GTK_BOX(box), wrap_lbl(s, "dim-label"), FALSE, FALSE, 0);
        g_signal_connect(G.c_rate_country, "changed", G_CALLBACK(on_rate_country), NULL);
        g_signal_connect(G.c_rate_region, "changed", G_CALLBACK(on_rate_region), NULL);
        g_signal_connect(G.c_rate_zip, "activate", G_CALLBACK(on_rate_zip), NULL);
        gtk_box_pack_start(GTK_BOX(vb), frame("Don't know your price? Typical prices for your area (offline)", box), FALSE, FALSE, 0);
    }

    /* projections + CO2 */
    {
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        G.c_proj = wrap_lbl("", NULL);
        gtk_box_pack_start(GTK_BOX(box), G.c_proj, FALSE, FALSE, 0);
        hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_box_pack_start(GTK_BOX(hb), lbl("CO\xe2\x82\x82 estimate:", NULL, 0), FALSE, FALSE, 0);
        G.c_co2 = gtk_combo_box_text_new();
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(G.c_co2), "Off");
        for (i = 0; i < prt_co2_count(); i++) {
            prt_co2 c;
            char s[160];
            prt_co2_get(i, &c);
            snprintf(s, sizeof s, "%s (%.0f g/kWh)", c.name, c.g_per_kwh);
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(G.c_co2), s);
        }
        g_signal_connect(G.c_co2, "changed", G_CALLBACK(on_co2), NULL);
        gtk_box_pack_start(GTK_BOX(hb), G.c_co2, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), hb, FALSE, FALSE, 0);
        G.c_co2src = wrap_lbl("", "dim-label");
        gtk_box_pack_start(GTK_BOX(box), G.c_co2src, FALSE, FALSE, 0);
        f = frame("What if: projections and CO\xe2\x82\x82 (grid averages; your supplier may differ)", box);
        gtk_box_pack_start(GTK_BOX(vb), f, FALSE, FALSE, 0);
    }
    return scrolled(vb);
}

static void price_fill(void)
{
    int i;
    G.loading = 1;
    gtk_combo_box_set_active(GTK_COMBO_BOX(G.c_cur), G.cur_idx);
    gtk_entry_set_text(GTK_ENTRY(G.c_sym), G.money.sym);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(G.c_after), G.money.after);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(G.c_dec), G.money.dec);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(G.c_type[G.tar.type >= 0 && G.tar.type < 3 ? G.tar.type : 0]), TRUE);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(G.c_flat), G.tar.flat);
    for (i = 0; i < PM_TIERS; i++) {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(G.c_tup[i]), i < G.tar.ntier - 1 ? G.tar.tier_upto[i] : 0);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(G.c_tpr[i]), G.tar.tier_price[i]);
    }
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(G.c_house), G.tar.household_kwh);
    for (i = 0; i < PM_TOU_LEVELS; i++) {
        gtk_entry_set_text(GTK_ENTRY(G.c_tname[i]), G.tar.tou_name[i]);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(G.c_tprice[i]), G.tar.tou_price[i]);
    }
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(G.c_tax), G.tar.tax_pct);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(G.c_fixed), G.tar.fixed_day);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(G.c_fixon), G.tar.fixed_on);
    gtk_combo_box_set_active(GTK_COMBO_BOX(G.c_co2), 0);
    for (i = 0; i < prt_co2_count(); i++) {
        prt_co2 c;
        if (prt_co2_get(i, &c) && G.co2_g > 0 && fabs(c.g_per_kwh - G.co2_g) < 0.01 && strstr(G.co2_src, c.name))
            gtk_combo_box_set_active(GTK_COMBO_BOX(G.c_co2), i + 1);
    }
    gtk_label_set_text(GTK_LABEL(G.c_co2src), G.co2_g > 0 ? G.co2_src : "");
    gtk_combo_box_set_active(GTK_COMBO_BOX(G.c_rate_country), prt_guess_country());
    G.loading = 0;
    {
        char keep[400];
        snprintf(keep, sizeof keep, "%s", G.price_src);
        on_price_changed(NULL, NULL);   /* syncs the stack, tier sensitivity and hour colours */
        snprintf(G.price_src, sizeof G.price_src, "%s", keep);
        gtk_label_set_text(GTK_LABEL(G.c_src), G.price_src);
    }
}

/* ====================================================================== Setup tab */
enum { PC_ON, PC_NAME, PC_COUNT, PC_LO, PC_HI, PC_KIND, PC_HOW, PC_N };

static void parts_fill(void)
{
    int i;
    char lo[32], hi[32], cnt[16];
    gtk_list_store_clear(G.s_pstore);
    for (i = 0; i < G.nparts; i++) {
        GtkTreeIter it;
        pm_part *p = &G.parts[i];
        int pct = p->kind == PK_CPU_VRM || p->kind == PK_GPU_REST;
        snprintf(lo, sizeof lo, pct ? "%g %%" : "%g", p->lo);
        snprintf(hi, sizeof hi, pct ? "%g %%" : "%g", p->hi);
        snprintf(cnt, sizeof cnt, "%d", p->count);
        gtk_list_store_append(G.s_pstore, &it);
        gtk_list_store_set(G.s_pstore, &it, PC_ON, p->on, PC_NAME, p->name, PC_COUNT, cnt, PC_LO, lo, PC_HI, hi, PC_KIND,
                           pm_kind_name(p->kind), PC_HOW, p->how, -1);
    }
}

static void parts_changed(void)
{
    cfg_save();
    parts_fill();
    if (G.have_est) { pm_estimate(&G.meas, G.parts, G.nparts, &G.psu, &G.est); ui_now(); ui_slow(); }
}

static int path_index(const char *path) { return atoi(path); }

static void on_part_toggle(GtkCellRendererToggle *r, gchar *path, gpointer d)
{
    int i = path_index(path);
    (void)r; (void)d;
    if (i < 0 || i >= G.nparts) return;
    G.parts[i].on = !G.parts[i].on;
    parts_changed();
}

static void on_part_edit(GtkCellRendererText *r, gchar *path, gchar *text, gpointer d)
{
    int i = path_index(path), col = GPOINTER_TO_INT(d), k;
    pm_part *p;
    (void)r;
    if (i < 0 || i >= G.nparts) return;
    p = &G.parts[i];
    switch (col) {
    case PC_NAME: if (*text) snprintf(p->name, sizeof p->name, "%s", text); break;
    case PC_COUNT: p->count = atoi(text); if (p->count < 0) p->count = 0; if (p->count > 999) p->count = 999; break;
    case PC_LO: case PC_HI: {
        double v = g_strtod(text, NULL);
        if (v < 0) v = 0;
        if (v > 5000) v = 5000;
        if (col == PC_LO) p->lo = v; else p->hi = v;
        if (p->hi < p->lo) { double t = p->hi; p->hi = p->lo; p->lo = t; }
        snprintf(p->how, sizeof p->how, "your entry");
        break;
    }
    case PC_KIND:
        for (k = 0; k < PK_N; k++) if (!strcmp(text, pm_kind_name(k))) p->kind = k;
        break;
    case PC_HOW: snprintf(p->how, sizeof p->how, "%s", text); break;
    default: break;
    }
    parts_changed();
}

static void on_part_add(GtkMenuItem *mi, gpointer d)
{
    const pm_part *p = psrc_preset(GPOINTER_TO_INT(d));
    (void)mi;
    if (!p || G.nparts >= MAXPARTS) return;
    G.parts[G.nparts++] = *p;
    parts_changed();
}

static void on_part_remove(GtkButton *b, gpointer d)
{
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(G.s_pview));
    GtkTreeModel *m;
    GtkTreeIter it;
    (void)b; (void)d;
    if (gtk_tree_selection_get_selected(sel, &m, &it)) {
        GtkTreePath *p = gtk_tree_model_get_path(m, &it);
        int i = gtk_tree_path_get_indices(p)[0];
        gtk_tree_path_free(p);
        if (i >= 0 && i < G.nparts) {
            memmove(&G.parts[i], &G.parts[i + 1], sizeof G.parts[0] * (size_t)(G.nparts - i - 1));
            G.nparts--;
            parts_changed();
        }
    }
}

static void on_part_reset(GtkButton *b, gpointer d)
{
    (void)d;
    if (!confirm(GTK_WIDGET(b), "Go back to SRS's guess?", "The parts list is replaced by what SRS detects on this PC. Your changes to it are lost.", "_Reset"))
        return;
    G.nparts = psrc_default_parts(&G.src, G.parts, MAXPARTS);
    parts_changed();
}

static void psu_fill(void)
{
    char c[300];
    G.loading = 1;
    gtk_combo_box_set_active(GTK_COMBO_BOX(G.s_psu), G.psu_preset);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(G.s_rating), G.psu.rating);
    pm_psu_curve_text(&G.psu, c, sizeof c);
    gtk_entry_set_text(GTK_ENTRY(G.s_curve), c);
    gtk_label_set_text(GTK_LABEL(G.s_psusrc), G.psu.source);
    G.loading = 0;
}

static void on_psu_preset(GtkComboBox *cb, gpointer d)
{
    (void)d;
    if (G.loading) return;
    G.psu_preset = gtk_combo_box_get_active(cb);
    pm_psu_preset(G.psu_preset, gtk_spin_button_get_value(GTK_SPIN_BUTTON(G.s_rating)), &G.psu);
    psu_fill();
    gtk_label_set_text(GTK_LABEL(G.s_curveerr), "");
    parts_changed();
}

static void on_psu_rating(GtkSpinButton *s, gpointer d)
{
    (void)d;
    if (G.loading) return;
    G.psu.rating = gtk_spin_button_get_value(s);
    parts_changed();
}

static void on_psu_curve(GtkWidget *w, gpointer d)
{
    pm_psu t = G.psu;
    (void)w; (void)d;
    if (pm_psu_parse_curve(&t, gtk_entry_get_text(GTK_ENTRY(G.s_curve))) < 0) {
        gtk_label_set_text(GTK_LABEL(G.s_curveerr), "Not understood. Write load %:efficiency % pairs going up in load, e.g. 10:86, 20:90, 50:92, 100:89");
        return;
    }
    G.psu = t;
    snprintf(G.psu.source, sizeof G.psu.source, "Your typed curve (load %%:efficiency %%).");
    gtk_label_set_text(GTK_LABEL(G.s_psusrc), G.psu.source);
    gtk_label_set_text(GTK_LABEL(G.s_curveerr), "Curve in use.");
    parts_changed();
}

static void on_setup_misc(GtkWidget *w, gpointer d)
{
    int iv;
    (void)w; (void)d;
    if (G.loading) return;
    G.sleep_w = gtk_spin_button_get_value(GTK_SPIN_BUTTON(G.s_sleep));
    G.off_w = gtk_spin_button_get_value(GTK_SPIN_BUTTON(G.s_off));
    iv = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(G.s_int));
    if (iv != G.interval) { G.interval = iv; timer_restart(); }
    cfg_save();
}

/* ---- the background recorder: a systemd user service running `srs-gui --power-recorder` ---- */
static char *bg_unit_path(void) { return g_build_filename(g_get_user_config_dir(), "systemd", "user", "srs-power-recorder.service", NULL); }

static int bg_installed(void)
{
    char *p = bg_unit_path();
    int r = g_file_test(p, G_FILE_TEST_EXISTS);
    g_free(p);
    return r;
}

static int run_cmd(const char *const argv[], char **out)
{
    int st = -1;
    if (!g_spawn_sync(NULL, (char **)argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, out, NULL, &st, NULL)) return -1;
    return st;
}

static void bg_status(void)
{
    char *out = NULL, s[300];
    const char *const a[] = { "systemctl", "--user", "is-active", "srs-power-recorder.service", NULL };
    if (!G.s_bgstat) return;
    if (!bg_installed()) { gtk_label_set_text(GTK_LABEL(G.s_bgstat), "Not set up."); return; }
    run_cmd(a, &out);
    snprintf(s, sizeof s, "Set up (%s). It records only while Recording is \"For a set time\" or \"Always\".", out && *out ? g_strstrip(out) : "state unknown");
    gtk_label_set_text(GTK_LABEL(G.s_bgstat), s);
    g_free(out);
}

static void on_bg_toggle(GtkToggleButton *t, gpointer d)
{
    int on = gtk_toggle_button_get_active(t);
    char *p = bg_unit_path();
    (void)d;
    if (G.loading) { g_free(p); return; }
    if (on) {
        const char *ai = getenv("APPIMAGE");
        char *exe = ai && *ai ? g_strdup(ai) : g_file_read_link("/proc/self/exe", NULL), *dir = g_path_get_dirname(p), *txt;
        const char *const r1[] = { "systemctl", "--user", "daemon-reload", NULL };
        const char *const r2[] = { "systemctl", "--user", "enable", "--now", "srs-power-recorder.service", NULL };
        g_mkdir_with_parents(dir, 0755);
        txt = g_strdup_printf("# Written by SRS (Power & cost > Setup). Remove it there, or delete this file.\n"
                              "[Unit]\nDescription=SRS power recorder (Power & cost history)\n\n"
                              "[Service]\nExecStart=\"%s\" --power-recorder\nRestart=on-failure\nRestartSec=30\nNice=10\n\n"
                              "[Install]\nWantedBy=default.target\n", exe ? exe : "srs-gui");
        g_file_set_contents(p, txt, -1, NULL);
        run_cmd(r1, NULL);
        run_cmd(r2, NULL);
        g_free(txt); g_free(exe); g_free(dir);
    } else {
        const char *const r1[] = { "systemctl", "--user", "disable", "--now", "srs-power-recorder.service", NULL };
        const char *const r2[] = { "systemctl", "--user", "daemon-reload", NULL };
        run_cmd(r1, NULL);
        g_unlink(p);
        run_cmd(r2, NULL);
    }
    g_free(p);
    bg_status();
}

static void on_delete_history(GtkButton *b, gpointer d)
{
    (void)d;
    if (!db_ensure(0)) return;
    if (!confirm(GTK_WIDGET(b), "Delete all recorded power history?", "Every recorded minute, gap and note in the power database is "
                 "deleted. Settings stay. This cannot be undone.", "_Delete"))
        return;
    pdb_delete_all();
    G.month_key = -1;
    G.month_wh = 0;
    hist_reload();
    ui_slow();
}

static void show_disclaimer(GtkWidget *parent, int first);
static void on_disclaimer_btn(GtkButton *b, gpointer d) { (void)d; show_disclaimer(GTK_WIDGET(b), 0); }

static GtkWidget *build_setup(void)
{
    GtkWidget *vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12), *box, *hb, *g, *tv, *sw, *b, *menu;
    GtkCellRenderer *rr;
    int i;
    gtk_container_set_border_width(GTK_CONTAINER(vb), 14);
    /* PSU */
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    G.s_psu = gtk_combo_box_text_new();
    for (i = 0; i < pm_psu_preset_count(); i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(G.s_psu), pm_psu_preset_name(i));
    g_signal_connect(G.s_psu, "changed", G_CALLBACK(on_psu_preset), NULL);
    gtk_box_pack_start(GTK_BOX(hb), G.s_psu, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hb), lbl("Rated:", NULL, 0), FALSE, FALSE, 0);
    G.s_rating = gtk_spin_button_new_with_range(100, 3000, 50);
    g_signal_connect(G.s_rating, "value-changed", G_CALLBACK(on_psu_rating), NULL);
    gtk_box_pack_start(GTK_BOX(hb), G.s_rating, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hb), lbl("W", NULL, 0), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), hb, FALSE, FALSE, 0);
    hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(hb), lbl("Efficiency curve (load %:efficiency %):", NULL, 0), FALSE, FALSE, 0);
    G.s_curve = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(G.s_curve), 44);
    gtk_widget_set_tooltip_text(G.s_curve, "For experts: points from your power supply's Cybenetics or review report, e.g. 2:68, 10:86, 20:89, 50:91, 100:88. Press Enter.");
    g_signal_connect(G.s_curve, "activate", G_CALLBACK(on_psu_curve), NULL);
    gtk_box_pack_start(GTK_BOX(hb), G.s_curve, TRUE, TRUE, 0);
    b = gtk_button_new_with_mnemonic("_Use curve");
    g_signal_connect(b, "clicked", G_CALLBACK(on_psu_curve), NULL);
    gtk_box_pack_start(GTK_BOX(hb), b, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), hb, FALSE, FALSE, 0);
    G.s_curveerr = wrap_lbl("", NULL);
    gtk_box_pack_start(GTK_BOX(box), G.s_curveerr, FALSE, FALSE, 0);
    G.s_psusrc = wrap_lbl("", "dim-label");
    gtk_box_pack_start(GTK_BOX(box), G.s_psusrc, FALSE, FALSE, 0);
    G.s_psunow = wrap_lbl("", NULL);
    gtk_box_pack_start(GTK_BOX(box), G.s_psunow, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vb), frame("Power supply (PSU)", box), FALSE, FALSE, 0);

    /* parts */
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_pack_start(GTK_BOX(box), wrap_lbl("Everything SRS cannot measure. Each row adds a low and a high guess (watts each, "
                                              "times the number). Double-click a value to change it; untick a row to leave it out.", "dim-label"),
                       FALSE, FALSE, 0);
    G.s_pstore = gtk_list_store_new(PC_N, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    G.s_kinds = gtk_list_store_new(1, G_TYPE_STRING);
    for (i = 0; i < PK_N; i++) { GtkTreeIter it; gtk_list_store_append(G.s_kinds, &it); gtk_list_store_set(G.s_kinds, &it, 0, pm_kind_name(i), -1); }
    tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(G.s_pstore));
    G.s_pview = tv;
    rr = gtk_cell_renderer_toggle_new();
    g_signal_connect(rr, "toggled", G_CALLBACK(on_part_toggle), NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tv), gtk_tree_view_column_new_with_attributes("On", rr, "active", PC_ON, NULL));
    {
        static const char *const t[] = { NULL, "Part", "Number", "Low", "High", "Counts as", "Where the figure comes from" };
        int c;
        for (c = PC_NAME; c < PC_N; c++) {
            GtkTreeViewColumn *col;
            if (c == PC_KIND) {
                rr = gtk_cell_renderer_combo_new();
                g_object_set(rr, "model", G.s_kinds, "text-column", 0, "has-entry", FALSE, "editable", TRUE, NULL);
            } else {
                rr = gtk_cell_renderer_text_new();
                g_object_set(rr, "editable", TRUE, NULL);
            }
            if (c == PC_COUNT || c == PC_LO || c == PC_HI) g_object_set(rr, "xalign", 1.0, NULL);
            if (c == PC_HOW) g_object_set(rr, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
            g_signal_connect(rr, "edited", G_CALLBACK(on_part_edit), GINT_TO_POINTER(c));
            col = gtk_tree_view_column_new_with_attributes(t[c], rr, "text", c, NULL);
            gtk_tree_view_column_set_resizable(col, TRUE);
            if (c == PC_HOW) gtk_tree_view_column_set_expand(col, TRUE);
            gtk_tree_view_append_column(GTK_TREE_VIEW(tv), col);
        }
    }
    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(sw), tv);
    gtk_widget_set_size_request(sw, -1, 300);
    gtk_box_pack_start(GTK_BOX(box), sw, TRUE, TRUE, 0);
    hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    b = gtk_menu_button_new();
    gtk_button_set_label(GTK_BUTTON(b), "Add a part...");
    menu = gtk_menu_new();
    for (i = 0; i < psrc_preset_count(); i++) {
        GtkWidget *mi = gtk_menu_item_new_with_label(psrc_preset(i)->name);
        g_signal_connect(mi, "activate", G_CALLBACK(on_part_add), GINT_TO_POINTER(i));
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
    }
    gtk_widget_show_all(menu);
    gtk_menu_button_set_popup(GTK_MENU_BUTTON(b), menu);
    gtk_box_pack_start(GTK_BOX(hb), b, FALSE, FALSE, 0);
    b = gtk_button_new_with_mnemonic("Re_move");
    g_signal_connect(b, "clicked", G_CALLBACK(on_part_remove), NULL);
    gtk_box_pack_start(GTK_BOX(hb), b, FALSE, FALSE, 0);
    b = gtk_button_new_with_mnemonic("Reset to SRS's _guess...");
    g_signal_connect(b, "clicked", G_CALLBACK(on_part_reset), NULL);
    gtk_box_pack_start(GTK_BOX(hb), b, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), hb, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vb), frame("Parts that cannot be measured", box), FALSE, FALSE, 0);

    /* sleep / off / recording */
    g = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(g), 8);
    gtk_grid_set_row_spacing(GTK_GRID(g), 6);
    gtk_grid_attach(GTK_GRID(g), lbl("While the PC sleeps it draws about", NULL, 1), 0, 0, 1, 1);
    G.s_sleep = gtk_spin_button_new_with_range(0, 100, 0.5);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(G.s_sleep), 1);
    gtk_grid_attach(GTK_GRID(g), G.s_sleep, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(g), lbl("W (counted while recording; 0 = do not count)", NULL, 0), 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(g), lbl("While it is switched off (standby)", NULL, 1), 0, 1, 1, 1);
    G.s_off = gtk_spin_button_new_with_range(0, 50, 0.1);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(G.s_off), 1);
    gtk_grid_attach(GTK_GRID(g), G.s_off, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(g), lbl("W (counted between shut-down and the next start, up to 31 days)", NULL, 0), 2, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(g), lbl("Read the sensors every", NULL, 1), 0, 2, 1, 1);
    G.s_int = gtk_spin_button_new_with_range(1, 60, 1);
    gtk_grid_attach(GTK_GRID(g), G.s_int, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(g), lbl("seconds (energy counters keep exact totals; 1-10 s is plenty)", NULL, 0), 2, 2, 1, 1);
    g_signal_connect(G.s_sleep, "value-changed", G_CALLBACK(on_setup_misc), NULL);
    g_signal_connect(G.s_off, "value-changed", G_CALLBACK(on_setup_misc), NULL);
    g_signal_connect(G.s_int, "value-changed", G_CALLBACK(on_setup_misc), NULL);
    G.s_bg = gtk_check_button_new_with_mnemonic("_Keep recording when SRS is closed (a background recorder: systemd user service)");
    gtk_widget_set_tooltip_text(G.s_bg, "Writes ~/.config/systemd/user/srs-power-recorder.service and enables it. It runs as you, not as root, "
                                        "and only records while Recording is set to a time or Always.");
    g_signal_connect(G.s_bg, "toggled", G_CALLBACK(on_bg_toggle), NULL);
    gtk_grid_attach(GTK_GRID(g), G.s_bg, 0, 3, 3, 1);
    G.s_bgstat = lbl("", "dim-label", 0);
    gtk_grid_attach(GTK_GRID(g), G.s_bgstat, 0, 4, 3, 1);
    G.s_db = wrap_lbl("", "dim-label");
    gtk_grid_attach(GTK_GRID(g), G.s_db, 0, 5, 3, 1);
    hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    b = gtk_button_new_with_mnemonic("_Delete all history...");
    g_signal_connect(b, "clicked", G_CALLBACK(on_delete_history), NULL);
    gtk_box_pack_start(GTK_BOX(hb), b, FALSE, FALSE, 0);
    b = gtk_button_new_with_mnemonic("Read the _note about estimates");
    g_signal_connect(b, "clicked", G_CALLBACK(on_disclaimer_btn), NULL);
    gtk_box_pack_start(GTK_BOX(hb), b, FALSE, FALSE, 0);
    gtk_grid_attach(GTK_GRID(g), hb, 0, 6, 3, 1);
    gtk_box_pack_start(GTK_BOX(vb), frame("Sleep, off and recording", g), FALSE, FALSE, 0);
    return scrolled(vb);
}

static void setup_fill(void)
{
    char s[1300];
    G.loading = 1;
    psu_fill();
    G.loading = 1;
    parts_fill();
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(G.s_sleep), G.sleep_w);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(G.s_off), G.off_w);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(G.s_int), G.interval);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(G.s_bg), bg_installed());
    snprintf(s, sizeof s, "History: %s (one row per minute, about 30 MB a year; only you can read it).  Settings: %s", G.db_path, G.ini_path);
    gtk_label_set_text(GTK_LABEL(G.s_db), s);
    G.loading = 0;
    bg_status();
}

/* ====================================================================== Alarms tab */
static int a_sel(void)
{
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(G.a_view));
    GtkTreeModel *m;
    GtkTreeIter it;
    int i = -1;
    if (gtk_tree_selection_get_selected(sel, &m, &it)) {
        GtkTreePath *p = gtk_tree_model_get_path(m, &it);
        i = gtk_tree_path_get_indices(p)[0];
        gtk_tree_path_free(p);
    }
    return i < G.nalarm ? i : -1;
}

static void alarm_desc(const pm_alarm *a, char *s, int sz)
{
    char lim[64];
    if (pm_alarm_is_cost(a->type)) pm_fmt_money(&G.money, a->limit, lim, sizeof lim);
    else snprintf(lim, sizeof lim, "%g %s", a->limit, pm_unit_name(a->unit));
    if (a->type == PA_W_NOW) snprintf(s, (size_t)sz, "%s %s for %.0f s", pm_alarm_name(a->type), lim, a->secs);
    else if (a->type == PA_W_AVG) snprintf(s, (size_t)sz, "%s %s over %.0f min", pm_alarm_name(a->type), lim, a->secs / 60);
    else snprintf(s, (size_t)sz, "%s %s", pm_alarm_name(a->type), lim);
}

static void alarms_fill(void)
{
    int i, keep = G.a_view ? a_sel() : -1;
    gtk_list_store_clear(G.a_store);
    for (i = 0; i < G.nalarm; i++) {
        GtkTreeIter it;
        char s[300];
        alarm_desc(&G.alarm[i], s, sizeof s);
        gtk_list_store_append(G.a_store, &it);
        gtk_list_store_set(G.a_store, &it, 0, G.alarm[i].on, 1, s, -1);
    }
    if (keep >= 0 && keep < G.nalarm) {
        GtkTreePath *p = gtk_tree_path_new_from_indices(keep, -1);
        gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(G.a_view)), p);
        gtk_tree_path_free(p);
    }
}

static void a_units_fill(int type, int unit)
{
    int u, k = 0, act = 0;
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(G.a_unit));
    if (pm_alarm_is_cost(type)) {
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(G.a_unit), "-1", G.money.code[0] ? G.money.code : "money");
        gtk_combo_box_set_active(GTK_COMBO_BOX(G.a_unit), 0);
        return;
    }
    for (u = 0; u < PU_N; u++) {
        char id[8];
        if (pm_unit_is_power(u) != pm_alarm_is_power(type)) continue;
        snprintf(id, sizeof id, "%d", u);
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(G.a_unit), id, pm_unit_name(u));
        if (u == unit) act = k;
        k++;
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(G.a_unit), act);
}

static void a_conv_text(void)
{
    int i = a_sel();
    pm_alarm *a;
    char s[400];
    if (i < 0) { gtk_label_set_text(GTK_LABEL(G.a_conv), ""); return; }
    a = &G.alarm[i];
    if (pm_alarm_is_cost(a->type)) {
        double price = pm_price(&G.tar, time(NULL), 0);
        if (price > 0) snprintf(s, sizeof s, "At today's price that is about %.2f kWh.", a->limit / price);
        else s[0] = 0;
    } else if (pm_alarm_is_power(a->type)) {
        double w = pm_to_base(a->limit, a->unit);
        snprintf(s, sizeof s, "Same as %g W = %g kW = %.0f BTU/h (heat into the room). In an hour: %g Wh = %g kWh.", w, w / 1000,
                 pm_from_base(w, PU_BTUH), w, w / 1000);
    } else {
        double wh = pm_to_base(a->limit, a->unit);
        snprintf(s, sizeof s, "Same as %g Wh = %g kWh = %g MJ. Over 24 h that is an average of %.0f W.", wh, wh / 1000,
                 pm_from_base(wh, PU_MJ), a->type == PA_E_DAY ? wh / 24 : wh / (24 * 30.4375));
    }
    gtk_label_set_text(GTK_LABEL(G.a_conv), s);
}

static void a_form_load(void)
{
    int i = a_sel();
    gtk_widget_set_sensitive(G.a_form, i >= 0);
    if (i < 0) return;
    G.loading = 1;
    gtk_combo_box_set_active(GTK_COMBO_BOX(G.a_type), G.alarm[i].type);
    a_units_fill(G.alarm[i].type, G.alarm[i].unit);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(G.a_limit), G.alarm[i].limit);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(G.a_secs), G.alarm[i].type == PA_W_AVG ? G.alarm[i].secs / 60 : G.alarm[i].secs);
    gtk_widget_set_sensitive(G.a_secs, pm_alarm_is_power(G.alarm[i].type));
    G.loading = 0;
    a_conv_text();
}

static void on_a_sel(GtkTreeSelection *s, gpointer d) { (void)s; (void)d; a_form_load(); }

static void on_a_form(GtkWidget *w, gpointer d)
{
    int i = a_sel(), t;
    pm_alarm *a;
    const char *id;
    (void)d;
    if (G.loading || i < 0) return;
    a = &G.alarm[i];
    t = gtk_combo_box_get_active(GTK_COMBO_BOX(G.a_type));
    if (t != a->type) {
        a->type = t;
        a->unit = pm_alarm_is_power(t) ? PU_W : PU_KWH;
        a->fired = 0; a->fired_period = -1; a->above_since = 0;
        if (a->type == PA_W_AVG && a->secs < 60) a->secs = 1800;
        G.loading = 1;
        a_units_fill(t, a->unit);
        G.loading = 0;
    } else if (w == G.a_unit && (id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(G.a_unit))) && atoi(id) >= 0)
        a->unit = atoi(id);
    a->limit = gtk_spin_button_get_value(GTK_SPIN_BUTTON(G.a_limit));
    if (w == G.a_secs || w == G.a_type) {
        double v = gtk_spin_button_get_value(GTK_SPIN_BUTTON(G.a_secs));
        if (w == G.a_secs) a->secs = a->type == PA_W_AVG ? v * 60 : v;
    }
    a->fired = 0;
    a->fired_period = -1;
    cfg_save();
    alarms_fill();
    a_form_load();
}

static void on_a_toggle(GtkCellRendererToggle *r, gchar *path, gpointer d)
{
    int i = atoi(path);
    (void)r; (void)d;
    if (i < 0 || i >= G.nalarm) return;
    G.alarm[i].on = !G.alarm[i].on;
    cfg_save();
    alarms_fill();
}

static void on_a_add(GtkMenuItem *mi, gpointer d)
{
    int k = GPOINTER_TO_INT(d);
    pm_alarm *a;
    GtkTreePath *p;
    (void)mi;
    if (G.nalarm >= MAXALARM) return;
    a = &G.alarm[G.nalarm++];
    memset(a, 0, sizeof *a);
    a->on = 1;
    a->fired_period = -1;
    switch (k) {
    case 0: a->type = PA_W_NOW; a->unit = PU_W; a->limit = 400; a->secs = 10; break;
    case 1: a->type = PA_W_AVG; a->unit = PU_W; a->limit = 250; a->secs = 1800; break;
    case 2: a->type = PA_E_DAY; a->unit = PU_KWH; a->limit = 3; break;
    case 3: a->type = PA_E_MONTH; a->unit = PU_KWH; a->limit = 90; break;
    case 4: a->type = PA_COST_MONTH; a->unit = PU_KWH; a->limit = 15; break;
    default: a->type = PA_FC_COST_MONTH; a->unit = PU_KWH; a->limit = 20; break;
    }
    cfg_save();
    alarms_fill();
    p = gtk_tree_path_new_from_indices(G.nalarm - 1, -1);
    gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(G.a_view)), p);
    gtk_tree_path_free(p);
}

static void on_a_remove(GtkButton *b, gpointer d)
{
    int i = a_sel();
    (void)b; (void)d;
    if (i < 0) return;
    memmove(&G.alarm[i], &G.alarm[i + 1], sizeof G.alarm[0] * (size_t)(G.nalarm - i - 1));
    G.nalarm--;
    cfg_save();
    alarms_fill();
    a_form_load();
}

static void on_a_test(GtkButton *b, gpointer d)
{
    (void)b; (void)d;
    notify_desktop("SRS power alarm (test)", "This is how an SRS power alarm looks. Alarms are estimates and can be late or missed.");
    snprintf(G.last_alarm, sizeof G.last_alarm, "Test notification sent.");
    gtk_label_set_text(GTK_LABEL(G.a_last), G.last_alarm);
}

static void on_a_notify(GtkToggleButton *t, gpointer d) { (void)d; if (!G.loading) { G.notify = gtk_toggle_button_get_active(t); cfg_save(); } }

static GtkWidget *build_alarms(void)
{
    GtkWidget *vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10), *hb, *tv, *sw, *b, *menu, *g;
    GtkCellRenderer *rr;
    int i;
    static const char *const tmpl[] = { "Warn me over 400 W (for 10 s)", "Warn me when the 30-minute average is over 250 W",
                                        "Daily energy budget (3 kWh)", "Monthly energy budget (90 kWh)", "Monthly cost budget",
                                        "This month is heading over budget (forecast)" };
    gtk_container_set_border_width(GTK_CONTAINER(vb), 14);
    gtk_box_pack_start(GTK_BOX(vb), wrap_lbl("SRS tells you once when a limit is crossed, with a desktop notification. Power alarms re-arm "
                                             "when the power drops clearly below the limit; day and month alarms once per day or month. "
                                             "Energy and cost alarms need Recording on. They are estimates too: they can be late or missed.",
                                             "dim-label"), FALSE, FALSE, 0);
    G.a_notify = gtk_check_button_new_with_mnemonic("Show alarms as _desktop notifications (otherwise only on this tab)");
    g_signal_connect(G.a_notify, "toggled", G_CALLBACK(on_a_notify), NULL);
    gtk_box_pack_start(GTK_BOX(vb), G.a_notify, FALSE, FALSE, 0);
    G.a_store = gtk_list_store_new(2, G_TYPE_BOOLEAN, G_TYPE_STRING);
    tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(G.a_store));
    G.a_view = tv;
    rr = gtk_cell_renderer_toggle_new();
    g_signal_connect(rr, "toggled", G_CALLBACK(on_a_toggle), NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tv), gtk_tree_view_column_new_with_attributes("On", rr, "active", 0, NULL));
    rr = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(tv), gtk_tree_view_column_new_with_attributes("Alarm", rr, "text", 1, NULL));
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(tv)), "changed", G_CALLBACK(on_a_sel), NULL);
    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(sw), tv);
    gtk_widget_set_size_request(sw, -1, 170);
    gtk_box_pack_start(GTK_BOX(vb), sw, FALSE, FALSE, 0);
    hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    b = gtk_menu_button_new();
    gtk_button_set_label(GTK_BUTTON(b), "Add an alarm...");
    menu = gtk_menu_new();
    for (i = 0; i < 6; i++) {
        GtkWidget *mi = gtk_menu_item_new_with_label(tmpl[i]);
        g_signal_connect(mi, "activate", G_CALLBACK(on_a_add), GINT_TO_POINTER(i));
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
    }
    gtk_widget_show_all(menu);
    gtk_menu_button_set_popup(GTK_MENU_BUTTON(b), menu);
    gtk_box_pack_start(GTK_BOX(hb), b, FALSE, FALSE, 0);
    b = gtk_button_new_with_mnemonic("Re_move");
    g_signal_connect(b, "clicked", G_CALLBACK(on_a_remove), NULL);
    gtk_box_pack_start(GTK_BOX(hb), b, FALSE, FALSE, 0);
    b = gtk_button_new_with_mnemonic("_Test notification");
    g_signal_connect(b, "clicked", G_CALLBACK(on_a_test), NULL);
    gtk_box_pack_start(GTK_BOX(hb), b, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vb), hb, FALSE, FALSE, 0);
    g = gtk_grid_new();
    G.a_form = g;
    gtk_grid_set_column_spacing(GTK_GRID(g), 8);
    gtk_grid_set_row_spacing(GTK_GRID(g), 6);
    G.a_type = gtk_combo_box_text_new();
    for (i = 0; i < PA_N; i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(G.a_type), pm_alarm_name(i));
    gtk_grid_attach(GTK_GRID(g), lbl("When", NULL, 1), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(g), G.a_type, 1, 0, 2, 1);
    G.a_limit = gtk_spin_button_new_with_range(0, 1e9, 1);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(G.a_limit), 2);
    gtk_grid_attach(GTK_GRID(g), lbl("Limit", NULL, 1), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(g), G.a_limit, 1, 1, 1, 1);
    G.a_unit = gtk_combo_box_text_new();
    gtk_grid_attach(GTK_GRID(g), G.a_unit, 2, 1, 1, 1);
    G.a_secs = gtk_spin_button_new_with_range(1, 3600, 1);
    gtk_grid_attach(GTK_GRID(g), lbl("For / over", NULL, 1), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(g), G.a_secs, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(g), lbl("seconds (power right now) or minutes (average)", "dim-label", 0), 2, 2, 1, 1);
    G.a_conv = wrap_lbl("", "dim-label");
    gtk_grid_attach(GTK_GRID(g), G.a_conv, 0, 3, 3, 1);
    g_signal_connect(G.a_type, "changed", G_CALLBACK(on_a_form), NULL);
    g_signal_connect(G.a_unit, "changed", G_CALLBACK(on_a_form), NULL);
    g_signal_connect(G.a_limit, "value-changed", G_CALLBACK(on_a_form), NULL);
    g_signal_connect(G.a_secs, "value-changed", G_CALLBACK(on_a_form), NULL);
    gtk_box_pack_start(GTK_BOX(vb), frame("The selected alarm", g), FALSE, FALSE, 0);
    G.a_last = wrap_lbl("", NULL);
    gtk_box_pack_start(GTK_BOX(vb), frame("Last alarm", G.a_last), FALSE, FALSE, 0);
    return scrolled(vb);
}

/* ====================================================================== Sources tab */
static void on_open_item(GtkButton *b, gpointer d) { (void)b; if (G.open_item) G.open_item(d); }

static GtkWidget *build_sources(void)
{
    GtkWidget *vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10), *hb, *b;
    GString *g = g_string_new(NULL);
    int i;
    gtk_container_set_border_width(GTK_CONTAINER(vb), 14);
    for (i = 0; i < G.src.nlines; i++) g_string_append_printf(g, "%s\xe2\x80\xa2 %s", i ? "\n\n" : "", G.src.lines[i]);
    G.src_lbl = wrap_lbl(g->str, NULL);
    g_string_free(g, TRUE);
    gtk_label_set_selectable(GTK_LABEL(G.src_lbl), TRUE);
    gtk_widget_set_can_focus(G.src_lbl, FALSE);   /* a focused selectable label selects all of its text */
    gtk_box_pack_start(GTK_BOX(vb), frame("What SRS can measure on this PC", G.src_lbl), FALSE, FALSE, 0);
    G.src_raw = lbl("", "pw-mono", 0);
    gtk_label_set_selectable(GTK_LABEL(G.src_raw), TRUE);
    gtk_widget_set_can_focus(G.src_raw, FALSE);
    gtk_box_pack_start(GTK_BOX(vb), frame("Raw readings (source, file, watts)", G.src_raw), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vb), wrap_lbl("SRS never changes who may read these files. On Linux 5.10 and later the CPU's powercap "
                                             "energy counter is root-only on purpose (PLATYPUS, CVE-2020-8694/8695/12912); the zenergy "
                                             "driver offers the same AMD counter safely to normal users.", "dim-label"), FALSE, FALSE, 0);
    hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    b = gtk_button_new_with_mnemonic("Open _Power status (expert item)");
    g_signal_connect(b, "clicked", G_CALLBACK(on_open_item), (gpointer)"power");
    gtk_box_pack_start(GTK_BOX(hb), b, FALSE, FALSE, 0);
    b = gtk_button_new_with_mnemonic("Open _kWh cost (expert item)");
    g_signal_connect(b, "clicked", G_CALLBACK(on_open_item), (gpointer)"kwh-cost");
    gtk_box_pack_start(GTK_BOX(hb), b, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vb), hb, FALSE, FALSE, 0);
    return scrolled(vb);
}

/* ====================================================================== page */
static void show_disclaimer(GtkWidget *parent, int first)
{
    GtkWidget *dlg = gtk_dialog_new_with_buttons("Power & cost: estimates only", GTK_WINDOW(gtk_widget_get_toplevel(parent)),
                                                 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, first ? "I _understand" : "_Close",
                                                 GTK_RESPONSE_ACCEPT, NULL);
    GtkWidget *l = gtk_label_new(NULL), *ca = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_label_set_markup(GTK_LABEL(l), g_disclaimer);
    gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(l), 80);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_widget_set_margin_start(l, 16); gtk_widget_set_margin_end(l, 16); gtk_widget_set_margin_top(l, 16); gtk_widget_set_margin_bottom(l, 16);
    gtk_box_pack_start(GTK_BOX(ca), l, TRUE, TRUE, 0);
    gtk_widget_show_all(dlg);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT && first) { G.disclaimer_ok = 1; cfg_save(); }
    gtk_widget_destroy(dlg);
}

static gboolean first_note_idle(gpointer d)
{
    (void)d;
    if (G.page && !G.disclaimer_ok && gtk_widget_get_mapped(G.page)) show_disclaimer(G.page, 1);
    return G_SOURCE_REMOVE;
}

static void on_page_map(GtkWidget *w, gpointer d)
{
    (void)w; (void)d;
    if (!G.disclaimer_ok) g_idle_add(first_note_idle, NULL);
    hist_reload();
    ui_now();
    ui_slow();
}

static void on_tab_switch(GtkNotebook *nb, GtkWidget *p, guint n, gpointer d)
{
    (void)nb; (void)p; (void)d;
    if (n == 1) hist_reload();
    if (n == 2) { sess_reload(); sess_now_text(); }
    if (n == 4) bg_status();
}

static void on_page_destroy(GtkWidget *w, gpointer d)
{
    (void)w; (void)d;
    if (G.timer) { g_source_remove(G.timer); G.timer = 0; }
    rec_end();
    pdb_close();
    if (G.lockfd > 0) { close(G.lockfd); G.lockfd = 0; }
    G.page = NULL;
}

static void css(void)
{
    static int done;
    GtkCssProvider *p;
    if (done) return;
    done = 1;
    p = gtk_css_provider_new();
    gtk_css_provider_load_from_data(p,
        ".pw-big { font-size: 26pt; font-weight: bold; }\n"
        ".pw-sub { font-size: 11pt; }\n"
        ".pw-cost { font-size: 12pt; }\n"
        ".pw-today { font-weight: bold; }\n"
        ".pw-mono { font-family: monospace; }\n"
        ".pw-note { background-color: rgba(255, 193, 7, 0.22); border: 1px solid rgba(255, 160, 0, 0.6); border-radius: 6px; padding: 6px 10px; }\n"
        "button.pw-hour, button.pw-hour:hover { background-image: none; min-width: 20px; min-height: 18px; padding: 0; }\n"
        ".pw-tou0, .pw-tou0:hover, .pw-tou0:disabled { background-image: none; background-color: #3a8a4a; }\n"
        ".pw-tou1, .pw-tou1:hover, .pw-tou1:disabled { background-image: none; background-color: #3d7fd6; }\n"
        ".pw-tou2, .pw-tou2:hover, .pw-tou2:disabled { background-image: none; background-color: #e08a1e; }\n"
        ".pw-tou3, .pw-tou3:hover, .pw-tou3:disabled { background-image: none; background-color: #c8323c; }\n",
        -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(p), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(p);
}

GtkWidget *power_page_new(void (*open_item)(const char *id))
{
    GtkWidget *vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0), *head = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8), *tr, *v, *note, *b;
    G.open_item = open_item;
    css();
    rec_start_runtime();
    gtk_widget_set_margin_start(head, 20);
    gtk_widget_set_margin_end(head, 20);
    gtk_widget_set_margin_top(head, 16);
    gtk_widget_set_margin_bottom(head, 8);
    tr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    {
        const char *icons[] = { "battery-level-100-charged-symbolic", "power-profile-balanced-symbolic", "battery-full-symbolic", NULL };
        const char *ic = "battery-full-symbolic";
        int i;
        for (i = 0; icons[i]; i++) if (gtk_icon_theme_has_icon(gtk_icon_theme_get_default(), icons[i])) { ic = icons[i]; break; }
        gtk_box_pack_start(GTK_BOX(tr), gtk_image_new_from_icon_name(ic, GTK_ICON_SIZE_DND), FALSE, FALSE, 0);
    }
    v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_pack_start(GTK_BOX(v), lbl("Power & cost", "srs-h1", 0), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(v), wrap_lbl("How much power this PC uses, what it costs, and its history. The processor and graphics "
                                            "card are measured; the rest is estimated, so every figure is a range.", "dim-label"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tr), v, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(head), tr, FALSE, FALSE, 0);
    note = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(note), "pw-note");
    gtk_box_pack_start(GTK_BOX(note), gtk_image_new_from_icon_name("dialog-warning-symbolic", GTK_ICON_SIZE_BUTTON), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(note), wrap_lbl(g_disclaimer_short, NULL), TRUE, TRUE, 0);
    b = gtk_button_new_with_mnemonic("Read the _note");
    g_signal_connect(b, "clicked", G_CALLBACK(on_disclaimer_btn), NULL);
    gtk_box_pack_end(GTK_BOX(note), b, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(head), note, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vb), head, FALSE, FALSE, 0);

    G.nb = gtk_notebook_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(G.nb), "srs-nb");
    gtk_notebook_set_show_border(GTK_NOTEBOOK(G.nb), FALSE);
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(G.nb), TRUE);
    gtk_notebook_append_page(GTK_NOTEBOOK(G.nb), build_now(), gtk_label_new("Now"));
    gtk_notebook_append_page(GTK_NOTEBOOK(G.nb), build_history(), gtk_label_new("History"));
    gtk_notebook_append_page(GTK_NOTEBOOK(G.nb), build_sessions(), gtk_label_new("Sessions"));
    gtk_notebook_append_page(GTK_NOTEBOOK(G.nb), build_price(), gtk_label_new("Price"));
    gtk_notebook_append_page(GTK_NOTEBOOK(G.nb), build_setup(), gtk_label_new("Setup"));
    gtk_notebook_append_page(GTK_NOTEBOOK(G.nb), build_alarms(), gtk_label_new("Alarms"));
    gtk_notebook_append_page(GTK_NOTEBOOK(G.nb), build_sources(), gtk_label_new("Sources"));
    g_signal_connect(G.nb, "switch-page", G_CALLBACK(on_tab_switch), NULL);
    gtk_box_pack_start(GTK_BOX(vb), G.nb, TRUE, TRUE, 0);
    G.page = vb;

    /* fill the controls from the settings */
    G.loading = 1;
    gtk_combo_box_set_active(GTK_COMBO_BOX(G.rec_combo), G.mode);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(G.rec_n), G.until_n);
    gtk_combo_box_set_active(GTK_COMBO_BOX(G.rec_unit), G.until_unit);
    gtk_widget_set_sensitive(G.rec_n, G.mode == 1);
    gtk_widget_set_sensitive(G.rec_unit, G.mode == 1);
    gtk_widget_set_sensitive(G.rec_start, G.mode == 1);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(G.a_notify), G.notify);
    G.loading = 0;
    price_fill();
    setup_fill();
    alarms_fill();
    if (G.nalarm) {
        GtkTreePath *p = gtk_tree_path_new_from_indices(0, -1);
        gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(G.a_view)), p);
        gtk_tree_path_free(p);
    }
    a_form_load();
    sess_fill();
    g_signal_connect(vb, "map", G_CALLBACK(on_page_map), NULL);
    g_signal_connect(vb, "destroy", G_CALLBACK(on_page_destroy), NULL);
    rec_tick();   /* first reading now; the next one gives the first measured interval */
    return vb;
}
