/* sec_machine.c - SIV "Machine" group on Linux: SMBIOS/DMI (BIOS, System, Board, Chassis,
 * processor socket, memory array/devices), DIMMs + DIMM status, memory, physical map, ACPI
 * (tables, MADT/FADT/IVRS/TPM2 decode from the srs-root cache, devices, buses, ports, power,
 * SPB, WMI, GPEs, AML method scan), UEFI (efivars, ESRT, boot entries), TPM and CMOS/RTC.
 *
 * Sources (all read-only): /sys/class/dmi/id, /run/udev/data/+dmi:id (systemd's SMBIOS type 17
 * import, world-readable), /proc/meminfo, /sys/devices/system/memory, /sys/devices/system/edac,
 * spd5118 hwmon (temperatures only - the SPD eeprom attribute is never opened), /sys/firmware,
 * /sys/bus/acpi, /sys/bus/wmi, /sys/class/tpm, /sys/class/rtc, /proc/driver/rtc, lspci -mm.
 * Root-only data comes only from the srs-root cache: dmidecode.txt, spd.txt, acpi-tables/<SIG>,
 * iomem.txt. Nothing here writes sysfs, evaluates ACPI methods, opens /dev/mem, /dev/nvram,
 * /dev/port, /dev/tpm*, i2c devices or SMN registers. */
#define _GNU_SOURCE
#include "sec.h"
#include "mem_timings.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define DMI_ID   "/sys/class/dmi/id/"
#define ACPI_TAB "/sys/firmware/acpi/tables"
#define ACPI_DEV "/sys/bus/acpi/devices"
#define EFI_DIR  "/sys/firmware/efi"
#define EFIVARS  EFI_DIR "/efivars"
#define EFI_GLOBAL "8be4df61-93ca-11d2-aa0d-00e098032b8c"
#define NEEDS_ROOT "Needs root: Menu > Run as root (srs-root)"

/* ======================================================================
 * small helpers
 * ====================================================================== */
static void dmi_rd(const char *f, char *b, size_t sz) { rd_fmt(b, sz, DMI_ID "%s", f); }

static int junk(const char *s)
{
    static const char *const j[] = { "", "Default string", "To be filled by O.E.M.", "System Product Name", "System Version",
                                     "SKU", "Not Specified", "None", "Unknown", "System manufacturer", "Not Applicable", NULL };
    int i;
    for (i = 0; j[i]; i++) if (!strcmp(s, j[i])) return 1;
    return 0;
}
static const char *na(const char *s) { return junk(s) ? "NA" : s; }

static int safe_arg(const char *a) { return a && *a && !strchr(a, '/') && strcmp(a, ".") && strcmp(a, ".."); }

static int nat_cmp(const void *a, const void *b) { return strverscmp(*(char *const *)a, *(char *const *)b); }
static void nat_sort(char **v, int n) { if (n > 1) qsort(v, (size_t)n, sizeof *v, nat_cmp); }

static long long fsize(const char *path) { struct stat st; return stat(path, &st) == 0 ? (long long)st.st_size : -1; }

static int link_base(const char *path, char *b, size_t sz)
{
    char t[1024];
    ssize_t n = readlink(path, t, sizeof t - 1);
    const char *s;
    b[0] = 0;
    if (n <= 0) return 0;
    t[n] = 0;
    s = strrchr(t, '/');
    snprintf(b, sz, "%s", s ? s + 1 : t);
    return 1;
}

/* copy of a srs-root cache path (so_root_file returns a static buffer) */
static int root_path(const char *name, char *out, size_t sz)
{
    const char *p = so_root_file(name);
    if (!p) { out[0] = 0; return 0; }
    snprintf(out, sz, "%s", p);
    return 1;
}

static void needs_root(srs_out *o) { so_par(o); so_t(o, C_ORANGE, NEEDS_ROOT); so_par(o); }

/* 16 bytes per row; offsets count from base; masked: bytes shown as XX (Redact Reports) */
static void hexdump_at(srs_out *o, const unsigned char *d, int n, int max, unsigned base, int masked)
{
    int i, j;
    char line[128];
    if (n > max) n = max;
    for (i = 0; i < n; i += 16) {
        int p = snprintf(line, sizeof line, "%04X  ", base + (unsigned)i);
        for (j = 0; j < 16; j++) {
            if (i + j >= n) p += snprintf(line + p, sizeof line - (size_t)p, "   ");
            else if (masked) p += snprintf(line + p, sizeof line - (size_t)p, "XX ");
            else p += snprintf(line + p, sizeof line - (size_t)p, "%02X ", d[i + j]);
        }
        p += snprintf(line + p, sizeof line - (size_t)p, " ");
        for (j = 0; j < 16 && i + j < n; j++) line[p++] = (char)(!masked && isprint(d[i + j]) ? d[i + j] : '.');
        line[p] = 0;
        so_m(o, C_VALUE, line);
        so_par(o);
    }
}
static void hexdump(srs_out *o, const unsigned char *d, int n, int max) { hexdump_at(o, d, n, max, 0, 0); }

static uint16_t le16(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const unsigned char *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t le64(const unsigned char *p) { return (uint64_t)le32(p) | (uint64_t)le32(p + 4) << 32; }

/* ======================================================================
 * dmidecode.txt (srs-root cache) parser
 * ====================================================================== */
static char *g_dmid;
static int g_dmid_tried;
static const char *dmid(void)
{
    char p[1100];
    if (!g_dmid_tried) {
        g_dmid_tried = 1;
        if (root_path("dmidecode.txt", p, sizeof p) && rd_all(p, &g_dmid) < 0) g_dmid = NULL;
    }
    return g_dmid;
}

/* value of "\tKey: value" inside the inst-th structure of DMI type `type` */
static int dmid_get(int type, int inst, const char *key, char *out, size_t sz)
{
    const char *t = dmid(), *p;
    int cur = -1, seen = -1;
    size_t kl = strlen(key);
    out[0] = 0;
    if (!t) return 0;
    for (p = t; *p; ) {
        const char *e = strchr(p, '\n');
        size_t l = e ? (size_t)(e - p) : strlen(p);
        if (!strncmp(p, "Handle 0x", 9)) {
            const char *ty = strstr(p, "DMI type ");
            cur = ty && ty < p + l ? atoi(ty + 9) : -1;
            if (cur == type) seen++;
        } else if (cur == type && seen == inst && p[0] == '\t' && p[1] != '\t' && l > kl + 2 &&
                   !strncmp(p + 1, key, kl) && p[1 + kl] == ':') {
            const char *v = p + 2 + kl;
            while (*v == ' ') v++;
            snprintf(out, sz, "%.*s", (int)(l - (size_t)(v - p)), v);
            return 1;
        }
        if (!e) break;
        p = e + 1;
    }
    return 0;
}

static int dmid_count(int type)
{
    const char *t = dmid(), *p = t;
    char pat[32];
    int n = 0;
    if (!t) return 0;
    snprintf(pat, sizeof pat, "DMI type %d,", type);
    while ((p = strstr(p, pat))) { n++; p += strlen(pat); }
    return n;
}

/* "SMBIOS 3.0.0 present." / "13 structures occupying 3414 bytes." / "Table at 0x..." */
static int dmid_line(const char *prefix, const char *contains, char *out, size_t sz)
{
    const char *t = dmid(), *p;
    out[0] = 0;
    if (!t) return 0;
    for (p = t; *p; ) {
        const char *e = strchr(p, '\n');
        size_t l = e ? (size_t)(e - p) : strlen(p);
        if ((!prefix || !strncmp(p, prefix, strlen(prefix))) && (!contains || (strstr(p, contains) && strstr(p, contains) < p + l))) {
            snprintf(out, sz, "%.*s", (int)l, p);
            return 1;
        }
        if (!e) break;
        p = e + 1;
    }
    return 0;
}

/* DMI value: unprivileged sysfs first, then the dmidecode cache; "" when neither */
static void dmi_or_cache(const char *sysf, int type, const char *key, char *b, size_t sz)
{
    dmi_rd(sysf, b, sz);
    if (!*b) dmid_get(type, 0, key, b, sz);
}

/* ======================================================================
 * udev SMBIOS memory import (/run/udev/data/+dmi:id)
 * ====================================================================== */
typedef struct { char k[64], v[160]; } ukv;
static ukv g_u[320];
static int g_nu = -1;

static void udev_load(void)
{
    char *txt = NULL, *p, *save;
    int from_cmd = 0;
    if (g_nu >= 0) return;
    g_nu = 0;
    if (rd_all("/run/udev/data/+dmi:id", &txt) < 0) {
        static const char *const argv[] = { "udevadm", "info", "-q", "property", "-p", "/devices/virtual/dmi/id", NULL };
        free(txt);
        txt = NULL;
        if (run_out(&txt, argv) != 0) { free(txt); return; }
        from_cmd = 1;
    }
    for (p = strtok_r(txt, "\n", &save); p && g_nu < (int)(sizeof g_u / sizeof *g_u); p = strtok_r(NULL, "\n", &save)) {
        char *eq;
        if (!from_cmd) { if (strncmp(p, "E:", 2)) continue; p += 2; }
        eq = strchr(p, '=');
        if (!eq) continue;
        snprintf(g_u[g_nu].k, sizeof g_u[g_nu].k, "%.*s", (int)(eq - p), p);
        snprintf(g_u[g_nu].v, sizeof g_u[g_nu].v, "%s", eq + 1);
        g_nu++;
    }
    free(txt);
}

static const char *uget(const char *k)
{
    int i;
    udev_load();
    for (i = 0; i < g_nu; i++) if (!strcmp(g_u[i].k, k)) return g_u[i].v;
    return "";
}

typedef struct { char hw[32], dev[32]; int addr; } spdmon;
static int spd_monitors(spdmon *m, int max);

/* ======================================================================
 * memory slots: udev first, dmidecode type 17 second
 * ====================================================================== */
typedef struct {
    int present;
    char loc[48], bank[48], size[32], type[24], detail[64], form[24], speed[24], cspeed[24], manuf[48], part[48],
         serial[48], rank[8], volt[16], mfid[48], tech[24];
    double bytes;
    char spd_dev[16];   /* "1-0051" when an spd5118 client sits at 0x50+slot */
} slot_t;

static int slots_get(slot_t *s, int max, int *src_udev)
{
    int n = 0, i;
    char k[96];
    udev_load();
    *src_udev = 0;
    for (i = 0; i < max; i++) {
        const char *v;
        slot_t *d = &s[i];
        snprintf(k, sizeof k, "MEMORY_DEVICE_%d_LOCATOR", i);
        if (!*uget(k)) break;
        memset(d, 0, sizeof *d);
#define UF(dst, f) do { snprintf(k, sizeof k, "MEMORY_DEVICE_%d_" f, i); snprintf(dst, sizeof dst, "%s", uget(k)); } while (0)
        UF(d->loc, "LOCATOR"); UF(d->bank, "BANK_LOCATOR"); UF(d->type, "TYPE"); UF(d->detail, "TYPE_DETAIL");
        UF(d->form, "FORM_FACTOR"); UF(d->manuf, "MANUFACTURER"); UF(d->part, "PART_NUMBER"); UF(d->serial, "SERIAL_NUMBER");
        UF(d->rank, "RANK"); UF(d->mfid, "MODULE_MANUFACTURER_ID"); UF(d->tech, "MEMORY_TECHNOLOGY");
        UF(d->volt, "CONFIGURED_VOLTAGE");
#undef UF
        snprintf(k, sizeof k, "MEMORY_DEVICE_%d_SIZE", i);
        v = uget(k);
        d->bytes = *v ? atof(v) : 0;
        d->present = d->bytes > 0;
        if (d->present) fmt_bytes(d->bytes, d->size, sizeof d->size);
        snprintf(k, sizeof k, "MEMORY_DEVICE_%d_SPEED_MTS", i);
        if (*uget(k)) snprintf(d->speed, sizeof d->speed, "%s MT/s", uget(k));
        snprintf(k, sizeof k, "MEMORY_DEVICE_%d_CONFIGURED_SPEED_MTS", i);
        if (*uget(k)) snprintf(d->cspeed, sizeof d->cspeed, "%s MT/s", uget(k));
        n++;
    }
    if (n) *src_udev = 1;
    else {
        int c = dmid_count(17);
        for (i = 0; i < c && i < max; i++) {
            slot_t *d = &s[i];
            char sz[64];
            memset(d, 0, sizeof *d);
            dmid_get(17, i, "Locator", d->loc, sizeof d->loc);
            dmid_get(17, i, "Bank Locator", d->bank, sizeof d->bank);
            dmid_get(17, i, "Type", d->type, sizeof d->type);
            dmid_get(17, i, "Type Detail", d->detail, sizeof d->detail);
            dmid_get(17, i, "Form Factor", d->form, sizeof d->form);
            dmid_get(17, i, "Speed", d->speed, sizeof d->speed);
            if (!dmid_get(17, i, "Configured Memory Speed", d->cspeed, sizeof d->cspeed))
                dmid_get(17, i, "Configured Clock Speed", d->cspeed, sizeof d->cspeed);
            dmid_get(17, i, "Manufacturer", d->manuf, sizeof d->manuf);
            dmid_get(17, i, "Part Number", d->part, sizeof d->part);
            dmid_get(17, i, "Serial Number", d->serial, sizeof d->serial);
            dmid_get(17, i, "Rank", d->rank, sizeof d->rank);
            dmid_get(17, i, "Configured Voltage", d->volt, sizeof d->volt);
            dmid_get(17, i, "Memory Technology", d->tech, sizeof d->tech);
            dmid_get(17, i, "Module Manufacturer ID", d->mfid, sizeof d->mfid);
            dmid_get(17, i, "Size", sz, sizeof sz);
            if (*sz && strncmp(sz, "No Module", 9)) {
                double v = atof(sz);
                d->present = 1;
                d->bytes = v * (strstr(sz, "GB") ? 1073741824.0 : strstr(sz, "MB") ? 1048576.0 : 1.0);
                snprintf(d->size, sizeof d->size, "%s", sz);
            }
            n++;
        }
    }
    {   /* pair slot i with the spd5118 client at SMBus address 0x50+i (inferred) */
        spdmon m[8];
        int nm = spd_monitors(m, 8), k;
        for (i = 0; i < n; i++)
            for (k = 0; k < nm; k++)
                if (m[k].addr == 0x50 + i) snprintf(s[i].spd_dev, sizeof s[i].spd_dev, "%s", m[k].dev);
    }
    return n;
}

/* ======================================================================
 * spd5118 hwmon (DIMM thermal sensors)
 * ====================================================================== */
static int spd_monitors(spdmon *m, int max)
{
    char **e;
    int ne = dir_list("/sys/class/hwmon", &e), i, n = 0;
    for (i = 0; i < ne && n < max; i++) {
        char nm[64], p[256];
        if (!rd_fmt(nm, sizeof nm, "/sys/class/hwmon/%s/name", e[i]) || strcmp(nm, "spd5118")) continue;
        snprintf(m[n].hw, sizeof m[n].hw, "%s", e[i]);
        snprintf(p, sizeof p, "/sys/class/hwmon/%s/device", e[i]);
        link_base(p, m[n].dev, sizeof m[n].dev);
        m[n].addr = strchr(m[n].dev, '-') ? (int)strtol(strchr(m[n].dev, '-') + 1, NULL, 16) : -1;
        n++;
    }
    dir_free(e, ne);
    /* sort by device name */
    for (i = 1; i < n; i++) {
        int j = i;
        while (j > 0 && strcmp(m[j - 1].dev, m[j].dev) > 0) { spdmon t = m[j]; m[j] = m[j - 1]; m[j - 1] = t; j--; }
    }
    return n;
}

static void milli(const char *hw, const char *attr, char *b, size_t sz)
{
    long v;
    char p[256];
    snprintf(p, sizeof p, "/sys/class/hwmon/%s/%s", hw, attr);
    if (rd_long(p, &v)) snprintf(b, sz, "%.1f°C", v / 1000.0); else snprintf(b, sz, "-");
}

/* ======================================================================
 * ACPI table helpers
 * ====================================================================== */
typedef struct { char sig[5], oem[7], oemtab[9], creator[5]; uint32_t len, oemrev, crrev; uint8_t rev, csum; } acpi_hdr;

static int acpi_hdr_parse(const unsigned char *d, int n, acpi_hdr *h)
{
    int i;
    if (n < 36) return 0;
    memset(h, 0, sizeof *h);
    memcpy(h->sig, d, 4);
    h->len = le32(d + 4);
    h->rev = d[8];
    h->csum = d[9];
    memcpy(h->oem, d + 10, 6);
    memcpy(h->oemtab, d + 16, 8);
    h->oemrev = le32(d + 24);
    memcpy(h->creator, d + 28, 4);
    h->crrev = le32(d + 32);
    for (i = 0; i < 6; i++) if (!isprint((unsigned char)h->oem[i])) h->oem[i] = ' ';
    for (i = 0; i < 8; i++) if (!isprint((unsigned char)h->oemtab[i])) h->oemtab[i] = ' ';
    for (i = 0; i < 4; i++) { if (!isprint((unsigned char)h->creator[i])) h->creator[i] = ' '; if (!isprint((unsigned char)h->sig[i])) h->sig[i] = '?'; }
    return 1;
}

/* table bytes from the srs-root cache (malloc); returns length or -1 */
static int acpi_load(const char *sig, unsigned char **d)
{
    char name[64], p[1100];
    char *b;
    int n;
    *d = NULL;
    if (!safe_arg(sig)) return -1;
    snprintf(name, sizeof name, "acpi-tables/%s", sig);
    if (!root_path(name, p, sizeof p)) return -1;
    n = rd_all(p, &b);
    if (n < 0) return -1;
    *d = (unsigned char *)b;
    return n;
}

static int acpi_present(const char *sig)
{
    char p[256];
    snprintf(p, sizeof p, ACPI_TAB "/%s", sig);
    return path_exists(p);
}

/* sorted list of tables (incl. the dynamic dir) */
static int acpi_list(char ***out)
{
    char **e, **dy, **v;
    int ne = dir_list(ACPI_TAB, &e), nd = dir_list(ACPI_TAB "/dynamic", &dy), i, n = 0;
    v = malloc(sizeof *v * (size_t)((ne > 0 ? ne : 0) + (nd > 0 ? nd : 0) + 1));
    if (!v) { dir_free(e, ne); dir_free(dy, nd); *out = NULL; return 0; }
    for (i = 0; i < ne; i++) if (strcmp(e[i], "data") && strcmp(e[i], "dynamic")) v[n++] = strdup(e[i]);
    for (i = 0; i < nd; i++) { char b[96]; snprintf(b, sizeof b, "dynamic/%s", dy[i]); v[n++] = strdup(b); }
    dir_free(e, ne);
    dir_free(dy, nd);
    nat_sort(v, n);
    *out = v;
    return n;
}
static void list_free(char **v, int n) { int i; for (i = 0; i < n; i++) free(v[i]); free(v); }

static void acpi_hdr_rows(srs_out *o, const acpi_hdr *h)
{
    so_kvf(o, "Signature", C_BLACK, "%s", h->sig);
    so_kvf(o, "Length", C_VALUE, "%u (0x%X)", h->len, h->len);
    so_kvf(o, "Revision", C_VALUE, "%u", h->rev);
    so_kvf(o, "Checksum", C_VALUE, "0x%02X", h->csum);
    so_kvf(o, "OEM ID", C_TEAL, "%s", h->oem);
    so_kvf(o, "OEM Table ID", C_TEAL, "%s", h->oemtab);
    so_kvf(o, "OEM Revision", C_VALUE, "0x%08X", h->oemrev);
    so_kvf(o, "Creator ID", C_PURPLE, "%s", h->creator);
    so_kvf(o, "Creator Revision", C_PURPLE, "0x%08X", h->crrev);
}

static void present_tables_line(srs_out *o)
{
    char **v, line[2048] = "";
    int n = acpi_list(&v), i;
    size_t p = 0;
    for (i = 0; i < n; i++) p += (size_t)snprintf(line + p, p < sizeof line ? sizeof line - p : 0, "%s%s", i ? " " : "", v[i]);
    list_free(v, n);
    so_kvf(o, "Tables present", C_PURPLE, "%d: %s", n, line);
}

/* ======================================================================
 * ACPI device helpers
 * ====================================================================== */
static void adev(const char *dev, const char *attr, char *b, size_t sz) { rd_fmt(b, sz, ACPI_DEV "/%s/%s", dev, attr); }

static void adev_phys(const char *dev, char *b, size_t sz)
{
    char p[256], t[1024];
    ssize_t n;
    snprintf(p, sizeof p, ACPI_DEV "/%s/physical_node", dev);
    n = readlink(p, t, sizeof t - 1);
    b[0] = 0;
    if (n <= 0) return;
    t[n] = 0;
    {
        const char *q = t;
        while (!strncmp(q, "../", 3)) q += 3;
        snprintf(b, sz, "%s", q);
    }
}

static void adev_driver(const char *dev, char *b, size_t sz)
{
    char p[256];
    snprintf(p, sizeof p, ACPI_DEV "/%s/physical_node/driver", dev);
    if (!link_base(p, b, sz)) snprintf(b, sz, "-");
}

static const char *sta_str(const char *s, char *b, size_t sz)
{
    long v;
    if (!*s) return "-";
    v = strtol(s, NULL, 0);
    snprintf(b, sz, "0x%02lX %s%s%s%s", v, v & 1 ? "P" : "-", v & 2 ? "E" : "-", v & 4 ? "S" : "-", v & 8 ? "F" : "-");
    return b;
}

/* acpi devices whose name starts with one of the HID prefixes */
static int acpi_probe(srs_out *o, const char *const *hids)
{
    char **e;
    int ne = dir_list(ACPI_DEV, &e), i, j, hits = 0;
    for (i = 0; i < ne; i++)
        for (j = 0; hids[j]; j++)
            if (!strncmp(e[i], hids[j], strlen(hids[j]))) {
                char path[160];
                adev(e[i], "path", path, sizeof path);
                so_kvf(o, e[i], C_VALUE, "%s", path);
                hits++;
            }
    dir_free(e, ne);
    return hits;
}

/* ======================================================================
 * EFI variable helpers
 * ====================================================================== */
static int efivar(const char *name, const char *guid, unsigned char **d)
{
    char p[512];
    char *b;
    int n;
    *d = NULL;
    snprintf(p, sizeof p, EFIVARS "/%s-%s", name, guid);
    n = rd_all(p, &b);
    if (n < 4) { free(b); return -1; }
    *d = (unsigned char *)b;
    return n - 4;   /* data at +4 (attributes first) */
}

static void ucs2(const unsigned char *p, int n, char *out, size_t sz)
{
    size_t k = 0;
    int i;
    for (i = 0; i + 1 < n && k + 1 < sz; i += 2) {
        uint16_t c = le16(p + i);
        if (!c) break;
        out[k++] = c < 128 && isprint(c) ? (char)c : '?';
    }
    out[k] = 0;
}

static int efi_u8(const char *name, const char *guid)
{
    unsigned char *d;
    int n = efivar(name, guid, &d), v = -1;
    if (n >= 1) v = d[4];
    free(d);
    return v;
}

static const char *secure_boot_str(void)
{
    int sb = efi_u8("SecureBoot", EFI_GLOBAL);
    return sb < 0 ? "Unknown" : sb ? "Enabled" : "Disabled";
}

/* ======================================================================
 * [machine]  SIV Machine panel: SMBIOS header, BIOS, System, Chassis, Board, CPU socket, boot
 * ====================================================================== */
static const char *chassis_name(int t)
{
    static const char *const n[] = { "", "Other", "Unknown", "Desktop", "Low Profile Desktop", "Pizza Box", "Mini Tower", "Tower",
                                     "Portable", "Laptop", "Notebook", "Hand Held", "Docking Station", "All in One", "Sub Notebook",
                                     "Space-saving", "Lunch Box", "Main Server Chassis", "Expansion Chassis", "SubChassis",
                                     "Bus Expansion Chassis", "Peripheral Chassis", "RAID Chassis", "Rack Mount Chassis",
                                     "Sealed-case PC", "Multi-system chassis", "Compact PCI", "Advanced TCA", "Blade",
                                     "Blade Enclosure", "Tablet", "Convertible", "Detachable", "IoT Gateway", "Embedded PC",
                                     "Mini PC", "Stick PC" };
    t &= 0x7F;
    return t > 0 && t < (int)(sizeof n / sizeof *n) ? n[t] : "Unknown";
}

static void smbios_ver(char *b, size_t sz, char *count, size_t csz)
{
    char l[160];
    long long ep = fsize("/sys/firmware/dmi/tables/smbios_entry_point");
    count[0] = 0;
    if (dmid_line("SMBIOS ", "present", l, sizeof l)) {
        char v[32] = "";
        sscanf(l, "SMBIOS %31s", v);
        snprintf(b, sz, "SMBios V%s", v);
    } else if (ep == 24) snprintf(b, sz, "SMBios V3.x");
    else if (ep == 31) snprintf(b, sz, "SMBios V2.x");
    else snprintf(b, sz, "SMBios");
    if (dmid_line(NULL, "structures occupying", l, sizeof l)) snprintf(count, csz, "Count %d", atoi(l));
    else {   /* dmidecode 3.6+ no longer prints the summary line: count the Handle records instead */
        const char *f = so_root_file("dmidecode.txt");
        char *txt, *q;
        int n = 0;
        if (f && rd_all(f, &txt) >= 0) {
            for (q = txt; (q = strstr(q, "\nHandle 0x")); q++) n++;
            free(txt);
            if (n) snprintf(count, csz, "Count %d", n);
        }
    }
}

static void s_machine(srs_out *o)
{
    static const int tabs[] = { 154, 350, 546 };
    char ver[64], cnt[32], b1[256], b2[256], b3[256], b4[256], v[128], cpu[192], ser[128];
    long long dsz = fsize("/sys/firmware/dmi/tables/DMI");
    int need_root = 0, ct = 0, nsl, src, i, pop = 0;
    slot_t sl[16];

    so_siv_header(o, "Machine");
    so_tabs(o, tabs, 3);

    smbios_ver(ver, sizeof ver, cnt, sizeof cnt);
    snprintf(b2, sizeof b2, "Size %lld (%04llX)", dsz < 0 ? 0 : dsz, dsz < 0 ? 0 : dsz);
    so_row(o, 4, (const char *const[]){ ver, *cnt ? cnt : "Count (needs root)", b2, "Description" },
           (const int[]){ C_TEAL, C_TEAL, C_VALUE, C_TEAL });
    so_par(o);

    /* BIOS */
    dmi_rd("bios_version", v, sizeof v);
    snprintf(b1, sizeof b1, "BIOS %s", v);
    dmi_rd("bios_date", v, sizeof v);
    snprintf(b2, sizeof b2, "Date %s", v);
    dmi_rd("bios_release", v, sizeof v);
    snprintf(b3, sizeof b3, "Release %s", *v ? v : "-");
    dmi_rd("bios_vendor", v, sizeof v);
    {
        char rom[48] = "", addr[48] = "";
        dmid_get(0, 0, "ROM Size", rom, sizeof rom);
        dmid_get(0, 0, "Address", addr, sizeof addr);
        snprintf(b4, sizeof b4, "%s%s%s%s%s", v, *addr ? "  ROM Base " : "", addr, *rom ? "  Size " : "", rom);
    }
    so_row(o, 4, (const char *const[]){ b1, b2, b3, b4 }, (const int[]){ C_VALUE, C_GRAY, C_VALUE, C_VALUE });

    /* System */
    dmi_rd("product_name", v, sizeof v);
    snprintf(b2, sizeof b2, "%s", na(v));
    dmi_rd("product_version", v, sizeof v);
    snprintf(b3, sizeof b3, "%s", na(v));
    dmi_rd("sys_vendor", v, sizeof v);
    dmi_or_cache("product_uuid", 1, "UUID", ser, sizeof ser);
    if (!*ser) { need_root = 1; snprintf(ser, sizeof ser, "(needs root)"); }
    snprintf(b4, sizeof b4, "%s  UUID {%s}", na(v), ser);
    so_row(o, 4, (const char *const[]){ "System", b2, b3, b4 }, (const int[]){ C_VALUE, C_VALUE, C_VALUE, C_GREEN });
    dmi_rd("product_family", v, sizeof v);
    snprintf(b2, sizeof b2, "Family %s", na(v));
    dmi_rd("product_sku", v, sizeof v);
    snprintf(b3, sizeof b3, "SKU %s", na(v));
    dmi_or_cache("product_serial", 1, "Serial Number", ser, sizeof ser);
    if (!*ser) { need_root = 1; snprintf(ser, sizeof ser, "(needs root)"); }
    snprintf(b4, sizeof b4, "Serial %s", na(ser));
    so_row(o, 4, (const char *const[]){ "", b2, b3, b4 }, (const int[]){ C_VALUE, C_GRAY, C_GRAY, C_GRAY });
    so_par(o);

    /* Chassis */
    dmi_rd("chassis_type", v, sizeof v);
    ct = atoi(v);
    dmi_rd("chassis_version", v, sizeof v);
    snprintf(b1, sizeof b1, "Chassis %s %s", na(v), chassis_name(ct));
    dmi_rd("chassis_vendor", v, sizeof v);
    snprintf(b2, sizeof b2, "%s", na(v));
    dmi_or_cache("chassis_serial", 3, "Serial Number", ser, sizeof ser);
    if (!*ser) { need_root = 1; snprintf(ser, sizeof ser, "(needs root)"); }
    dmi_rd("chassis_asset_tag", v, sizeof v);
    so_row(o, 4, (const char *const[]){ b1, b2, na(ser), na(v) }, (const int[]){ C_VALUE, C_VALUE, C_VALUE, C_VALUE });

    /* Board */
    dmi_rd("board_version", v, sizeof v);
    snprintf(b1, sizeof b1, "Board %s", na(v));
    dmi_rd("board_name", v, sizeof v);
    snprintf(b2, sizeof b2, "%s", v);
    dmi_or_cache("board_serial", 2, "Serial Number", ser, sizeof ser);
    if (!*ser) { need_root = 1; snprintf(ser, sizeof ser, "(needs root)"); }
    dmi_rd("board_vendor", v, sizeof v);
    so_row(o, 4, (const char *const[]){ b1, b2, ser, v }, (const int[]){ C_VALUE, C_VALUE, C_VALUE, C_VALUE });

    /* CPU socket (SMBIOS type 4) */
    {
        char sock[64] = "", vend[64] = "", maxs[32] = "", ext[32] = "", cores[16] = "", thr[16] = "", volt[32] = "";
        char *txt;
        snprintf(cpu, sizeof cpu, "%s", sys_cpu_name());
        if (rd_all("/proc/cpuinfo", &txt) >= 0) {
            char *p = strstr(txt, "vendor_id");
            if (p && strchr(p, ':')) { sscanf(strchr(p, ':') + 1, " %63s", vend); }
            free(txt);
        }
        dmid_get(4, 0, "Socket Designation", sock, sizeof sock);
        dmid_get(4, 0, "Max Speed", maxs, sizeof maxs);
        dmid_get(4, 0, "External Clock", ext, sizeof ext);
        dmid_get(4, 0, "Core Count", cores, sizeof cores);
        dmid_get(4, 0, "Thread Count", thr, sizeof thr);
        dmid_get(4, 0, "Voltage", volt, sizeof volt);
        if (!*cores) {
            int c = 0, k;
            for (k = 0; k < (int)sysconf(_SC_NPROCESSORS_CONF); k++) {
                char sib[64];
                if (rd_fmt(sib, sizeof sib, "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", k) && atoi(sib) == k) c++;
            }
            snprintf(cores, sizeof cores, "%d", c);
        }
        if (!*thr) snprintf(thr, sizeof thr, "%ld", sysconf(_SC_NPROCESSORS_CONF));
        if (!*sock) need_root = 1;
        snprintf(b1, sizeof b1, "CPU-0 Socket #0");
        snprintf(b3, sizeof b3, "%s", *sock ? sock : "Socket (needs root)");
        snprintf(b4, sizeof b4, "%s  Max %s  Ext %s  Cores %s  Threads %s%s%s", cpu, *maxs ? maxs : "-", *ext ? ext : "-",
                 cores, thr, *volt ? "  " : "", volt);
        so_row(o, 4, (const char *const[]){ b1, vend, b3, b4 }, (const int[]){ C_VALUE, C_BLACK, C_VALUE, C_BLACK });
    }

    /* System boot (type 32) */
    {
        char st[96] = "";
        if (!dmid_get(32, 0, "Status", st, sizeof st)) snprintf(st, sizeof st, "%s", dmid() ? "-" : "(needs root)");
        so_row(o, 4, (const char *const[]){ "System Boot", "Status", st, *secure_boot_str() == 'E' ? "UEFI  Secure Boot on" :
                                            path_exists(EFI_DIR) ? "UEFI  Secure Boot off" : "Legacy BIOS" },
               (const int[]){ C_VALUE, C_GRAY, C_VALUE, C_PURPLE });
    }
    so_par(o);

    /* Memory array (type 16) + devices (type 17) */
    {
        const char *mc = uget("MEMORY_ARRAY_MAX_CAPACITY"), *loc = uget("MEMORY_ARRAY_LOCATION"), *ec = uget("MEMORY_ARRAY_EC_TYPE");
        char cap[32] = "-", tot[32];
        double sum = 0;
        nsl = slots_get(sl, 16, &src);
        for (i = 0; i < nsl; i++) if (sl[i].present) { pop++; sum += sl[i].bytes; }
        if (*mc) fmt_bytes(atof(mc), cap, sizeof cap);
        else dmid_get(16, 0, "Maximum Capacity", cap, sizeof cap);
        fmt_bytes(sum, tot, sizeof tot);
        snprintf(b1, sizeof b1, "Memory Array");
        snprintf(b2, sizeof b2, "%s", *loc ? loc : "-");
        snprintf(b3, sizeof b3, "Maximum %s  ECC %s", cap, *ec ? ec : "None");
        snprintf(b4, sizeof b4, "DIMMs %d of %d  Total %s", pop, nsl, tot);
        so_row(o, 4, (const char *const[]){ b1, b2, b3, b4 }, (const int[]){ C_VALUE, C_VALUE, C_VALUE, C_TEAL });
        for (i = 0; i < nsl; i++) {
            snprintf(b1, sizeof b1, "%s", sl[i].loc);
            snprintf(b2, sizeof b2, "%s", sl[i].bank);
            if (sl[i].present) {
                snprintf(b3, sizeof b3, "%s %s %s", sl[i].size, sl[i].type, sl[i].cspeed);
                snprintf(b4, sizeof b4, "%s  %s", sl[i].manuf, sl[i].part);
            } else { snprintf(b3, sizeof b3, "Empty"); b4[0] = 0; }
            so_row(o, 4, (const char *const[]){ b1, b2, b3, b4 },
                   (const int[]){ C_LABEL, C_GRAY, sl[i].present ? C_VALUE : C_GRAY, C_PURPLE });
        }
        if (!nsl) { so_note(o, "No SMBIOS memory devices: udev import absent and no dmidecode cache"); need_root = 1; }
    }
    if (need_root && !dmid()) needs_root(o);
    so_board_footer(o);
}

/* ======================================================================
 * [dimms]  per-slot table (SMBIOS type 17 via udev / dmidecode; spd5118 temps)
 * ====================================================================== */
static void dimm_detail(srs_out *o, int idx)
{
    static const int tabs[] = { 210 };
    char pre[48];
    int i, n = 0;
    so_tabs(o, tabs, 1);
    snprintf(pre, sizeof pre, "MEMORY_DEVICE_%d_", idx);
    udev_load();
    for (i = 0; i < g_nu; i++)
        if (!strncmp(g_u[i].k, pre, strlen(pre))) { so_kv(o, g_u[i].k + strlen(pre), C_VALUE, g_u[i].v); n++; }
    if (!n) {
        /* dmidecode instance */
        const char *t = dmid();
        int c = dmid_count(17);
        if (t && idx < c) {
            static const char *const keys[] = { "Locator", "Bank Locator", "Size", "Type", "Type Detail", "Form Factor", "Speed",
                                                "Configured Memory Speed", "Manufacturer", "Part Number", "Serial Number", "Rank",
                                                "Minimum Voltage", "Maximum Voltage", "Configured Voltage", "Total Width", "Data Width",
                                                NULL };
            char v[160];
            for (i = 0; keys[i]; i++) if (dmid_get(17, idx, keys[i], v, sizeof v)) { so_kv(o, keys[i], C_VALUE, v); n++; }
        }
    }
    if (!n) so_err(o, "No such memory device");
    /* spd5118 sensor */
    {
        spdmon m[8];
        int nm = spd_monitors(m, 8);

        for (i = 0; i < nm; i++)
            if (m[i].addr == 0x50 + idx) {
                char t[32];
                milli(m[i].hw, "temp1_input", t, sizeof t);
                so_kvf(o, "SPD5118 sensor", C_ORANGE, "%s %s  %s (slot pairing inferred from SPD address 0x50+index)", m[i].dev, m[i].hw, t);
            }
    }
}

static void s_dimms(srs_out *o)
{
    static const int tabs[] = { 60, 160, 220, 265, 315, 395, 475, 515, 610, 740, 830, 900 };
    static const char *const h[] = { "Slot", "Bank", "Size", "Type", "Form", "Rated", "Configured", "Rank", "Manufacturer",
                                     "Part Number", "Serial", "SPD", "Temp" };
    slot_t sl[16];
    int src, n = slots_get(sl, 16, &src), i, pop = 0;
    double sum = 0;
    char b[64];

    so_siv_header(o, "DIMMs");
    if (o->arg && *o->arg) { dimm_detail(o, atoi(o->arg)); so_board_footer(o); return; }
    so_tabs(o, tabs, 12);
    so_row(o, 13, h, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL,
                                    C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (i = 0; i < n; i++) {
        char temp[32] = "-", slot[16];
        if (*sl[i].spd_dev) {
            char p[128], **e;
            int ne;
            snprintf(p, sizeof p, "/sys/bus/i2c/drivers/spd5118/%s/hwmon", sl[i].spd_dev);
            ne = dir_list(p, &e);
            if (ne > 0) milli(e[0], "temp1_input", temp, sizeof temp);
            dir_free(e, ne);
        }
        snprintf(slot, sizeof slot, "%d", i);
        if (sl[i].present) {
            pop++;
            sum += sl[i].bytes;
            so_row(o, 13, (const char *const[]){ sl[i].loc, sl[i].bank, sl[i].size, sl[i].type, sl[i].form, sl[i].speed,
                                                 sl[i].cspeed, sl[i].rank, sl[i].manuf, sl[i].part, sl[i].serial,
                                                 *sl[i].spd_dev ? sl[i].spd_dev : "-", temp },
                   (const int[]){ C_LABEL, C_GRAY, C_BLACK, C_TEAL, C_VALUE, C_VALUE, C_GREEN, C_VALUE, C_PURPLE, C_PURPLE,
                                  C_GRAY, C_GRAY, C_ORANGE });
        } else {
            so_row(o, 3, (const char *const[]){ sl[i].loc, sl[i].bank, "Empty" }, (const int[]){ C_LABEL, C_GRAY, C_GRAY });
        }
    }
    so_par(o);
    if (!n) { so_err(o, "No memory devices found (udev SMBIOS import missing, no dmidecode cache)"); needs_root(o); }
    else {
        fmt_bytes(sum, b, sizeof b);
        so_f(o, C_TEAL, "%d of %d slots populated, %s installed", pop, n, b);
        so_par(o);
        so_note(o, src ? "Source: SMBIOS type 17 as imported by systemd-udevd (/run/udev/data/+dmi:id)" :
                         "Source: srs-root cache dmidecode.txt (type 17)");
        so_note(o, "SPD column: spd5118 client at SMBus address 0x50+slot index (pairing inferred). Detail: dimms:<slot>");
    }
    so_board_footer(o);
}

/* ======================================================================
 * [dimm-status] / [spd-temp]  spd5118 thermal sensors
 * ====================================================================== */
static void spd_temps(srs_out *o, const char *what)
{
    static const int tabs[] = { 80, 150, 210, 280, 350, 420, 490, 560, 640 };
    static const char *const h[] = { "Sensor", "Device", "Slot", "Temp", "Min", "Max", "Crit", "LCrit", "Alarms", "Enabled" };
    spdmon m[8];
    slot_t sl[16];
    int n = spd_monitors(m, 8), src, ns = slots_get(sl, 16, &src), i;
    so_siv_header(o, what);
    if (!n) {
        so_err(o, "No spd5118 hwmon sensors (driver not bound, or DIMMs without a thermal sensor)");
        so_note(o, "DDR5 DIMM sensors appear when the spd5118 module binds to SMBus addresses 0x50-0x57");
        so_board_footer(o);
        return;
    }
    so_tabs(o, tabs, 9);
    so_row(o, 10, h, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (i = 0; i < n; i++) {
        char c[10][64], al[64] = "";
        const char *cells[10];
        long a, en = -1;
        int k, idx = m[i].addr >= 0x50 ? m[i].addr - 0x50 : -1;
        static const char *const alarms[] = { "temp1_min_alarm", "temp1_max_alarm", "temp1_crit_alarm", "temp1_lcrit_alarm", NULL };
        snprintf(c[0], 64, "%s", m[i].hw);
        snprintf(c[1], 64, "%s", m[i].dev);
        if (idx >= 0 && idx < ns) snprintf(c[2], 64, "%s %s", sl[idx].bank + (strncmp(sl[idx].bank, "P0 ", 3) ? 0 : 3), sl[idx].loc);
        else snprintf(c[2], 64, "-");
        /* compact slot label: "CHANNEL A DIMM 1" -> "A1" */
        if (idx >= 0 && idx < ns) {
            const char *ch = strstr(sl[idx].bank, "CHANNEL ");
            const char *d = strrchr(sl[idx].loc, ' ');
            if (ch && d) snprintf(c[2], 64, "%c%s", ch[8], d + 1);
        }
        milli(m[i].hw, "temp1_input", c[3], 64);
        milli(m[i].hw, "temp1_min", c[4], 64);
        milli(m[i].hw, "temp1_max", c[5], 64);
        milli(m[i].hw, "temp1_crit", c[6], 64);
        milli(m[i].hw, "temp1_lcrit", c[7], 64);
        for (k = 0; alarms[k]; k++) {
            char p[160];
            snprintf(p, sizeof p, "/sys/class/hwmon/%s/%s", m[i].hw, alarms[k]);
            if (rd_long(p, &a) && a) { strncat(al, alarms[k] + 6, sizeof al - strlen(al) - 2); strcat(al, " "); }
        }
        snprintf(c[8], 64, "%s", *al ? al : "none");
        {
            char p[160];
            snprintf(p, sizeof p, "/sys/class/hwmon/%s/temp1_enable", m[i].hw);
            rd_long(p, &en);
        }
        snprintf(c[9], 64, "%s", en < 0 ? "-" : en ? "yes" : "no");
        for (k = 0; k < 10; k++) cells[k] = c[k];
        so_row(o, 10, cells, (const int[]){ C_LABEL, C_GRAY, C_TEAL, C_BLACK, C_GRAY, C_GRAY, C_GRAY, C_GRAY,
                                            *al ? C_RED : C_GREEN, C_VALUE });
    }
    so_par(o);
    so_note(o, "Read from spd5118 hwmon attributes only; the SPD EEPROM is not opened. Slot = SMBus 0x50+index (inferred).");
    so_board_footer(o);
}
static void s_dimm_status(srs_out *o) { spd_temps(o, "DIMM Status"); }
static void s_spd_temp(srs_out *o) { spd_temps(o, "SPD Thermal Sensors"); }

/* ======================================================================
 * [spd]  decode-dimms output from the srs-root cache, else SMBIOS summary
 * ====================================================================== */
static void s_spd(srs_out *o)
{
    static const int tabs[] = { 200 };
    char p[1100], *txt;
    slot_t sl[16];
    int src, n, i;
    so_siv_header(o, "SPD");
    if (root_path("spd.txt", p, sizeof p) && rd_all(p, &txt) >= 0 && strstr(txt, "decoded: 0")) {
        /* decode-dimms found nothing (e.g. DDR5 behind spd5118, which it does not read): use SMBIOS */
        so_note(o, "srs-root cache spd.txt: decode-dimms decoded 0 DIMMs (DDR5 SPD hubs are not supported by it);");
        so_note(o, "showing the SMBIOS type 17 summary instead.");
        so_par(o);
        free(txt);
    } else if (root_path("spd.txt", p, sizeof p) && rd_all(p, &txt) >= 0) {
        so_note(o, "srs-root cache spd.txt (decode-dimms):");
        so_par(o);
        so_lines(o, C_VALUE, txt);
        free(txt);
        so_board_footer(o);
        return;
    }
    so_tabs(o, tabs, 1);
    n = slots_get(sl, 16, &src);
    for (i = 0; i < n; i++) {
        if (!sl[i].present) continue;
        so_b(o, C_VALUE, sl[i].bank);
        so_t(o, C_VALUE, "  ");
        so_b(o, C_VALUE, sl[i].loc);
        so_par(o);
        so_kv(o, "Module Manufacturer", C_PURPLE, sl[i].manuf);
        so_kv(o, "Manufacturer ID (JEDEC)", C_GRAY, sl[i].mfid);
        so_kv(o, "Part Number", C_PURPLE, sl[i].part);
        so_kv(o, "Size", C_BLACK, sl[i].size);
        so_kvf(o, "Type", C_TEAL, "%s %s %s", sl[i].type, sl[i].form, sl[i].detail);
        so_kv(o, "Rated Speed (SPD/JEDEC)", C_VALUE, sl[i].speed);
        so_kv(o, "Configured Speed", C_GREEN, sl[i].cspeed);
        so_kv(o, "Ranks", C_VALUE, sl[i].rank);
        so_kv(o, "SPD Hub", C_GRAY, *sl[i].spd_dev ? sl[i].spd_dev : "-");
        so_par(o);
    }
    so_note(o, "Full SPD decode (timings, XMP/EXPO profiles, manufacture date) comes from the srs-root cache spd.txt.");
    so_note(o, "srs never reads the SPD EEPROM directly (SMBus access); the summary above is SMBIOS type 17.");
    if (!n) so_err(o, "No SMBIOS memory devices (no udev import and no dmidecode cache)");
    if (!root_path("spd.txt", p, sizeof p)) needs_root(o);
    so_board_footer(o);
}

/* ======================================================================
 * [memory]
 * ====================================================================== */
static long mi(const char *txt, const char *key)
{
    const char *p = txt;
    size_t l = strlen(key);
    while ((p = strstr(p, key))) {
        if ((p == txt || p[-1] == '\n') && p[l] == ':') return atol(p + l + 1);
        p += l;
    }
    return -1;
}

static void memrow(srs_out *o, const char *name, double used, double avail, double total)
{
    char a[32], b[32], c[32], pct[16];
    fmt_bytes(used, a, sizeof a);
    fmt_bytes(avail, b, sizeof b);
    fmt_bytes(total, c, sizeof c);
    snprintf(pct, sizeof pct, "%.0f%%", total > 0 ? used * 100.0 / total : 0.0);
    so_row(o, 5, (const char *const[]){ name, a, b, c, pct }, (const int[]){ C_VALUE, C_BLACK, C_VALUE, C_VALUE, C_ORANGE });
}

static void s_memory(srs_out *o)
{
    static const int tabs[] = { 210, 300, 390, 480 };
    static const int t2[] = { 210 };
    char *txt, b[64], b2[64];
    long tot, avail, sw, swf, cl, cas;
    slot_t sl[16];
    int src, n, i, pop = 0, online = 0, blocks = 0;
    double inst = 0;
    so_siv_header(o, "Memory");
    if (rd_all("/proc/meminfo", &txt) < 0) { so_err(o, "/proc/meminfo unreadable"); return; }
    tot = mi(txt, "MemTotal"); avail = mi(txt, "MemAvailable"); sw = mi(txt, "SwapTotal"); swf = mi(txt, "SwapFree");
    cl = mi(txt, "CommitLimit"); cas = mi(txt, "Committed_AS");
    so_tabs(o, tabs, 4);
    so_row(o, 5, (const char *const[]){ "Resource Usage", "Used", "Available", "Total", "Load" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    memrow(o, "System Physical Memory", (tot - avail) * 1024.0, avail * 1024.0, tot * 1024.0);
    memrow(o, "System Paging File (swap)", (sw - swf) * 1024.0, swf * 1024.0, sw * 1024.0);
    memrow(o, "Commit Charge", cas * 1024.0, (cl - cas) * 1024.0, cl * 1024.0);
    so_par(o);

    so_tabs(o, t2, 1);
    {
        static const char *const keys[] = { "MemFree", "Buffers", "Cached", "SwapCached", "Active", "Inactive", "Dirty", "AnonPages",
                                            "Mapped", "Shmem", "Slab", "SReclaimable", "KernelStack", "PageTables", "VmallocUsed",
                                            "Percpu", "HardwareCorrupted", "AnonHugePages", "HugePages_Total", "Hugepagesize",
                                            "Zswap", "Zswapped", "DirectMap4k", "DirectMap2M", "DirectMap1G", NULL };
        for (i = 0; keys[i]; i++) {
            long v = mi(txt, keys[i]);
            if (v < 0) continue;
            if (!strcmp(keys[i], "HugePages_Total")) snprintf(b, sizeof b, "%ld", v);
            else fmt_bytes(v * 1024.0, b, sizeof b);
            so_kv(o, keys[i], !strcmp(keys[i], "HardwareCorrupted") && v ? C_RED : C_VALUE, b);
        }
    }
    free(txt);
    so_par(o);

    n = slots_get(sl, 16, &src);
    for (i = 0; i < n; i++) if (sl[i].present) { pop++; inst += sl[i].bytes; }
    fmt_bytes(inst, b, sizeof b);
    so_kvf(o, "Installed (SMBIOS)", C_BLACK, "%s in %d of %d DIMM slots", n ? b : "-", pop, n);
    for (i = 0; i < n; i++)
        if (sl[i].present) { so_kvf(o, "Memory Speed", C_GREEN, "%s %s (rated %s)", sl[i].type, sl[i].cspeed, sl[i].speed); break; }
    {
        char **e;
        int ne = dir_list("/sys/devices/system/memory", &e);
        long bs = 0;
        char p[160];
        for (i = 0; i < ne; i++) {
            char st[32];
            if (strncmp(e[i], "memory", 6) || !isdigit((unsigned char)e[i][6])) continue;
            blocks++;
            if (rd_fmt(st, sizeof st, "/sys/devices/system/memory/%s/state", e[i]) && !strcmp(st, "online")) online++;
        }
        dir_free(e, ne);
        rd_str("/sys/devices/system/memory/block_size_bytes", p, sizeof p);
        bs = strtol(p, NULL, 16);
        fmt_bytes((double)bs, b, sizeof b);
        fmt_bytes((double)bs * online, b2, sizeof b2);
        so_kvf(o, "Memory Blocks", C_TEAL, "%d of %d online, block size %s, online %s", online, blocks, b, b2);
        rd_str("/sys/devices/system/memory/auto_online_blocks", p, sizeof p);
        so_kv(o, "Auto Online Blocks", C_VALUE, p);
    }
    {
        char **e;
        int ne = dir_list("/sys/devices/system/edac/mc", &e), mcs = 0;
        for (i = 0; i < ne; i++) if (!strncmp(e[i], "mc", 2) && isdigit((unsigned char)e[i][2])) mcs++;
        dir_free(e, ne);
        if (mcs) {
            char ce[32] = "", ue[32] = "", ty[64] = "";
            rd_str("/sys/devices/system/edac/mc/mc0/ce_count", ce, sizeof ce);
            rd_str("/sys/devices/system/edac/mc/mc0/ue_count", ue, sizeof ue);
            rd_str("/sys/devices/system/edac/mc/mc0/mc_name", ty, sizeof ty);
            so_kvf(o, "EDAC", C_PURPLE, "%d controller(s), mc0 %s  CE %s  UE %s", mcs, ty, ce, ue);
        } else so_kv(o, "EDAC", C_GRAY, "no memory controllers registered (non-ECC memory or amd64_edac not loaded)");
    }
    so_board_footer(o);
}

/* ======================================================================
 * [physical]  firmware memory map
 * ====================================================================== */
static void s_physical(srs_out *o)
{
    static const int tabs[] = { 40, 190, 340, 430 };
    char **e, p[1100];
    int ne = dir_list("/sys/firmware/memmap", &e), i, nt = 0, k;
    struct { char t[48]; double sz; int n; } typ[16];
    so_siv_header(o, "Physical Memory Map");
    so_tabs(o, tabs, 4);
    so_row(o, 5, (const char *const[]){ "#", "Start", "End", "Size", "Type" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    nat_sort(e, ne);
    for (i = 0; i < ne; i++) {
        char s[40], en[40], t[48], sz[32], a[40], b[40];
        unsigned long long vs, ve;
        rd_fmt(s, sizeof s, "/sys/firmware/memmap/%s/start", e[i]);
        rd_fmt(en, sizeof en, "/sys/firmware/memmap/%s/end", e[i]);
        rd_fmt(t, sizeof t, "/sys/firmware/memmap/%s/type", e[i]);
        vs = strtoull(s, NULL, 16);
        ve = strtoull(en, NULL, 16);
        fmt_bytes((double)(ve - vs + 1), sz, sizeof sz);
        snprintf(a, sizeof a, "%016llX", vs);
        snprintf(b, sizeof b, "%016llX", ve);
        so_row(o, 5, (const char *const[]){ e[i], a, b, sz, t },
               (const int[]){ C_GRAY, C_PURPLE, C_PURPLE, C_BLACK, strstr(t, "RAM") ? C_GREEN : C_VALUE });
        for (k = 0; k < nt; k++) if (!strcmp(typ[k].t, t)) break;
        if (k == nt && nt < 16) { snprintf(typ[nt].t, sizeof typ[nt].t, "%s", t); typ[nt].sz = 0; typ[nt].n = 0; nt++; }
        if (k < 16) { typ[k].sz += (double)(ve - vs + 1); typ[k].n++; }
    }
    dir_free(e, ne);
    if (ne <= 0) so_err(o, "/sys/firmware/memmap not available");
    so_par(o);
    for (k = 0; k < nt; k++) {
        char sz[32], c[16];
        fmt_bytes(typ[k].sz, sz, sizeof sz);
        snprintf(c, sizeof c, "%d", typ[k].n);
        so_row(o, 5, (const char *const[]){ "", "Total", typ[k].t, sz, c }, (const int[]){ C_GRAY, C_LABEL, C_VALUE, C_BLACK, C_GRAY });
    }
    if (root_path("iomem.txt", p, sizeof p)) {
        char *txt, *q, *save;
        so_par(o);
        so_note(o, "Top-level /proc/iomem (srs-root cache iomem.txt):");
        if (rd_all(p, &txt) >= 0) {
            for (q = strtok_r(txt, "\n", &save); q; q = strtok_r(NULL, "\n", &save))
                if (*q != ' ') { so_m(o, C_VALUE, q); so_par(o); }
            free(txt);
        }
    } else {
        so_par(o);
        so_note(o, "Resource owners (/proc/iomem with addresses) need root: Menu > Run as root (srs-root)");
    }
    so_board_footer(o);
}

/* ======================================================================
 * [overview]  SIV overview header
 * ====================================================================== */
static void s_overview(srs_out *o)
{
    static const int tabs[] = { 270, 400, 530, 620 };
    char host[128] = "", tz[64] = "", lang[64] = "", bv[64], bd[64], *txt, b[256], up[64], now[64], pw[64] = "Mains Power";
    double upt = 0;
    time_t t = time(NULL);
    struct tm tm;
    long tot = 0, avail = 0, sw = 0, swf = 0, cl = 0, cas = 0;
    slot_t sl[16];
    int src, n, i, pop = 0, cores = 0, pk = 0;
    const char *user = getenv("USER");
    gethostname(host, sizeof host);
    localtime_r(&t, &tm);
    strftime(tz, sizeof tz, "%Z", &tm);
    strftime(now, sizeof now, "%Y-%m-%d %H:%M:%S", &tm);
    snprintf(lang, sizeof lang, "%s", getenv("LANG") ? getenv("LANG") : "C");
    dmi_rd("bios_version", bv, sizeof bv);
    dmi_rd("bios_date", bd, sizeof bd);
    if (rd_all("/proc/uptime", &txt) >= 0) { upt = atof(txt); free(txt); }
    snprintf(up, sizeof up, "%02d %02d:%02d:%02d", (int)(upt / 86400), (int)upt / 3600 % 24, (int)upt / 60 % 60, (int)upt % 60);
    {
        char **e;
        int ne = dir_list("/sys/class/power_supply", &e);
        for (i = 0; i < ne; i++) {
            char ty[32], on[8];
            if (!rd_fmt(ty, sizeof ty, "/sys/class/power_supply/%s/type", e[i])) continue;
            if (!strcmp(ty, "Mains") && rd_fmt(on, sizeof on, "/sys/class/power_supply/%s/online", e[i]))
                snprintf(pw, sizeof pw, "%s", atoi(on) ? "Mains Power" : "Battery Power");
            if (!strcmp(ty, "Battery")) {
                char cap[8] = "", sc[16] = "";
                if (rd_fmt(sc, sizeof sc, "/sys/class/power_supply/%s/scope", e[i]) && !strcmp(sc, "Device")) continue;   /* peripheral */
                rd_fmt(cap, sizeof cap, "/sys/class/power_supply/%s/capacity", e[i]);
                snprintf(pw + strlen(pw), sizeof pw - strlen(pw), "  Battery %s%%", cap);
            }
        }
        dir_free(e, ne);
    }
    {
        char d[64];
        so_t(o, C_VALUE, sys_os_name());
        so_t(o, C_VALUE, "  ");
        dmi_rd("board_name", d, sizeof d);
        {
            char v[64];
            dmi_rd("sys_vendor", v, sizeof v);
            snprintf(b, sizeof b, "%s %s", v, d);
        }
        so_t(o, C_VALUE, b);
        snprintf(b, sizeof b, "  %s::%s  %s  %s  BIOS %s %s", host, user ? user : "?", lang, tz, bv, bd);
        so_t(o, C_VALUE, b);
        so_par(o);
    }
    so_tabs(o, tabs, 4);
    snprintf(b, sizeof b, "[BIOS %s %s]", bv, bd);
    so_row(o, 4, (const char *const[]){ up, now, b, pw }, (const int[]){ C_TEAL, C_GRAY, C_VALUE, C_VALUE });
    so_par(o);

    if (rd_all("/proc/meminfo", &txt) >= 0) {
        tot = mi(txt, "MemTotal"); avail = mi(txt, "MemAvailable"); sw = mi(txt, "SwapTotal"); swf = mi(txt, "SwapFree");
        cl = mi(txt, "CommitLimit"); cas = mi(txt, "Committed_AS");
        free(txt);
    }
    {
        static const int t3[] = { 270, 360, 450 };
        char a[32], c[32], d[32], pct[48];
        so_tabs(o, t3, 3);
        snprintf(pct, sizeof pct, "Resource Usage %.0f%%", tot ? (tot - avail) * 100.0 / tot : 0.0);
        so_row(o, 4, (const char *const[]){ pct, "Current", "Committed", "Maximum" }, (const int[]){ C_VALUE, C_VALUE, C_VALUE, C_VALUE });
        fmt_bytes((tot - avail) * 1024.0, a, sizeof a); fmt_bytes(cas * 1024.0, c, sizeof c); fmt_bytes(tot * 1024.0, d, sizeof d);
        so_row(o, 4, (const char *const[]){ "System Physical Memory", a, c, d }, (const int[]){ C_VALUE, C_BLACK, C_VALUE, C_VALUE });
        fmt_bytes((sw - swf) * 1024.0, a, sizeof a); fmt_bytes(sw * 1024.0, d, sizeof d);
        so_row(o, 4, (const char *const[]){ "System Paging File (swap)", a, "", d }, (const int[]){ C_VALUE, C_BLACK, C_VALUE, C_VALUE });
        fmt_bytes(cas * 1024.0, a, sizeof a); fmt_bytes(cl * 1024.0, d, sizeof d);
        so_row(o, 4, (const char *const[]){ "Commit Charge (limit)", a, "", d }, (const int[]){ C_VALUE, C_BLACK, C_VALUE, C_VALUE });
        {
            char k[128] = "";
            rd_str("/proc/sys/kernel/osrelease", k, sizeof k);
            snprintf(b, sizeof b, "Linux %s", k);
            so_t(o, C_VALUE, b);
            so_par(o);
        }
    }
    so_par(o);
    for (i = 0; i < (int)sysconf(_SC_NPROCESSORS_CONF); i++) {
        char s[64];
        long pp = -1;
        if (rd_fmt(s, sizeof s, "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", i) && atoi(s) == i) cores++;
        if (rd_fmt(s, sizeof s, "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", i)) pp = atol(s);
        if (pp + 1 > pk) pk = (int)pp + 1;
    }
    n = slots_get(sl, 16, &src);
    for (i = 0; i < n; i++) pop += sl[i].present;
    so_f(o, C_VALUE, "CPUs %ld Cores %d Chips %d  DIMMs %d", sysconf(_SC_NPROCESSORS_CONF), cores, pk, pop);
    so_par(o);
    so_t(o, C_BLACK, sys_cpu_name());
    so_par(o);
    for (i = 0; i < n; i++)
        if (sl[i].present) {
            so_f(o, C_PURPLE, "%s %s  %s %s %s  %s %s", sl[i].bank, sl[i].loc, sl[i].size, sl[i].type, sl[i].cspeed, sl[i].manuf, sl[i].part);
            so_par(o);
        }
    so_board_footer(o);
}

/* ======================================================================
 * [hardware]  summary page
 * ====================================================================== */
static void s_hardware(srs_out *o)
{
    static const int tabs[] = { 190 };
    static const int t2[] = { 190, 260 };
    char v[160], b[256], *txt;
    slot_t sl[16];
    int src, n, i, pop = 0;
    double sum = 0;
    so_siv_header(o, "Hardware");
    so_tabs(o, tabs, 1);
    so_kv(o, "Processor", C_BLACK, sys_cpu_name());
    so_kvf(o, "Logical CPUs", C_VALUE, "%ld", sysconf(_SC_NPROCESSORS_CONF));
    {
        char sv[96], pn[96];
        dmi_rd("sys_vendor", sv, sizeof sv);
        dmi_rd("product_name", pn, sizeof pn);
        so_kvf(o, "System", C_VALUE, "%s %s", sv, na(pn));
    }
    dmi_rd("board_vendor", v, sizeof v);
    snprintf(b, sizeof b, "%s ", v);
    dmi_rd("board_name", v, sizeof v);
    strncat(b, v, sizeof b - strlen(b) - 1);
    so_kv(o, "Motherboard", C_VALUE, b);
    {
        char bv[64], bd[64], vn[96];
        dmi_rd("bios_vendor", vn, sizeof vn);
        dmi_rd("bios_version", bv, sizeof bv);
        dmi_rd("bios_date", bd, sizeof bd);
        so_kvf(o, "BIOS", C_VALUE, "%s %s %s (%s)", vn, bv, bd, path_exists(EFI_DIR) ? "UEFI" : "Legacy");
    }
    n = slots_get(sl, 16, &src);
    for (i = 0; i < n; i++) if (sl[i].present) { pop++; sum += sl[i].bytes; }
    fmt_bytes(sum, v, sizeof v);
    for (i = 0; i < n && !sl[i].present; i++) ;
    if (i < n) so_kvf(o, "Memory", C_VALUE, "%s %s %s, %d of %d DIMMs (%s %s)", v, sl[i].type, sl[i].cspeed, pop, n, sl[i].manuf, sl[i].part);
    else if (rd_all("/proc/meminfo", &txt) >= 0) { fmt_bytes(mi(txt, "MemTotal") * 1024.0, v, sizeof v); free(txt); so_kv(o, "Memory", C_VALUE, v); }
    {
        char t[16] = "";
        rd_str("/sys/class/tpm/tpm0/tpm_version_major", t, sizeof t);
        so_kv(o, "TPM", C_VALUE, *t ? (atoi(t) == 2 ? "TPM 2.0" : "TPM 1.2") : "none");
    }
    so_kv(o, "Secure Boot", C_VALUE, path_exists(EFI_DIR) ? secure_boot_str() : "n/a (legacy BIOS)");
    so_par(o);
    {
        static const char *const want[] = { "VGA compatible controller", "Display controller", "3D controller", "Audio device",
                                            "Multimedia audio controller", "Ethernet controller", "Network controller",
                                            "Non-Volatile memory controller", "SATA controller", "RAID bus controller",
                                            "USB controller", "Thunderbolt", NULL };
        static const char *const argv[] = { "lspci", "-mm", NULL };
        char *out = NULL, *p, *save;
        so_tabs(o, t2, 2);
        so_row(o, 3, (const char *const[]){ "Class", "Address", "Device" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL });
        if (run_out(&out, argv) == 0 && out) {
            for (p = strtok_r(out, "\n", &save); p; p = strtok_r(NULL, "\n", &save)) {
                char addr[16], f[4][128];
                int k = 0, w;
                const char *q = p;
                if (sscanf(p, "%15s", addr) != 1) continue;
                q = strchr(p, '"');
                while (q && k < 4) {
                    const char *e2 = strchr(q + 1, '"');
                    if (!e2) break;
                    snprintf(f[k++], 128, "%.*s", (int)(e2 - q - 1), q + 1);
                    q = strchr(e2 + 1, '"');
                }
                if (k < 3) continue;
                for (w = 0; want[w]; w++) if (!strncmp(f[0], want[w], strlen(want[w]))) break;
                if (!want[w]) continue;
                snprintf(b, sizeof b, "%s %s", f[1], f[2]);
                so_row(o, 3, (const char *const[]){ f[0], addr, b }, (const int[]){ C_VALUE, C_GRAY, C_PURPLE });
            }
        } else so_note(o, "lspci not available");
        free(out);
    }
    so_board_footer(o);
}

/* ======================================================================
 * [machine-opt]  which sources the machine group can use
 * ====================================================================== */
static void opt_row(srs_out *o, int on, const char *what, const char *src)
{
    so_check(o, on);
    so_t(o, C_VALUE, " ");
    so_t(o, on ? C_VALUE : C_GRAY, what);
    so_tab(o);
    so_t(o, C_GRAY, src);
    so_par(o);
}

static void s_machine_opt(srs_out *o)
{
    static const int tabs[] = { 300 };
    char p[1100];
    spdmon m[8];
    char **e;
    int ne, mcs = 0, i;
    so_siv_header(o, "Machine Options");
    so_tabs(o, tabs, 1);
    udev_load();
    opt_row(o, path_exists(DMI_ID "bios_version"), "DMI identification (unprivileged)", DMI_ID);
    opt_row(o, *uget("MEMORY_DEVICE_0_LOCATOR") != 0, "SMBIOS memory devices via udev", "/run/udev/data/+dmi:id");
    opt_row(o, root_path("dmidecode.txt", p, sizeof p), "dmidecode cache (serials, UUID, socket)", "~/.cache/srs/root/dmidecode.txt");
    opt_row(o, root_path("spd.txt", p, sizeof p), "SPD decode cache", "~/.cache/srs/root/spd.txt");
    opt_row(o, root_path("acpi-tables", p, sizeof p), "ACPI table copies", "~/.cache/srs/root/acpi-tables/");
    opt_row(o, root_path("iomem.txt", p, sizeof p), "I/O memory map cache", "~/.cache/srs/root/iomem.txt");
    opt_row(o, spd_monitors(m, 8) > 0, "spd5118 DIMM thermal sensors", "/sys/class/hwmon/*/name = spd5118");
    ne = dir_list("/sys/devices/system/edac/mc", &e);
    for (i = 0; i < ne; i++) if (!strncmp(e[i], "mc", 2) && isdigit((unsigned char)e[i][2])) mcs++;
    dir_free(e, ne);
    opt_row(o, mcs > 0, "EDAC memory controllers", "/sys/devices/system/edac/mc");
    opt_row(o, path_exists(EFIVARS), "UEFI variables (read-only)", EFIVARS);
    opt_row(o, path_exists("/sys/class/tpm/tpm0"), "TPM", "/sys/class/tpm/tpm0");
    opt_row(o, path_exists("/proc/driver/rtc"), "CMOS RTC", "/proc/driver/rtc");
    opt_row(o, path_exists("/sys/bus/wmi/devices"), "ACPI WMI bus", "/sys/bus/wmi/devices");
    opt_row(o, 0, "Direct SMBus / SPD EEPROM reads", "disabled: never opened by srs");
    opt_row(o, 0, "ACPI method evaluation / EC writes", "disabled: srs is read-only");
    opt_row(o, 0, "/dev/mem, /dev/nvram, /dev/port", "disabled: never opened by srs");
    so_board_footer(o);
}

/* ======================================================================
 * [bios-make] [bios-tables] [system-ids]
 * ====================================================================== */
static void s_bios_make(srs_out *o)
{
    static const int tabs[] = { 190 };
    char v[256];
    so_siv_header(o, "BIOS Maker");
    so_tabs(o, tabs, 1);
    dmi_rd("bios_vendor", v, sizeof v);
    so_kv(o, "BIOS Vendor", C_BLACK, v);
    if (strstr(v, "American Megatrends")) so_kv(o, "Family", C_TEAL, "AMI (Aptio V UEFI)");
    else if (strstr(v, "Phoenix")) so_kv(o, "Family", C_TEAL, "Phoenix");
    else if (strstr(v, "Insyde")) so_kv(o, "Family", C_TEAL, "Insyde H2O");
    dmi_rd("bios_version", v, sizeof v); so_kv(o, "BIOS Version", C_VALUE, v);
    dmi_rd("bios_date", v, sizeof v); so_kv(o, "BIOS Date", C_VALUE, v);
    dmi_rd("bios_release", v, sizeof v); so_kv(o, "BIOS Release", C_VALUE, v);
    dmi_rd("board_vendor", v, sizeof v); so_kv(o, "Board Maker", C_VALUE, v);
    dmi_rd("board_name", v, sizeof v); so_kv(o, "Board", C_VALUE, v);
    so_kv(o, "Firmware Type", C_PURPLE, path_exists(EFI_DIR) ? "UEFI" : "Legacy BIOS");
    if (dmid()) {
        static const char *const keys[] = { "Address", "Runtime Size", "ROM Size", "BIOS Revision", "Firmware Revision", NULL };
        int i;
        for (i = 0; keys[i]; i++) if (dmid_get(0, 0, keys[i], v, sizeof v)) so_kv(o, keys[i], C_VALUE, v);
        {   /* characteristics: tab-tab lines after "Characteristics:" */
            const char *t = dmid(), *p = strstr(t, "\tCharacteristics:");
            if (p) {
                so_par(o);
                so_b(o, C_VALUE, "Characteristics");
                so_par(o);
                p = strchr(p, '\n');
                while (p && p[1] == '\t' && p[2] == '\t') {
                    const char *e = strchr(p + 1, '\n');
                    so_check(o, 1);
                    so_f(o, C_VALUE, " %.*s", (int)((e ? e : p + strlen(p)) - (p + 3)), p + 3);
                    so_par(o);
                    p = e;
                }
            }
        }
    } else {
        so_kv(o, "ROM Size / Address", C_GRAY, "(needs root)");
        needs_root(o);
    }
    so_board_footer(o);
}

static void s_bios_tables(srs_out *o)
{
    static const int tabs[] = { 200, 290, 380, 450 };
    static const int t2[] = { 60, 340 };
    long long ep = fsize("/sys/firmware/dmi/tables/smbios_entry_point"), dsz = fsize("/sys/firmware/dmi/tables/DMI");
    char ver[64], cnt[32], l[160], b[64], c[64];
    so_siv_header(o, "BIOS Information");
    smbios_ver(ver, sizeof ver, cnt, sizeof cnt);
    so_tabs(o, tabs, 4);
    so_row(o, 5, (const char *const[]){ "BIOS", "Anchor", "EP Size", "Table", "Structures" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    snprintf(b, sizeof b, "%lld", ep);
    snprintf(c, sizeof c, "%lld bytes", dsz);
    so_row(o, 5, (const char *const[]){ ver, ep == 24 ? "\"_SM3_\"" : ep == 31 ? "\"_SM_\" + \"_DMI_\"" : "-", b, c,
                                        *cnt ? cnt + 6 : "(needs root)" },
           (const int[]){ C_BLACK, C_PURPLE, C_VALUE, C_VALUE, C_VALUE });
    if (dmid_line("Table at ", NULL, l, sizeof l)) { size_t k = strlen(l); if (k && l[k - 1] == '.') l[k - 1] = 0; so_par(o); so_kv(o, "Table address", C_PURPLE, l + 9); }
    so_par(o);
    if (dmid()) {
        static const char *const names[] = { "BIOS Information", "System Information", "Base Board", "Chassis", "Processor",
                                             "Memory Controller", "Memory Module", "Cache", "Port Connector", "System Slots",
                                             "On Board Devices", "OEM Strings", "System Configuration Options", "BIOS Language",
                                             "Group Associations", "System Event Log", "Physical Memory Array", "Memory Device",
                                             "32-bit Memory Error", "Memory Array Mapped Address", "Memory Device Mapped Address",
                                             "Built-in Pointing Device", "Portable Battery", "System Reset", "Hardware Security",
                                             "System Power Controls", "Voltage Probe", "Cooling Device", "Temperature Probe",
                                             "Electrical Current Probe", "Out-of-band Remote Access", "BIS Entry Point",
                                             "System Boot", "64-bit Memory Error", "Management Device", "Management Device Component",
                                             "Management Device Threshold", "Memory Channel", "IPMI Device", "Power Supply",
                                             "Additional Information", "Onboard Device Extended", "Management Controller Host",
                                             "TPM Device", "Processor Additional", "Firmware Inventory", "String Property" };
        int t;
        so_tabs(o, t2, 2);
        so_row(o, 3, (const char *const[]){ "Type", "Name", "Count" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL });
        for (t = 0; t < 256; t++) {
            int n = dmid_count(t);
            char ts[8], ns[8];
            if (!n) continue;
            snprintf(ts, sizeof ts, "%d", t);
            snprintf(ns, sizeof ns, "%d", n);
            so_row(o, 3, (const char *const[]){ ts, t < (int)(sizeof names / sizeof *names) ? names[t] : t >= 128 ? "OEM-specific" : "-", ns },
                   (const int[]){ C_GRAY, C_VALUE, C_BLACK });
        }
    } else {
        so_note(o, "SMBIOS version inferred from the entry point size (24 = 64-bit SMBIOS 3 anchor, 31 = 32-bit SMBIOS 2 anchor).");
        needs_root(o);
    }
    so_board_footer(o);
}

static void s_system_ids(srs_out *o)
{
    static const int tabs[] = { 200 };
    static const struct { const char *label, *sysf, *key; int type; } ids[] = {
        { "System UUID", "product_uuid", "UUID", 1 },
        { "System Serial", "product_serial", "Serial Number", 1 },
        { "Board Serial", "board_serial", "Serial Number", 2 },
        { "Chassis Serial", "chassis_serial", "Serial Number", 3 },
        { "System SKU", "product_sku", "SKU Number", 1 },
        { "System Family", "product_family", "Family", 1 },
        { "Board Asset Tag", "board_asset_tag", "Asset Tag", 2 },
        { "Chassis Asset Tag", "chassis_asset_tag", "Asset Tag", 3 },
        { NULL, NULL, NULL, 0 } };
    char v[256];
    int i, need = 0;
    slot_t sl[16];
    int src, n;
    so_siv_header(o, "BIOS IDs");
    so_tabs(o, tabs, 1);
    for (i = 0; ids[i].label; i++) {
        dmi_or_cache(ids[i].sysf, ids[i].type, ids[i].key, v, sizeof v);
        if (!*v) { need = 1; so_kv(o, ids[i].label, C_GRAY, "(needs root)"); }
        else so_kv(o, ids[i].label, i == 0 ? C_GREEN : C_VALUE, v);
    }
    n = slots_get(sl, 16, &src);
    for (i = 0; i < n; i++)
        if (sl[i].present) {
            char k[96];
            snprintf(k, sizeof k, "DIMM Serial %s %s", sl[i].bank + (strncmp(sl[i].bank, "P0 ", 3) ? 0 : 3), sl[i].loc);
            so_kv(o, k, C_PURPLE, sl[i].serial);
        }
    dmi_rd("modalias", v, sizeof v);
    so_par(o);
    so_kv(o, "DMI modalias", C_GRAY, v);
    if (need) needs_root(o);
    so_board_footer(o);
}

/* ======================================================================
 * [firmware] [uefi-info] [uefi-data]
 * ====================================================================== */
static void s_firmware(srs_out *o)
{
    static const int tabs[] = { 220 };
    char v[256], **e;
    int ne, i;
    so_siv_header(o, "Firmware");
    so_tabs(o, tabs, 1);
    if (!path_exists(EFI_DIR)) {
        so_kv(o, "Firmware Type", C_BLACK, "Legacy BIOS (no /sys/firmware/efi)");
    } else {
        so_kv(o, "Firmware Type", C_BLACK, "UEFI");
        rd_str(EFI_DIR "/fw_platform_size", v, sizeof v);
        so_kvf(o, "Platform Size", C_VALUE, "%s-bit", v);
        rd_str(EFI_DIR "/fw_vendor", v, sizeof v); so_kv(o, "Firmware Vendor Pointer", C_PURPLE, v);
        rd_str(EFI_DIR "/runtime", v, sizeof v); so_kv(o, "Runtime Services", C_PURPLE, v);
        rd_str(EFI_DIR "/config_table", v, sizeof v); so_kv(o, "Configuration Table", C_PURPLE, v);
        if (!rd_str(EFI_DIR "/systab", v, sizeof v)) so_kv(o, "System Table", C_GRAY, "(root only)");
        else so_kv(o, "System Table", C_PURPLE, v);
        so_kv(o, "Secure Boot", C_TEAL, secure_boot_str());
        ne = dir_list(EFI_DIR "/esrt/entries", &e);
        so_kvf(o, "ESRT Entries", C_VALUE, "%d%s", ne < 0 ? 0 : ne, ne > 0 ? " (fw_class / versions are root-only)" : "");
        for (i = 0; i < ne; i++) {
            char t[64];
            if (rd_fmt(t, sizeof t, EFI_DIR "/esrt/entries/%s/fw_version", e[i])) so_kvf(o, e[i], C_VALUE, "fw_version %s", t);
        }
        dir_free(e, ne);
        ne = dir_list(EFIVARS, &e);
        so_kvf(o, "UEFI Variables", C_VALUE, "%d", ne < 0 ? 0 : ne);
        dir_free(e, ne);
    }
    dmi_rd("bios_vendor", v, sizeof v); so_kv(o, "BIOS Vendor", C_VALUE, v);
    {
        char ver[64], d[64];
        dmi_rd("bios_version", ver, sizeof ver);
        dmi_rd("bios_date", d, sizeof d);
        so_kvf(o, "BIOS Version", C_VALUE, "%s %s", ver, d);
    }
    {
        long long fs = -1, bl = -1, ebs = -1, ebe = -1;
        char t[64];
        if (rd_str("/sys/firmware/acpi/fpdt/boot/firmware_start_ns", t, sizeof t)) fs = atoll(t);
        if (rd_str("/sys/firmware/acpi/fpdt/boot/bootloader_load_ns", t, sizeof t)) bl = atoll(t);
        if (rd_str("/sys/firmware/acpi/fpdt/boot/exitbootservice_start_ns", t, sizeof t)) ebs = atoll(t);
        if (rd_str("/sys/firmware/acpi/fpdt/boot/exitbootservice_end_ns", t, sizeof t)) ebe = atoll(t);
        if (bl >= 0) {
            so_par(o);
            so_b(o, C_VALUE, "Firmware Boot Performance (ACPI FPDT)");
            so_par(o);
            so_kvf(o, "Firmware start", C_VALUE, "%.3f s", fs / 1e9);
            so_kvf(o, "Boot loader load", C_VALUE, "%.3f s", bl / 1e9);
            so_kvf(o, "ExitBootServices", C_VALUE, "%.3f s -> %.3f s", ebs / 1e9, ebe / 1e9);
            so_kvf(o, "Time in firmware + loader", C_ORANGE, "%.3f s", (ebe - fs) / 1e9);
        }
    }
    so_board_footer(o);
}

static void s_uefi_info(srs_out *o)
{
    static const int tabs[] = { 230 };
    unsigned char *d;
    int n, i;
    char v[256];
    so_siv_header(o, "UEFI Information");
    so_tabs(o, tabs, 1);
    if (!path_exists(EFI_DIR)) { so_err(o, "Not booted via UEFI (/sys/firmware/efi absent)"); so_board_footer(o); return; }
    rd_str(EFI_DIR "/fw_platform_size", v, sizeof v);
    so_kvf(o, "UEFI Platform", C_BLACK, "%s-bit", v);
    {
        static const char *const u8v[] = { "SecureBoot", "SetupMode", "AuditMode", "DeployedMode", NULL };
        for (i = 0; u8v[i]; i++) {
            int x = efi_u8(u8v[i], EFI_GLOBAL);
            if (x >= 0) so_kv(o, u8v[i], x ? C_GREEN : C_VALUE, x ? "1 (on)" : "0 (off)");
        }
    }
    if ((n = efivar("Timeout", EFI_GLOBAL, &d)) >= 2) so_kvf(o, "Timeout", C_VALUE, "%u s", le16(d + 4));
    free(d);
    if ((n = efivar("PlatformLang", EFI_GLOBAL, &d)) >= 1) so_kvf(o, "PlatformLang", C_VALUE, "%.*s", n, (char *)d + 4);
    free(d);
    if ((n = efivar("OsIndicationsSupported", EFI_GLOBAL, &d)) >= 8) {
        uint64_t x = le64(d + 4);
        so_kvf(o, "OsIndicationsSupported", C_PURPLE, "0x%016llX%s%s%s", (unsigned long long)x, x & 1 ? "  BootToFwUI" : "",
               x & 4 ? "  FileCapsule" : "", x & 0x10 ? "  CapsuleResultVar" : "");
    }
    free(d);
    {
        static const struct { const char *name, *label; } ld[] = {
            { "LoaderInfo", "Boot Loader" }, { "LoaderFirmwareInfo", "Firmware Info" }, { "LoaderFirmwareType", "Firmware Type" },
            { "StubInfo", "Kernel Stub" }, { NULL, NULL } };
        for (i = 0; ld[i].name; i++)
            if ((n = efivar(ld[i].name, "4a67b082-0a4c-41cf-b6c7-440b29bb8c4f", &d)) > 0) {
                ucs2(d + 4, n, v, sizeof v);
                so_kv(o, ld[i].label, C_TEAL, v);
                free(d);
            } else free(d);
    }
    {
        int cur = -1;
        if ((n = efivar("BootCurrent", EFI_GLOBAL, &d)) >= 2) cur = le16(d + 4);
        free(d);
        if (cur >= 0) so_kvf(o, "BootCurrent", C_GREEN, "Boot%04X", cur);
        if ((n = efivar("BootOrder", EFI_GLOBAL, &d)) >= 2) {
            char ord[512] = "";
            size_t p = 0;
            unsigned char *bo = d;
            int cnt = n / 2;
            for (i = 0; i < cnt; i++) p += (size_t)snprintf(ord + p, p < sizeof ord ? sizeof ord - p : 0, "%s%04X", i ? "," : "", le16(bo + 4 + 2 * i));
            so_kv(o, "BootOrder", C_VALUE, ord);
            so_par(o);
            so_b(o, C_VALUE, "Boot Entries");
            so_par(o);
            for (i = 0; i < cnt && i < 32; i++) {
                char name[16], desc[160];
                unsigned char *be;
                int bn, num = le16(bo + 4 + 2 * i);
                snprintf(name, sizeof name, "Boot%04X", num);
                if ((bn = efivar(name, EFI_GLOBAL, &be)) >= 6) {
                    uint32_t at = le32(be + 4);
                    ucs2(be + 4 + 6, bn - 6, desc, sizeof desc);
                    so_t(o, num == cur ? C_GREEN : C_LABEL, name);
                    so_tab(o);
                    so_f(o, C_VALUE, "%s %s", at & 1 ? "*" : " ", desc);
                    so_par(o);
                }
                free(be);
            }
        }
        free(d);
    }
    so_board_footer(o);
}

static void s_uefi_data(srs_out *o)
{
    static const int tabs[] = { 330, 400 };
    char **e;
    int ne, i, j;
    so_siv_header(o, "UEFI Data");
    if (!path_exists(EFIVARS)) { so_err(o, "efivarfs not mounted / not UEFI"); so_board_footer(o); return; }
    if (safe_arg(o->arg)) {
        char p[512];
        char *b;
        int n;
        snprintf(p, sizeof p, EFIVARS "/%s", o->arg);
        n = rd_all(p, &b);
        if (n < 4) { so_err(o, "No such variable (use the full Name-GUID from the list)"); free(b); so_board_footer(o); return; }
        so_b(o, C_VALUE, o->arg);
        so_par(o);
        so_f(o, C_PURPLE, "Attributes 0x%08X%s%s%s%s  Size %d", le32((unsigned char *)b), le32((unsigned char *)b) & 1 ? " NV" : "",
             le32((unsigned char *)b) & 2 ? " BS" : "", le32((unsigned char *)b) & 4 ? " RT" : "",
             le32((unsigned char *)b) & 0x20 ? " TA" : "", n - 4);
        so_par(o);
        so_par(o);
        hexdump(o, (unsigned char *)b + 4, n - 4, 4096);
        free(b);
        so_board_footer(o);
        return;
    }
    ne = dir_list(EFIVARS, &e);
    /* group by GUID (last 36 chars) */
    {
        char **guids = malloc(sizeof *guids * (size_t)(ne > 0 ? ne : 1));
        int ng = 0;
        for (i = 0; guids && i < ne; i++) {
            size_t l = strlen(e[i]);
            const char *g = l > 37 ? e[i] + l - 36 : "";
            for (j = 0; j < ng; j++) if (!strcmp(guids[j], g)) break;
            if (j == ng) guids[ng++] = (char *)g;
        }
        so_tabs(o, tabs, 2);
        so_row(o, 3, (const char *const[]){ "Variable", "Size", "" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL });
        for (j = 0; j < ng; j++) {
            so_par(o);
            so_b(o, C_TEAL, guids[j]);
            if (!strcmp(guids[j], EFI_GLOBAL)) so_t(o, C_GRAY, "  EFI Global Variable");
            else if (!strcmp(guids[j], "d719b2cb-3d3a-4596-a3bc-dad00e67656f")) so_t(o, C_GRAY, "  Image Security Database");
            else if (!strcmp(guids[j], "4a67b082-0a4c-41cf-b6c7-440b29bb8c4f")) so_t(o, C_GRAY, "  systemd Boot Loader Interface");
            so_par(o);
            for (i = 0; i < ne; i++) {
                size_t l = strlen(e[i]);
                char p[512], nm[256], sz[24];
                long long s;
                if (l <= 37 || strcmp(e[i] + l - 36, guids[j])) continue;
                snprintf(nm, sizeof nm, "%.*s", (int)(l - 37), e[i]);
                snprintf(p, sizeof p, EFIVARS "/%s", e[i]);
                s = fsize(p);
                snprintf(sz, sizeof sz, "%lld", s >= 4 ? s - 4 : s);
                so_row(o, 2, (const char *const[]){ nm, sz }, (const int[]){ C_VALUE, C_GRAY });
            }
        }
        free(guids);
    }
    dir_free(e, ne);
    so_par(o);
    so_note(o, "Names and sizes only; contents of one variable: uefi-data:<Name-GUID> (read-only).");
    so_board_footer(o);
}

/* ======================================================================
 * [tpm-device] [acpi-tpm]
 * ====================================================================== */
static void s_tpm_device(srs_out *o)
{
    static const int tabs[] = { 200 };
    char v[256], **e;
    int ne, i;
    so_siv_header(o, "TPM Device");
    so_tabs(o, tabs, 1);
    if (!path_exists("/sys/class/tpm/tpm0")) { so_err(o, "No TPM (/sys/class/tpm empty)"); so_board_footer(o); return; }
    rd_str("/sys/class/tpm/tpm0/tpm_version_major", v, sizeof v);
    so_kv(o, "TPM Version", C_BLACK, atoi(v) == 2 ? "TPM 2.0" : *v ? "TPM 1.2" : "-");
    link_base("/sys/class/tpm/tpm0/device/driver", v, sizeof v);
    so_kv(o, "Driver", C_VALUE, v);
    so_kv(o, "Interface", C_TEAL, strstr(v, "crb") ? "CRB (Command Response Buffer)" : strstr(v, "tis") ? "TIS/FIFO" : "-");
    if (strstr(v, "crb") && !strncmp(sys_cpu_name(), "AMD", 3)) so_kv(o, "Implementation", C_TEAL, "firmware TPM (AMD fTPM in the PSP, likely)");
    link_base("/sys/class/tpm/tpm0/device/firmware_node", v, sizeof v);
    so_kv(o, "ACPI Node", C_PURPLE, v);
    if (*v) {
        char pth[128], st[32], sb[32];
        adev(v, "path", pth, sizeof pth);
        adev(v, "status", st, sizeof st);
        so_kv(o, "ACPI Path", C_PURPLE, pth);
        so_kv(o, "ACPI Status", C_VALUE, sta_str(st, sb, sizeof sb));
    }
    if (rd_str("/sys/class/tpm/tpm0/ppi/version", v, sizeof v)) so_kv(o, "Physical Presence Interface", C_VALUE, v);
    ne = dir_list("/sys/class/tpm/tpm0", &e);
    {
        char banks[256] = "";
        for (i = 0; i < ne; i++) if (!strncmp(e[i], "pcr-", 4)) { strncat(banks, e[i] + 4, sizeof banks - strlen(banks) - 2); strcat(banks, " "); }
        so_kv(o, "PCR Banks", C_VALUE, *banks ? banks : "-");
    }
    dir_free(e, ne);
    so_kv(o, "Device Nodes", C_GRAY, path_exists("/dev/tpmrm0") ? "/dev/tpm0 /dev/tpmrm0" : "/dev/tpm0");
    {
        long long s = fsize(ACPI_TAB "/TPM2");
        if (s >= 0) so_kvf(o, "ACPI TPM2 table", C_VALUE, "%lld bytes", s);
    }
    if (o->arg && !strcmp(o->arg, "pcr")) {
        so_par(o);
        so_b(o, C_VALUE, "PCR sha256");
        so_par(o);
        for (i = 0; i < 24; i++) {
            char k[16], p[96];
            snprintf(p, sizeof p, "/sys/class/tpm/tpm0/pcr-sha256/%d", i);
            snprintf(k, sizeof k, "PCR %d", i);
            if (rd_str(p, v, sizeof v)) so_kv(o, k, C_GRAY, v);
        }
    } else so_note(o, "PCR values are not read by default (TPM commands); open tpm-device:pcr to read them.");
    so_board_footer(o);
}

static void s_acpi_tpm(srs_out *o)
{
    static const int tabs[] = { 200 };
    char **e, v[160], sb[32];
    int ne = dir_list(ACPI_DEV, &e), i, hits = 0;
    unsigned char *d;
    int n;
    so_siv_header(o, "ACPI TPM");
    so_tabs(o, tabs, 1);
    for (i = 0; i < ne; i++)
        if (!strncmp(e[i], "MSFT0101", 8) || !strncmp(e[i], "PNP0C31", 7) || !strncmp(e[i], "IFX0102", 7)) {
            hits++;
            so_b(o, C_VALUE, e[i]);
            so_par(o);
            adev(e[i], "path", v, sizeof v); so_kv(o, "Path", C_PURPLE, v);
            adev(e[i], "hid", v, sizeof v); so_kv(o, "HID", C_VALUE, v);
            adev(e[i], "uid", v, sizeof v); so_kv(o, "UID", C_VALUE, v);
            adev(e[i], "status", v, sizeof v); so_kv(o, "_STA", C_VALUE, sta_str(v, sb, sizeof sb));
            adev_driver(e[i], v, sizeof v); so_kv(o, "Driver", C_TEAL, v);
        }
    dir_free(e, ne);
    if (!hits) so_note(o, "No ACPI TPM device (MSFT0101 / PNP0C31)");
    so_par(o);
    if (acpi_present("TPM2")) {
        so_b(o, C_VALUE, "TPM2 table");
        so_par(o);
        n = acpi_load("TPM2", &d);
        if (n >= 52) {
            acpi_hdr h;
            static const char *const sm[] = { "", "", "ACPI Start", "", "", "", "TIS 1.3 / FIFO", "Command Response Buffer",
                                              "CRB with ACPI Start", "", "", "CRB with ARM SMC", "CRB over FF-A", "", "", "FIFO over I2C" };
            uint32_t st;
            acpi_hdr_parse(d, n, &h);
            acpi_hdr_rows(o, &h);
            so_kvf(o, "Platform Class", C_VALUE, "%u (%s)", le16(d + 36), le16(d + 36) ? "Server" : "Client");
            so_kvf(o, "Control Area", C_PURPLE, "0x%016llX", (unsigned long long)le64(d + 40));
            st = le32(d + 48);
            so_kvf(o, "Start Method", C_TEAL, "%u %s", st, st < 16 ? sm[st] : "");
        } else {
            so_kvf(o, "Size", C_VALUE, "%lld bytes", fsize(ACPI_TAB "/TPM2"));
            needs_root(o);
        }
        free(d);
    } else so_note(o, "No TPM2 ACPI table");
    so_board_footer(o);
}

/* ======================================================================
 * [cmos-status] [system-cmos] [system-ebda]
 * ====================================================================== */
static void s_cmos_status(srs_out *o)
{
    static const int tabs[] = { 200 };
    char *txt, v[128];
    so_siv_header(o, "CMOS / RTC Status");
    so_tabs(o, tabs, 1);
    if (rd_all("/proc/driver/rtc", &txt) >= 0) {
        char *p, *save;
        for (p = strtok_r(txt, "\n", &save); p; p = strtok_r(NULL, "\n", &save)) {
            char *c = strchr(p, ':'), k[64];
            if (!c) continue;
            snprintf(k, sizeof k, "%.*s", (int)(c - p), p);
            while (*k && isspace((unsigned char)k[strlen(k) - 1])) k[strlen(k) - 1] = 0;
            c++;
            while (isspace((unsigned char)*c)) c++;
            so_kv(o, k, !strcmp(k, "batt_status") ? (strcmp(c, "okay") ? C_RED : C_GREEN) : C_VALUE, c);
        }
        free(txt);
    } else so_note(o, "/proc/driver/rtc not available (rtc_cmos not loaded)");
    so_par(o);
    if (path_exists("/sys/class/rtc/rtc0")) {
        static const char *const attrs[] = { "name", "date", "time", "since_epoch", "hctosys", "max_user_freq", "wakealarm", "offset", NULL };
        int i;
        long se = -1;
        so_b(o, C_VALUE, "/sys/class/rtc/rtc0");
        so_par(o);
        for (i = 0; attrs[i]; i++) if (rd_fmt(v, sizeof v, "/sys/class/rtc/rtc0/%s", attrs[i]) && *v) so_kv(o, attrs[i], C_VALUE, v);
        if (rd_long("/sys/class/rtc/rtc0/since_epoch", &se)) {
            long d = se - (long)time(NULL);
            so_kvf(o, "RTC - system clock", labs(d) > 2 ? C_ORANGE : C_GREEN, "%+ld s", d);
        }
    }
    if (rd_all("/etc/adjtime", &txt) >= 0) {
        so_kv(o, "RTC time scale", C_TEAL, strstr(txt, "LOCAL") ? "local time" : "UTC");
        free(txt);
    }
    so_board_footer(o);
}

#define CMOS_NVMEM "/sys/bus/nvmem/devices/cmos_nvram0/nvmem"
#define CMOS_BASE  0x0E   /* the kernel's CMOS nvmem starts after the 14 clock registers */

static int bcd2(int v) { return (v >> 4) <= 9 && (v & 15) <= 9 ? (v >> 4) * 10 + (v & 15) : -1; }

static const char *floppy_type(int t)
{
    static const char *const ft[] = { "none", "360 KB 5.25\"", "1.2 MB 5.25\"", "720 KB 3.5\"", "1.44 MB 3.5\"", "2.88 MB 3.5\"" };
    return t >= 0 && t <= 5 ? ft[t] : "unknown type";
}

static void s_system_cmos(srs_out *o)
{
    static const int tabs[] = { 200 };
    char v[128], *raw = NULL;
    const unsigned char *c;
    int n;
    so_siv_header(o, "System CMOS");
    so_tabs(o, tabs, 1);
    n = rd_all(CMOS_NVMEM, &raw);
    if (n <= 0) {
        so_kv(o, "CMOS RAM", C_ORANGE, path_exists(CMOS_NVMEM) ? "not readable" : "not available: the kernel has no cmos_nvram0 nvmem device (rtc_cmos driver)");
        free(raw);
        raw = NULL;
    }
    rd_str("/sys/class/rtc/rtc0/name", v, sizeof v);
    so_kv(o, "RTC driver", C_VALUE, v);
    {
        char *txt;
        if (rd_all("/proc/driver/rtc", &txt) >= 0) {
            const char *b = strstr(txt, "batt_status");
            if (b && strchr(b, ':')) {
                char st[32] = "";
                sscanf(strchr(b, ':') + 1, " %31s", st);
                so_kv(o, "CMOS battery", strcmp(st, "okay") ? C_RED : C_GREEN, st);
            }
            free(txt);
        }
    }
    if (raw) {
        c = (const unsigned char *)raw;
#define CM(a) ((a) - CMOS_BASE < n ? c[(a) - CMOS_BASE] : -1)
        so_kvf(o, "CMOS RAM", C_VALUE, "%d bytes, addresses 0x%02X - 0x%02X (" CMOS_NVMEM ")", n, CMOS_BASE, CMOS_BASE + n - 1);
        so_par(o);
        so_head(o, "Standard IBM AT bytes");
        if (CM(0x0E) >= 0) {
            static const char *const bits[] = { NULL, NULL, "time not valid", "disk controller failed", "memory size mismatch",
                                                "configuration not valid", "checksum bad", "clock lost power" };
            int d = CM(0x0E), k;
            char t[256] = "";
            for (k = 7; k >= 2; k--) if (d & (1 << k)) { strncat(t, bits[k], sizeof t - strlen(t) - 3); strcat(t, ", "); }
            if (*t) t[strlen(t) - 2] = 0;
            so_kvf(o, "0x0E Diagnostic status", d & 0xFC ? C_ORANGE : C_GREEN, "0x%02X  %s", d, *t ? t : "no errors flagged");
        }
        if (CM(0x0F) >= 0)
            so_kvf(o, "0x0F Shutdown status", C_VALUE, "0x%02X  %s", CM(0x0F), CM(0x0F) ? "restart code left by the last reset" : "normal power-on or reset");
        if (CM(0x10) >= 0)
            so_kvf(o, "0x10 Floppy drives", C_VALUE, "0x%02X  A: %s, B: %s", CM(0x10), floppy_type(CM(0x10) >> 4), floppy_type(CM(0x10) & 15));
        if (CM(0x16) >= 0) so_kvf(o, "0x15-0x16 Base memory", C_VALUE, "%d KB", CM(0x15) | CM(0x16) << 8);
        if (CM(0x18) >= 0) so_kvf(o, "0x17-0x18 Extended memory", C_VALUE, "%d KB (the AT field; it cannot show more than 64 MB)", CM(0x17) | CM(0x18) << 8);
        if (CM(0x2F) >= 0) {
            int a, sum = 0, st = CM(0x2E) << 8 | CM(0x2F);
            for (a = 0x10; a <= 0x2D; a++) sum += CM(a);
            if (!sum && !st) so_kv(o, "0x2E-0x2F Checksum", C_GRAY, "0x0000, matches (the AT area 0x10-0x2D is all zero: this firmware does not use it)");
            else so_kvf(o, "0x2E-0x2F Checksum", sum == st ? C_GREEN : C_GRAY, "0x%04X, sum of 0x10-0x2D is 0x%04X: %s", st, sum,
                        sum == st ? "matches" : "differs (many modern firmwares no longer keep this AT checksum)");
        }
        if (CM(0x32) >= 0) {
            int cy = bcd2(CM(0x32));
            if (cy >= 19 && cy <= 21) so_kvf(o, "0x32 Century", C_VALUE, "0x%02X  %d (years %d00 - %d99)", CM(0x32), cy, cy, cy);
            else so_kvf(o, "0x32 Century", C_GRAY, "0x%02X  not a century value (the firmware keeps the century elsewhere or not at all)", CM(0x32));
        }
#undef CM
        so_par(o);
        so_head(o, "CMOS RAM contents");
        hexdump_at(o, c, n, 256, CMOS_BASE, srs_redact);
        so_par(o);
        if (srs_redact) so_note(o, "Bytes masked (Redact Reports is on): CMOS can hold the BIOS password check value.");
        so_note(o, "Read-only copy from the kernel (no port I/O). Bytes not listed above belong to the BIOS maker and are not decoded.");
        free(raw);
    }
    so_note(o, "See CMOS / RTC status (cmos-status) for the clock registers.");
    so_board_footer(o);
}

static void s_system_ebda(srs_out *o)
{
    static const int tabs[] = { 200 };
    char s[40], e[40], t[48], p[1100];
    so_siv_header(o, "System EBDA");
    so_tabs(o, tabs, 1);
    if (root_path("ebda.txt", p, sizeof p)) {
        char *txt = NULL, *q, *save;
        unsigned long seg = 0, addr = 0;
        long kib = -1, bytes = 0;
        int have_seg = 0;
        if (rd_all(p, &txt) >= 0) {
            for (q = strtok_r(txt, "\n", &save); q; q = strtok_r(NULL, "\n", &save)) {
                if (!strncmp(q, "segment ", 8)) { seg = strtoul(q + 8, NULL, 16); have_seg = 1; }
                else if (!strncmp(q, "address ", 8)) addr = strtoul(q + 8, NULL, 16);
                else if (!strncmp(q, "size_kib ", 9)) kib = atol(q + 9);
                else if (!strncmp(q, "bytes ", 6)) bytes = atol(q + 6);
                else if (!strncmp(q, "error ", 6)) so_kvf(o, "Not readable", C_ORANGE, "%s", q + 6);
                else if (*q) so_kv(o, "Note", C_ORANGE, q);
            }
        }
        free(txt);
        if (have_seg && !seg) so_kv(o, "EBDA pointer (0x40E)", C_GREEN, "0000: no EBDA (normal on UEFI firmware)");
        else if (have_seg) {
            so_kvf(o, "EBDA pointer (0x40E)", C_VALUE, "%04lX (address 0x%05lX)", seg, addr);
            if (kib >= 0) so_kvf(o, "EBDA size", C_VALUE, "%ld KiB (from its first byte)", kib);
            if (bytes > 0 && root_path("ebda.bin", p, sizeof p)) {
                char *bin = NULL;
                int n = rd_all(p, &bin);
                if (n > 0) {
                    so_par(o);
                    so_head(o, "EBDA contents");
                    hexdump_at(o, (const unsigned char *)bin, n, 4096, (unsigned)addr, 0);
                    if (n > 4096) { so_f(o, C_GRAY, "%d more bytes in %s", n - 4096, p); so_par(o); }
                    so_par(o);
                }
                free(bin);
            }
        }
        so_note(o, "Read once by srs-root from the first megabyte of memory (read-only).");
        so_par(o);
    } else if (so_root_file("collected.txt")) {
        so_kv(o, "EBDA contents", C_ORANGE, "not in the root cache yet: run Menu > Run as root again");
    } else so_kv(o, "EBDA contents", C_ORANGE, "not read yet: needs the root cache (Menu > Run as root)");
    if (rd_str("/sys/firmware/memmap/0/start", s, sizeof s) && rd_str("/sys/firmware/memmap/0/end", e, sizeof e)) {
        rd_str("/sys/firmware/memmap/0/type", t, sizeof t);
        so_kvf(o, "Conventional memory", C_VALUE, "%s - %s %s", s, e, t);
        if (strtoull(e, NULL, 16) >= 0x9FFFFULL) so_kv(o, "EBDA (implied)", C_PURPLE, "none reserved: the firmware map gives RAM up to 0x9FFFF");
        else so_kvf(o, "EBDA (implied)", C_PURPLE, "0x%llX - 0x9FFFF (below the 640KB line)", strtoull(e, NULL, 16) + 1);
    }
    so_kv(o, "Firmware", C_VALUE, path_exists(EFI_DIR) ? "UEFI (EBDA kept only for CSM/legacy compatibility)" : "Legacy BIOS");
    if (root_path("iomem.txt", p, sizeof p)) {
        char *txt;
        if (rd_all(p, &txt) >= 0) {
            char *q, *save;
            so_par(o);
            so_note(o, "iomem below 1MB (srs-root cache):");
            for (q = strtok_r(txt, "\n", &save); q; q = strtok_r(NULL, "\n", &save))
                if (strtoull(q + strspn(q, " "), NULL, 16) < 0x100000ULL) { so_m(o, C_VALUE, q); so_par(o); }
            free(txt);
        }
    } else needs_root(o);
    so_board_footer(o);
}

/* ======================================================================
 * ACPI: [acpi-tables] [acpi-hal] [acpi-int] [acpi-dev] [acpi-buses] [acpi-ports] [acpi-pwr]
 *       [acpi-spb] [acpi-wmi] [acpi-pdx] [acpi-ec] [acpi-eval] [acpi-methods]
 * ====================================================================== */
static void s_acpi_tables(srs_out *o)
{
    static const int tabs[] = { 130, 200, 240, 290, 360, 450, 540, 590 };
    char **v;
    int n = acpi_list(&v), i, cached = 0;
    so_siv_header(o, "ACPI Tables");
    if (safe_arg(o->arg)) {
        unsigned char *d;
        int len = acpi_load(o->arg, &d);
        static const int t1[] = { 180 };
        list_free(v, n);
        so_tabs(o, t1, 1);
        if (len >= 64 && !memcmp(d, "FACS", 4)) {
            so_kv(o, "Signature", C_BLACK, "FACS (no standard ACPI header)");
            so_kvf(o, "Length", C_VALUE, "%u", le32(d + 4));
            so_kvf(o, "Hardware Signature", C_VALUE, "0x%08X", le32(d + 8));
            so_kvf(o, "Firmware Waking Vector", C_PURPLE, "0x%08X", le32(d + 12));
            so_kvf(o, "Global Lock", C_VALUE, "0x%08X", le32(d + 16));
            so_kvf(o, "Flags", C_VALUE, "0x%08X%s", le32(d + 20), le32(d + 20) & 1 ? "  S4BIOS_F" : "");
            so_kvf(o, "X Firmware Waking Vector", C_PURPLE, "0x%016llX", (unsigned long long)le64(d + 24));
            so_kvf(o, "Version", C_VALUE, "%u", d[32]);
            so_par(o);
            hexdump(o, d, len, 4096);
        } else if (len >= 36) {
            acpi_hdr h;
            acpi_hdr_parse(d, len, &h);
            acpi_hdr_rows(o, &h);
            so_par(o);
            hexdump(o, d, len, 4096);
        } else { so_kvf(o, o->arg, C_VALUE, "%s", acpi_present(o->arg) ? "present" : "not present"); needs_root(o); }
        free(d);
        so_board_footer(o);
        return;
    }
    so_tabs(o, tabs, 8);
    so_row(o, 9, (const char *const[]){ "Table", "Length", "Rev", "Check", "OEM", "OEM Table", "OEM Rev", "Creator", "Creator Rev" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (i = 0; i < n; i++) {
        char p[256], c[9][48];
        unsigned char *d;
        int len = acpi_load(v[i], &d);
        snprintf(p, sizeof p, ACPI_TAB "/%s", v[i]);
        snprintf(c[0], 48, "%s", v[i]);
        if (len >= 64 && !strncmp(v[i], "FACS", 4) && !memcmp(d, "FACS", 4)) {
            /* FACS has no standard header: signature, length, hardware signature, waking vector,
               global lock, flags, X waking vector, version - no checksum / OEM fields */
            snprintf(c[1], 48, "%u", le32(d + 4));
            snprintf(c[2], 48, "%u", d[32]);
            snprintf(c[3], 48, "-");
            snprintf(c[4], 48, "HW sig %08X", le32(d + 8));
            c[5][0] = c[6][0] = c[7][0] = c[8][0] = 0;
            cached++;
            so_row(o, 9, (const char *const[]){ c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], c[8] },
                   (const int[]){ C_BLACK, C_VALUE, C_VALUE, C_GRAY, C_GRAY, C_GRAY, C_GRAY, C_GRAY, C_GRAY });
            free(d);
            continue;
        }
        if (len >= 36) {
            acpi_hdr h;
            acpi_hdr_parse(d, len, &h);
            cached++;
            snprintf(c[1], 48, "%u", h.len);
            snprintf(c[2], 48, "%u", h.rev);
            snprintf(c[3], 48, "%02X", h.csum);
            snprintf(c[4], 48, "%s", h.oem);
            snprintf(c[5], 48, "%s", h.oemtab);
            snprintf(c[6], 48, "%08X", h.oemrev);
            snprintf(c[7], 48, "%s", h.creator);
            snprintf(c[8], 48, "%08X", h.crrev);
            so_row(o, 9, (const char *const[]){ c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], c[8] },
                   (const int[]){ C_BLACK, C_VALUE, C_VALUE, C_GRAY, C_TEAL, C_TEAL, C_GRAY, C_PURPLE, C_GRAY });
        } else {
            snprintf(c[1], 48, "%lld", fsize(p));
            so_row(o, 2, (const char *const[]){ c[0], c[1] }, (const int[]){ C_BLACK, C_VALUE });
        }
        free(d);
    }
    list_free(v, n);
    so_par(o);
    so_f(o, C_TEAL, "%d tables", n);
    so_par(o);
    if (!cached) { so_note(o, "Headers (OEM, revision, creator) need the srs-root acpi-tables/ cache."); needs_root(o); }
    else so_note(o, "One table with hex dump: acpi-tables:<SIG>");
    so_board_footer(o);
}

static void s_acpi_hal(srs_out *o)
{
    static const int tabs[] = { 220 };
    static const char *const prof[] = { "Unspecified", "Desktop", "Mobile", "Workstation", "Enterprise Server", "SOHO Server",
                                        "Appliance PC", "Performance Server", "Tablet" };
    char v[128], **e, **tl;
    int ne, i, nt;
    long pm = -1;
    so_siv_header(o, "ACPI HAL");
    so_tabs(o, tabs, 1);
    if (!path_exists("/sys/firmware/acpi")) { so_err(o, "ACPI not enabled"); so_board_footer(o); return; }
    rd_long("/sys/firmware/acpi/pm_profile", &pm);
    so_kvf(o, "Preferred PM Profile", C_BLACK, "%ld (%s)", pm, pm >= 0 && pm < 9 ? prof[pm] : "Reserved");
    if (rd_str("/sys/module/acpi/parameters/acpica_version", v, sizeof v)) so_kv(o, "ACPICA Version", C_VALUE, v);
    nt = acpi_list(&tl);
    list_free(tl, nt);
    so_kvf(o, "ACPI Tables", C_VALUE, "%d", nt);
    ne = dir_list(ACPI_DEV, &e);
    so_kvf(o, "ACPI Devices", C_VALUE, "%d", ne < 0 ? 0 : ne);
    dir_free(e, ne);
    rd_str("/sys/firmware/acpi/interrupts/sci", v, sizeof v);
    so_kvf(o, "SCI Interrupts", C_VALUE, "%ld", atol(v));
    rd_str("/sys/firmware/acpi/interrupts/gpe_all", v, sizeof v);
    so_kvf(o, "GPE Events", C_VALUE, "%ld", atol(v));
    so_kv(o, "Hardware-reduced ACPI", C_VALUE, path_exists("/sys/firmware/acpi/interrupts/ff_pmtimer") ? "No (fixed hardware present)" : "Yes / unknown");
    so_kv(o, "Firmware Performance (FPDT)", C_VALUE, path_exists("/sys/firmware/acpi/fpdt") ? "present" : "absent");
    ne = dir_list("/sys/firmware/acpi/hotplug", &e);
    for (i = 0; i < ne; i++) {
        char k[96];
        snprintf(k, sizeof k, "Hotplug %s", e[i]);
        if (rd_fmt(v, sizeof v, "/sys/firmware/acpi/hotplug/%s/enabled", e[i])) so_kv(o, k, C_VALUE, atoi(v) ? "enabled" : "disabled");
    }
    dir_free(e, ne);
    so_par(o);
    ne = dir_list("/sys/module/acpi/parameters", &e);
    for (i = 0; i < ne; i++) {
        char k[96];
        if (!strcmp(e[i], "acpica_version")) continue;
        snprintf(k, sizeof k, "acpi.%s", e[i]);
        if (rd_fmt(v, sizeof v, "/sys/module/acpi/parameters/%s", e[i])) so_kv(o, k, C_GRAY, v);
    }
    dir_free(e, ne);
    so_board_footer(o);
}

static void s_acpi_int(srs_out *o)
{
    static const int tabs[] = { 150, 260 };
    char **e, v[128];
    int ne = dir_list("/sys/firmware/acpi/interrupts", &e), i, zero = 0;
    so_siv_header(o, "ACPI Interrupts and GPEs");
    if (ne <= 0) { so_err(o, "/sys/firmware/acpi/interrupts not available"); so_board_footer(o); return; }
    nat_sort(e, ne);
    so_tabs(o, tabs, 2);
    so_row(o, 3, (const char *const[]){ "Event", "Count", "State" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (i = 0; i < ne; i++) {
        char cnt[32] = "", st[96] = "";
        const char *r;
        int en;
        if (!rd_fmt(v, sizeof v, "/sys/firmware/acpi/interrupts/%s", e[i])) continue;
        sscanf(v, "%31s", cnt);
        r = v + strspn(v, " ");
        r += strcspn(r, " ");
        while (*r == ' ') r++;
        {   /* collapse runs of blanks: "EN     enabled" -> "EN enabled" */
            size_t k = 0;
            for (; *r && k + 1 < sizeof st; r++) if (*r != ' ' || (k && st[k - 1] != ' ')) st[k++] = *r;
            st[k] = 0;
        }
        en = strstr(st, "enabled") && !strstr(st, "disabled");
        if (!strncmp(e[i], "gpe", 3) && isxdigit((unsigned char)e[i][3]) && !strcmp(cnt, "0") && !en) { zero++; continue; }
        so_row(o, 3, (const char *const[]){ e[i], cnt, *st ? st : "-" },
               (const int[]){ C_LABEL, atol(cnt) ? C_BLACK : C_GRAY, en ? C_GREEN : C_GRAY });
    }
    dir_free(e, ne);
    so_par(o);
    so_f(o, C_GRAY, "%d idle disabled/invalid GPEs not shown", zero);
    so_par(o);
    {
        char *txt;
        if (rd_all("/proc/interrupts", &txt) >= 0) {
            char *p, *save;
            for (p = strtok_r(txt, "\n", &save); p; p = strtok_r(NULL, "\n", &save))
                if (strstr(p, "acpi")) {
                    unsigned long long sum = 0;
                    char *q = strchr(p, ':'), irq[16];
                    snprintf(irq, sizeof irq, "%.*s", q ? (int)(q - p) : 0, p);
                    if (q) {
                        char *r = q + 1, *end;
                        for (;;) { unsigned long long x = strtoull(r, &end, 10); if (end == r) break; sum += x; r = end; }
                    }
                    so_f(o, C_PURPLE, "SCI IRQ %s: %llu interrupts (all CPUs)", irq + strspn(irq, " "), sum);
                    so_par(o);
                }
            free(txt);
        }
    }
    so_board_footer(o);
}

static void s_acpi_dev(srs_out *o)
{
    static const int tabs[] = { 120, 210, 290, 520 };
    char **e;
    int ne, i;
    so_siv_header(o, "ACPI Devices");
    if (safe_arg(o->arg)) {
        static const int t1[] = { 170 };
        static const char *const attrs[] = { "path", "hid", "modalias", "uid", "adr", "status", "description", "power_state",
                                             "real_power_state", "resource_in_use", NULL };
        char p[256], v[256], sb[32];
        snprintf(p, sizeof p, ACPI_DEV "/%s", o->arg);
        so_tabs(o, t1, 1);
        if (!path_exists(p)) { so_err(o, "No such ACPI device"); so_board_footer(o); return; }
        for (i = 0; attrs[i]; i++)
            if (rd_fmt(v, sizeof v, ACPI_DEV "/%s/%s", o->arg, attrs[i])) so_kv(o, attrs[i], C_VALUE, !strcmp(attrs[i], "status") ? sta_str(v, sb, sizeof sb) : v);
        adev_phys(o->arg, v, sizeof v); so_kv(o, "physical_node", C_PURPLE, v);
        adev_driver(o->arg, v, sizeof v); so_kv(o, "driver", C_TEAL, v);
        ne = dir_list(p, &e);
        for (i = 0; i < ne; i++) if (strchr(e[i], ':') && strcmp(e[i], "physical_node")) so_kv(o, "child", C_GRAY, e[i]);
        dir_free(e, ne);
        so_board_footer(o);
        return;
    }
    ne = dir_list(ACPI_DEV, &e);
    nat_sort(e, ne);
    so_tabs(o, tabs, 4);
    so_row(o, 5, (const char *const[]){ "Device", "HID", "_STA", "Path", "Physical node / driver" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (i = 0; i < ne; i++) {
        char hid[32], st[32], sb[32], path[160], ph[256], dr[64], pd[320];
        adev(e[i], "hid", hid, sizeof hid);
        adev(e[i], "status", st, sizeof st);
        adev(e[i], "path", path, sizeof path);
        adev_phys(e[i], ph, sizeof ph);
        adev_driver(e[i], dr, sizeof dr);
        snprintf(pd, sizeof pd, "%s%s%s", ph, *ph && strcmp(dr, "-") ? "  " : "", strcmp(dr, "-") ? dr : "");
        so_row(o, 5, (const char *const[]){ e[i], *hid ? hid : "-", *st ? sta_str(st, sb, sizeof sb) : "-", path, pd },
               (const int[]){ C_LABEL, C_VALUE, C_GRAY, C_PURPLE, C_TEAL });
    }
    so_par(o);
    so_f(o, C_TEAL, "%d ACPI namespace devices  (_STA: P present E enabled S shown F functioning; detail: acpi-dev:<device>)", ne < 0 ? 0 : ne);
    so_par(o);
    dir_free(e, ne);
    so_board_footer(o);
}

static void s_acpi_buses(srs_out *o)
{
    static const int tabs[] = { 110, 290, 400, 560 };
    char **e;
    int ne = dir_list(ACPI_DEV, &e), i;
    so_siv_header(o, "ACPI PCI Root Buses");
    so_tabs(o, tabs, 4);
    so_row(o, 5, (const char *const[]){ "Device", "Path", "UID", "Physical node", "Status" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    for (i = 0; i < ne; i++)
        if (!strncmp(e[i], "PNP0A08", 7) || !strncmp(e[i], "PNP0A03", 7) || !strncmp(e[i], "ACPI0016", 8)) {
            char path[128], uid[16], ph[128], st[32], sb[32];
            adev(e[i], "path", path, sizeof path);
            adev(e[i], "uid", uid, sizeof uid);
            adev(e[i], "status", st, sizeof st);
            adev_phys(e[i], ph, sizeof ph);
            so_row(o, 5, (const char *const[]){ e[i], path, uid, ph, sta_str(st, sb, sizeof sb) },
                   (const int[]){ C_LABEL, C_PURPLE, C_VALUE, C_TEAL, C_GRAY });
        }
    dir_free(e, ne);
    so_par(o);
    {
        static const int t2[] = { 110, 230, 470 };
        so_tabs(o, t2, 3);
        so_row(o, 4, (const char *const[]){ "PCI Bus", "Bridge", "Bridge ACPI path", "Bus range" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL });
        ne = dir_list("/sys/class/pci_bus", &e);
        for (i = 0; i < ne; i++) {
            char p[256], br[64] = "", path[160] = "-", rng[32] = "";
            snprintf(p, sizeof p, "/sys/class/pci_bus/%s/device", e[i]);
            link_base(p, br, sizeof br);
            if (strchr(br, ':') && strchr(br, '.')) {
                char fn[256], an[64];
                snprintf(fn, sizeof fn, "/sys/bus/pci/devices/%s/firmware_node", br);
                if (link_base(fn, an, sizeof an)) adev(an, "path", path, sizeof path);
                {
                    char sec[16], sub[16];
                    if (rd_fmt(sec, sizeof sec, "/sys/bus/pci/devices/%s/secondary_bus_number", br) &&
                        rd_fmt(sub, sizeof sub, "/sys/bus/pci/devices/%s/subordinate_bus_number", br))
                        snprintf(rng, sizeof rng, "%02lX-%02lX", atol(sec), atol(sub));
                }
            } else {
                char an[64];
                snprintf(p, sizeof p, "/sys/class/pci_bus/%s/device/firmware_node", e[i]);
                if (link_base(p, an, sizeof an)) adev(an, "path", path, sizeof path);
                snprintf(br, sizeof br, "root");
            }
            so_row(o, 4, (const char *const[]){ e[i], br, path, rng }, (const int[]){ C_LABEL, C_VALUE, C_PURPLE, C_GRAY });
        }
        dir_free(e, ne);
    }
    so_board_footer(o);
}

static void s_acpi_ports(srs_out *o)
{
    static const int tabs[] = { 100, 160, 260, 340, 540 };
    char **e;
    int ne = dir_list("/sys/bus/pci/devices", &e), i, n = 0;
    so_siv_header(o, "ACPI PCI Buses and Ports");
    so_tabs(o, tabs, 5);
    so_row(o, 6, (const char *const[]){ "PCI", "Class", "ID", "ACPI node", "ACPI path", "Driver" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (i = 0; i < ne; i++) {
        char p[256], an[64], path[160] = "-", cls[16] = "", ven[16] = "", dev[16] = "", id[24], dr[64] = "-";
        long c;
        snprintf(p, sizeof p, "/sys/bus/pci/devices/%s/firmware_node", e[i]);
        if (!link_base(p, an, sizeof an)) continue;
        adev(an, "path", path, sizeof path);
        rd_fmt(cls, sizeof cls, "/sys/bus/pci/devices/%s/class", e[i]);
        rd_fmt(ven, sizeof ven, "/sys/bus/pci/devices/%s/vendor", e[i]);
        rd_fmt(dev, sizeof dev, "/sys/bus/pci/devices/%s/device", e[i]);
        snprintf(p, sizeof p, "/sys/bus/pci/devices/%s/driver", e[i]);
        link_base(p, dr, sizeof dr);
        c = strtol(cls, NULL, 16);
        snprintf(cls, sizeof cls, "%04lX%s", c >> 8, (c >> 8) == 0x0604 ? " PtP" : "");
        snprintf(id, sizeof id, "%s:%s", ven + (ven[0] == '0' && ven[1] == 'x' ? 2 : 0), dev + (dev[0] == '0' && dev[1] == 'x' ? 2 : 0));
        so_row(o, 6, (const char *const[]){ e[i] + 5, cls, id, an, path, *dr ? dr : "-" },
               (const int[]){ C_LABEL, (c >> 8) == 0x0604 ? C_ORANGE : C_GRAY, C_GRAY, C_VALUE, C_PURPLE, C_TEAL });
        n++;
    }
    dir_free(e, ne);
    so_par(o);
    so_f(o, C_TEAL, "%d PCI functions with an ACPI companion (PtP = PCI-to-PCI bridge / port)", n);
    so_par(o);
    so_board_footer(o);
}

static void s_acpi_pwr(srs_out *o)
{
    static const int tabs[] = { 120, 360, 450 };
    char **e;
    int ne = dir_list(ACPI_DEV, &e), i, n = 0;
    so_siv_header(o, "ACPI Power Resources");
    so_tabs(o, tabs, 3);
    so_row(o, 4, (const char *const[]){ "Resource", "Path", "In use", "Status" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    for (i = 0; i < ne; i++)
        if (!strncmp(e[i], "LNXPOWER", 8)) {
            char path[160], use[16], st[32], sb[32];
            adev(e[i], "path", path, sizeof path);
            adev(e[i], "resource_in_use", use, sizeof use);
            adev(e[i], "status", st, sizeof st);
            so_row(o, 4, (const char *const[]){ e[i], path, atoi(use) ? "on" : "off", *st ? sta_str(st, sb, sizeof sb) : "-" },
                   (const int[]){ C_LABEL, C_PURPLE, atoi(use) ? C_GREEN : C_GRAY, C_GRAY });
        }
    so_par(o);
    {
        static const int t2[] = { 120, 360, 450 };
        so_tabs(o, t2, 3);
        so_row(o, 4, (const char *const[]){ "Device", "Path", "power_state", "real_power_state" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL });
        for (i = 0; i < ne; i++) {
            char path[160], ps[32], rps[32] = "";
            if (!rd_fmt(ps, sizeof ps, ACPI_DEV "/%s/power_state", e[i])) continue;
            adev(e[i], "path", path, sizeof path);
            adev(e[i], "real_power_state", rps, sizeof rps);
            so_row(o, 4, (const char *const[]){ e[i], path, ps, *rps ? rps : "-" },
                   (const int[]){ C_LABEL, C_PURPLE, !strcmp(ps, "D0") ? C_GREEN : C_ORANGE, C_GRAY });
            n++;
        }
    }
    dir_free(e, ne);
    so_par(o);
    so_f(o, C_TEAL, "%d devices with ACPI power management (_PSx / _PRx)", n);
    so_par(o);
    so_board_footer(o);
}

static void s_acpi_spb(srs_out *o)
{
    static const int tabs[] = { 130, 340, 470 };
    static const char *const spb[] = { "AMDI0010", "AMDI0019", "AMDI0020", "AMDI0060", "AMDI0062", "AMDI0510", "AMDI0511",
                                       "INT33C", "INT3432", "INT3433", "80860F41", "PNP0501", "PNP0500", "RTK", NULL };
    char **e;
    int ne = dir_list(ACPI_DEV, &e), i, j;
    so_siv_header(o, "ACPI Serial Peripheral Buses");
    so_tabs(o, tabs, 3);
    so_row(o, 4, (const char *const[]){ "Device", "Path", "Driver", "Physical node" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (i = 0; i < ne; i++)
        for (j = 0; spb[j]; j++)
            if (!strncmp(e[i], spb[j], strlen(spb[j]))) {
                char path[160], dr[64], ph[256];
                adev(e[i], "path", path, sizeof path);
                adev_driver(e[i], dr, sizeof dr);
                adev_phys(e[i], ph, sizeof ph);
                so_row(o, 4, (const char *const[]){ e[i], path, dr, ph }, (const int[]){ C_LABEL, C_PURPLE, C_TEAL, C_GRAY });
                break;
            }
    dir_free(e, ne);
    so_par(o);
    {
        static const int t2[] = { 130, 430 };
        so_tabs(o, t2, 2);
        so_row(o, 3, (const char *const[]){ "I2C", "Adapter / client name", "ACPI path" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL });
        ne = dir_list("/sys/bus/i2c/devices", &e);
        nat_sort(e, ne);
        for (i = 0; i < ne; i++) {
            char nm[128] = "", p[256], an[64], path[160] = "-";
            rd_fmt(nm, sizeof nm, "/sys/bus/i2c/devices/%s/name", e[i]);
            snprintf(p, sizeof p, "/sys/bus/i2c/devices/%s/firmware_node", e[i]);
            if (link_base(p, an, sizeof an)) adev(an, "path", path, sizeof path);
            so_row(o, 3, (const char *const[]){ e[i], nm, path }, (const int[]){ C_LABEL, C_VALUE, C_PURPLE });
        }
        dir_free(e, ne);
        ne = dir_list("/sys/bus/spi/devices", &e);
        for (i = 0; i < ne; i++) so_row(o, 2, (const char *const[]){ e[i], "SPI device" }, (const int[]){ C_LABEL, C_VALUE });
        dir_free(e, ne);
    }
    so_note(o, "Names only: srs lists I2C/SPI devices from sysfs and never talks on the bus.");
    so_board_footer(o);
}

static const char *wmi_guid_name(const char *g)
{
    static const struct { const char *g, *n; } k[] = {
        { "05901221-D566-11D1-B2F0-00A0C9062910", "Binary MOF (WMI class descriptions)" },
        { "97845ED0-4E6D-11DE-8A39-0800200C9A66", "ASUS management (ATK / asus-wmi)" },
        { "466747A0-70EC-11DE-8A39-0800200C9A66", "ASUS hardware monitor (asus-wmi-sensors)" },
        { "8F661E33-E9A7-4B9F-9A0D-6DAD48EE95A3", "Dell / Alienware" },
        { NULL, NULL } };
    int i;
    for (i = 0; k[i].g; i++) if (!strncasecmp(g, k[i].g, 36)) return k[i].n;
    return "";
}

static void s_acpi_wmi(srs_out *o)
{
    static const int tabs[] = { 300, 340, 380, 420, 470, 620 };
    char **e;
    int ne = dir_list("/sys/bus/wmi/devices", &e), i;
    so_siv_header(o, "ACPI WMI (PNP0C14)");
    if (ne <= 0) { so_err(o, "No WMI devices (/sys/bus/wmi/devices empty or wmi module not loaded)"); dir_free(e, ne); so_board_footer(o); return; }
    so_tabs(o, tabs, 6);
    so_row(o, 7, (const char *const[]){ "GUID", "Obj", "Inst", "Exp", "Set", "Driver", "Description" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    nat_sort(e, ne);
    for (i = 0; i < ne; i++) {
        char g[64] = "", obj[16] = "-", ins[16] = "-", ex[8] = "-", se[8] = "-", dr[64] = "-", p[256];
        rd_fmt(g, sizeof g, "/sys/bus/wmi/devices/%s/guid", e[i]);
        rd_fmt(obj, sizeof obj, "/sys/bus/wmi/devices/%s/object_id", e[i]);
        if (!*obj) rd_fmt(obj, sizeof obj, "/sys/bus/wmi/devices/%s/notify_id", e[i]);
        rd_fmt(ins, sizeof ins, "/sys/bus/wmi/devices/%s/instance_count", e[i]);
        rd_fmt(ex, sizeof ex, "/sys/bus/wmi/devices/%s/expensive", e[i]);
        rd_fmt(se, sizeof se, "/sys/bus/wmi/devices/%s/setable", e[i]);
        snprintf(p, sizeof p, "/sys/bus/wmi/devices/%s/driver", e[i]);
        link_base(p, dr, sizeof dr);
        so_row(o, 7, (const char *const[]){ *g ? g : e[i], *obj ? obj : "-", *ins ? ins : "-", *ex ? ex : "-", *se ? se : "-",
                                            *dr ? dr : "-", wmi_guid_name(*g ? g : e[i]) },
               (const int[]){ C_PURPLE, C_VALUE, C_VALUE, C_GRAY, C_GRAY, C_TEAL, C_VALUE });
    }
    dir_free(e, ne);
    so_par(o);
    {
        static const int t2[] = { 120 };
        static const char *const h[] = { "PNP0C14", NULL };
        so_tabs(o, t2, 1);
        so_b(o, C_VALUE, "WMI mapper devices");
        so_par(o);
        acpi_probe(o, h);
    }
    so_note(o, "Obj = ACPI WQxx/WMxx object id or notify id. srs never invokes WMI methods.");
    so_board_footer(o);
}

static void s_acpi_pdx(srs_out *o)
{
    static const int tabs[] = { 110, 160, 380 };
    char **e;
    int ne = dir_list(ACPI_DEV, &e), i, j;
    char seen[64][16];
    int ns = 0;
    so_siv_header(o, "ACPI Vendor Extensions");
    so_tabs(o, tabs, 3);
    so_row(o, 4, (const char *const[]){ "HID", "Count", "First path", "Driver" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (i = 0; i < ne; i++) {
        char hid[16], path[160] = "", dr[64] = "-", cnt[8];
        int c = 0;
        snprintf(hid, sizeof hid, "%.*s", (int)strcspn(e[i], ":"), e[i]);
        if (!strncmp(hid, "PNP", 3) || !strncmp(hid, "LNX", 3) || !strncmp(hid, "ACPI", 4) || !strcmp(hid, "device")) continue;
        for (j = 0; j < ns; j++) if (!strcmp(seen[j], hid)) break;
        if (j < ns || ns >= 64) continue;
        snprintf(seen[ns++], 16, "%s", hid);
        for (j = 0; j < ne; j++) if (!strncmp(e[j], hid, strlen(hid)) && e[j][strlen(hid)] == ':') c++;
        adev(e[i], "path", path, sizeof path);
        adev_driver(e[i], dr, sizeof dr);
        snprintf(cnt, sizeof cnt, "%d", c);
        so_row(o, 4, (const char *const[]){ hid, cnt, path, dr }, (const int[]){ C_LABEL, C_VALUE, C_PURPLE, C_TEAL });
    }
    dir_free(e, ne);
    so_par(o);
    so_note(o, "Vendor-specific (non PNP/LNX/ACPI) hardware IDs in the ACPI namespace: AMDI = AMD, MSFT = Microsoft, RTK = Realtek, ASMT = ASMedia.");
    so_board_footer(o);
}

static void s_acpi_ec(srs_out *o)
{
    static const int tabs[] = { 200 };
    static const char *const h[] = { "PNP0C09", NULL };
    char **e, v[160];
    int ne = dir_list(ACPI_DEV, &e), i, hits;
    so_siv_header(o, "ACPI Embedded Controller");
    so_tabs(o, tabs, 1);
    hits = acpi_probe(o, h);
    for (i = 0; i < ne; i++)
        if (!strncmp(e[i], "PNP0C09", 7)) {
            char sb[32];
            adev(e[i], "status", v, sizeof v); so_kv(o, "_STA", C_VALUE, sta_str(v, sb, sizeof sb));
            adev(e[i], "uid", v, sizeof v); so_kv(o, "UID", C_VALUE, v);
            adev_driver(e[i], v, sizeof v); so_kv(o, "Driver", C_TEAL, v);
        }
    dir_free(e, ne);
    if (!hits) so_note(o, "No ACPI EC (PNP0C09)");
    if (rd_str("/sys/module/acpi/parameters/ec_storm_threshold", v, sizeof v)) so_kv(o, "ec_storm_threshold", C_GRAY, v);
    if (rd_str("/sys/module/acpi/parameters/ec_busy_polling", v, sizeof v)) so_kv(o, "ec_busy_polling", C_GRAY, v);
    if (rd_str("/sys/module/acpi/parameters/ec_max_queries", v, sizeof v)) so_kv(o, "ec_max_queries", C_GRAY, v);
    so_kv(o, "ec_sys module", C_VALUE, path_exists("/sys/module/ec_sys") ? "loaded (register dump via debugfs, root only)" : "not loaded");
    so_kv(o, "EC register dump", C_ORANGE, "not read: /sys/kernel/debug/ec/ec0/io is root-only and not in the srs-root cache");
    needs_root(o);
    so_board_footer(o);
}

static void s_acpi_eval(srs_out *o)
{
    static const int tabs[] = { 200 };
    so_siv_header(o, "ACPI Method Evaluation");
    so_tabs(o, tabs, 1);
    so_kv(o, "acpi_call", C_VALUE, path_exists("/proc/acpi/call") ? "loaded (/proc/acpi/call, root only)"
                                   : "not loaded (srs-root loads it when you evaluate a method; Arch package: acpi_call-dkms)");
    so_kv(o, "Evaluate a method", C_ORANGE, "Tools > ACPI method evaluation (for experts: runs BIOS code as root and can change hardware state)");
    so_kv(o, "Method names", C_TEAL, "acpi-methods (the methods in this PC's DSDT / SSDTs), acpi-dev (_STA, _HID, _UID, _ADR per device)");
    {   /* srs-root logs every evaluation: time TAB method + arguments TAB result */
        const char *lf = so_root_file("acpi-calls.txt");
        char *t = NULL;
        if (lf && rd_all(lf, &t) > 0) {
            static const int ct[] = { 190, 560 };
            char *save = NULL, *ln, *lines[400];
            int n = 0, i;
            for (ln = strtok_r(t, "\n", &save); ln && n < 400; ln = strtok_r(NULL, "\n", &save)) lines[n++] = ln;
            so_par(o);
            so_head(o, n > 30 ? "Your last 30 evaluations (newest last)" : "Your evaluations (newest last)");
            so_tabs(o, ct, 2);
            {
                const char *h[3] = { "When", "Method and arguments", "Result" };
                static const int hc[3] = { C_LABEL, C_LABEL, C_LABEL };
                so_row(o, 3, h, hc);
            }
            for (i = n > 30 ? n - 30 : 0; i < n; i++) {
                char *f1 = lines[i], *f2 = strchr(f1, '\t'), *f3 = f2 ? strchr(f2 + 1, '\t') : NULL;
                const char *cells[3];
                static const int cc[3] = { C_GRAY, C_BLACK, C_VALUE };
                if (!f2 || !f3) continue;
                *f2++ = 0; *f3++ = 0;
                cells[0] = f1; cells[1] = f2; cells[2] = f3;
                so_row(o, 3, cells, cc);
            }
            so_tabs(o, tabs, 1);
        }
        free(t);
    }
    so_board_footer(o);
}

/* AML MethodOp scan over a cached DSDT/SSDT */
static int aml_pkglen(const unsigned char *p, int avail, int *bytes)
{
    int n, len, i;
    if (avail < 1) return -1;
    n = p[0] >> 6;
    if (avail < n + 1) return -1;
    if (!n) { *bytes = 1; return p[0] & 0x3F; }
    len = p[0] & 0x0F;
    for (i = 0; i < n; i++) len |= p[1 + i] << (4 + 8 * i);
    *bytes = n + 1;
    return len;
}
static int seg_ok(const unsigned char *s)
{
    int i;
    if (!(isupper(s[0]) || s[0] == '_')) return 0;
    for (i = 1; i < 4; i++) if (!(isupper(s[i]) || isdigit(s[i]) || s[i] == '_')) return 0;
    return 1;
}
static int aml_name(const unsigned char *p, int avail, char *out, size_t sz)
{
    int i = 0, segs = 1, k;
    size_t o = 0;
    while (i < avail && (p[i] == '\\' || p[i] == '^')) { if (o + 1 < sz) out[o++] = (char)p[i]; i++; }
    if (i >= avail) return -1;
    if (p[i] == 0x2E) { segs = 2; i++; }
    else if (p[i] == 0x2F) { if (i + 1 >= avail) return -1; segs = p[i + 1]; i += 2; if (segs < 1 || segs > 16) return -1; }
    for (k = 0; k < segs; k++) {
        int c;
        if (i + 4 > avail || !seg_ok(p + i)) return -1;
        if (k && o + 1 < sz) out[o++] = '.';
        for (c = 0; c < 4; c++) if (o + 1 < sz) out[o++] = (char)p[i + c];
        i += 4;
    }
    out[o] = 0;
    return i;
}

static void s_acpi_methods(srs_out *o)
{
    static const int tabs[] = { 100, 340, 390 };
    char **v;
    int n = acpi_list(&v), t, total = 0, any = 0;
    so_siv_header(o, "ACPI Methods");
    so_tabs(o, tabs, 3);
    so_row(o, 4, (const char *const[]){ "Table", "Method", "Args", "Flags" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (t = 0; t < n; t++) {
        unsigned char *d;
        int len, i, cnt = 0;
        const char *sig = strncmp(v[t], "dynamic/", 8) ? v[t] : v[t] + 8;
        if (strncmp(sig, "DSDT", 4) && strncmp(sig, "SSDT", 4)) continue;
        if (o->arg && *o->arg && strcmp(o->arg, sig)) continue;
        len = acpi_load(v[t], &d);
        if (len < 36) { free(d); continue; }
        any = 1;
        if (le32(d + 4) < (uint32_t)len) len = (int)le32(d + 4);
        for (i = 36; i < len - 6; i++) {
            int pb, pl, nl;
            char name[96], args[8], fl[32];
            if (d[i] != 0x14) continue;
            pl = aml_pkglen(d + i + 1, len - i - 1, &pb);
            if (pl < 6 || i + 1 + pl > len) continue;
            nl = aml_name(d + i + 1 + pb, pl - pb, name, sizeof name);
            if (nl < 0 || 1 + pb + nl >= 1 + pl) continue;
            {
                unsigned f = d[i + 1 + pb + nl];
                snprintf(args, sizeof args, "%u", f & 7);
                snprintf(fl, sizeof fl, "%s%s", f & 8 ? "Serialized" : "NotSerialized", (f >> 4) ? " Sync" : "");
            }
            so_row(o, 4, (const char *const[]){ sig, name, args, fl }, (const int[]){ C_GRAY, C_VALUE, C_BLACK, C_GRAY });
            cnt++;
            i += pl;   /* methods do not nest: skip the body */
        }
        total += cnt;
        free(d);
    }
    list_free(v, n);
    so_par(o);
    if (!any) {
        so_note(o, "Method list needs the DSDT/SSDT copies from the srs-root cache (acpi-tables/).");
        needs_root(o);
    } else {
        so_f(o, C_TEAL, "%d methods (names relative to their enclosing Scope; one table: acpi-methods:<SIG>)", total);
        so_par(o);
    }
    so_board_footer(o);
}

/* ======================================================================
 * BIOS / ACPI table decoders: [bios-apic] [bios-facp] [bios-dmar] + absent tables
 * ====================================================================== */
static int table_or_root(srs_out *o, const char *sig, unsigned char **d)
{
    static const int tabs[] = { 200 };
    int n;
    so_tabs(o, tabs, 1);
    if (!acpi_present(sig)) { *d = NULL; return -2; }
    n = acpi_load(sig, d);
    if (n < 36) {
        char p[256];
        snprintf(p, sizeof p, ACPI_TAB "/%s", sig);
        so_kvf(o, sig, C_VALUE, "present, %lld bytes", fsize(p));
        needs_root(o);
        free(*d);
        *d = NULL;
        return -1;
    }
    {
        acpi_hdr h;
        acpi_hdr_parse(*d, n, &h);
        acpi_hdr_rows(o, &h);
        if (h.len < (uint32_t)n) n = (int)h.len;
    }
    so_par(o);
    return n;
}

static void s_bios_apic(srs_out *o)
{
    static const int tabs[] = { 200, 290, 380, 470 };
    unsigned char *d;
    int n, i, lap = 0, io = 0, iso = 0, nmi = 0, x2 = 0;
    so_siv_header(o, "APIC (MADT)");
    n = table_or_root(o, "APIC", &d);
    if (n == -2) so_note(o, "No MADT (APIC) table");
    if (n < 44) { free(d); so_board_footer(o); return; }
    so_kvf(o, "Local APIC Address", C_PURPLE, "0x%08X", le32(d + 36));
    so_kvf(o, "Flags", C_VALUE, "0x%08X%s", le32(d + 40), le32(d + 40) & 1 ? " PCAT_COMPAT (dual 8259)" : "");
    so_par(o);
    so_tabs(o, tabs, 4);
    so_row(o, 5, (const char *const[]){ "Entry", "ID", "UID/Addr", "GSI/Flags", "Detail" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    for (i = 44; i + 2 <= n; ) {
        int ty = d[i], l = d[i + 1];
        char a[32] = "", b[32] = "", c[32] = "", e[64] = "";
        const char *nm = "Other";
        if (l < 2 || i + l > n) break;
        switch (ty) {
        case 0: if (l >= 8) { nm = "Local APIC"; lap++; snprintf(a, 32, "%u", d[i + 3]); snprintf(b, 32, "%u", d[i + 2]);
                    snprintf(c, 32, "%08X", le32(d + i + 4)); snprintf(e, 64, "%s", le32(d + i + 4) & 1 ? "Enabled" : le32(d + i + 4) & 2 ? "Online capable" : "Disabled"); } break;
        case 1: if (l >= 12) { nm = "I/O APIC"; io++; snprintf(a, 32, "%u", d[i + 2]); snprintf(b, 32, "%08X", le32(d + i + 4));
                    snprintf(c, 32, "GSI %u", le32(d + i + 8)); } break;
        case 2: if (l >= 10) { nm = "Int Override"; iso++; snprintf(a, 32, "IRQ %u", d[i + 3]); snprintf(b, 32, "bus %u", d[i + 2]);
                    snprintf(c, 32, "GSI %u", le32(d + i + 4)); snprintf(e, 64, "flags %04X", le16(d + i + 8)); } break;
        case 3: nm = "NMI Source"; break;
        case 4: if (l >= 6) { nm = "Local APIC NMI"; nmi++; snprintf(a, 32, "%s", d[i + 2] == 0xFF ? "all" : ""); if (d[i + 2] != 0xFF) snprintf(a, 32, "%u", d[i + 2]);
                    snprintf(c, 32, "flags %04X", le16(d + i + 3)); snprintf(e, 64, "LINT%u", d[i + 5]); } break;
        case 5: nm = "LAPIC Addr Override"; if (l >= 12) snprintf(b, 32, "%016llX", (unsigned long long)le64(d + i + 4)); break;
        case 9: if (l >= 16) { nm = "Local x2APIC"; x2++; snprintf(a, 32, "%u", le32(d + i + 4)); snprintf(b, 32, "%u", le32(d + i + 12));
                    snprintf(c, 32, "%08X", le32(d + i + 8)); snprintf(e, 64, "%s", le32(d + i + 8) & 1 ? "Enabled" : "Disabled"); } break;
        case 10: nm = "x2APIC NMI"; break;
        default: snprintf(e, 64, "type %d len %d", ty, l); break;
        }
        so_row(o, 5, (const char *const[]){ nm, a, b, c, e }, (const int[]){ C_VALUE, C_BLACK, C_PURPLE, C_GRAY, C_TEAL });
        i += l;
    }
    so_par(o);
    so_f(o, C_TEAL, "Local APIC %d  x2APIC %d  I/O APIC %d  Overrides %d  LAPIC NMI %d", lap, x2, io, iso, nmi);
    so_par(o);
    free(d);
    so_board_footer(o);
}

static void s_bios_facp(srs_out *o)
{
    static const char *const prof[] = { "Unspecified", "Desktop", "Mobile", "Workstation", "Enterprise Server", "SOHO Server",
                                        "Appliance PC", "Performance Server", "Tablet" };
    static const char *const fl[] = { "WBINVD", "WBINVD_FLUSH", "PROC_C1", "P_LVL2_UP", "PWR_BUTTON", "SLP_BUTTON", "FIX_RTC",
                                      "RTC_S4", "TMR_VAL_EXT", "DCK_CAP", "RESET_REG_SUP", "SEALED_CASE", "HEADLESS", "CPU_SW_SLP",
                                      "PCI_EXP_WAK", "USE_PLATFORM_CLOCK", "S4_RTC_STS_VALID", "REMOTE_POWER_ON_CAPABLE",
                                      "FORCE_APIC_CLUSTER_MODEL", "FORCE_APIC_PHYSICAL_DESTINATION_MODE", "HW_REDUCED_ACPI",
                                      "LOW_POWER_S0_IDLE_CAPABLE", "PERSISTENT_CPU_CACHES(0)", "PERSISTENT_CPU_CACHES(1)" };
    static const char *const ba[] = { "LEGACY_DEVICES", "8042", "VGA Not Present", "MSI Not Supported", "PCIe ASPM Controls", "CMOS RTC Not Present" };
    unsigned char *d;
    int n, i;
    long pm = -1;
    so_siv_header(o, "FACP (FADT)");
    n = table_or_root(o, "FACP", &d);
    if (n < 0) {
        rd_long("/sys/firmware/acpi/pm_profile", &pm);
        so_kvf(o, "Preferred PM Profile", C_VALUE, "%ld (%s)  [from /sys/firmware/acpi/pm_profile]", pm, pm >= 0 && pm < 9 ? prof[pm] : "?");
        if (n == -2) so_note(o, "No FACP table");
        free(d);
        so_board_footer(o);
        return;
    }
#define F8(off) (n > (off) ? d[off] : 0)
#define F16(off) (n >= (off) + 2 ? le16(d + (off)) : 0)
#define F32(off) (n >= (off) + 4 ? le32(d + (off)) : 0)
    so_kvf(o, "FIRMWARE_CTRL (FACS)", C_PURPLE, "0x%08X", F32(36));
    so_kvf(o, "DSDT", C_PURPLE, "0x%08X", F32(40));
    if (n >= 148) so_kvf(o, "X_DSDT", C_PURPLE, "0x%016llX", (unsigned long long)le64(d + 140));
    so_kvf(o, "Preferred PM Profile", C_BLACK, "%u (%s)", F8(45), F8(45) < 9 ? prof[F8(45)] : "Reserved");
    so_kvf(o, "SCI Interrupt", C_VALUE, "%u", F16(46));
    so_kvf(o, "SMI Command Port", C_VALUE, "0x%X  (enable 0x%02X, disable 0x%02X)", F32(48), F8(52), F8(53));
    so_kvf(o, "PM1a Event / Control", C_VALUE, "0x%X / 0x%X", F32(56), F32(64));
    so_kvf(o, "PM Timer Block", C_VALUE, "0x%X (len %u, %s)", F32(76), F8(91), F32(112) & 0x100 ? "32-bit" : "24-bit");
    so_kvf(o, "GPE0 Block", C_VALUE, "0x%X (len %u)", F32(80), F8(92));
    so_kvf(o, "GPE1 Block", C_VALUE, "0x%X (len %u, base %u)", F32(84), F8(93), F8(94));
    so_kvf(o, "C2 / C3 Latency", C_VALUE, "%u / %u us", F16(96), F16(98));
    so_kvf(o, "RTC Day/Month Alarm, Century", C_VALUE, "0x%02X / 0x%02X, 0x%02X", F8(106), F8(107), F8(108));
    so_kvf(o, "FADT Minor Version", C_VALUE, "%u", F8(131));
    if (n >= 129) so_kvf(o, "Reset Register", C_VALUE, "space %u addr 0x%llX value 0x%02X", F8(116), (unsigned long long)(n >= 128 ? le64(d + 120) : 0), F8(128));
    if (n >= 276) so_kvf(o, "Hypervisor Vendor", C_VALUE, "0x%016llX", (unsigned long long)le64(d + 268));
    so_par(o);
    so_b(o, C_VALUE, "Flags");
    so_f(o, C_GRAY, "  0x%08X", F32(112));
    so_par(o);
    for (i = 0; i < 24; i++) { so_check(o, (F32(112) >> i) & 1); so_f(o, C_VALUE, " %s", fl[i]); so_par(o); }
    so_par(o);
    so_b(o, C_VALUE, "IA-PC Boot Architecture");
    so_f(o, C_GRAY, "  0x%04X", F16(109));
    so_par(o);
    for (i = 0; i < 6; i++) { so_check(o, (F16(109) >> i) & 1); so_f(o, C_VALUE, " %s", ba[i]); so_par(o); }
#undef F8
#undef F16
#undef F32
    free(d);
    so_board_footer(o);
}

static void absent_table_body(srs_out *o, const char *sig, const char *title, const char *what)
{
    unsigned char *d;
    int n;
    so_siv_header(o, title);
    n = table_or_root(o, sig, &d);
    if (n == -2) {
        so_kvf(o, sig, C_ORANGE, "not present in /sys/firmware/acpi/tables");
        so_kv(o, "Purpose", C_GRAY, what);
        present_tables_line(o);
    } else if (n > 0) hexdump(o, d, n, 1024);
    free(d);
}
static void absent_table(srs_out *o, const char *sig, const char *title, const char *what)
{
    absent_table_body(o, sig, title, what);
    so_board_footer(o);
}
static void s_bios_aspt(srs_out *o) { absent_table(o, "ASPT", "ASPT Table", "AMD Secure Processor Table (PSP mailbox location)"); }
static void s_bios_msdm(srs_out *o) { absent_table(o, "MSDM", "MSDM Table", "Microsoft Data Management (embedded Windows product key)"); }
static void s_bios_oemb(srs_out *o) { absent_table(o, "OEMB", "OEMB Table", "AMI OEM-specific table"); }
static void s_bios_sdev(srs_out *o) { absent_table(o, "SDEV", "SDEV Table", "Secure Devices table"); }
static void s_bios_slic(srs_out *o) { absent_table(o, "SLIC", "SLIC Table", "Software Licensing (OEM Windows activation)"); }
static void s_bios_slit(srs_out *o) { absent_table(o, "SLIT", "SLIT Table", "System Locality Information (NUMA distances)"); }
static void s_bios_srat(srs_out *o) { absent_table(o, "SRAT", "SRAT Table", "System Resource Affinity (NUMA memory/CPU affinity)"); }

static void s_bios_dmar(srs_out *o)
{
    static const int tabs[] = { 80, 160, 220, 300 };
    unsigned char *d;
    int n, i;
    absent_table_body(o, "DMAR", "DMAR Table", "Intel VT-d DMA remapping; AMD platforms use IVRS instead");
    if (!acpi_present("IVRS")) { so_board_footer(o); return; }
    so_par(o);
    so_b(o, C_VALUE, "IVRS (AMD I/O Virtualization Reporting Structure)");
    so_par(o);
    n = table_or_root(o, "IVRS", &d);
    if (n >= 48) {
        uint32_t iv = le32(d + 36);
        so_kvf(o, "IVinfo", C_VALUE, "0x%08X  VA %u bits, PA %u bits, GVA %u bits%s", iv, (iv >> 15) & 0x7F, (iv >> 8) & 0x7F,
               (iv >> 5) & 7, iv & 1 ? ", EFR supported" : "");
        so_tabs(o, tabs, 4);
        so_row(o, 5, (const char *const[]){ "Type", "Flags", "DevID", "Segment", "Base" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
        for (i = 48; i + 4 <= n; ) {
            int ty = d[i], l = le16(d + i + 2);
            char t[16], f[16], dv[16], sg[16], b[32];
            if (l < 4 || i + l > n) break;
            snprintf(t, sizeof t, "IVHD %02Xh", ty);
            if (ty >= 0x20) snprintf(t, sizeof t, "IVMD %02Xh", ty);
            snprintf(f, sizeof f, "%02X", d[i + 1]);
            snprintf(dv, sizeof dv, "%04X", l >= 6 ? le16(d + i + 4) : 0);
            snprintf(sg, sizeof sg, "%u", l >= 18 ? le16(d + i + 16) : 0);
            snprintf(b, sizeof b, "%016llX", l >= 16 ? (unsigned long long)le64(d + i + 8) : 0ULL);
            so_row(o, 5, (const char *const[]){ t, f, dv, sg, b }, (const int[]){ C_VALUE, C_GRAY, C_PURPLE, C_GRAY, C_PURPLE });
            i += l;
        }
    }
    free(d);
    so_board_footer(o);
}

/* legacy / non-x86-64 structures */
static void legacy_body(srs_out *o, const char *title, const char *what, const char *why)
{
    static const int tabs[] = { 200 };
    so_siv_header(o, title);
    so_tabs(o, tabs, 1);
    so_kv(o, "Status", C_ORANGE, "Not present on this system");
    so_kv(o, "Structure", C_VALUE, what);
    so_kv(o, "Reason", C_GRAY, why);
    so_kv(o, "Firmware", C_VALUE, path_exists(EFI_DIR) ? "UEFI x86-64 (ACPI MADT/_PRT replace the legacy tables)" : "Legacy BIOS");
}
static void legacy(srs_out *o, const char *title, const char *what, const char *why) { legacy_body(o, title, what, why); so_board_footer(o); }
static void s_bios_mps(srs_out *o) { legacy(o, "MPS Table", "Intel MultiProcessor Specification (_MP_ floating pointer)", "superseded by the ACPI MADT; scanning needs /dev/mem, srs never opens it"); }
static void s_bios_irq(srs_out *o)
{
    char *txt;
    legacy_body(o, "PCI IRQ Routing", "$PIR PCI IRQ routing table", "UEFI firmware routes PCI interrupts through ACPI _PRT; scanning needs /dev/mem");
    if (rd_all("/proc/interrupts", &txt) >= 0) {
        int ioapic = 0, msi = 0;
        char *p = txt;
        while ((p = strstr(p, "IO-APIC"))) { ioapic++; p += 7; }
        p = txt;
        while ((p = strstr(p, "PCI-MSI"))) { msi++; p += 7; }
        so_kvf(o, "Linux routing", C_TEAL, "%d IO-APIC lines, %d PCI-MSI/MSI-X vectors (/proc/interrupts)", ioapic, msi);
        free(txt);
    }
    so_board_footer(o);
}
static void s_bios_sfi(srs_out *o) { legacy(o, "SFI", "Simple Firmware Interface", "Intel MID (Moorestown/Medfield) only; removed from Linux 5.12"); }
static void s_fw_table(srs_out *o) { legacy(o, "Firmware Interface Table", "IA-64 (Itanium) firmware interface table", "IA-64 only"); }

/* [bios-option] */
static void s_bios_option(srs_out *o)
{
    static const int tabs[] = { 110, 170, 270, 350 };
    char **e;
    int ne = dir_list("/sys/bus/pci/devices", &e), i, n = 0;
    so_siv_header(o, "BIOS Option ROMs");
    so_tabs(o, tabs, 4);
    so_row(o, 5, (const char *const[]){ "PCI", "Class", "ID", "ROM size", "Driver" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (i = 0; i < ne; i++) {
        char p[256], cls[16] = "", ven[16] = "", dev[16] = "", id[24], sz[32], dr[64] = "-";
        long long s;
        snprintf(p, sizeof p, "/sys/bus/pci/devices/%s/rom", e[i]);
        if ((s = fsize(p)) < 0) continue;
        rd_fmt(cls, sizeof cls, "/sys/bus/pci/devices/%s/class", e[i]);
        rd_fmt(ven, sizeof ven, "/sys/bus/pci/devices/%s/vendor", e[i]);
        rd_fmt(dev, sizeof dev, "/sys/bus/pci/devices/%s/device", e[i]);
        snprintf(p, sizeof p, "/sys/bus/pci/devices/%s/driver", e[i]);
        link_base(p, dr, sizeof dr);
        snprintf(id, sizeof id, "%s:%s", ven + 2 * (ven[1] == 'x'), dev + 2 * (dev[1] == 'x'));
        fmt_bytes((double)s, sz, sizeof sz);
        so_row(o, 5, (const char *const[]){ e[i] + 5, cls + 2 * (cls[1] == 'x'), id, sz, *dr ? dr : "-" },
               (const int[]){ C_LABEL, C_GRAY, C_VALUE, C_BLACK, C_TEAL });
        n++;
    }
    dir_free(e, ne);
    so_par(o);
    so_f(o, C_TEAL, "%d PCI devices expose an expansion ROM BAR", n);
    so_par(o);
    so_note(o, "The ROM images themselves are not read (the ROM must be switched on by a write first). For an AMD GPU, "
               "GPU VBIOS status (gpu-bios) shows the driver's own copy of its image.");
    so_board_footer(o);
}

/* ======================================================================
 * probes for absent vendor items: [acpi-ipf] [acpi-itm] [acpi-test] [acpi-tfp] [acpi-tms] [acpi-tos]
 *                                 [dell-bios] [heci-status]
 * ====================================================================== */
static void probe(srs_out *o, const char *title, const char *const *hids, const char *what)
{
    static const int tabs[] = { 200 };
    char ids[256] = "";
    int i, hits;
    so_siv_header(o, title);
    so_tabs(o, tabs, 1);
    for (i = 0; hids[i]; i++) { strncat(ids, hids[i], sizeof ids - strlen(ids) - 2); strcat(ids, " "); }
    hits = acpi_probe(o, hids);
    so_kv(o, "Device", C_VALUE, what);
    so_kvf(o, "Probed HIDs", C_PURPLE, "%s", ids);
    so_kv(o, "Status", hits ? C_GREEN : C_ORANGE, hits ? "present" : "Not present on this system");
    so_board_footer(o);
}
static void s_acpi_ipf(srs_out *o) { static const char *const h[] = { "INTC1040", "INTC1041", "INTC1042", "INTC1043", NULL }; probe(o, "ACPI IPF", h, "Intel Platform Framework (DPTF successor)"); }
static void s_acpi_itm(srs_out *o) { static const char *const h[] = { "ICD0001", NULL }; probe(o, "ACPI ICD0001", h, "ICD0001 thermal/management device"); }
static void s_acpi_test(srs_out *o) { static const char *const h[] = { "TOS", NULL }; probe(o, "ACPI Toshiba Test", h, "Toshiba laptop ACPI devices"); }
static void s_acpi_tfp(srs_out *o) { static const char *const h[] = { "INT3403", "INTC1043", NULL }; probe(o, "ACPI TFP", h, "Intel DPTF thermal participant"); }
static void s_acpi_tms(srs_out *o) { static const char *const h[] = { "INT340E", NULL }; probe(o, "ACPI TMS", h, "Intel DPTF INT340E"); }
static void s_acpi_tos(srs_out *o) { static const char *const h[] = { "TOS1900", "TOS6200", "TOS6207", "TOS6208", NULL }; probe(o, "ACPI TOS1900", h, "Toshiba laptop extras"); }

static void s_dell_bios(srs_out *o)
{
    static const int tabs[] = { 200 };
    char v[128];
    so_siv_header(o, "Dell BIOS");
    so_tabs(o, tabs, 1);
    dmi_rd("sys_vendor", v, sizeof v);
    so_kv(o, "System Vendor", C_VALUE, v);
    if (strstr(v, "Dell")) {
        so_kv(o, "dell-smbios", C_VALUE, path_exists("/sys/bus/platform/devices/dell-smbios.0") ? "loaded" : "not loaded");
        so_kv(o, "dell_smm_hwmon", C_VALUE, path_exists("/sys/module/dell_smm_hwmon") ? "loaded" : "not loaded");
    } else so_kv(o, "Status", C_ORANGE, "Not a Dell system: Dell BIOS SMI data does not apply");
    so_board_footer(o);
}

static void s_heci(srs_out *o)
{
    static const int tabs[] = { 200 };
    char **e;
    int ne, i, psp = 0;
    so_siv_header(o, "HECI Status");
    so_tabs(o, tabs, 1);
    so_kv(o, "Intel MEI / HECI", path_exists("/sys/class/mei") ? C_GREEN : C_ORANGE, path_exists("/sys/class/mei") ? "present (/sys/class/mei)" : "Not present on this system");
    ne = dir_list("/sys/bus/pci/drivers/ccp", &e);
    for (i = 0; i < ne; i++)
        if (strchr(e[i], ':') && strchr(e[i], '.')) { so_kvf(o, "AMD PSP / CCP", C_TEAL, "%s (ccp driver; AMD's security co-processor, ME analogue)", e[i]); psp++; }
    dir_free(e, ne);
    if (!psp) so_kv(o, "AMD PSP / CCP", C_GRAY, "no ccp-bound device");
    so_board_footer(o);
}

/* [mem-tweak] */
/* ======================================================================
 * Memory Timings (2.4): live DDR5 timings from the AMD memory controller (UMC registers, read as root by srs-root
 * through the ryzen_smu driver into umc.txt), clocks and voltages from ryzen_smu's PM table (readable by everyone),
 * and every module's SPD (spd5118 driver, readable by everyone): JEDEC, EXPO and XMP 3.0 profiles. Read-only.
 * Register map from ZenStates-Core / ZenTimings (Ivan Rusanov) and memtest86+ (Sam Demeulemeester): see mem_timings.c.
 * ====================================================================== */
typedef struct { int ok; float fclk, uclk, mclk, vsoc, vddp, vddg_iod, vddg_ccd, misc; unsigned ver; int checked; } mt_pmt;
static void mt_pmt_read(mt_pmt *p)
{
    /* PM table layouts: version, FCLK, UCLK, MCLK, VDDCR_SOC, CLDO_VDDP, VDDG_IOD, VDDG_CCD, MISC byte offsets
       (ZenStates-Core PowerTable.cs); checked = seen right on a real PC here */
    static const struct { unsigned v, mask; int f, u, m, soc, vddp, iod, ccd, misc, checked; } L[] = {
        { 0x540100, 0xFFFFF0, 0x118, 0x128, 0x138, 0xD0, 0x430, -1, -1, 0xB8, 1 },    /* Ryzen 7000 (Raphael); 0x540104 checked */
        { 0x621100, 0xFFFF00, 0x11C, 0x12C, 0x13C, 0xD8, 0x434, 0x40C, 0x414, 0xC0, 0 },   /* Ryzen 9000 (Granite Ridge), 1 CCD */
        { 0x621200, 0xFFFF00, 0x11C, 0x12C, 0x13C, 0xD8, 0x434, 0x40C, 0x414, 0xC0, 0 },   /* 2 CCDs */
    };
    const char *dir = getenv("SRS_RSMU_DIR");
    char path[600];
    unsigned char *t = NULL;
    int n, i;
    unsigned ver = 0;
    FILE *f;
    memset(p, 0, sizeof *p);
    if (!dir || !*dir) dir = "/sys/kernel/ryzen_smu_drv";
    snprintf(path, sizeof path, "%s/pm_table_version", dir);
    if (!(f = fopen(path, "rb"))) return;
    if (fread(&ver, 1, 4, f) != 4) ver = 0;
    fclose(f);
    p->ver = ver;
    snprintf(path, sizeof path, "%s/pm_table", dir);
    if ((n = rd_all(path, (char **)&t)) <= 0) { free(t); return; }
    for (i = 0; i < (int)(sizeof L / sizeof L[0]); i++)
        if ((ver & L[i].mask) == L[i].v && n >= L[i].vddp + 4) {
#define PF(dst, off) do { if ((off) >= 0 && (off) + 4 <= n) memcpy(&(dst), t + (off), 4); } while (0)
            PF(p->fclk, L[i].f); PF(p->uclk, L[i].u); PF(p->mclk, L[i].m); PF(p->vsoc, L[i].soc); PF(p->vddp, L[i].vddp);
            PF(p->vddg_iod, L[i].iod); PF(p->vddg_ccd, L[i].ccd); PF(p->misc, L[i].misc);
#undef PF
            p->ok = p->mclk > 200 && p->mclk < 6000;
            p->checked = L[i].checked;
            break;
        }
    free(t);
}

/* one module's SPD: the spd5118 client at SMBus 0x50 + slot index (the hwmon pairing), cross-checked by part number */
static int mt_spd_load(const char *dev, mt_spd *s)
{
    char path[300];
    unsigned char *d = NULL;
    int n;
    snprintf(path, sizeof path, "%s/%s/eeprom", getenv("SRS_SPD_DIR") ? getenv("SRS_SPD_DIR") : "/sys/bus/i2c/drivers/spd5118", dev);
    n = rd_all(path, (char **)&d);
    mt_spd_decode(d, n > 0 ? (size_t)n : 0, s);
    free(d);
    return s->ok;
}

static void mt_ns(char *b, size_t n, int clk, int mts)
{
    double ns = clk >= 0 && mts > 0 ? clk * 2000.0 / mts : -1;
    if (ns < 0) snprintf(b, n, "-");
    else if (ns >= 1000) snprintf(b, n, "%.2f us", ns / 1000);
    else snprintf(b, n, "%.2f ns", ns);
}

static void mt_maker(char *b, size_t n, unsigned code)
{
    const char *m = mt_jep106(code);
    if (m) snprintf(b, n, "%s", m);
    else if (code) snprintf(b, n, "JEP106 bank %u, 0x%02X", (code >> 8) + 1, code & 0x7F);
    else snprintf(b, n, "-");
}

static void mt_prof_row(srs_out *o, const char *name, const mt_prof *p)
{
    char c[6][96];
    const char *cells[6] = { c[0], c[1], c[2], c[3], c[4], c[5] };
    static const int col[6] = { C_LABEL, C_VALUE, C_VALUE, C_VALUE, C_VALUE, C_VALUE };
    if (!p->valid) return;
    snprintf(c[0], sizeof c[0], "%s", name);
    snprintf(c[1], sizeof c[1], "%d MT/s", p->mts);
    snprintf(c[2], sizeof c[2], "%d-%d-%d-%d-%d", p->cl, p->rcd, p->rp, p->ras, p->rc);
    snprintf(c[3], sizeof c[3], "%d / %d / %d ns", p->rfc1ns, p->rfc2ns, p->rfcsbns);
    if (p->vdd) snprintf(c[4], sizeof c[4], "%.3f / %.3f / %.3f V", p->vdd / 1000.0, p->vddq / 1000.0, p->vpp / 1000.0);
    else snprintf(c[4], sizeof c[4], "-");
    snprintf(c[5], sizeof c[5], "%s", p->name);
    so_row(o, 6, cells, col);
}

static void s_mem_tweak(srs_out *o)
{
    static const int tabs[] = { 220 };
    static const int ttabs[] = { 110, 190, 270, 380 };
    static const int ptabs[] = { 120, 230, 380, 550, 740 };
    static mt_umc ch[MT_NCH];
    const char *cache = so_root_file("umc.txt");
    char *txt = NULL, b[200], vend[64] = "";
    mt_pmt pm;
    slot_t sl[16];
    int src, nsl = slots_get(sl, 16, &src), nch = 0, i, k, mts = 0, nd, amd;
    const mt_def *defs = mt_defs(&nd);
    so_siv_header(o, "Memory Timings");
    so_tabs(o, tabs, 1);
    {
        FILE *f = fopen("/proc/cpuinfo", "r");
        char l[256];
        while (f && fgets(l, sizeof l, f)) if (!strncmp(l, "vendor_id", 9)) { sscanf(strchr(l, ':') + 1, "%63s", vend); break; }
        if (f) fclose(f);
    }
    amd = !strcmp(vend, "AuthenticAMD");
    if (cache && rd_all(cache, &txt) > 0) nch = mt_umc_parse(txt, ch);
    for (i = 0; i < MT_NCH && !mts; i++) if (ch[i].present) mts = mt_umc_mts(&ch[i]);
    mt_pmt_read(&pm);

    so_head(o, "Clocks and voltages");
    if (mts) so_kvf(o, "Memory speed", C_GREEN, "%d MT/s (MEMCLK %d MHz)", mts, mts / 2);
    else {
        for (i = 0; i < nsl; i++) if (sl[i].present && *sl[i].cspeed) { so_kvf(o, "Memory speed", C_VALUE, "%s (SMBIOS)", sl[i].cspeed); break; }
    }
    if (pm.ok) {
        so_kvf(o, "MEMCLK", C_VALUE, "%.0f MHz", pm.mclk);
        so_kvf(o, "UCLK", C_VALUE, "%.0f MHz (UCLK:MEMCLK %s)", pm.uclk, pm.uclk > pm.mclk * 0.75 ? "1:1" : "1:2");
        so_kvf(o, "FCLK", C_VALUE, "%.0f MHz", pm.fclk);
        so_kvf(o, "VSOC", C_VALUE, "%.4f V", pm.vsoc);
        so_kvf(o, "VDDP", C_VALUE, "%.4f V", pm.vddp);
        if (pm.vddg_iod > 0.3) so_kvf(o, "VDDG IOD / CCD", C_VALUE, "%.4f / %.4f V", pm.vddg_iod, pm.vddg_ccd);
        else so_kv(o, "VDDG IOD / CCD", C_GRAY, "not in this PM table version");
        if (pm.misc > 0.3) so_kvf(o, "VDD MISC", C_VALUE, "%.4f V", pm.misc);
        if (!pm.checked) so_kvf(o, "PM table", C_ORANGE, "0x%06X: layout from ZenStates-Core, not checked on this CPU yet - please report wrong values", pm.ver);
    } else if (amd) {
        if (pm.ver) so_kvf(o, "Clocks / SoC voltages", C_GRAY, "not shown: PM table version 0x%06X is not known to SRS yet", pm.ver);
        else so_kv(o, "Clocks / SoC voltages", C_GRAY, "not shown: the ryzen_smu driver is not loaded (it gives the PM table)");
    }

    if (nch) {
        int first = -1, gdm, t2, pd, fgr, pb;
        for (i = 0; i < MT_NCH; i++) if (ch[i].present) { first = i; break; }
        gdm = mt_field(&ch[first], 0x50200, 18, 18);
        t2 = mt_field(&ch[first], 0x50200, 17, 17);
        pd = mt_field(&ch[first], 0x5012C, 28, 28);
        fgr = mt_field(&ch[first], 0x5012C, 18, 16);
        pb = mt_field(&ch[first], 0x5012C, 1, 1);
        so_par(o);
        so_head(o, "Memory controller");
        {
            char cl[16] = "";
            for (i = 0; i < MT_NCH; i++) if (ch[i].present) { size_t l = strlen(cl); snprintf(cl + l, sizeof cl - l, "%s%c", l ? ", " : "", 'A' + i); }
            so_kvf(o, "Channels with a DIMM", C_VALUE, "%d (%s)", nch, cl);
        }
        so_kv(o, "Gear Down Mode (GDM)", C_VALUE, gdm < 0 ? "-" : gdm ? "Enabled" : "Disabled");
        so_kv(o, "Command rate", C_VALUE, t2 < 0 ? "-" : t2 ? "2T" : "1T");
        so_kv(o, "Power down", C_VALUE, pd < 0 ? "-" : pd ? "Enabled" : "Disabled");
        so_kv(o, "Bank groups", C_VALUE, mt_field(&ch[first], 0x50200, 16, 16) == 1 ? "Enabled" : "Disabled");
        so_kv(o, "Refresh mode", C_VALUE, fgr < 0 ? "-" : pb == 1 ? (fgr ? "Mixed (fine granularity + per bank)" : "Per bank") :
                                         fgr ? "Fine granularity (FGR)" : "Normal");
        {
            unsigned nv;
            if (mt_reg(&ch[first], 0x50284, &nv))
                so_kvf(o, "Nitro mode (Rx / Tx / Ctrl)", C_VALUE, "%u / %u / %u", (nv >> 8) & 3, (nv >> 4) & 3, nv & 3);
        }
        {   /* the BIOS's own memory settings (AOD table), when srs-root could read it */
            const char *af = so_root_file("aod.bin");
            char afp[1200] = "", why[400] = "";
            unsigned char *ad = NULL;
            int an = 0;
            mt_aod a;
            if (af) { snprintf(afp, sizeof afp, "%s", af); an = rd_all(afp, (char **)&ad); }
            {
                const char *tf = so_root_file("aod.txt");
                if (tf) rd_str(tf, why, sizeof why);
            }
            so_par(o);
            so_head(o, "BIOS memory settings (AMD AOD table)");
            if (an > 0 && mt_aod_decode(ad, (size_t)an, mts / 2, mt_field(&ch[first], 0x50204, 5, 0), mt_field(&ch[first], 0x50204, 21, 16), &a)) {
                char t1[48], t2[48];
                mt_aod_text(0, a.procodt, t1, sizeof t1); so_kv(o, "ProcODT", C_VALUE, t1);
                mt_aod_text(1, a.procdrv, t1, sizeof t1); so_kv(o, "Proc data drive strength", C_VALUE, t1);
                mt_aod_text(2, a.dramdrv, t1, sizeof t1); so_kv(o, "DRAM data drive strength", C_VALUE, t1);
                mt_aod_text(3, a.cadbus, t1, sizeof t1); so_kv(o, "CAD bus drive strength", C_VALUE, t1);
                mt_aod_text(4, a.rttnomwr, t1, sizeof t1); mt_aod_text(4, a.rttnomrd, t2, sizeof t2);
                so_kvf(o, "RttNomWr / RttNomRd", C_VALUE, "%s / %s", t1, t2);
                mt_aod_text(4, a.rttwr, t1, sizeof t1); so_kv(o, "RttWr", C_VALUE, t1);
                mt_aod_text(4, a.rttpark, t1, sizeof t1); mt_aod_text(4, a.rttparkdqs, t2, sizeof t2);
                so_kvf(o, "RttPark / RttParkDqs", C_VALUE, "%s / %s", t1, t2);
                if (a.vdd > 500 && a.vdd < 3000) so_kvf(o, "DIMM VDD / VDDQ / VPP (set)", C_VALUE, "%.3f / %.3f / %.3f V", a.vdd / 1000.0, a.vddq / 1000.0, a.vpp / 1000.0);
                if (a.vddio > 500 && a.vddio < 3000) so_kvf(o, "CPU VDDIO (memory)", C_VALUE, "%.3f V", a.vddio / 1000.0);
                so_kv(o, "Source", C_GRAY, "the BIOS's AOD table; its timings match the live ones above, so the layout fits this BIOS");
            } else if (an > 0) {
                so_kv(o, "Status", C_GRAY, "the AOD table was read, but its layout is not known for this processor / BIOS yet (please report it)");
            } else if (*why && !strncmp(why, "not readable", 12)) {
                so_kv(o, "Status", C_GRAY, "not readable on this kernel: the table sits in memory that the kernel keeps from programs");
                so_kv(o, "Why", C_GRAY, "Linux kernels built with IO_STRICT_DEVMEM (most) block it unless booted with iomem=relaxed, which weakens");
                so_kv(o, "", C_GRAY, "that protection. SRS does not ask you to change it; the timings above do not need it.");
            } else {
                so_kv(o, "Status", C_GRAY, "not read yet (Menu > Run as root), or this BIOS has no AOD table");
            }
            free(ad);
        }
        so_par(o);
        so_head(o, "Timings (memory controller clocks; time at the current speed)");
        so_tabs(o, ttabs, 4);
        {
            const char *h[5] = { "Timing", "Channel A", "Channel B", "Time", "What it is" };
            static const int hc[5] = { C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL };
            so_row(o, 5, h, hc);
        }
        {
            int rf[MT_NCH][3];
            for (i = 0; i < MT_NCH; i++) mt_umc_rfc(&ch[i], &rf[i][0], &rf[i][1], &rf[i][2]);
            int order[64], no = 0, j;
            for (j = 0; j < nd && no < 60; j++) {   /* tRFC, tRFC2, tRFCsb right after tREFI */
                order[no++] = j;
                if (!strcmp(defs[j].name, "tREFI")) { order[no++] = nd; order[no++] = nd + 1; order[no++] = nd + 2; }
            }
            for (j = 0; j < no; j++) {
                k = order[j];
                char c[5][120];
                const char *cells[5] = { c[0], c[1], c[2], c[3], c[4] };
                static const int col[5] = { C_LABEL, C_VALUE, C_VALUE, C_GRAY, C_GRAY };
                int va = -1, vb = -1, ns = 1, r;
                const char *name, *what;
                if (k < nd) { name = defs[k].name; what = defs[k].what; va = mt_field(&ch[0], defs[k].off, defs[k].hi, defs[k].lo);
                              vb = mt_field(&ch[1], defs[k].off, defs[k].hi, defs[k].lo); ns = defs[k].ns; }
                else {
                    static const char *const rn[3] = { "tRFC", "tRFC2", "tRFCsb" };
                    static const char *const rw[3] = { "refresh cycle (all banks)", "refresh cycle, fine granularity", "same-bank refresh cycle" };
                    r = k - nd;
                    name = rn[r]; what = rw[r]; va = rf[0][r]; vb = rf[1][r];
                }
                if (!ch[0].present) va = -1;
                if (!ch[1].present) vb = -1;
                snprintf(c[0], sizeof c[0], "%s", name);
                if (va >= 0) snprintf(c[1], sizeof c[1], "%d", va); else snprintf(c[1], sizeof c[1], "-");
                if (vb >= 0) snprintf(c[2], sizeof c[2], "%d", vb); else snprintf(c[2], sizeof c[2], "-");
                if (ns) mt_ns(c[3], sizeof c[3], va >= 0 ? va : vb, mts); else snprintf(c[3], sizeof c[3], " ");
                snprintf(c[4], sizeof c[4], "%s", what);
                so_row(o, 5, cells, col);
            }
        }
        so_tabs(o, tabs, 1);
    } else {
        so_par(o);
        so_head(o, "Timings");
        if (!amd) so_kv(o, "Status", C_GRAY, "live timings are read on AMD Ryzen 7000 / 9000 desktop processors so far (Intel: planned)");
        else if (txt && strstr(txt, "not read")) so_kv(o, "Status", C_ORANGE, "the ryzen_smu driver is not loaded: install ryzen_smu (AUR ryzen_smu-dkms-git), then Menu > Run as root");
        else if (cache) so_kv(o, "Status", C_GRAY, "this processor's memory controller is not supported yet (Ryzen 7000 / 9000 desktop so far)");
        else so_kv(o, "Status", C_ORANGE, "needs root once: Menu > Run as root (one password prompt; the registers are only read)");
        so_kv(o, "Source", C_PURPLE, "AMD memory controller (UMC) registers through the ryzen_smu driver; the SPD profiles below need no root");
    }
    free(txt);

    {   /* modules and their SPD profiles */
        int shown = 0;
        for (i = 0; i < nsl; i++) {
            mt_spd s;
            char mk[80], dm[80];
            if (!sl[i].present || !*sl[i].spd_dev || !mt_spd_load(sl[i].spd_dev, &s)) continue;
            if (!shown++) { so_par(o); so_head(o, "Modules (SPD)"); }
            so_tabs(o, tabs, 1);
            mt_maker(mk, sizeof mk, s.mfr);
            mt_maker(dm, sizeof dm, s.dram_mfr);
            snprintf(b, sizeof b, "%s %s", sl[i].bank, sl[i].loc);
            so_kv(o, "Slot", C_BLACK, b);
            if (*sl[i].part && *s.part && strcmp(sl[i].part, s.part) && strcmp(sl[i].part, "Unknown"))
                so_kvf(o, "Note", C_ORANGE, "SMBIOS names %s for this slot, the SPD at %s says %s", sl[i].part, sl[i].spd_dev, s.part);
            so_kvf(o, "Module", C_VALUE, "%s %s", mk, *s.part ? s.part : "(no part number)");
            so_kvf(o, "Size", C_VALUE, "%.0f GB, %d rank%s, %d Gb x%d chips%s%s", s.size_gb, s.ranks, s.ranks > 1 ? "s" : "", s.density_gb, s.width,
                   s.dies > 1 ? ", stacked " : "", s.dies > 1 ? "(3DS)" : "");
            so_kv(o, "DRAM chips", C_VALUE, dm);
            if (s.year) so_kvf(o, "Made", C_VALUE, "%d, week %d", s.year, s.week);
            {   /* the module's own temperature sensor (spd5118 hub) */
                spdmon m[8];
                int nm = spd_monitors(m, 8), q;
                for (q = 0; q < nm; q++)
                    if (!strcmp(m[q].dev, sl[i].spd_dev)) {
                        char tv[32];
                        milli(m[q].hw, "temp1_input", tv, sizeof tv);
                        if (strcmp(tv, "-")) so_kv(o, "Temperature", C_VALUE, tv);
                    }
            }
            {   /* opt-in PMIC registers (srs-root's pmic-00HH.txt, an i2cdump of 0x00-0x3F): the set voltages, JESD301-2 VIDs
                   R0x21 SWA = VDD, R0x25 SWB = VDDQ (800 + n x 5 mV), R0x27 SWC = VPP (1500 + n x 5 mV), n = bits 7:1. Overclocking
                   PMICs in high-voltage mode use the whole byte (800 + v x 5 mV); the reading closer to the module's fastest
                   profile voltage is taken. Never written: the live meter (R0x30) would need a write. */
                char pf[64], *pt = NULL;
                const char *pp;
                snprintf(pf, sizeof pf, "pmic-%s.txt", strchr(sl[i].spd_dev, '-') ? strchr(sl[i].spd_dev, '-') + 1 : sl[i].spd_dev);
                if ((pp = so_root_file(pf)) && rd_all(pp, &pt) > 0) {
                    int r[64], got = 0, row, k2, want = 0;
                    char *ln = pt;
                    memset(r, -1, sizeof r);
                    while (ln && *ln) {   /* "20: 00 9c 00 ..." */
                        if (sscanf(ln, "%x:", (unsigned *)&row) == 1 && row >= 0 && row < 64 && !(row & 15) && strchr(ln, ':')) {
                            char *q = strchr(ln, ':') + 1;
                            for (k2 = 0; k2 < 16; k2++) { unsigned v; int used; if (sscanf(q, " %2x%n", &v, &used) != 1) break; r[row + k2] = (int)v; q += used; got++; }
                        }
                        ln = strchr(ln, '\n'); if (ln) ln++;
                    }
                    for (k = 0; k < 2; k++) if (s.expo[k].valid && s.expo[k].vdd > want) want = s.expo[k].vdd;
                    for (k = 0; k < 3; k++) if (s.xmp[k].valid && s.xmp[k].vdd > want) want = s.xmp[k].vdd;
                    if (!want) want = 1100;
                    if (got >= 48 && r[0x21] >= 0 && r[0x25] >= 0 && r[0x27] >= 0) {
                        int v7 = 800 + (r[0x21] >> 1) * 5, v8 = 800 + r[0x21] * 5, hv = abs(v8 - want) < abs(v7 - want);
                        int vdd = hv ? v8 : v7, vddq = hv ? 800 + r[0x25] * 5 : 800 + (r[0x25] >> 1) * 5;
                        int vpp = hv ? 1500 + r[0x27] * 5 : 1500 + (r[0x27] >> 1) * 5;
                        so_kvf(o, "PMIC set VDD / VDDQ / VPP", C_VALUE, "%.3f / %.3f / %.3f V%s", vdd / 1000.0, vddq / 1000.0, vpp / 1000.0,
                               hv ? " (high-voltage mode)" : "");
                        if (r[0x2F] >= 0) so_kv(o, "PMIC mode", C_VALUE, r[0x2F] & 4 ? "programmable (unlocked)" : "secure (locked)");
                    } else so_kv(o, "PMIC", C_GRAY, "the read did not answer (see the cache file)");
                }
                free(pt);
            }
            so_tabs(o, ptabs, 5);
            {
                const char *h[6] = { "Profile", "Speed", "CL-RCD-RP-RAS-RC", "tRFC1 / 2 / sb", "VDD / VDDQ / VPP", "Name" };
                static const int hc[6] = { C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL };
                so_row(o, 6, h, hc);
            }
            mt_prof_row(o, "JEDEC", &s.jedec);
            for (k = 0; k < 2; k++) { char nm[24]; snprintf(nm, sizeof nm, "EXPO %d", k + 1); mt_prof_row(o, nm, &s.expo[k]); }
            for (k = 0; k < 3; k++) { char nm[24]; snprintf(nm, sizeof nm, "XMP %d", k + 1); mt_prof_row(o, nm, &s.xmp[k]); }
            so_par(o);
        }
        so_tabs(o, tabs, 1);
        if (!shown) so_kv(o, "SPD", C_GRAY, "not read: the spd5118 (DDR5) driver has no modules here, or the modules are not DDR5");
    }
    so_note(o, "Everything on this page is only read. Live timings: AMD memory controller registers (srs-root, ryzen_smu driver);");
    so_note(o, "register map from ZenStates-Core / ZenTimings by Ivan Rusanov and memtest86+ by Sam Demeulemeester. Profiles: the modules' SPD.");
    so_board_footer(o);
}

/* ======================================================================
 * table
 * ====================================================================== */
const srs_item sec_machine[] = {
    { "machine", "hardware", "Machine", B_LINUX, 0, "/sys/class/dmi/id; /run/udev/data/+dmi:id; dmidecode.txt cache", s_machine, 0, "machine" },
    { "machine-opt", "hardware", "Machine options", B_LINUX, 0, "internal; /sys/class/dmi/id", s_machine_opt, 0, "machine" },
    { "overview", "hardware", "Overview", B_LINUX, 0, "/etc/os-release; /sys/class/dmi/id; /proc/meminfo", s_overview, 1000, "machine" },
    { "hardware", "hardware", "Hardware summary", B_LINUX, 0, "/sys/class/dmi/id; /proc/cpuinfo; lspci -mm", s_hardware, 0, "machine" },
    { "dimms", "hardware", "DIMMs", B_LINUX, 0, "SMBIOS type 17 via /run/udev/data/+dmi:id or dmidecode.txt; spd5118", s_dimms, 0, "machine" },
    { "dimm-status", "hardware", "DIMM status/temperatures", B_LINUX, 0, "/sys/class/hwmon (spd5118)", s_dimm_status, 2000, "machine" },
    { "spd", "hardware", "SPD (DIMMs)", B_LINUX, 0, "spd.txt cache (decode-dimms); SMBIOS type 17", s_spd, 0, "machine" },
    { "spd-temp", "hardware", "SPD thermal", B_LINUX, 0, "/sys/class/hwmon (spd5118) temp1_*", s_spd_temp, 2000, "machine" },
    { "memory", "hardware", "Memory", B_LINUX, 0, "/proc/meminfo; /sys/devices/system/memory; edac", s_memory, 1000, "machine" },
    { "mem-tweak", "hardware", "Memory timings", B_LINUX, 1, "umc.txt cache (srs-root, ryzen_smu SMN reads); ryzen_smu pm_table; spd5118 eeprom", s_mem_tweak, 0, "machine" },
    { "physical", "hardware", "Physical memory map", B_LINUX, 0, "/sys/firmware/memmap; iomem.txt cache", s_physical, 0, "machine" },
    { "bios-make", "hardware", "BIOS maker", B_LINUX, 0, "/sys/class/dmi/id/bios_*; dmidecode.txt cache", s_bios_make, 0, "machine" },
    { "bios-tables", "hardware", "BIOS / SMBIOS tables", B_LINUX, 1, "/sys/firmware/dmi/tables (sizes); dmidecode.txt cache", s_bios_tables, 0, "machine" },
    { "system-ids", "hardware", "BIOS IDs", B_LINUX, 1, "/sys/class/dmi/id; dmidecode.txt cache", s_system_ids, 0, "machine" },
    { "firmware", "hardware", "Firmware", B_LINUX, 0, "/sys/firmware/efi; esrt; acpi/fpdt", s_firmware, 0, "machine" },
    { "uefi-info", "hardware", "UEFI information", B_LINUX, 0, "/sys/firmware/efi; efivars (SecureBoot, Boot####)", s_uefi_info, 0, "machine" },
    { "uefi-data", "hardware", "UEFI data", B_LINUX, 0, "/sys/firmware/efi/efivars", s_uefi_data, 0, "machine" },
    { "tpm-device", "hardware", "TPM device", B_LINUX, 0, "/sys/class/tpm/tpm0", s_tpm_device, 0, "machine" },
    { "acpi-tpm", "hardware", "ACPI TPM", B_LINUX, 0, "/sys/bus/acpi/devices/MSFT0101:00; acpi-tables/TPM2 cache", s_acpi_tpm, 0, "machine" },
    { "cmos-status", "hardware", "CMOS / RTC status", B_LINUX, 0, "/proc/driver/rtc; /sys/class/rtc/rtc0", s_cmos_status, 1000, "machine" },
    { "system-cmos", "hardware", "System CMOS", B_LINUX, 0, "/sys/bus/nvmem/devices/cmos_nvram0/nvmem", s_system_cmos, 0, "machine" },
    { "system-ebda", "hardware", "System EBDA", B_LINUX, 1, "ebda.txt, ebda.bin cache (/dev/mem below 1 MB, srs-root); /sys/firmware/memmap; iomem.txt cache", s_system_ebda, 0, "machine" },
    { "acpi-tables", "hardware", "ACPI tables", B_LINUX, 1, "/sys/firmware/acpi/tables; acpi-tables/ cache", s_acpi_tables, 0, "machine" },
    { "acpi-hal", "hardware", "ACPI HAL", B_LINUX, 0, "/sys/firmware/acpi; /sys/module/acpi/parameters", s_acpi_hal, 0, "machine" },
    { "acpi-int", "hardware", "ACPI interrupts and GPEs", B_LINUX, 0, "/sys/firmware/acpi/interrupts; /proc/interrupts", s_acpi_int, 2000, "machine" },
    { "acpi-dev", "hardware", "ACPI devices", B_LINUX, 0, "/sys/bus/acpi/devices", s_acpi_dev, 0, "machine" },
    { "acpi-buses", "hardware", "ACPI PCI root buses", B_LINUX, 0, "/sys/bus/acpi/devices/PNP0A08:00; /sys/class/pci_bus", s_acpi_buses, 0, "machine" },
    { "acpi-ports", "hardware", "ACPI PCI buses and ports", B_LINUX, 0, "/sys/bus/pci/devices/*/firmware_node", s_acpi_ports, 0, "machine" },
    { "acpi-pwr", "hardware", "ACPI power resource methods", B_LINUX, 0, "/sys/bus/acpi/devices/LNXPOWER:*; */power_state", s_acpi_pwr, 0, "machine" },
    { "acpi-spb", "hardware", "ACPI SPB (serial peripheral bus) methods", B_LINUX, 0, "/sys/bus/acpi/devices; /sys/bus/i2c/devices (names)", s_acpi_spb, 0, "machine" },
    { "acpi-wmi", "hardware", "ACPI WMI (PNP0C14) methods", B_LINUX, 0, "/sys/bus/wmi/devices", s_acpi_wmi, 0, "machine" },
    { "acpi-pdx", "hardware", "ACPI extension (PDX)", B_LINUX, 0, "/sys/bus/acpi/devices (vendor HIDs)", s_acpi_pdx, 0, "machine" },
    { "acpi-ec", "hardware", "ACPI embedded controller", B_LINUX, 1, "/sys/bus/acpi/devices/PNP0C09:00 (EC registers not read)", s_acpi_ec, 0, "machine" },
    { "acpi-eval", "hardware", "ACPI method evaluation", B_LINUX, 1, "Tools > ACPI method evaluation (acpi_call, as root); acpi-calls.txt log", s_acpi_eval, 0, "machine" },
    { "acpi-methods", "hardware", "ACPI methods", B_LINUX, 1, "acpi-tables/DSDT, SSDT* cache (AML MethodOp scan)", s_acpi_methods, 0, "machine" },
    { "acpi-ipf", "hardware", "ACPI IPF (Intel Platform Framework) methods", B_ABSENT, 0, "/sys/bus/acpi/devices (probe INTC1040-1043)", s_acpi_ipf, 0, "machine" },
    { "acpi-itm", "hardware", "ACPI ICD0001 methods", B_ABSENT, 0, "/sys/bus/acpi/devices (probe ICD0001)", s_acpi_itm, 0, "machine" },
    { "acpi-test", "hardware", "ACPI Toshiba test", B_ABSENT, 0, "/sys/bus/acpi/devices (probe TOS*)", s_acpi_test, 0, "machine" },
    { "acpi-tfp", "hardware", "ACPI TFP INT3403 methods", B_ABSENT, 0, "/sys/bus/acpi/devices (probe INT3403)", s_acpi_tfp, 0, "machine" },
    { "acpi-tms", "hardware", "ACPI TMS INT340E methods", B_ABSENT, 0, "/sys/bus/acpi/devices (probe INT340E)", s_acpi_tms, 0, "machine" },
    { "acpi-tos", "hardware", "ACPI TOS1900 methods", B_ABSENT, 0, "/sys/bus/acpi/devices (probe TOS1900)", s_acpi_tos, 0, "machine" },
    { "bios-apic", "hardware", "APIC (MADT) summary", B_LINUX, 1, "acpi-tables/APIC cache", s_bios_apic, 0, "machine" },
    { "bios-facp", "hardware", "FACP (FADT) summary", B_LINUX, 1, "acpi-tables/FACP cache; /sys/firmware/acpi/pm_profile", s_bios_facp, 0, "machine" },
    { "bios-aspt", "hardware", "ASPT table summary", B_ABSENT, 1, "/sys/firmware/acpi/tables (ASPT absent)", s_bios_aspt, 0, "machine" },
    { "bios-dmar", "hardware", "DMAR table summary", B_ABSENT, 1, "/sys/firmware/acpi/tables (DMAR absent; IVRS shown)", s_bios_dmar, 0, "machine" },
    { "bios-msdm", "hardware", "MSDM table summary", B_ABSENT, 1, "/sys/firmware/acpi/tables (MSDM absent)", s_bios_msdm, 0, "machine" },
    { "bios-oemb", "hardware", "OEMB table summary", B_ABSENT, 1, "/sys/firmware/acpi/tables (OEMB absent)", s_bios_oemb, 0, "machine" },
    { "bios-sdev", "hardware", "SDEV table summary", B_ABSENT, 1, "/sys/firmware/acpi/tables (SDEV absent)", s_bios_sdev, 0, "machine" },
    { "bios-slic", "hardware", "SLIC table summary", B_ABSENT, 1, "/sys/firmware/acpi/tables (SLIC absent)", s_bios_slic, 0, "machine" },
    { "bios-slit", "hardware", "SLIT table summary", B_ABSENT, 1, "/sys/firmware/acpi/tables (SLIT absent)", s_bios_slit, 0, "machine" },
    { "bios-srat", "hardware", "SRAT table summary", B_ABSENT, 1, "/sys/firmware/acpi/tables (SRAT absent)", s_bios_srat, 0, "machine" },
    { "bios-mps", "hardware", "MPS table summary", B_ABSENT, 1, "/dev/mem (legacy _MP_ scan; not present)", s_bios_mps, 0, "machine" },
    { "bios-irq", "hardware", "PCI IRQ routing ($PIR)", B_ABSENT, 1, "legacy $PIR (not on UEFI); /proc/interrupts", s_bios_irq, 0, "machine" },
    { "bios-sfi", "hardware", "SFI summary", B_ABSENT, 0, "n/a (Simple Firmware Interface: Intel MID only)", s_bios_sfi, 0, "machine" },
    { "f-w-table", "hardware", "Firmware interface table", B_ABSENT, 0, "n/a (IA-64 only)", s_fw_table, 0, "machine" },
    { "bios-option", "hardware", "BIOS option ROMs", B_LINUX, 1, "/sys/bus/pci/devices/*/rom (sizes)", s_bios_option, 0, "machine" },
    { "dell-bios", "hardware", "Dell BIOS", B_ABSENT, 0, "/sys/class/dmi/id/sys_vendor (not Dell)", s_dell_bios, 0, "machine" },
    { "heci-status", "hardware", "HECI (Intel ME) status", B_ABSENT, 0, "/sys/class/mei (absent); AMD PSP via ccp", s_heci, 0, "machine" },
    { NULL, NULL, NULL, B_LINUX, 0, NULL, NULL, 0, NULL }
};
