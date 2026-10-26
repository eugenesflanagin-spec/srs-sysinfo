/* power_rates.h - SRS "Power & cost": offline electricity price and grid-CO2 tables.
 *
 * Built into SRS (power_rates_data.inc, made by tools/gen_power_rates.py from public data; sources,
 * dates and licences in that file's header and in the user guide). The US ZIP -> utility table is
 * a data file (power-zip-rates.tsv) looked up next to the program or in the system data folders.
 * Nothing is fetched from the network.
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#ifndef SRS_POWER_RATES_H
#define SRS_POWER_RATES_H

typedef struct {
    char name[160];
    double price;          /* per kWh, taxes included where the source includes them */
    char currency[8];
    double standing_day;   /* per day, 0 = not given */
    char note[400];        /* source, period and what the number includes */
} prt_rate;

typedef struct {
    char name[120];
    double g_per_kwh;
    char source[200];
} prt_co2;

int prt_country_count(void);
const char *prt_country_name(int c);
int prt_region_count(int c);
const char *prt_region_name(int c, int r);
int prt_get(int c, int r, prt_rate *out);
int prt_has_zip(int c);
/* US ZIP code -> index of its state in the US table (or -1); util/price: the utility serving it
   and its average residential price from the NREL table, when the data file is installed */
int prt_zip_lookup(const char *zip, char *util, int usz, double *price);
const char *prt_zip_source(void);
const char *prt_tables_date(void);
/* the country to pre-select, from the system time zone (/etc/localtime); no network */
int prt_guess_country(void);

int prt_co2_count(void);
int prt_co2_get(int i, prt_co2 *out);

#endif
