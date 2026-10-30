/* mem_timings.c - DDR5 memory timings (2.4); see mem_timings.h.
 *
 * UMC register map (AMD Zen 4 / Zen 5, DDR5): register offsets and bit fields are facts taken from ZenStates-Core
 * (Ivan Rusanov, github.com/irusanov/ZenStates-Core, the library behind ZenTimings) and cross-checked with memtest86+
 * (Sam Demeulemeester, system/imc/x86/amd_zen.c); no code was copied. Checked on a Ryzen 7 7800X3D with DDR5-6000
 * EXPO (30-38-38-96, GDM on, 1T).
 * SPD layout: JEDEC JESD400-5 (DDR5 SPD), AMD EXPO and Intel XMP 3.0 blocks as described by ZenStates-Core and
 * i2c-tools' decode-dimms. Clock counts use the JEDEC rounding (time x 0.997 / tCK, rounded up). */
#include "mem_timings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int mt_umc_parse(const char *text, mt_umc ch[MT_NCH])
{
    const char *p = text;
    int i, n = 0;
    memset(ch, 0, sizeof(mt_umc) * MT_NCH);
    while (p && *p) {
        int c;
        unsigned off, v;
        if (sscanf(p, "ch%d %x %x", &c, &off, &v) == 3 && c >= 0 && c < MT_NCH && off >= 0x50000 && off < 0x50000 + MT_NREG * 4 && !(off & 3)) {
            ch[c].reg[(off - 0x50000) / 4] = v;
            ch[c].have[(off - 0x50000) / 4] = 1;
        }
        p = strchr(p, '\n');
        if (p) p++;
    }
    for (i = 0; i < MT_NCH; i++) {   /* a UMC that exists reads real values; a missing one reads all ones */
        unsigned cfg, d0, d1, mx;
        if (!mt_reg(&ch[i], 0x50200, &cfg) || cfg == 0xFFFFFFFFu || !(cfg & 0xFFFF)) continue;
        if (!mt_reg(&ch[i], 0x50000, &d0) || !mt_reg(&ch[i], 0x50008, &d1)) continue;
        if (mt_reg(&ch[i], 0x50DF0, &mx) && ((mx >> 19) & 1)) continue;   /* channel disabled */
        if (!(d0 & 1) && !(d1 & 1)) continue;                              /* no DIMM on it */
        ch[i].present = 1;
        n++;
    }
    return n;
}

int mt_reg(const mt_umc *c, unsigned off, unsigned *v)
{
    unsigned k;
    if (off < 0x50000 || off >= 0x50000 + MT_NREG * 4) return 0;
    k = (off - 0x50000) / 4;
    if (!c->have[k]) return 0;
    *v = c->reg[k];
    return 1;
}

int mt_field(const mt_umc *c, unsigned off, int hi, int lo)
{
    unsigned v;
    if (!mt_reg(c, off, &v)) return -1;
    return (int)((v >> lo) & (hi - lo >= 31 ? 0xFFFFFFFFu : ((1u << (hi - lo + 1)) - 1)));
}

int mt_umc_mts(const mt_umc *c)
{
    int r = mt_field(c, 0x50200, 15, 0);
    return r > 0 ? (int)(r / 100.0 * 100 * 2 + 0.5) : 0;
}

void mt_umc_rfc(const mt_umc *c, int *rfc, int *rfc2, int *rfcsb)
{
    static const unsigned rr[4] = { 0x50260, 0x50264, 0x50268, 0x5026C }, sb[4] = { 0x502C0, 0x502C4, 0x502C8, 0x502CC };
    unsigned v;
    int i;
    *rfc = *rfc2 = *rfcsb = -1;
    for (i = 0; i < 4; i++)
        if (mt_reg(c, rr[i], &v) && v && v != 0x00C00138u) { *rfc = (int)(v & 0xFFFF); *rfc2 = (int)(v >> 16); break; }
    for (i = 0; i < 4; i++)
        if (mt_reg(c, sb[i], &v) && (v & 0x7FF)) { *rfcsb = (int)(v & 0x7FF); break; }
}

/* display order as in ZenTimings: primary, secondary, turnarounds, the rest. ns = 1: also shown in nanoseconds */
static const mt_def g_defs[] = {
    { "tCL", "Primary", "CAS latency: clocks from a read command to the first data", 0x50204, 5, 0, 1 },
    { "tRCDRD", "Primary", "row open (activate) to a read", 0x50204, 21, 16, 1 },
    { "tRCDWR", "Primary", "row open (activate) to a write", 0x50204, 29, 24, 1 },
    { "tRP", "Primary", "row close (precharge) time", 0x50208, 21, 16, 1 },
    { "tRAS", "Primary", "shortest time a row stays open", 0x50204, 14, 8, 1 },
    { "tRC", "Primary", "row cycle: activate to the next activate of the same bank", 0x50208, 7, 0, 1 },
    { "tRRDS", "Secondary", "activate to activate, different bank group", 0x5020C, 4, 0, 0 },
    { "tRRDL", "Secondary", "activate to activate, same bank group", 0x5020C, 12, 8, 0 },
    { "tFAW", "Secondary", "four-activate window", 0x50210, 7, 0, 0 },
    { "tWTRS", "Secondary", "write to read, different bank group", 0x50214, 12, 8, 0 },
    { "tWTRL", "Secondary", "write to read, same bank group", 0x50214, 22, 16, 0 },
    { "tWR", "Secondary", "write recovery before a precharge", 0x50218, 7, 0, 1 },
    { "tRTP", "Secondary", "read to precharge", 0x5020C, 28, 24, 0 },
    { "tCWL", "Secondary", "CAS write latency", 0x50214, 5, 0, 0 },
    { "tREFI", "Secondary", "time between refreshes (higher = fewer refreshes)", 0x50230, 15, 0, 1 },
    { "tRDRDSCL", "Turnaround", "read to read, same bank group", 0x50220, 29, 24, 0 },
    { "tRDRDSC", "Turnaround", "read to read, same chip select", 0x50220, 19, 16, 0 },
    { "tRDRDSD", "Turnaround", "read to read, same DIMM", 0x50220, 11, 8, 0 },
    { "tRDRDDD", "Turnaround", "read to read, different DIMM", 0x50220, 3, 0, 0 },
    { "tWRWRSCL", "Turnaround", "write to write, same bank group", 0x50224, 29, 24, 0 },
    { "tWRWRSC", "Turnaround", "write to write, same chip select", 0x50224, 19, 16, 0 },
    { "tWRWRSD", "Turnaround", "write to write, same DIMM", 0x50224, 11, 8, 0 },
    { "tWRWRDD", "Turnaround", "write to write, different DIMM", 0x50224, 3, 0, 0 },
    { "tRDWR", "Turnaround", "read to write", 0x50228, 13, 8, 0 },
    { "tWRRD", "Turnaround", "write to read (different rank)", 0x50228, 3, 0, 0 },
    { "tCKE", "Other", "clock enable", 0x50254, 28, 24, 0 },
    { "tXP", "Other", "power-down exit", 0x50254, 5, 0, 0 },
    { "tCPDED", "Other", "command pass disable delay", 0x50254, 20, 16, 0 },
    { "tPD", "Other", "power-down entry", 0x50244, 4, 0, 0 },
    { "tCSH", "Other", "chip select hold", 0x50244, 25, 20, 0 },
    { "tMOD", "Other", "mode register set to other commands", 0x50234, 13, 8, 0 },
    { "tMRD", "Other", "mode register set to mode register set", 0x50234, 5, 0, 0 },
    { "tPPD", "Other", "precharge to precharge", 0x5021C, 2, 0, 0 },
    { "tSTAG", "Other", "refresh stagger", 0x50250, 26, 16, 0 },
    { "tSTAGsb", "Other", "same-bank refresh stagger", 0x50250, 8, 0, 0 },
    { "tPHYWRD", "PHY", "PHY write data delay", 0x50258, 26, 24, 0 },
    { "tPHYWRL", "PHY", "PHY write latency", 0x50258, 15, 8, 0 },
    { "tPHYRDL", "PHY", "PHY read latency", 0x50258, 23, 16, 0 },
};

const mt_def *mt_defs(int *n)
{
    *n = (int)(sizeof g_defs / sizeof g_defs[0]);
    return g_defs;
}

/* ---------------------------------------------------------------- SPD */
static int u16(const unsigned char *d, size_t n, size_t o) { return o + 1 < n ? d[o] | d[o + 1] << 8 : 0; }
/* JEDEC: clocks = time x 0.997 / tCK, rounded up (integer math as in JESD400-5) */
static int nck(int ps, int tck) { return ps > 0 && tck > 0 ? (int)(((long)ps * 997 / tck + 1000) / 1000) : 0; }
static int volt_mv(int code) { return ((code >> 5) & 7) * 1000 + (code & 0x1F) * 50; }
static int mts_of(int tck) { return tck > 0 ? (int)(2000000.0 / tck / 100.0 + 0.5) * 100 : 0; }
static void prof_times(mt_prof *p, const unsigned char *d, size_t n, size_t t, int tck)
{
    p->mts = mts_of(tck);
    p->cl = nck(u16(d, n, t), tck);
    if (p->cl & 1) p->cl++;                           /* DDR5 has even CAS latencies only */
    p->rcd = nck(u16(d, n, t + 2), tck);
    p->rp = nck(u16(d, n, t + 4), tck);
    p->ras = nck(u16(d, n, t + 6), tck);
    p->rc = nck(u16(d, n, t + 8), tck);
    if (p->rc < p->ras + p->rp) p->rc = p->ras + p->rp;
    p->wr = nck(u16(d, n, t + 10), tck);
    p->rfc1ns = u16(d, n, t + 12);
    p->rfc2ns = u16(d, n, t + 14);
    p->rfcsbns = u16(d, n, t + 16);
    p->valid = p->mts > 0;
}

void mt_spd_decode(const unsigned char *d, size_t n, mt_spd *s)
{
    static const int dens[] = { 0, 4, 8, 12, 16, 24, 32, 48, 64 };
    static const int dies[] = { 1, 0, 2, 4, 8, 16 };
    int i;
    memset(s, 0, sizeof *s);
    if (n < 1024 || d[2] != 0x12) return;
    s->ok = 1;
    s->density_gb = (d[4] & 0x1F) < 9 ? dens[d[4] & 0x1F] : 0;
    s->dies = (d[4] >> 5) < 6 ? dies[d[4] >> 5] : 0;
    s->width = 4 << ((d[6] >> 5) & 3);
    s->ranks = ((d[234] >> 3) & 7) + 1;
    s->subch = ((d[235] >> 5) & 3) + 1;              /* byte 235: [6:5] subchannels, [4:3] ECC width, [2:0] bus width */
    s->buswidth = 8 << (d[235] & 7);
    if (s->width && s->density_gb && s->dies)
        s->size_gb = (double)s->subch * s->buswidth / s->width * s->density_gb / 8.0 * s->ranks * s->dies;
    s->mfr = (unsigned)(d[512] & 0x7F) << 8 | (d[513] & 0x7F);
    s->dram_mfr = (unsigned)(d[552] & 0x7F) << 8 | (d[553] & 0x7F);
    if (d[515] && d[515] != 0xFF) { s->year = 2000 + (d[515] >> 4) * 10 + (d[515] & 15); s->week = (d[516] >> 4) * 10 + (d[516] & 15); }
    for (i = 0; i < 30 && d[521 + i] >= 0x20 && d[521 + i] < 0x7F; i++) s->part[i] = (char)d[521 + i];
    while (i > 0 && s->part[i - 1] == ' ') s->part[--i] = 0;
    /* JEDEC base profile: tCKAVGmin at 20, tAA at 30, then tRCD, tRP, tRAS, tRC, tWR (ps), tRFC1, tRFC2, tRFCsb (ns) */
    prof_times(&s->jedec, d, n, 30, u16(d, n, 20));
    s->jedec.vdd = s->jedec.vddq = 1100;              /* DDR5 nominal */
    s->jedec.vpp = 1800;
    /* AMD EXPO: "EXPO" at 832, revision 836, enabled profiles 837, two 40-byte profiles from 842 */
    if (!memcmp(d + 832, "EXPO", 4)) {
        int en = d[837], k;
        s->has_expo = 1;
        snprintf(s->expo_rev, sizeof s->expo_rev, "%d.%d", d[836] >> 4, d[836] & 15);
        for (k = 0; k < 2; k++) {
            size_t o = 842 + 40 * (size_t)k;
            int tck = u16(d, n, o + 4);
            if (!(k == 0 ? en & 1 : en & 0x0E) || tck <= 0) continue;
            prof_times(&s->expo[k], d, n, o + 6, tck);
            s->expo[k].vdd = volt_mv(d[o]);
            s->expo[k].vddq = volt_mv(d[o + 1]);
            s->expo[k].vpp = volt_mv(d[o + 2]);
            if (!s->expo[k].vdd) s->expo[k].valid = 0;
        }
    }
    /* Intel XMP 3.0: 0x0C 0x4A at 640, revision 642, enabled 643, names at 654 (16 bytes), 64-byte profiles from 704 */
    if (d[640] == 0x0C && d[641] == 0x4A) {
        int k;
        s->has_xmp = 1;
        snprintf(s->xmp_rev, sizeof s->xmp_rev, "%d.%d", d[642] >> 4, d[642] & 15);
        for (k = 0; k < 3; k++) {
            size_t o = 704 + 64 * (size_t)k;
            int tck = u16(d, n, o + 5), j;
            if (!((d[643] >> k) & 1) || tck <= 0) continue;
            if (s->has_expo && o + 64 > 832) continue;    /* EXPO sits where the third XMP profile would be */
            prof_times(&s->xmp[k], d, n, o + 13, tck);
            s->xmp[k].vpp = volt_mv(d[o]);
            s->xmp[k].vdd = volt_mv(d[o + 1]);
            s->xmp[k].vddq = volt_mv(d[o + 2]);
            for (j = 0; j < 16 && d[654 + 16 * k + j] >= 0x20 && d[654 + 16 * k + j] < 0x7F; j++) s->xmp[k].name[j] = (char)d[654 + 16 * k + j];
            while (j > 0 && s->xmp[k].name[j - 1] == ' ') s->xmp[k].name[--j] = 0;
        }
    }
}

/* the module and DRAM makers seen most on desktop DDR5; the full JEP106 list comes with "Update hardware lists" */
const char *mt_jep106(unsigned code)
{
    static const struct { unsigned c; const char *n; } t[] = {
        { 0x04E, "Samsung" }, { 0x02D, "SK Hynix" }, { 0x02C, "Micron" }, { 0x30B, "Nanya" },
        { 0x44D, "G.Skill" }, { 0x21E, "Corsair" }, { 0x118, "Kingston" }, { 0x51B, "Crucial" },
        { 0x46F, "Team Group" }, { 0x44B, "ADATA" },
    };
    size_t i;
    for (i = 0; i < sizeof t / sizeof t[0]; i++) if (t[i].c == code) return t[i].n;
    return NULL;
}

/* ---------------------------------------------------------------- AOD table
   Ryzen 7000 (Raphael) layout from ZenStates-Core (AodDataDictionaryV1): 32-bit values from MemClk at byte 8924:
   MemClk, tCL, tRCD, tRP, tRAS, tRC, tWR, tRFC, tRFC2, tRFCsb, tRTP, tRRD_L, tRRD_S, tFAW, tWTR_L, tWTR_S, eight
   turnarounds, tWRRD, tRDWR, then CAD bus / proc data / ProcODT / DRAM data drive strengths, RttNomWr, RttNomRd, RttWr,
   RttPark, RttParkDqs (MemClk + 104 .. + 136) and the memory VDD, VDDQ, VPP and CPU VDDIO in mV (MemClk + 172 .. + 184).
   The block is found at 8924, else at the last place the MEMCLK value appears, and used only when its timings match the
   live ones. Checked on a Ryzen 7 7800X3D (ASUS BIOS 1805, AGESA 1.2.x). */
static int i32(const unsigned char *d, size_t n, size_t o) { int v; if (o + 4 > n) return -1; memcpy(&v, d + o, 4); return v; }
int mt_aod_decode(const unsigned char *d, size_t n, int memclk, int cl, int rcd, mt_aod *a)
{
    size_t cand[2] = { 8924, 0 }, k;
    int j;
    memset(a, 0, sizeof *a);
    if (!d || n < 9200 || memclk <= 0) return 0;
    for (k = n - 4; k > 0; k--) if (d[k] == (memclk & 0xFF) && d[k + 1] == ((memclk >> 8) & 0xFF) && !d[k + 2] && !d[k + 3]) { cand[1] = k; break; }
    for (j = 0; j < 2; j++) {
        size_t b = cand[j];
        if (!b || b + 188 > n || i32(d, n, b) != memclk || i32(d, n, b + 4) != cl || i32(d, n, b + 8) != rcd) continue;
        a->ok = 1; a->base = (int)b;
        a->memclk = memclk; a->cl = cl; a->rcd = rcd; a->rp = i32(d, n, b + 12); a->ras = i32(d, n, b + 16);
        a->cadbus = i32(d, n, b + 104); a->procdrv = i32(d, n, b + 108); a->procodt = i32(d, n, b + 112); a->dramdrv = i32(d, n, b + 116);
        a->rttnomwr = i32(d, n, b + 120); a->rttnomrd = i32(d, n, b + 124); a->rttwr = i32(d, n, b + 128);
        a->rttpark = i32(d, n, b + 132); a->rttparkdqs = i32(d, n, b + 136);
        a->vdd = i32(d, n, b + 172); a->vddq = i32(d, n, b + 176); a->vpp = i32(d, n, b + 180); a->vddio = i32(d, n, b + 184);
        return 1;
    }
    return 0;
}

/* code -> meaning (ZenStates-Core EncodedValueDictionaries; RZQ = 240 ohm) */
void mt_aod_text(int kind, int code, char *b, size_t n)
{
    static const struct { int c; const char *t; } odt[] = {
        { 0, "Hi-Z" }, { 1, "480.0" }, { 2, "240.0" }, { 3, "160.0" }, { 4, "120.0" }, { 5, "96.0" }, { 6, "80.0" }, { 7, "68.6" },
        { 12, "60.0" }, { 13, "53.3" }, { 14, "48.0" }, { 15, "43.6" }, { 28, "40.0" }, { 29, "36.9" }, { 30, "34.3" }, { 31, "32.0" },
        { 60, "30.0" }, { 61, "28.2" }, { 62, "26.7" }, { 63, "25.3" } };
    static const char *const dram[] = { "34.0", "40.0", "48.0" };
    size_t i;
    if (kind == 0 || kind == 1) {   /* ProcODT and proc data drive strength share the code table */
        for (i = 0; i < sizeof odt / sizeof odt[0]; i++)
            if (odt[i].c == code && (kind == 0 || !(code & 1) || code == 0)) { snprintf(b, n, "%s%s", odt[i].t, code ? " Ω" : ""); return; }
    } else if (kind == 2) {
        if (code >= 0 && code <= 2) { snprintf(b, n, "%s Ω", dram[code]); return; }
    } else if (kind == 3) {
        if (code == 0) { snprintf(b, n, "Hi-Z"); return; }
        if (code == 30 || code == 40 || code == 60 || code == 120) { snprintf(b, n, "%d.0 Ω", code); return; }
    } else if (kind == 4) {
        if (code == 0) { snprintf(b, n, "Off"); return; }
        if (code >= 1 && code <= 7) { snprintf(b, n, "RZQ/%d (%.0f Ω)", code, 240.0 / code); return; }
    }
    snprintf(b, n, "code %d", code);
}
