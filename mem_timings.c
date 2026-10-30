/* mem_timings.c - DDR5 memory timings (2.4); see mem_timings.h.
 *
 * UMC register map (AMD Zen 4 / Zen 5, DDR5): register offsets and bit fields are facts taken from ZenStates-Core
 * (Ivan Rusanov, github.com/irusanov/ZenStates-Core, the library behind ZenTimings) and cross-checked with memtest86+
 * (Sam Demeulemeester, system/imc/x86/amd_zen.c); no code was copied. Checked on a Ryzen 7 7800X3D with DDR5-6000
 * EXPO (30-38-38-96, GDM on, 1T).
 * SPD layout: JEDEC JESD400-5 (DDR5 SPD), AMD EXPO and Intel XMP 3.0 blocks as described by ZenStates-Core and
 * i2c-tools' decode-dimms. Clock counts use the JEDEC rounding (time x 0.997 / tCK, rounded up). */
#include "mem_timings.h"

#include <errno.h>
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

/* ---------------------------------------------------------------- Intel (2.4, experimental: no Intel PC to check on)
   The memory controller's register window (MCHBAR) holds the timings. Its address is in the host bridge's PCI config
   (00:00.0, 0x48 / 0x4C, bit 0 = window on). Layouts per generation from memtest86+ (system/imc/x86/intel_*.c, Sam
   Demeulemeester and contributors), facts only. SRS never turns the window on (memtest86+ does, with a write):
   when it is off, nothing is read. Generation by CPU model (family 6). */
typedef struct { const char *gen, *name; unsigned long long mask; unsigned window; const unsigned *regs; } mt_igen;
static const unsigned r_skl[] = { 0x5918, 0x5E00, 0x500C, 0x5010, 0x4000, 0x4070, 0x401C, 0x4400, 0x4470, 0x441C, 0 };
static const unsigned r_icl[] = { 0x5F60, 0x5E00, 0x5E04, 0x500C, 0x5010, 0x4000, 0x4070, 0x4400, 0x4470, 0 };
static const unsigned r_adl[] = { 0xD800, 0xD80C, 0x1D800, 0x1D80C, 0x5F60, 0x5918, 0x5E04, 0xE000, 0xE004, 0xE070,
                                  0x1E000, 0x1E004, 0x1E070, 0 };
static const unsigned r_mtl[] = { 0xD800, 0xD80C, 0xD810, 0x1D800, 0x1D80C, 0x1D810, 0x13D98, 0xE000, 0xE004, 0xE070, 0xE138,
                                  0xE800, 0xE804, 0xE870, 0xE938, 0x1E000, 0x1E004, 0x1E070, 0x1E138, 0x1E800, 0x1E804, 0x1E870, 0x1E938, 0 };
static const mt_igen g_igen[] = {
    { "skl", "Core 6th - 10th gen (Skylake to Comet Lake)", 0x7FFFFF8000ULL, 0x8000, r_skl },
    { "icl", "Core 11th gen desktop (Rocket Lake)", 0x7FFFFF8000ULL, 0x8000, r_icl },
    { "adl", "Core 12th - 14th gen (Alder Lake, Raptor Lake)", 0x3FFFFFE0000ULL, 0x20000, r_adl },
    { "mtl", "Core Ultra (Meteor Lake, Arrow Lake)", 0x3FFFFFE0000ULL, 0x20000, r_mtl },
};
const char *mt_intel_gen(int family, int model)
{
    static const struct { int m; const char *g; } map[] = {
        { 0x4E, "skl" }, { 0x5E, "skl" }, { 0x8E, "skl" }, { 0x9E, "skl" }, { 0xA5, "skl" }, { 0xA6, "skl" },
        { 0xA7, "icl" },
        { 0x97, "adl" }, { 0x9A, "adl" }, { 0xB7, "adl" }, { 0xBA, "adl" }, { 0xBF, "adl" },
        { 0xAA, "mtl" }, { 0xAC, "mtl" }, { 0xC5, "mtl" }, { 0xC6, "mtl" } };
    size_t i;
    if (family != 6) return NULL;
    for (i = 0; i < sizeof map / sizeof map[0]; i++) if (map[i].m == model) return map[i].g;
    return NULL;
}
static const mt_igen *igen_of(const char *g)
{
    size_t i;
    for (i = 0; g && i < sizeof g_igen / sizeof g_igen[0]; i++) if (!strcmp(g_igen[i].gen, g)) return &g_igen[i];
    return NULL;
}

#if defined(__linux__)
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
/* root only, run by srs-root: "gen NAME / base 0x.. / reg 0xOFF 0xVAL" lines, or "error ..." */
int mt_intel_dump(FILE *out)
{
    FILE *c = fopen("/proc/cpuinfo", "r");
    char l[256], vend[32] = "";
    int fam = -1, mod = -1, fd;
    unsigned char cfg[0x50];
    unsigned long long base;
    const mt_igen *G;
    const unsigned *r;
    volatile const unsigned char *w;
    while (c && fgets(l, sizeof l, c)) {
        if (!strncmp(l, "vendor_id", 9)) sscanf(strchr(l, ':') + 1, "%31s", vend);
        else if (!strncmp(l, "cpu family", 10)) fam = atoi(strchr(l, ':') + 1);
        else if (!strncmp(l, "model\t", 6)) mod = atoi(strchr(l, ':') + 1);
        if (*vend && fam >= 0 && mod >= 0) break;
    }
    if (c) fclose(c);
    if (strcmp(vend, "GenuineIntel")) { fprintf(out, "error not an Intel CPU\n"); return 1; }
    if (!(G = igen_of(mt_intel_gen(fam, mod)))) { fprintf(out, "error family %d model 0x%X: no known memory controller layout\n", fam, mod); return 1; }
    fprintf(out, "gen %s\nmodel 0x%X\n", G->gen, mod);
    if (!(c = fopen("/sys/bus/pci/devices/0000:00:00.0/config", "rb")) || fread(cfg, 1, sizeof cfg, c) != sizeof cfg) {
        if (c) fclose(c);
        fprintf(out, "error the host bridge's PCI config could not be read (needs root)\n");
        return 1;
    }
    fclose(c);
    if (cfg[0] != 0x86 || cfg[1] != 0x80) { fprintf(out, "error 00:00.0 is not an Intel host bridge\n"); return 1; }
    base = (unsigned long long)(cfg[0x48] | cfg[0x49] << 8 | cfg[0x4A] << 16 | (unsigned)cfg[0x4B] << 24) |
           (unsigned long long)(cfg[0x4C] | cfg[0x4D] << 8 | cfg[0x4E] << 16 | (unsigned)cfg[0x4F] << 24) << 32;
    if (!(base & 1)) { fprintf(out, "error the memory controller window (MCHBAR) is switched off; SRS does not switch it on\n"); return 1; }
    base &= G->mask;
    fprintf(out, "base 0x%llX\n", base);
    if (sizeof(off_t) < 8 && base > 0xFFFFFFFFULL) { fprintf(out, "error the window lies above 4 GB, out of reach of this 32-bit build\n"); return 1; }
    if ((fd = open("/dev/mem", O_RDONLY | O_SYNC | O_CLOEXEC)) < 0) { fprintf(out, "error /dev/mem: %s\n", strerror(errno)); return 1; }
    w = mmap(NULL, G->window, PROT_READ, MAP_SHARED, fd, (off_t)base);
    close(fd);
    if (w == MAP_FAILED) { fprintf(out, "error the window could not be mapped (%s; kernels with IO_STRICT_DEVMEM need iomem=relaxed)\n", strerror(errno)); return 1; }
    for (r = G->regs; *r; r++)
        if (*r + 4 <= G->window) fprintf(out, "reg 0x%05X 0x%08X\n", *r, *(volatile const unsigned *)(w + *r));   /* aligned 32-bit reads only */
    munmap((void *)w, G->window);
    return 0;
}
#endif

/* decode intel-imc.txt: speed in MT/s, type, channels, CL-RCD-RP-RAS; 0 when not decodable */
int mt_intel_decode(const char *txt, mt_intel *o)
{
    const char *p = txt;
    unsigned off[64], val[64];
    int n = 0, i;
    char gen[8] = "";
    memset(o, 0, sizeof *o);
    while (p && *p) {
        unsigned a, b;
        if (sscanf(p, "gen %7s", gen) == 1) {}
        else if (sscanf(p, "reg %x %x", &a, &b) == 2 && n < 64) { off[n] = a; val[n] = b; n++; }
        else if (!strncmp(p, "error ", 6)) { snprintf(o->error, sizeof o->error, "%.*s", (int)strcspn(p + 6, "\n"), p + 6); }
        p = strchr(p, '\n'); if (p) p++;
    }
#define RV(x) ({ unsigned _v = 0xFFFFFFFFu; for (i = 0; i < n; i++) if (off[i] == (x)) { _v = val[i]; break; } _v; })
    if (!*gen || o->error[0] || !n) return 0;
    snprintf(o->gen, sizeof o->gen, "%s", gen);
    if (igen_of(gen)) snprintf(o->name, sizeof o->name, "%s", igen_of(gen)->name);
    if (!strcmp(gen, "skl") || !strcmp(gen, "icl")) {
        unsigned c0 = RV(0x500C) & 0x003F003F, c1 = RV(0x5010) & 0x003F003F, ofs = c0 ? 0 : 0x400, t = RV(0x4000 + ofs), cas = RV(0x4070 + ofs);
        o->channels = (c0 != 0) + (c1 != 0);
        if (!strcmp(gen, "skl")) {
            unsigned s = RV(0x5918) & 0xFF;
            o->mts = (s & 0x7F) < 3 ? 0 : (int)((s & 0x80) ? (s & 0x7F) * 100.0 : (s & 0x7F) * 133.34 + 0.5);
            snprintf(o->type, sizeof o->type, "%s", (RV(0x401C + ofs) & 3) ? "DDR3" : "DDR4");
            o->ras = (int)((t >> 8) & 0x7F);
        } else {
            unsigned bc = RV(0x5F60), mb = RV(0x5E04);
            double f = (mb & 0xFF) * (bc / 1000.0);
            if (mb & 0x10000) f *= 2;
            if (!(mb & 0xF00)) f *= 133.34 / 100.0;
            o->mts = (int)(f + 0.5);
            snprintf(o->type, sizeof o->type, "DDR4");
            o->ras = (int)((t >> 9) & 0x7F);
        }
        o->cl = (int)((cas >> 16) & 0x1F); o->rcd = o->rp = (int)(t & 0x3F);
    } else if (!strcmp(gen, "adl")) {
        unsigned a = RV(0xD80C), b = RV(0x1D80C), ofs, mb = RV(0x5E04);
        unsigned ca = ~a ? ((a >> 16) & 0x7F) + (a & 0x7F) : 0, cb = ~b ? ((b >> 16) & 0x7F) + (b & 0x7F) : 0;
        double f;
        ofs = ca ? 0 : 0x10000;
        o->channels = (ca != 0) + (cb != 0);
        snprintf(o->type, sizeof o->type, "%s", ((RV(0xD800 + ofs) & 7) == 1 || (RV(0xD800 + ofs) & 7) == 2) ? "DDR5" : "DDR4");
        f = ((RV(0x5918) >> 2) & 0xFF) * (RV(0x5F60) / 1000.0);
        f *= 1 << ((mb >> 12) & 3);
        if (!(mb & 0xF00)) f *= 133.34 / 100.0;
        o->mts = (int)(f + 0.5);
        o->cl = (int)((RV(0xE070 + ofs) >> 16) & 0x7F);
        o->rp = (int)(RV(0xE000 + ofs) & 0xFF);
        o->ras = (int)((RV(0xE004 + ofs) >> 10) & 0x1FF);
        o->rcd = (int)((RV(0xE004 + ofs) >> 19) & 0xFF);
    } else if (!strcmp(gen, "mtl")) {
        unsigned a = RV(0xD80C), b = RV(0x1D80C), ofs, pg = RV(0x13D98);
        static const int ref[16] = { 200, 200, 100, 200, 200, 200, 200, 200, 200, 200, 133, 66, 33, 200, 200, 200 };
        int ca = ~a ? 1 : 0, cb = ~b ? 1 : 0;
        ofs = ca ? 0 : 0x10000;
        if (!(RV(0xD80C + ofs) & 0x007F007F) && (RV(0xD810 + ofs) & 0x007F007F)) ofs += 0x800;
        o->channels = ca + cb;
        snprintf(o->type, sizeof o->type, "DDR5");
        o->mts = ref[(pg >> 20) & 0xF] * (int)((pg >> 12) & 0xFF) * ((int)((pg >> 24) & 1) + 1) * 2;
        o->cl = (int)((RV(0xE070 + ofs) >> 16) & 0x7F);
        o->rcd = (int)((RV(0xE138 + ofs) >> 22) & 0xFF);
        o->rp = (int)(RV(0xE000 + ofs) & 0xFF);
        o->ras = (int)((RV(0xE004 + ofs) >> 13) & 0x1FF);
    } else return 0;
#undef RV
    if (o->mts < 200 || o->mts > 12000) o->mts = 0;
    return o->cl > 0;
}
