/* lists.c - "Update hardware lists" (2.4): fresh copies of the device-name lists SRS uses (pci.ids, usb.ids, pnp.ids),
 * downloaded only when the user asks, from the lists' own official sources, into ~/.local/share/srs/lists (no root).
 * SRS then uses whichever copy is newer, the user's or the system's (hwdata), by the version date inside the list, so a
 * later system update still wins. Every download is checked (its header, a sane number of entries) and replaces the old
 * copy only when it passes; the previous copy is kept as <name>.old.
 * Sources: The PCI ID Repository (pci-ids.ucw.cz, Martin Mares and contributors), the Linux USB ID list
 * (linux-usb.org, Stephen J. Gowdy), hwdata's pnp.ids (github.com/vcrhonek/hwdata). Each list carries its own licence
 * (pci.ids / usb.ids: GPL v2 or later, or 3-clause BSD; hwdata: GPL v2 or later). Internal to SRS. */
#include "srs.h"
#include "lists.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

const srs_list srs_lists[SRS_NLISTS] = {
    { "pci.ids", "PCI device names", "https://pci-ids.ucw.cz/v2.2/pci.ids", "List of PCI ID", 20000 },
    { "usb.ids", "USB device names", "http://www.linux-usb.org/usb.ids", "List of USB ID", 10000 },
    { "pnp.ids", "PnP / monitor maker names", "https://raw.githubusercontent.com/vcrhonek/hwdata/master/pnp.ids", NULL, 1500 },
};

int srs_lists_dir(char *out, size_t n)
{
    const char *x = getenv("XDG_DATA_HOME"), *h = getenv("HOME");
    if (x && *x) snprintf(out, n, "%s/srs/lists", x);
    else if (h && *h) snprintf(out, n, "%s/.local/share/srs/lists", h);
    else return -1;
    return 0;
}

/* "2026.10.01" from a list's "Version:" line; "" when it has none (pnp.ids); -1 when the file is missing */
int srs_ids_version(const char *path, char *out, size_t n)
{
    FILE *f = fopen(path, "r");
    char l[256];
    int i;
    struct stat st;
    out[0] = 0;
    if (!f) return -1;
    for (i = 0; i < 40 && fgets(l, sizeof l, f); i++) {
        char *v = strstr(l, "Version:");
        if (l[0] != '#') break;
        if (v) {
            v += 8;
            while (*v == ' ' || *v == '\t') v++;
            snprintf(out, n, "%.10s", v);
            break;
        }
    }
    fclose(f);
    (void)st;
    return 0;
}

/* entries (non-comment lines) of a list without a version line: more entries = the newer list */
long srs_ids_entries(const char *path)
{
    FILE *f = fopen(path, "r");
    char l[512];
    long n = 0;
    if (!f) return -1;
    while (fgets(l, sizeof l, f)) if (l[0] != '#' && l[0] != '\n') n++;
    fclose(f);
    return n;
}

/* the newer of the user's copy and the system's; NULL when neither exists. *src = "your copy" / "system" */
const char *srs_ids_path(const char *file, char *out, size_t n, const char **src)
{
    char dir[1024], up[1200], sp[1200], uv[32], sv[32];
    int hu, hs;
    if (src) *src = "";
    snprintf(sp, sizeof sp, "%s/%s", plat_hwdata_dir(), file);
    up[0] = 0;
    if (!srs_lists_dir(dir, sizeof dir)) snprintf(up, sizeof up, "%s/%s", dir, file);
    hu = *up && !srs_ids_version(up, uv, sizeof uv);
    hs = !srs_ids_version(sp, sv, sizeof sv);
    if (hu && hs && !*uv && !*sv) hu = srs_ids_entries(up) > srs_ids_entries(sp) ? 1 : 0, hs = !hu;   /* no versions: by entries */
    if (hu && (!hs || strcmp(uv, sv) > 0 || (!*uv && !*sv))) { snprintf(out, n, "%s", up); if (src) *src = "your copy (Update hardware lists)"; return out; }
    if (hs) { snprintf(out, n, "%s", sp); if (src) *src = "system (hwdata)"; return out; }
    return NULL;
}

/* is this download a real list? its header (when it has one) and enough entries */
static int list_ok(const srs_list *L, const char *path, char *why, size_t wn)
{
    FILE *f = fopen(path, "r");
    char l[512];
    int lines = 0, entries = 0, magic = !L->magic;
    if (!f) { snprintf(why, wn, "the download is missing"); return 0; }
    while (fgets(l, sizeof l, f)) {
        lines++;
        if (lines < 30 && L->magic && strstr(l, L->magic)) magic = 1;
        if (l[0] != '#' && l[0] != '\n') entries++;
        if (lines < 5 && (strstr(l, "<html") || strstr(l, "<HTML") || strstr(l, "<!DOCTYPE"))) { fclose(f); snprintf(why, wn, "the server sent a web page, not the list"); return 0; }
    }
    fclose(f);
    if (!magic) { snprintf(why, wn, "it does not start like %s", L->file); return 0; }
    if (entries < L->min_entries) { snprintf(why, wn, "only %d entries (at least %d expected)", entries, L->min_entries); return 0; }
    return 1;
}

static int fetch(const char *url, const char *to, char *why, size_t wn)
{
    char out[512] = "";
    const char *curl[] = { "curl", "-fsSL", "--max-time", "180", "--retry", "2", "-o", to, url, NULL };
    const char *wget[] = { "wget", "-q", "-T", "60", "-t", "2", "-O", to, url, NULL };
    int rc = plat_run_capture(curl, out, sizeof out);
    if (rc == -1) rc = plat_run_capture(wget, out, sizeof out);
    if (rc == -1) { snprintf(why, wn, "neither curl nor wget is installed"); return -1; }
    if (rc) { snprintf(why, wn, "the download failed (exit %d%s%.120s)", rc, *out ? ": " : "", out); return -1; }
    return 0;
}

int srs_lists_update(int which, char *report, size_t rn)
{
    char dir[1024];
    int i, ok = 0;
    size_t used = 0;
    report[0] = 0;
    if (srs_lists_dir(dir, sizeof dir)) { snprintf(report, rn, "no home folder to keep the lists in\n"); return -1; }
    {   /* mkdir -p */
        char p[1024];
        size_t k;
        snprintf(p, sizeof p, "%s", dir);
        for (k = 1; p[k]; k++) if (p[k] == '/') { p[k] = 0; mkdir(p, 0755); p[k] = '/'; }
        mkdir(p, 0755);
    }
    for (i = 0; i < SRS_NLISTS; i++) {
        const srs_list *L = &srs_lists[i];
        char tmp[1200], dst[1200], old[1200], why[300] = "", nv[32] = "", ov[32] = "", cur[1200];
        const char *src = NULL;
        if (which >= 0 && which != i) continue;
        snprintf(tmp, sizeof tmp, "%s/.%s.download", dir, L->file);
        snprintf(dst, sizeof dst, "%s/%s", dir, L->file);
        snprintf(old, sizeof old, "%s/%s.old", dir, L->file);
        if (srs_ids_path(L->file, cur, sizeof cur, &src)) srs_ids_version(cur, ov, sizeof ov);
        if (!*ov && *cur) snprintf(ov, sizeof ov, "%ld entries", srs_ids_entries(cur));
        if (fetch(L->url, tmp, why, sizeof why) || !list_ok(L, tmp, why, sizeof why)) {
            unlink(tmp);
            used += (size_t)snprintf(report + used, used < rn ? rn - used : 0, "%-10s not updated: %s (the copy in use stays)\n", L->file, why);
            continue;
        }
        srs_ids_version(tmp, nv, sizeof nv);
        {   /* no version line (pnp.ids): compare entries */
            long ne = srs_ids_entries(tmp), oe = *cur ? srs_ids_entries(cur) : 0;
            if (!*nv) {
                snprintf(nv, sizeof nv, "%ld entries", ne);
                if (ne <= oe) {   /* nothing new: keep using the copy in use */
                    unlink(tmp);
                    used += (size_t)snprintf(report + used, used < rn ? rn - used : 0, "%-10s %s, the same as the copy in use: no newer version yet\n", L->file, nv);
                    ok++;
                    continue;
                }
            }
        }
        if (!access(dst, F_OK)) rename(dst, old);
        if (rename(tmp, dst)) {
            unlink(tmp);
            used += (size_t)snprintf(report + used, used < rn ? rn - used : 0, "%-10s not updated: could not write %s\n", L->file, dst);
            continue;
        }
        ok++;
        used += (size_t)snprintf(report + used, used < rn ? rn - used : 0, "%-10s %s (was %s, %s)\n", L->file, nv, *ov ? ov : "none",
                                 strcmp(nv, ov) > 0 || strstr(nv, "entries") ? "newer" : "no newer version yet");
    }
    return ok;
}
