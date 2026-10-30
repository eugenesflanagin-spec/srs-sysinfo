/* tests/mem_timings_harness.c - unit checks of mem_timings.c (2.4) on made-up data: a UMC register dump, DDR5 SPD
 * images, an AMD AOD table and Intel memory controller dumps. Built and run by tests/mem-timings-test.sh.
 *
 *   mem_timings_harness            run the checks, print "  ok" / "  FAIL" lines, exit code = failures
 *   mem_timings_harness gen DIR    write the made-up files the page test needs into DIR: spd.bin, aod.bin,
 *                                  pm_table, pm_table_version
 *   mem_timings_harness umc        print the UMC fixture (srs-root's umc.txt format)
 *
 * Every SPD byte here is synthetic (no real module's SPD is copied: a real one holds a serial number). The UMC values
 * are the register words of a Ryzen 7 7800X3D at DDR5-6000 30-38-38-96 (no serial numbers in them). Nothing here
 * touches hardware. */
#include "mem_timings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(cond, ...) do { if (cond) { printf("  ok    "); printf(__VA_ARGS__); printf("\n"); } \
                              else { printf("  FAIL  "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

/* the UMC fixture: channels 0 and 1 hold a DIMM, 2 and 3 read back all ones (no such UMC) */
static const char *umc_fixture(void)
{
    static char buf[4096];
    static const struct { unsigned off, v; } r[] = {
        { 0x50000, 0x00000000 }, { 0x50008, 0x00000001 },   /* chip selects: one DIMM */
        { 0x50200, 0x00050bb8 },                            /* ratio 3000 (6000 MT/s), bit 18 GDM, bit 16 bank groups */
        { 0x50204, 0x2626601e },                            /* tCL 30, tRAS 96, tRCDRD 38, tRCDWR 38 */
        { 0x50208, 0x00260092 },                            /* tRC 146, tRP 38 */
        { 0x5020C, 0x0C080808 },                            /* tRRDS 8, tRRDL 8, tRTP 12 */
        { 0x50210, 0x00000020 },                            /* tFAW 32 */
        { 0x50214, 0x0010081C },                            /* tCWL 28, tWTRS 8, tWTRL 16 */
        { 0x50218, 0x0000005A },                            /* tWR 90 */
        { 0x50230, 0x00003A98 },                            /* tREFI 15000 */
        { 0x50260, 0x01e00374 },                            /* tRFC 884, tRFC2 480 */
        { 0x502C0, 0x00000186 },                            /* tRFCsb 390 */
        { 0x50DF0, 0x0001fd08 },                            /* bit 19 clear: channel on */
    };
    size_t i, k, n = 0;
    for (k = 0; k < 2; k++)
        for (i = 0; i < sizeof r / sizeof r[0]; i++)
            n += (size_t)snprintf(buf + n, sizeof buf - n, "ch%zu 0x%05X %08x\n", k, r[i].off, r[i].v);
    for (k = 2; k < 4; k++)
        for (i = 0; i < sizeof r / sizeof r[0]; i++)
            n += (size_t)snprintf(buf + n, sizeof buf - n, "ch%zu 0x%05X ffffffff\n", k, r[i].off);
    return buf;
}

static void put16(unsigned char *d, size_t o, int v) { d[o] = (unsigned char)(v & 0xFF); d[o + 1] = (unsigned char)(v >> 8); }
static void put32(unsigned char *d, size_t o, int v) { memcpy(d + o, &v, 4); }
/* the 9 timing words of a profile: tAA tRCD tRP tRAS tRC tWR (ps), tRFC1 tRFC2 tRFCsb (ns) */
static void times(unsigned char *d, size_t o, int aa, int rcd, int rp, int ras, int rc, int wr, int rfc1, int rfc2, int rfcsb)
{
    int v[9] = { aa, rcd, rp, ras, rc, wr, rfc1, rfc2, rfcsb }, i;
    for (i = 0; i < 9; i++) put16(d, o + 2 * (size_t)i, v[i]);
}

/* a made-up 16 GB DDR5 module: 16 Gb x8 chips, one rank, 2 x 32-bit subchannels; JEDEC 4800, EXPO 6000 30-38-38-96,
   XMP 3.0 "Lv1-6000C36" */
static void make_spd(unsigned char *d, int expo, int xmp)
{
    memset(d, 0, 1024);
    d[2] = 0x12;                      /* DDR5 */
    d[4] = 0x04;                      /* 16 Gb per die, one die */
    d[6] = 0x20;                      /* x8 */
    d[234] = 0x00;                    /* one rank */
    d[235] = 0x22;                    /* 2 subchannels, 32 bit */
    d[512] = 0x80; d[513] = 0xCE;     /* module maker: bank 1 (no continuation codes), 0x4E with its parity bit set */
    d[552] = 0x00; d[553] = 0x2C;     /* DRAM maker: bank 1, 0x2C (no parity bit, also accepted) */
    d[515] = 0x26; d[516] = 0x44;     /* 2026, week 44 */
    memcpy(d + 521, "SRS-TEST-16G        ", 20);
    put16(d, 20, 416);                /* JEDEC tCKAVGmin 416 ps = 4800 MT/s */
    times(d, 30, 16640, 16000, 16000, 32000, 48000, 30000, 295, 160, 130);
    if (expo) {
        memcpy(d + 832, "EXPO", 4);
        d[836] = 0x10;                /* revision 1.0 */
        d[837] = 0x01;                /* profile 1 on */
        d[842] = 0x27; d[843] = 0x27; d[844] = 0x30;   /* VDD 1.35, VDDQ 1.35, VPP 1.80 V */
        put16(d, 846, 333);           /* 6000 MT/s */
        times(d, 848, 10000, 12500, 12500, 32000, 44500, 30000, 295, 160, 130);
    }
    if (xmp) {
        d[640] = 0x0C; d[641] = 0x4A;
        d[642] = 0x30;                /* XMP 3.0 */
        d[643] = 0x01;                /* profile 1 on */
        memcpy(d + 654, "Lv1-6000C36     ", 16);
        d[704] = 0x30; d[705] = 0x27; d[706] = 0x27;   /* VPP 1.80, VDD 1.35, VDDQ 1.35 V */
        put16(d, 709, 333);
        times(d, 717, 12000, 12000, 12000, 32000, 44000, 30000, 295, 160, 130);
    }
}

/* a made-up AOD table: the Raphael block at 8924 (MEMCLK, tCL, tRCD, tRP, tRAS ...) */
#define AOD_N 9400
static void make_aod(unsigned char *d, int cl)
{
    memset(d, 0, AOD_N);
    put32(d, 8924, 3000); put32(d, 8928, cl); put32(d, 8932, 38); put32(d, 8936, 38); put32(d, 8940, 96);
    put32(d, 8924 + 104, 30);          /* CAD bus drive strength 30 ohm */
    put32(d, 8924 + 108, 28);          /* proc data drive strength 40 ohm */
    put32(d, 8924 + 112, 14);          /* ProcODT 48 ohm */
    put32(d, 8924 + 116, 1);           /* DRAM data drive strength 40 ohm */
    put32(d, 8924 + 120, 5); put32(d, 8924 + 124, 5); put32(d, 8924 + 128, 0); put32(d, 8924 + 132, 4); put32(d, 8924 + 136, 5);
    put32(d, 8924 + 172, 1435); put32(d, 8924 + 176, 1400); put32(d, 8924 + 180, 1800); put32(d, 8924 + 184, 1400);
}

static int wr(const char *dir, const char *name, const void *p, size_t n)
{
    char path[1200];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (!(f = fopen(path, "wb")) || fwrite(p, 1, n, f) != n) { perror(path); if (f) fclose(f); return 1; }
    return fclose(f) != 0;
}

static int gen(const char *dir)
{
    static unsigned char spd[1024], aod[AOD_N], pm[0x440];
    unsigned ver = 0x540104;
    float v;
    int e = 0;
    make_spd(spd, 1, 1);
    make_aod(aod, 30);
    memset(pm, 0, sizeof pm);   /* Ryzen 7000 PM table 0x540104: FCLK 0x118, UCLK 0x128, MEMCLK 0x138, VSOC 0xD0, VDDP 0x430, MISC 0xB8 */
    v = 2000.0f; memcpy(pm + 0x118, &v, 4);
    v = 1500.0f; memcpy(pm + 0x128, &v, 4);
    v = 3000.0f; memcpy(pm + 0x138, &v, 4);
    v = 1.25f;   memcpy(pm + 0xD0, &v, 4);
    v = 1.125f;  memcpy(pm + 0x430, &v, 4);
    v = 1.0f;    memcpy(pm + 0xB8, &v, 4);
    e |= wr(dir, "spd.bin", spd, sizeof spd);
    e |= wr(dir, "aod.bin", aod, sizeof aod);
    e |= wr(dir, "pm_table", pm, sizeof pm);
    e |= wr(dir, "pm_table_version", &ver, 4);
    return e;
}

static void t_umc(void)
{
    static mt_umc ch[MT_NCH];
    int n = mt_umc_parse(umc_fixture(), ch), rfc, rfc2, rfcsb, nd, i, found = 0;
    const mt_def *d = mt_defs(&nd);
    printf("== UMC registers (umc.txt)\n");
    CHECK(n == 2 && ch[0].present && ch[1].present && !ch[2].present && !ch[3].present,
          "two channels with a DIMM, the all-ones UMCs 2 and 3 are not counted (n=%d)", n);
    CHECK(mt_umc_mts(&ch[0]) == 6000, "speed 6000 MT/s (got %d)", mt_umc_mts(&ch[0]));
    CHECK(mt_field(&ch[0], 0x50204, 5, 0) == 30, "tCL 30");
    CHECK(mt_field(&ch[0], 0x50204, 21, 16) == 38, "tRCDRD 38");
    CHECK(mt_field(&ch[0], 0x50204, 29, 24) == 38, "tRCDWR 38");
    CHECK(mt_field(&ch[0], 0x50208, 21, 16) == 38, "tRP 38");
    CHECK(mt_field(&ch[0], 0x50204, 14, 8) == 96, "tRAS 96");
    CHECK(mt_field(&ch[0], 0x50208, 7, 0) == 146, "tRC 146");
    CHECK(mt_field(&ch[0], 0x50200, 18, 18) == 1 && mt_field(&ch[0], 0x50200, 17, 17) == 0, "GDM 1, Cmd2T 0 (1T)");
    CHECK(mt_field(&ch[0], 0x50400, 3, 0) == -1, "a register that was not read gives -1");
    CHECK(mt_field(&ch[0], 0x50204, 31, 0) == 0x2626601e, "a whole 32-bit field");
    mt_umc_rfc(&ch[0], &rfc, &rfc2, &rfcsb);
    CHECK(rfc == 884 && rfc2 == 480 && rfcsb == 390, "tRFC 884 / tRFC2 480 / tRFCsb 390 (got %d / %d / %d)", rfc, rfc2, rfcsb);
    /* the table agrees with the hand decode above for the primary timings */
    for (i = 0; i < nd; i++)
        if (!strcmp(d[i].name, "tRP")) found = mt_field(&ch[1], d[i].off, d[i].hi, d[i].lo) == 38;
    CHECK(found, "the mt_defs table entry for tRP reads 38 on channel B too");
    {   /* tRFC: the BIOS's reset value 0x00C00138 is skipped for the next register */
        static mt_umc c2[MT_NCH];
        mt_umc_parse("ch0 0x50200 00050bb8\nch0 0x50000 0\nch0 0x50008 1\nch0 0x50260 00c00138\nch0 0x50264 01e00374\n", c2);
        mt_umc_rfc(&c2[0], &rfc, &rfc2, &rfcsb);
        CHECK(c2[0].present && rfc == 884 && rfc2 == 480 && rfcsb == -1, "tRFC: the reset value is skipped, tRFCsb not read = -1");
    }
    {   /* a disabled channel (0x50DF0 bit 19) and one without a DIMM are not counted; junk lines are ignored */
        static mt_umc c3[MT_NCH];
        n = mt_umc_parse("ch0 0x50200 00050bb8\nch0 0x50000 1\nch0 0x50008 0\nch0 0x50DF0 00080000\n"
                         "ch1 0x50200 00050bb8\nch1 0x50000 0\nch1 0x50008 0\n"
                         "ch9 0x50200 00050bb8\nch2 0x60000 1\nch2 0x50202 1\nnot read: ryzen_smu\n", c3);
        CHECK(n == 0, "disabled channel, empty channel, bad channel / offsets: none counted (n=%d)", n);
    }
}

static void t_spd(void)
{
    static unsigned char d[1024];
    mt_spd s;
    printf("== SPD (synthetic DDR5 images)\n");
    make_spd(d, 1, 1);
    mt_spd_decode(d, sizeof d, &s);
    CHECK(s.ok, "a DDR5 SPD (byte 2 = 0x12)");
    CHECK(s.density_gb == 16 && s.dies == 1 && s.width == 8 && s.ranks == 1 && s.subch == 2 && s.buswidth == 32,
          "16 Gb, 1 die, x8, 1 rank, 2 x 32 bit (got %d Gb, %d, x%d, %d, %d x %d)", s.density_gb, s.dies, s.width, s.ranks, s.subch, s.buswidth);
    CHECK(s.size_gb == 16.0, "size 16 GB (got %.1f)", s.size_gb);
    CHECK(s.mfr == 0x04E && s.dram_mfr == 0x02C, "makers: parity bit removed (0x%03X, 0x%03X)", s.mfr, s.dram_mfr);
    CHECK(s.mfr == 0x04E && mt_jep106(s.mfr) && !strcmp(mt_jep106(s.mfr), "Samsung") && !mt_jep106(0x7F7), "JEP106 short list: 0x04E Samsung, unknown = NULL");
    CHECK(s.year == 2026 && s.week == 44, "made 2026 week 44 (got %d / %d)", s.year, s.week);
    CHECK(!strcmp(s.part, "SRS-TEST-16G"), "part number, trailing spaces cut (\"%s\")", s.part);
    CHECK(s.jedec.valid && s.jedec.mts == 4800 && s.jedec.cl == 40 && s.jedec.rcd == 39 && s.jedec.rp == 39 && s.jedec.ras == 77 && s.jedec.rc == 116,
          "JEDEC 4800 40-39-39-77-116 (got %d %d-%d-%d-%d-%d)", s.jedec.mts, s.jedec.cl, s.jedec.rcd, s.jedec.rp, s.jedec.ras, s.jedec.rc);
    CHECK(s.jedec.vdd == 1100 && s.jedec.vpp == 1800, "JEDEC voltages 1.1 / 1.8 V");
    CHECK(s.has_expo && !strcmp(s.expo_rev, "1.0"), "EXPO block found, revision 1.0");
    CHECK(s.expo[0].valid && s.expo[0].mts == 6000 && s.expo[0].cl == 30 && s.expo[0].rcd == 38 && s.expo[0].rp == 38 && s.expo[0].ras == 96 && s.expo[0].rc == 134,
          "EXPO 1: 6000 30-38-38-96-134 (got %d %d-%d-%d-%d-%d)", s.expo[0].mts, s.expo[0].cl, s.expo[0].rcd, s.expo[0].rp, s.expo[0].ras, s.expo[0].rc);
    CHECK(s.expo[0].wr == 90 && s.expo[0].rfc1ns == 295 && s.expo[0].rfc2ns == 160 && s.expo[0].rfcsbns == 130, "EXPO 1: tWR 90, tRFC 295 / 160 / 130 ns");
    CHECK(s.expo[0].vdd == 1350 && s.expo[0].vddq == 1350 && s.expo[0].vpp == 1800, "EXPO 1: 1.35 / 1.35 / 1.80 V (got %d / %d / %d mV)", s.expo[0].vdd, s.expo[0].vddq, s.expo[0].vpp);
    CHECK(!s.expo[1].valid, "EXPO 2 not enabled: not shown");
    CHECK(s.has_xmp && !strcmp(s.xmp_rev, "3.0"), "XMP 3.0 block found");
    CHECK(s.xmp[0].valid && s.xmp[0].mts == 6000 && s.xmp[0].cl == 36 && s.xmp[0].rcd == 36 && s.xmp[0].rp == 36 && s.xmp[0].ras == 96 && s.xmp[0].rc == 132,
          "XMP 1: 6000 36-36-36-96-132 (got %d %d-%d-%d-%d-%d)", s.xmp[0].mts, s.xmp[0].cl, s.xmp[0].rcd, s.xmp[0].rp, s.xmp[0].ras, s.xmp[0].rc);
    CHECK(!strcmp(s.xmp[0].name, "Lv1-6000C36"), "XMP 1 name \"Lv1-6000C36\" (got \"%s\")", s.xmp[0].name);
    CHECK(s.xmp[0].vdd == 1350 && s.xmp[0].vpp == 1800, "XMP 1: VDD 1.35, VPP 1.80 V");
    CHECK(!s.xmp[1].valid && !s.xmp[2].valid, "XMP 2 and 3 not enabled");
    /* an odd JEDEC CAS latency is rounded up to the next even one */
    put16(d, 848, 9500);              /* 9500 ps at 333 ps = 29 clocks (JEDEC rounding) -> 30 */
    mt_spd_decode(d, sizeof d, &s);
    CHECK(s.expo[0].cl == 30, "CAS latency 29 becomes 30 (DDR5 has even CLs only), got %d", s.expo[0].cl);
    make_spd(d, 0, 0);
    mt_spd_decode(d, sizeof d, &s);
    CHECK(s.ok && !s.has_expo && !s.has_xmp && !s.expo[0].valid && !s.xmp[0].valid, "no EXPO / XMP blocks: none shown");
    d[2] = 0x0C;                      /* DDR4 */
    mt_spd_decode(d, sizeof d, &s);
    CHECK(!s.ok, "byte 2 = 0x0C (DDR4): not decoded");
    make_spd(d, 1, 1);
    mt_spd_decode(d, 512, &s);
    CHECK(!s.ok, "a 512-byte read: not decoded");
    mt_spd_decode(NULL, 0, &s);
    CHECK(!s.ok, "nothing read: not decoded, no crash");
    make_spd(d, 1, 1);
    d[4] = 0x44;                      /* 16 Gb, bits 7:5 = 010: a 2-high 3DS stack (001 is reserved) */
    d[234] = 0x08;                    /* 2 ranks */
    mt_spd_decode(d, sizeof d, &s);
    CHECK(s.dies == 2 && s.ranks == 2 && s.size_gb == 64.0, "2 dies x 2 ranks = 64 GB (got %d, %d, %.0f)", s.dies, s.ranks, s.size_gb);
}

static void t_aod(void)
{
    static unsigned char d[AOD_N];
    mt_aod a;
    char t[48];
    printf("== AMD AOD table (synthetic)\n");
    make_aod(d, 30);
    CHECK(mt_aod_decode(d, sizeof d, 3000, 30, 38, &a) == 1 && a.ok && a.base == 8924, "the block at 8924 matches the live timings (base %d)", a.base);
    CHECK(a.rp == 38 && a.ras == 96, "tRP 38, tRAS 96 from the table");
    CHECK(a.procodt == 14 && a.rttnomwr == 5 && a.vdd == 1435 && a.vddq == 1400 && a.vpp == 1800 && a.vddio == 1400,
          "ProcODT code 14, RttNomWr 5, VDD 1435 / VDDQ 1400 / VPP 1800 / VDDIO 1400 mV");
    mt_aod_text(0, a.procodt, t, sizeof t);
    CHECK(!strcmp(t, "48.0 Ω"), "ProcODT 14 -> \"48.0 Ω\" (got \"%s\")", t);
    mt_aod_text(4, a.rttnomwr, t, sizeof t);
    CHECK(!strcmp(t, "RZQ/5 (48 Ω)"), "RTT 5 -> \"RZQ/5 (48 Ω)\" (got \"%s\")", t);
    mt_aod_text(4, 0, t, sizeof t);
    CHECK(!strcmp(t, "Off"), "RTT 0 -> Off");
    mt_aod_text(1, 13, t, sizeof t);
    CHECK(!strcmp(t, "code 13"), "proc drive strength: odd codes are not in its table (\"%s\")", t);
    mt_aod_text(1, 28, t, sizeof t);
    CHECK(!strcmp(t, "40.0 Ω"), "proc drive strength 28 -> 40.0 Ω");
    mt_aod_text(2, 1, t, sizeof t);
    CHECK(!strcmp(t, "40.0 Ω"), "DRAM drive strength 1 -> 40.0 Ω");
    mt_aod_text(3, 30, t, sizeof t);
    CHECK(!strcmp(t, "30.0 Ω"), "CAD bus 30 -> 30.0 Ω");
    mt_aod_text(0, 99, t, sizeof t);
    CHECK(!strcmp(t, "code 99"), "an unknown code -> \"code 99\"");
    CHECK(mt_aod_decode(d, sizeof d, 3000, 32, 38, &a) == 0 && !a.ok, "live tCL 32 does not match the table's 30: rejected");
    CHECK(mt_aod_decode(d, sizeof d, 3200, 30, 38, &a) == 0, "live MEMCLK 3200: rejected");
    make_aod(d, 34);
    CHECK(mt_aod_decode(d, sizeof d, 3000, 30, 38, &a) == 0, "a table whose timings differ from the live ones: rejected");
    CHECK(mt_aod_decode(d, 9000, 3000, 30, 38, &a) == 0, "a table that is too short: rejected");
    {   /* the block somewhere else: found at the last place the MEMCLK value appears */
        memset(d, 0, sizeof d);
        put32(d, 9100, 3000); put32(d, 9104, 30); put32(d, 9108, 38); put32(d, 9100 + 112, 14);
        CHECK(mt_aod_decode(d, sizeof d, 3000, 30, 38, &a) == 1 && a.base == 9100 && a.procodt == 14, "a block moved to 9100 is found there (base %d)", a.base);
    }
}

static void t_intel(void)
{
    mt_intel m;
    printf("== Intel memory controller (synthetic intel-imc.txt)\n");
    CHECK(mt_intel_gen(6, 0xB7) && !strcmp(mt_intel_gen(6, 0xB7), "adl"), "model 0xB7 (Raptor Lake) -> adl");
    CHECK(mt_intel_gen(6, 0x9E) && !strcmp(mt_intel_gen(6, 0x9E), "skl"), "model 0x9E (Coffee Lake) -> skl");
    CHECK(mt_intel_gen(6, 0xC6) && !strcmp(mt_intel_gen(6, 0xC6), "mtl"), "model 0xC6 (Arrow Lake) -> mtl");
    CHECK(mt_intel_gen(6, 0xA7) && !strcmp(mt_intel_gen(6, 0xA7), "icl"), "model 0xA7 (Rocket Lake) -> icl");
    CHECK(!mt_intel_gen(6, 0x61), "model 0x61: not supported (NULL)");
    CHECK(!mt_intel_gen(25, 0xB7), "family 25 (AMD): NULL");
    CHECK(mt_intel_decode("gen adl\nmodel 0xB7\nbase 0xFEDC0000\n"
                          "reg 0x0D800 0x00000001\nreg 0x0D80C 0x00100010\nreg 0x1D800 0x00000001\nreg 0x1D80C 0x00100010\n"
                          "reg 0x05F60 0x000186A0\nreg 0x05918 0x00000078\nreg 0x05E04 0x00001100\n"
                          "reg 0x0E000 0x00000026\nreg 0x0E004 0x01318000\nreg 0x0E070 0x001E0000\n", &m) == 1,
          "adl: decoded");
    CHECK(!strcmp(m.gen, "adl") && !strcmp(m.type, "DDR5") && m.mts == 6000 && m.channels == 2,
          "adl: DDR5 6000 MT/s, 2 channels (got %s %d, %d)", m.type, m.mts, m.channels);
    CHECK(m.cl == 30 && m.rcd == 38 && m.rp == 38 && m.ras == 96, "adl: 30-38-38-96 (got %d-%d-%d-%d)", m.cl, m.rcd, m.rp, m.ras);
    CHECK(strstr(m.name, "Alder Lake") != NULL, "adl: name \"%s\"", m.name);
    CHECK(mt_intel_decode("gen skl\nreg 0x0500C 0x00100010\nreg 0x05010 0x00100010\nreg 0x05918 0x000000A0\n"
                          "reg 0x04000 0x00002612\nreg 0x04070 0x00100000\nreg 0x0401C 0x00000000\n", &m) == 1 &&
          !strcmp(m.type, "DDR4") && m.mts == 3200 && m.channels == 2 && m.cl == 16 && m.rcd == 18 && m.rp == 18 && m.ras == 38,
          "skl: DDR4 3200 MT/s, 2 channels, 16-18-18-38 (got %s %d, %d, %d-%d-%d-%d)", m.type, m.mts, m.channels, m.cl, m.rcd, m.rp, m.ras);
    CHECK(mt_intel_decode("gen mtl\nreg 0x0D80C 0x00100010\nreg 0x1D80C 0x00100010\nreg 0x13D98 0x00010000\n"
                          "reg 0x0E000 0x00000027\nreg 0x0E004 0x000CC000\nreg 0x0E070 0x00200000\nreg 0x0E138 0x09C00000\n", &m) == 1 &&
          !strcmp(m.type, "DDR5") && m.mts == 6400 && m.channels == 2 && m.cl == 32 && m.rcd == 39 && m.rp == 39 && m.ras == 102,
          "mtl: DDR5 6400 MT/s, 2 channels, 32-39-39-102 (got %s %d, %d, %d-%d-%d-%d)", m.type, m.mts, m.channels, m.cl, m.rcd, m.rp, m.ras);
    CHECK(mt_intel_decode("gen adl\nmodel 0xB7\nerror the memory controller window (MCHBAR) is switched off; SRS does not switch it on\n", &m) == 0 &&
          !strcmp(m.error, "the memory controller window (MCHBAR) is switched off; SRS does not switch it on"),
          "an \"error ...\" text: not decoded, the reason is kept (\"%s\")", m.error);
    CHECK(mt_intel_decode("gen xyz\nreg 0x05918 0x78\n", &m) == 0, "an unknown generation: not decoded");
    CHECK(mt_intel_decode("", &m) == 0 && mt_intel_decode("reg 0x05918 0x78\n", &m) == 0, "empty text / no gen line: not decoded");
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "gen")) return gen(argv[2]);
    if (argc == 2 && !strcmp(argv[1], "umc")) { fputs(umc_fixture(), stdout); return 0; }
    t_umc();
    t_spd();
    t_aod();
    t_intel();
    return fails > 255 ? 255 : fails;
}
