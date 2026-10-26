/* power_db.h - SRS "Power & cost" history in SQLite (~/.local/share/srs/power.db).
 * The only part of SRS that links SQLite. One row per minute, plus gaps and a few settings.
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#ifndef SRS_POWER_DB_H
#define SRS_POWER_DB_H

#include "power_model.h"

typedef struct {
    double secs_on, secs_sleep, secs_off;     /* seconds of the minute recorded awake / asleep / off */
    double wh_mid, wh_lo, wh_hi;              /* at the wall: estimate and its range */
    double wh_cpu, wh_gpu;                    /* measured parts (DC) */
    double cost;                              /* priced at the rate in force at the time */
    double w_max;
    int flags;                                /* PM_F_* bits seen in the minute */
} pdb_row;

typedef struct {
    long t;                                   /* first minute in the group (UTC seconds) */
    char label[40];                           /* local time: "2026-10-26", "2026-10", ... */
    double wh_mid, wh_lo, wh_hi, wh_cpu, wh_gpu, cost, secs_on, secs_sleep, secs_off, w_max;
} pdb_sum;

enum { PDB_MINUTE, PDB_HOUR, PDB_DAY, PDB_WEEK, PDB_MONTH, PDB_YEAR };
enum { PDB_GAP_ASLEEP, PDB_GAP_OFF, PDB_GAP_NOREC, PDB_GAP_STALL };

int pdb_open(const char *path);               /* 0 ok; creates the file (mode 0600) and folder (0700) */
void pdb_close(void);
int pdb_is_open(void);
const char *pdb_error(void);
const char *pdb_path(void);

int pdb_add(long minute, const pdb_row *r);   /* adds onto the minute's row (several writers per minute add up) */
int pdb_flush(void);                          /* commit what pdb_add wrote */
int pdb_gap(long t0, long t1, int kind);
int pdb_count_gaps(long from, long to);
int pdb_gaps(long from, long to, long *t0, long *t1, int *kind, int max);

int pdb_group(int group, long from, long to, pdb_sum *out, int max);
double pdb_sum_range(long from, long to, double *cost);
long pdb_first_minute(void);
long pdb_minutes_count(void);

int pdb_meta_get(const char *key, char *out, int sz);
int pdb_meta_set(const char *key, const char *val);

/* CSV with `note` lines (each prefixed "# ") on top; returns rows written or -1 */
int pdb_export_csv(const char *file, int group, long from, long to, const char *note, const char *currency, const char *decimal);
/* price every minute from `from` again with tariff t (month totals in local time); returns minutes changed */
long pdb_reprice(const pm_tariff *t, long from);
int pdb_delete_all(void);

#endif
