#!/usr/bin/env python3
"""Build SRS's offline electricity price and grid-CO2 tables from public data files.

  python3 -I tools/gen_power_rates.py DOWNLOAD_DIR
writes power_rates_data.inc (compiled into srs-gui) and data/power-zip-rates.tsv (installed to
/usr/share/srs-sysinfo/). Run from the repository root. It never downloads anything itself: fetch
the files below into DOWNLOAD_DIR first (see docs/USER-GUIDE.md, "Power & Cost", for the licences).

  eia_table_5_06_b.xlsx    https://www.eia.gov/electricity/monthly/xls/table_5_06_b.xlsx
                           U.S. EIA, Electric Power Monthly Table 5.6.B (year to date). U.S. government
                           work, public domain; EIA asks to be credited.
  eurostat_nrg_pc_204.json https://ec.europa.eu/eurostat/api/dissemination/statistics/1.0/data/nrg_pc_204?format=JSON&lang=en&nrg_cons=KWH2500-4999&unit=KWH&tax=I_TAX&currency=EUR&currency=NAC&lastTimePeriod=2
                           Eurostat, household prices, 2 500-4 999 kWh a year, all taxes included.
                           Reuse allowed with the source acknowledged (Eurostat copyright notice;
                           CC BY 4.0). SRS shows the figures unchanged.
  iou_zipcodes_2024.csv    https://data.openei.org/files/8563/iou_zipcodes_2024.csv
  non_iou_zipcodes_2024.csv https://data.openei.org/files/8563/non_iou_zipcodes_2024.csv
                           NREL, "U.S. Electric Utility Companies and Rates: Look-up by Zip Code (2024)",
                           from EIA-861 data. CC BY 4.0.
  ember_yearly.csv         https://files.ember-energy.org/public-downloads/generation/outputs/release_generation_yearly_global.csv
                           Ember, Yearly electricity data. CC BY 4.0.
The UK figure (Ofgem price cap, GB average, Direct Debit) is typed in below from
https://www.ofgem.gov.uk/your-energy-supply/your-energy-bill/energy-price-cap-unit-rates-and-standing-charges
"""
import csv
import json
import os
import sys

import openpyxl

OFGEM = {"period": "1 October to 31 December 2026", "p_kwh": 26.32, "p_day": 54.83,
         "fetched": "2026-10-26"}

STATES = {
    "Alabama": "AL", "Alaska": "AK", "Arizona": "AZ", "Arkansas": "AR", "California": "CA", "Colorado": "CO",
    "Connecticut": "CT", "Delaware": "DE", "District of Columbia": "DC", "Florida": "FL", "Georgia": "GA",
    "Hawaii": "HI", "Idaho": "ID", "Illinois": "IL", "Indiana": "IN", "Iowa": "IA", "Kansas": "KS",
    "Kentucky": "KY", "Louisiana": "LA", "Maine": "ME", "Maryland": "MD", "Massachusetts": "MA",
    "Michigan": "MI", "Minnesota": "MN", "Mississippi": "MS", "Missouri": "MO", "Montana": "MT",
    "Nebraska": "NE", "Nevada": "NV", "New Hampshire": "NH", "New Jersey": "NJ", "New Mexico": "NM",
    "New York": "NY", "North Carolina": "NC", "North Dakota": "ND", "Ohio": "OH", "Oklahoma": "OK",
    "Oregon": "OR", "Pennsylvania": "PA", "Rhode Island": "RI", "South Carolina": "SC",
    "South Dakota": "SD", "Tennessee": "TN", "Texas": "TX", "Utah": "UT", "Vermont": "VT",
    "Virginia": "VA", "Washington": "WA", "West Virginia": "WV", "Wisconsin": "WI", "Wyoming": "WY",
}


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def eia(d):
    ws = openpyxl.load_workbook(os.path.join(d, "eia_table_5_06_b.xlsx"), data_only=True).active
    rows = list(ws.iter_rows(values_only=True))
    period = rows[3][1]          # e.g. "August 2026 YTD"
    out = []
    for r in rows[4:]:
        name = (r[0] or "").strip()
        if name in STATES and isinstance(r[1], (int, float)):
            out.append((name, STATES[name], r[1] / 100.0))
        if name == "U.S. Total" and isinstance(r[1], (int, float)):
            us = r[1] / 100.0
    out.sort()
    return period, us, out


def eurostat(d):
    j = json.load(open(os.path.join(d, "eurostat_nrg_pc_204.json"), encoding="utf-8"))
    dims = j["id"]
    size = j["size"]
    cat = {k: j["dimension"][k]["category"] for k in dims}
    times = sorted(cat["time"]["index"], key=lambda t: cat["time"]["index"][t])

    def val(geo, cur, t):
        idx = 0
        for k, n in zip(dims, size):
            pos = {"geo": cat["geo"]["index"][geo], "currency": cat["currency"]["index"][cur],
                   "time": cat["time"]["index"][t]}.get(k, 0)
            idx = idx * n + pos
        return j["value"].get(str(idx))

    out = []
    for geo, label in cat["geo"]["label"].items():
        if geo.startswith("EU") or geo.startswith("EA"):
            continue
        for t in reversed(times):     # newest half-year that has a value
            v = val(geo, "EUR", t)
            if v is not None:
                nac = val(geo, "NAC", t)
                out.append((label, geo, v, nac, t))
                break
    out.sort()
    eu = None
    for t in reversed(times):
        if "EU27_2020" in cat["geo"]["index"] and val("EU27_2020", "EUR", t) is not None:
            eu = (val("EU27_2020", "EUR", t), t)
            break
    return j.get("updated", ""), eu, out


def zips(d, outfile):
    util = {}
    z = {}
    for fn in ("iou_zipcodes_2024.csv", "non_iou_zipcodes_2024.csv"):
        for r in csv.DictReader(open(os.path.join(d, fn), encoding="utf-8")):
            try:
                rate = float(r["res_rate"] or 0)
            except ValueError:
                rate = 0
            key = (r["eiaid"], r["utility_name"])
            if key not in util:
                util[key] = (len(util), r["state"], rate)
            uid = util[key][0]
            z.setdefault(r["zip"].zfill(5), [])
            if uid not in z[r["zip"].zfill(5)]:
                z[r["zip"].zfill(5)].append(uid)
    with open(outfile, "w", encoding="utf-8") as f:
        f.write("# SRS offline ZIP code -> electric utility table, made by tools/gen_power_rates.py\n")
        f.write("# Source: NREL, U.S. Electric Utility Companies and Rates: Look-up by Zip Code (2024), from EIA-861 data,\n")
        f.write("# https://catalog.data.gov/dataset/u-s-electric-utility-companies-and-rates-look-up-by-zip-code-2024\n")
        f.write("# Licence: Creative Commons Attribution 4.0 (CC BY 4.0). Reformatted (one line per utility and per ZIP code);\n")
        f.write("# the rates are the utility's average residential price in USD per kWh, not a price for that ZIP code.\n")
        f.write("# U <id> <state> <residential USD/kWh> <utility name>   /   Z <zip> <utility ids>\n")
        for (eid, name), (uid, st, rate) in sorted(util.items(), key=lambda x: x[1][0]):
            f.write("U\t%d\t%s\t%.5f\t%s\n" % (uid, st, rate, name.replace("\t", " ")))
        for zc in sorted(z):
            f.write("Z\t%s\t%s\n" % (zc, ",".join(str(u) for u in z[zc])))
    return len(util), len(z)


def ember(d):
    lat = {}
    for r in csv.DictReader(open(os.path.join(d, "ember_yearly.csv"), encoding="utf-8")):
        if r["Electricity source"] != "Total generation" or not r["Emissions intensity (gCO2e/kWh)"]:
            continue
        if r["Area type"] != "Country or economy" and r["Area"] not in ("World", "EU"):
            continue
        k = r["Area"]
        y = int(r["Year"])
        if k not in lat or y > lat[k][0]:
            lat[k] = (y, float(r["Emissions intensity (gCO2e/kWh)"]))
    first = [k for k in ("World", "EU") if k in lat]
    rest = sorted(k for k in lat if k not in first)
    return [(k, lat[k][0], lat[k][1]) for k in first + rest]


def main():
    d = sys.argv[1]
    period, us, states = eia(d)
    upd, eu, countries = eurostat(d)
    nu, nz = zips(d, "data/power-zip-rates.tsv")
    co2 = ember(d)
    o = []
    o.append("/* power_rates_data.inc - made by tools/gen_power_rates.py; do not edit by hand.")
    o.append(" * Sources (fetched 2026-10-26):")
    o.append(" *  U.S. EIA, Electric Power Monthly Table 5.6.B, residential, %s (public domain, credit EIA)." % period)
    o.append(" *  Eurostat nrg_pc_204, households 2 500-4 999 kWh/yr, all taxes, updated %s (reuse with acknowledgement)." % upd)
    o.append(" *  Ofgem energy price cap, GB average, Direct Debit, %s (Ofgem website)." % OFGEM["period"])
    o.append(" *  Ember, Yearly electricity data, emissions intensity (CC BY 4.0).")
    o.append(" *  NREL ZIP code table: data/power-zip-rates.tsv (CC BY 4.0). */")
    o.append("static const char *const g_tables_date = %s;" % c_str(
        "US EIA %s; Eurostat %s; Ofgem %s; Ember up to %d" % (period, max(c[4] for c in countries) if countries else "?", OFGEM["period"],
                                                              max(c[1] for c in co2))))
    o.append("static const prt_row g_us[] = {")
    o.append("    { %s, %s, %.4f, 0 }," % (c_str("United States average"), c_str("US"), us))
    for name, ab, p in states:
        o.append("    { %s, %s, %.4f, 0 }," % (c_str(name), c_str(ab), p))
    o.append("};")
    o.append("static const char *const g_us_note = %s;" % c_str(
        "U.S. EIA Electric Power Monthly, average residential price, %s, taxes as billed." % period))
    o.append("static const prt_row g_eu[] = {")
    if eu:
        o.append("    { %s, %s, %.4f, 0 }," % (c_str("European Union average"), c_str("EU27"), eu[0]))
    newest = max(c[4] for c in countries) if countries else ""
    for label, geo, v, nac, t in countries:
        name = label if t == newest else "%s (%s)" % (label, t)   # an older half-year says so
        o.append("    { %s, %s, %.4f, 0 }," % (c_str(name), c_str(geo), v))
    o.append("};")
    o.append("static const char *const g_eu_note = %s;" % c_str(
        "Eurostat nrg_pc_204, average household price (2 500-4 999 kWh a year), all taxes and levies included, %s "
        "(older half-year shown after the name), in EUR." % (max(c[4] for c in countries) if countries else "")))
    o.append("static const prt_row g_uk[] = {")
    o.append("    { %s, %s, %.4f, %.4f }," % (c_str("Great Britain average (price cap, Direct Debit)"), c_str("GB"),
                                             OFGEM["p_kwh"] / 100, OFGEM["p_day"] / 100))
    o.append("};")
    o.append("static const char *const g_uk_note = %s;" % c_str(
        "Ofgem energy price cap, %s: the most a default tariff may charge; GB average for Direct Debit. Your region and "
        "payment method change it; fixed tariffs differ." % OFGEM["period"]))
    o.append("static const prt_co2row g_co2[] = {")
    for k, y, v in co2:
        o.append("    { %s, %d, %.1f }," % (c_str(k), y, v))
    o.append("};")
    o.append("#define PRT_ZIP_UTILITIES %d" % nu)
    o.append("#define PRT_ZIP_CODES %d" % nz)
    open("power_rates_data.inc", "w", encoding="utf-8").write("\n".join(o) + "\n")
    print("states %d, EU countries %d, CO2 areas %d, utilities %d, ZIP codes %d" % (len(states), len(countries), len(co2), nu, nz))


if __name__ == "__main__":
    main()
