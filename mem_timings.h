/* mem_timings.h - DDR5 memory timings (2.4): the AMD UMC registers that srs-root reads through ryzen_smu
 * (umc.txt in the root cache) and the SPD EEPROM of each module (JEDEC, EXPO and XMP 3.0 profiles).
 * Decoding only; nothing here touches hardware. Internal to SRS. */
#ifndef MEM_TIMINGS_H
#define MEM_TIMINGS_H

#include <stddef.h>

#define MT_NCH 4             /* UMCs read by srs-root (AM5 has 2; the others read back 0xFFFFFFFF) */
#define MT_NREG 0x380        /* 32-bit registers 0x50000 .. 0x50DFC of one UMC */

typedef struct {
    int present;                     /* the UMC exists and has a DIMM */
    unsigned reg[MT_NREG];
    unsigned char have[MT_NREG];
} mt_umc;

/* one decoded timing: clocks per channel (-1 = not read) */
typedef struct { const char *name, *group, *what; unsigned off; int hi, lo, ns; } mt_def;

typedef struct {
    int valid;
    char name[20];                   /* XMP profile name, else "" */
    int mts, cl, rcd, rp, ras, rc, wr;
    int rfc1ns, rfc2ns, rfcsbns;
    int vdd, vddq, vpp;              /* mV, 0 = not in this profile */
} mt_prof;

typedef struct {
    int ok;                          /* a DDR5 SPD (byte 2 = 0x12) */
    int density_gb, dies, width, ranks, subch, buswidth;
    double size_gb;
    unsigned mfr, dram_mfr;          /* JEP106: bank (continuation count) << 8 | code, parity bit removed */
    int year, week;                  /* 0 = not set */
    char part[32];
    mt_prof jedec, expo[2], xmp[3];
    int has_expo, has_xmp;
    char expo_rev[8], xmp_rev[8];
} mt_spd;

/* parse srs-root's umc.txt ("ch0 0x50204 2626601e" per line); returns the number of channels with a DIMM */
int mt_umc_parse(const char *text, mt_umc ch[MT_NCH]);
/* a register of a channel: 1 and *v when read */
int mt_reg(const mt_umc *c, unsigned off, unsigned *v);
/* bits hi..lo of a register, -1 when not read */
int mt_field(const mt_umc *c, unsigned off, int hi, int lo);
/* data rate in MT/s from UMC 0x50200 (ratio / 100 x 100 MHz reference x 2), 0 when unknown */
int mt_umc_mts(const mt_umc *c);
/* tRFC, tRFC2 (first of 0x50260..0x5026C that is set and not the reset value) and tRFCsb */
void mt_umc_rfc(const mt_umc *c, int *rfc, int *rfc2, int *rfcsb);

/* the timing table, in display order; *n = entries */
const mt_def *mt_defs(int *n);

/* decode one DDR5 SPD image (1024 bytes) */
void mt_spd_decode(const unsigned char *d, size_t n, mt_spd *s);
/* the BIOS's AMD Overclocking Data (AOD) table (srs-root's aod.bin, readable only when the kernel allows /dev/mem
   there): the memory settings it holds. ok = its timing block matched the live timings (expect: MEMCLK MHz, tCL, tRCDRD) */
typedef struct {
    int ok, base;
    int memclk, cl, rcd, rp, ras;
    int procodt, procdrv, dramdrv, cadbus, rttnomwr, rttnomrd, rttwr, rttpark, rttparkdqs;
    int vdd, vddq, vpp, vddio;                       /* mV */
} mt_aod;
int mt_aod_decode(const unsigned char *d, size_t n, int memclk, int cl, int rcd, mt_aod *a);
/* the meaning of an AOD code: kind 0 ProcODT, 1 proc data drive strength, 2 DRAM data drive strength, 3 CAD bus drive
   strength, 4 RTT. Writes e.g. "48.0 Ω" or "RZQ/5 (48 Ω)"; "code N" when not known */
void mt_aod_text(int kind, int code, char *b, size_t n);

/* JEP106 maker name for a decoded code, or NULL when not in SRS's short list */
const char *mt_jep106(unsigned code);

#endif
