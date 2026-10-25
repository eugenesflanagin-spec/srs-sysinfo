/* core.c - portable core: .ids database, lookup chain, regex VM, report find, devices. */
#include "srs.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======================================================================
 * helpers
 * ====================================================================== */

const char *srs_bus_name(enum srs_bus b) { return b == BUS_USB ? "usb" : "pci"; }

void srs_thousands(size_t n, char *out, size_t outsz)
{
    char tmp[32], rev[48];
    int len, i, j = 0;
    snprintf(tmp, sizeof tmp, "%lu", (unsigned long)n);
    len = (int)strlen(tmp);
    for (i = 0; i < len; i++) {
        if (i && (len - i) % 3 == 0) rev[j++] = ',';
        rev[j++] = tmp[i];
    }
    rev[j] = 0;
    snprintf(out, outsz, "%s", rev);
}

/* copy at most `width` UTF-8 codepoints of s, then pad with spaces to width */
int srs_utf8_pad(char *out, size_t outsz, const char *s, int width)
{
    size_t o = 0;
    int cps = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p && cps < width) {
        int l = (*p < 0x80) ? 1 : (*p >> 5) == 6 ? 2 : (*p >> 4) == 14 ? 3 : (*p >> 3) == 30 ? 4 : 1;
        int k;
        if (o + (size_t)l + 1 >= outsz) break;
        for (k = 0; k < l && p[k]; k++) out[o++] = (char)p[k];
        p += k;
        cps++;
    }
    while (cps < width && o + 1 < outsz) { out[o++] = ' '; cps++; }
    out[o] = 0;
    return (int)o;
}

static int is_hex4(const char *s)
{
    int i;
    for (i = 0; i < 4; i++)
        if (!isxdigit((unsigned char)s[i])) return 0;
    return s[4] == 0 || s[4] == ':' ? 1 : 0;
}

static void upcase(char *s) { for (; *s; s++) *s = (char)toupper((unsigned char)*s); }

static void strip(char *s)
{
    size_t n = strlen(s), i = 0;
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
    while (s[i] && isspace((unsigned char)s[i])) i++;
    if (i) memmove(s, s + i, n - i + 1);
}

/* read one line of any length; returns NULL at EOF. Strips "\n" and a preceding "\r". */
static char *read_line(FILE *f, char **buf, size_t *cap)
{
    size_t n = 0;
    int c;
    if (!*buf) { *cap = 256; *buf = malloc(*cap); if (!*buf) return NULL; }
    while ((c = getc(f)) != EOF) {
        if (n + 2 >= *cap) {
            char *nb = realloc(*buf, *cap * 2);
            if (!nb) return NULL;
            *buf = nb; *cap *= 2;
        }
        if (c == '\n') break;
        (*buf)[n++] = (char)c;
    }
    if (c == EOF && n == 0) return NULL;
    if (n && (*buf)[n - 1] == '\r') n--;
    (*buf)[n] = 0;
    return *buf;
}

static void join(char *out, size_t outsz, const char *dir, const char *name)
{
    size_t l = strlen(dir);
    if (l && (dir[l - 1] == '/' || dir[l - 1] == '\\'))
        snprintf(out, outsz, "%s%s", dir, name);
    else
        snprintf(out, outsz, "%s%c%s", dir, plat_sep(), name);
}

/* ======================================================================
 * .ids database - flatten the tab-indented tree into SIV-shaped keys
 * ====================================================================== */

typedef struct { int pci, have_v, have_d; char v[5], d[5]; } ids_state;

/* returns 1 and fills key/name when the line yields a record */
static int ids_line(ids_state *st, char *line, char key[24], char **name)
{
    size_t len = strlen(line);
    char *t = line;
    while (*t && isspace((unsigned char)*t)) t++;
    if (!*t || line[0] == '#') return 0;
    if (line[0] != '\t') {
        if (len < 6 || !isxdigit((unsigned char)line[0]) || !isxdigit((unsigned char)line[1]) ||
            !isxdigit((unsigned char)line[2]) || !isxdigit((unsigned char)line[3]) ||
            line[4] != ' ' || line[5] != ' ') {
            st->have_v = st->have_d = 0;   /* usb.ids class/HID/language sections */
            return 0;
        }
        memcpy(st->v, line, 4); st->v[4] = 0; upcase(st->v);
        st->have_v = 1; st->have_d = 0;
        snprintf(key, 24, "%s", st->v);
        *name = line + 6;
        strip(*name);
        return 1;
    }
    if (!st->have_v) return 0;
    if (len > 1 && line[1] != '\t') {
        if (len < 5) return 0;
        memcpy(st->d, line + 1, 4); st->d[4] = 0; upcase(st->d);
        st->have_d = 1;
        snprintf(key, 24, "%s:%s", st->v, st->d);
        *name = len > 7 ? line + 7 : line + len;
        strip(*name);
        return 1;
    }
    if (st->have_d && st->pci && len >= 11) {   /* usb.ids two-tab lines are interfaces */
        char sub[10];
        memcpy(sub, line + 2, 9); sub[9] = 0; upcase(sub);
        if (sub[4] == ' ') sub[4] = ':';
        snprintf(key, 24, "%s:%s:%s", st->v, st->d, sub);
        *name = len > 13 ? line + 13 : line + len;
        strip(*name);
        return 1;
    }
    return 0;
}

const char *srs_db_find_file(enum srs_bus bus, char *out, size_t outsz)
{
    const char *fn = bus == BUS_USB ? "usb.ids" : "pci.ids";
    char dir[1024];
    FILE *f;
    /* SIV's pattern: data file beside the program first, then the system copy */
    if (plat_exedir(dir, sizeof dir) == 0) {
        join(out, outsz, dir, fn);
        if ((f = fopen(out, "rb"))) { fclose(f); return out; }
    }
    if (plat_hwdata_dir()) {
        join(out, outsz, plat_hwdata_dir(), fn);
        if ((f = fopen(out, "rb"))) { fclose(f); return out; }
    }
    return NULL;
}

static int SRS_CDECL rec_cmp(const void *a, const void *b)
{
    const srs_rec *x = a, *y = b;
    int c = strcmp(x->key, y->key);
    return c ? c : strcmp(x->name, y->name);
}

static int SRS_CDECL key_cmp(const void *k, const void *r) { return strcmp((const char *)k, ((const srs_rec *)r)->key); }

int srs_db_load(srs_db *db, enum srs_bus bus)
{
    FILE *f;
    char *buf = NULL, key[24], *name;
    size_t cap = 0, rcap = 0;
    ids_state st;
    double t0 = srs_now_ms();

    memset(db, 0, sizeof *db);
    db->bus = bus;
    if (!srs_db_find_file(bus, db->path, sizeof db->path)) return -1;
    if (!(f = fopen(db->path, "rb"))) return -1;
    memset(&st, 0, sizeof st);
    st.pci = bus == BUS_PCI;
    while (read_line(f, &buf, &cap)) {
        if (buf[0] == '#') {
            char *p = buf + 1;
            while (*p == ' ' || *p == '\t') p++;
            if (!strncmp(p, "Version:", 8)) { snprintf(db->version, sizeof db->version, "%s", p + 8); strip(db->version); }
            else if (!strncmp(p, "Date:", 5)) { snprintf(db->date, sizeof db->date, "%s", p + 5); strip(db->date); }
            continue;
        }
        if (!ids_line(&st, buf, key, &name)) continue;
        if (db->n == rcap) {
            srs_rec *nr;
            rcap = rcap ? rcap * 2 : 8192;
            nr = realloc(db->recs, rcap * sizeof *nr);
            if (!nr) break;
            db->recs = nr;
        }
        memcpy(db->recs[db->n].key, key, 24);
        db->recs[db->n].name = malloc(strlen(name) + 1);
        if (!db->recs[db->n].name) break;
        strcpy(db->recs[db->n].name, name);
        db->n++;
    }
    free(buf);
    fclose(f);
    /* sort at load (qsort), search with bsearch - SIV's "Do bsearch() of a B-Tree" */
    qsort(db->recs, db->n, sizeof *db->recs, rec_cmp);
    db->load_ms = srs_now_ms() - t0;
    return 0;
}

void srs_db_free(srs_db *db)
{
    size_t i;
    for (i = 0; i < db->n; i++) free(db->recs[i].name);
    free(db->recs);
    memset(db, 0, sizeof *db);
}

const char *srs_db_tree(const srs_db *db, const char *key)
{
    const srs_rec *r = bsearch(key, db->recs, db->n, sizeof *db->recs, key_cmp);
    if (!r) return NULL;
    while (r > db->recs && !strcmp(r[-1].key, key)) r--;   /* first of duplicates */
    return r->name;
}

const char *srs_db_scan(const srs_db *db, const char *key)
{
    static char found[512];
    FILE *f = fopen(db->path, "rb");
    char *buf = NULL, k[24], *name;
    size_t cap = 0;
    ids_state st;
    const char *ret = NULL;
    if (!f) return NULL;
    memset(&st, 0, sizeof st);
    st.pci = db->bus == BUS_PCI;
    while (read_line(f, &buf, &cap)) {
        if (ids_line(&st, buf, k, &name) && !strcmp(k, key)) {
            snprintf(found, sizeof found, "%s", name);
            ret = found;
            break;
        }
    }
    free(buf);
    fclose(f);
    return ret;
}

/* ======================================================================
 * lookup chain
 * ====================================================================== */

int srs_chain(const char *id, char chain[SRS_MAXCHAIN][24], int *n, char *err, size_t errsz)
{
    char tmp[64], *p[8], rev[3] = "";
    int np = 0, i;
    char *s;

    snprintf(tmp, sizeof tmp, "%s", id);
    upcase(tmp);
    for (s = tmp;;) {
        char *c = strchr(s, ':');
        if (np == 8) goto bad;
        p[np++] = s;
        if (!c) break;
        *c = 0;
        s = c + 1;
    }
    for (i = 0; i < np && i < 2; i++)
        if (strlen(p[i]) != 4 || !is_hex4(p[i])) goto bad;
    if ((np == 3 || np == 5) && strlen(p[np - 1]) == 2 &&
        isxdigit((unsigned char)p[np - 1][0]) && isxdigit((unsigned char)p[np - 1][1]) &&
        !(p[np - 1][0] >= 'a' && p[np - 1][0] <= 'f')) {
        snprintf(rev, sizeof rev, "%s", p[np - 1]);
        np--;
    }
    if (np != 1 && np != 2 && np != 4) goto bad;
    for (i = 0; i < np; i++)
        if (strlen(p[i]) != 4 || !is_hex4(p[i])) goto bad;
    *n = 0;
    if (np == 4) {
        if (*rev) snprintf(chain[(*n)++], 24, "%s:%s:%s:%s:%s", p[0], p[1], p[2], p[3], rev);
        snprintf(chain[(*n)++], 24, "%s:%s:%s:%s", p[0], p[1], p[2], p[3]);
    }
    if (np >= 2) {
        if (*rev) snprintf(chain[(*n)++], 24, "%s:%s:%s", p[0], p[1], rev);
        snprintf(chain[(*n)++], 24, "%s:%s", p[0], p[1]);
    }
    snprintf(chain[(*n)++], 24, "%s", p[0]);
    return 0;
bad:
    snprintf(err, errsz, "bad ID '%s': expected VVVV[:DDDD[:SSSS:ssss]][:RR]", id);
    return -1;
}

static const char *look(const srs_db *db, const char *key, enum srs_mode mode)
{
    return mode == MODE_SCAN ? srs_db_scan(db, key) : srs_db_tree(db, key);
}

static void copyname(char *dst, const char *src) { snprintf(dst, 256, "%s", src ? src : ""); }

int srs_resolve(const srs_db *db, const char *id, enum srs_mode mode,
                srs_result *r, char *err, size_t errsz)
{
    char parts[5][8];
    int np = 0, i;
    const char *s;
    char k[24];
    double t0;

    memset(r, 0, sizeof *r);
    r->matched = -1;
    if (srs_chain(id, r->chain, &r->nchain, err, errsz)) return -1;
    t0 = srs_now_ms();
    for (i = 0; i < r->nchain; i++) {
        copyname(r->name[i], look(db, r->chain[i], mode));
        if (r->matched < 0 && r->name[i][0]) r->matched = i;
    }
    for (s = r->chain[0]; np < 5;) {   /* split chain[0] */
        const char *c = strchr(s, ':');
        size_t l = c ? (size_t)(c - s) : strlen(s);
        snprintf(parts[np++], 8, "%.*s", (int)l, s);
        if (!c) break;
        s = c + 1;
    }
    if (np >= 2) { snprintf(k, sizeof k, "%s:%s", parts[0], parts[1]); copyname(r->stock, look(db, k, mode)); }
    if (np >= 4) copyname(r->subvendor, look(db, parts[2], mode));
    copyname(r->vendor, look(db, parts[0], mode));
    if (r->matched >= 0) copyname(r->resolved, r->name[r->matched]);
    r->lookup_ms = srs_now_ms() - t0;
    return 0;
}

int srs_info_lookup(enum srs_bus bus, const char *id, srs_info *out)
{
    char tmp[64], *p[8], buf[8192], *line;
    int np = 0;
    char *s;
    double t0 = srs_now_ms();

    memset(out, 0, sizeof *out);
    snprintf(tmp, sizeof tmp, "%s", id);
    upcase(tmp);
    for (s = tmp; np < 8;) {
        char *c = strchr(s, ':');
        p[np++] = s;
        if (!c) break;
        *c = 0;
        s = c + 1;
    }
    if (bus == BUS_PCI) {
        snprintf(out->query, sizeof out->query, "pci:v0000%sd0000%s", p[0], np > 1 ? p[1] : "*");
        if (np >= 4) {
            size_t l = strlen(out->query);
            snprintf(out->query + l, sizeof out->query - l, "sv0000%ssd0000%s", p[2], p[3]);
        }
    } else {
        snprintf(out->query, sizeof out->query, "usb:v%sp%s", p[0], np > 1 ? p[1] : "");
    }
    {
        const char *argv[] = { "systemd-hwdb", "query", out->query, NULL };
        int rc = plat_run_capture(argv, buf, sizeof buf);
        if (rc < 0) {
            /* not Linux: the Windows "API" mode - SetupAPI device descriptions */
            int r2 = plat_setupapi_info(bus, id, out);
            out->ms = srs_now_ms() - t0;
            if (r2 < 0 && !out->err[0]) snprintf(out->err, sizeof out->err, "no OS device database available");
            return (out->model[0] || out->vendor[0]) ? 0 : 1;
        }
    }
    for (line = strtok(buf, "\n"); line; line = strtok(NULL, "\n")) {
        if (!strncmp(line, "ID_MODEL_FROM_DATABASE=", 23)) snprintf(out->model, sizeof out->model, "%s", line + 23);
        else if (!strncmp(line, "ID_VENDOR_FROM_DATABASE=", 24)) snprintf(out->vendor, sizeof out->vendor, "%s", line + 24);
    }
    out->ms = srs_now_ms() - t0;
    return (out->model[0] || out->vendor[0]) ? 0 : 1;
}

/* ======================================================================
 * regex - parser -> AST -> backtracking VM with (pc,pos) memo.
 * Syntax/error messages follow Python's re (the original tool), ASCII only.
 * ====================================================================== */

enum { N_EMPTY, N_CHAR, N_ANY, N_CLASS, N_BOL, N_EOL, N_WB, N_NWB, N_CAT, N_ALT, N_REP };
enum { I_CHAR, I_ANY, I_CLASS, I_BOL, I_EOL, I_WB, I_NWB, I_SPLIT, I_JMP, I_MATCH };

typedef struct { int t, c, cls, a, b, min, max, greedy; } rnode;
typedef struct { unsigned char op, c; int x, y; } rinst;

struct srs_re {
    rinst *prog; int n, cap;
    unsigned char (*cls)[32]; int ncls, clscap;
    unsigned char *visit; size_t visitsz;
    int *stack; size_t stackcap;
};

typedef struct {
    const char *p; int pos, len, icase, failed;
    rnode *nodes; int nn, ncap;
    srs_re *re;
    char *err; size_t errsz;
} rparser;

#define REMAX 20000

static int rfail(rparser *ps, const char *fmt, int a, int b)
{
    if (!ps->failed) {
        char m[200];
        snprintf(m, sizeof m, fmt, a, b);
        snprintf(ps->err, ps->errsz, "%s", m);
    }
    ps->failed = 1;
    return 0;
}

static int rnew(rparser *ps, int t)
{
    if (ps->nn == ps->ncap) {
        rnode *nn;
        ps->ncap = ps->ncap ? ps->ncap * 2 : 64;
        nn = realloc(ps->nodes, (size_t)ps->ncap * sizeof *nn);
        if (!nn) { rfail(ps, "out of memory", 0, 0); return 0; }
        ps->nodes = nn;
    }
    memset(&ps->nodes[ps->nn], 0, sizeof ps->nodes[0]);
    ps->nodes[ps->nn].t = t;
    return ps->nn++;
}

static int rnew2(rparser *ps, int t, int a, int b) { int n = rnew(ps, t); ps->nodes[n].a = a; ps->nodes[n].b = b; return n; }

static int newcls(rparser *ps)
{
    srs_re *re = ps->re;
    if (re->ncls == re->clscap) {
        void *nc;
        re->clscap = re->clscap ? re->clscap * 2 : 16;
        nc = realloc(re->cls, (size_t)re->clscap * 32);
        if (!nc) { rfail(ps, "out of memory", 0, 0); return 0; }
        re->cls = nc;
    }
    memset(re->cls[re->ncls], 0, 32);
    return re->ncls++;
}

static void cls_set(unsigned char *c, int ch) { c[(unsigned char)ch >> 3] |= (unsigned char)(1u << (ch & 7)); }
static int  cls_has(const unsigned char *c, int ch) { return c[(unsigned char)ch >> 3] >> (ch & 7) & 1; }
static int  is_word(int ch) { return isalnum(ch) || ch == '_'; }

static void cls_add_named(unsigned char *c, int which)  /* d w s, upper = negated */
{
    int ch, neg = isupper(which);
    int lw = tolower(which);
    for (ch = 0; ch < 256; ch++) {
        int in = lw == 'd' ? (ch >= '0' && ch <= '9') :
                 lw == 'w' ? (ch < 128 && is_word(ch)) :
                 (ch == ' ' || (ch >= '\t' && ch <= '\r'));
        if (in != neg) cls_set(c, ch);
    }
}

static int char_node(rparser *ps, int ch)
{
    int n;
    if (ps->icase && isalpha(ch)) {
        n = rnew(ps, N_CLASS);
        ps->nodes[n].cls = newcls(ps);
        cls_set(ps->re->cls[ps->nodes[n].cls], tolower(ch));
        cls_set(ps->re->cls[ps->nodes[n].cls], toupper(ch));
        return n;
    }
    n = rnew(ps, N_CHAR);
    ps->nodes[n].c = (unsigned char)ch;
    return n;
}

static int hexval(int c) { return isdigit(c) ? c - '0' : tolower(c) - 'a' + 10; }

/* parse an escape after '\' at ps->pos (pointing past '\'); returns char, or -('d'|...) for classes */
static int parse_escape(rparser *ps, int inclass, int *named)
{
    int at = ps->pos - 1, c;
    *named = 0;
    if (ps->pos >= ps->len) return rfail(ps, "bad escape (end of pattern) at position %d", at, 0), -1;
    c = (unsigned char)ps->p[ps->pos++];
    switch (c) {
    case 'd': case 'D': case 'w': case 'W': case 's': case 'S': *named = c; return 0;
    case 't': return '\t';
    case 'n': return '\n';
    case 'r': return '\r';
    case 'f': return '\f';
    case 'v': return '\v';
    case 'a': return '\a';
    case 'x':
        if (ps->pos + 2 > ps->len || !isxdigit((unsigned char)ps->p[ps->pos]) || !isxdigit((unsigned char)ps->p[ps->pos + 1]))
            return rfail(ps, "incomplete escape \\x at position %d", at, 0), -1;
        c = hexval(ps->p[ps->pos]) * 16 + hexval(ps->p[ps->pos + 1]);
        ps->pos += 2;
        return c;
    case 'b': if (inclass) return '\b'; *named = 'b'; return 0;
    case 'B': if (inclass) return rfail(ps, "bad escape \\B at position %d", at, 0), -1; *named = 'B'; return 0;
    default:
        if (isdigit(c)) return rfail(ps, "invalid group reference %d at position %d", c - '0', at + 1), -1;
        if (isalpha(c)) {
            char m[64];
            snprintf(m, sizeof m, "bad escape \\%c at position %%d", c);
            return rfail(ps, m, at, 0), -1;
        }
        return c;
    }
}

static int parse_class(rparser *ps)
{
    int open = ps->pos - 1, neg = 0, first = 1, n, ch;
    unsigned char *c;
    n = rnew(ps, N_CLASS);
    ps->nodes[n].cls = newcls(ps);
    if (ps->failed) return 0;
    if (ps->pos < ps->len && ps->p[ps->pos] == '^') { neg = 1; ps->pos++; }
    for (;;) {
        int lo, hi, named = 0, lopos;
        c = ps->re->cls[ps->nodes[n].cls];   /* re-fetch: newcls may realloc */
        if (ps->pos >= ps->len) return rfail(ps, "unterminated character set at position %d", open, 0);
        if (ps->p[ps->pos] == ']' && !first) { ps->pos++; break; }
        first = 0;
        lopos = ps->pos;
        lo = (unsigned char)ps->p[ps->pos++];
        if (lo == '\\') {
            lo = parse_escape(ps, 1, &named);
            if (ps->failed) return 0;
            if (named) { cls_add_named(c, named); continue; }
        }
        if (ps->pos + 1 < ps->len && ps->p[ps->pos] == '-' && ps->p[ps->pos + 1] != ']') {
            ps->pos++;
            hi = (unsigned char)ps->p[ps->pos++];
            if (hi == '\\') {
                hi = parse_escape(ps, 1, &named);
                if (ps->failed) return 0;
                if (named) return rfail(ps, "bad character range at position %d", lopos, 0);
            }
            if (lo > hi) {
                char m[80];
                snprintf(m, sizeof m, "bad character range %c-%c at position %%d", lo, hi);
                return rfail(ps, m, lopos, 0);
            }
            for (ch = lo; ch <= hi; ch++) cls_set(c, ch);
        } else {
            cls_set(c, lo);
        }
    }
    c = ps->re->cls[ps->nodes[n].cls];
    if (ps->icase)
        for (ch = 0; ch < 256; ch++)
            if (isalpha(ch) && cls_has(c, ch)) { cls_set(c, tolower(ch)); cls_set(c, toupper(ch)); }
    if (neg)
        for (ch = 0; ch < 32; ch++) c[ch] = (unsigned char)~c[ch];
    return n;
}

static int parse_alt(rparser *ps, int depth);

/* is there a quantifier at pos? fills min/max/end for {..} */
static int quant_at(rparser *ps, int pos, int *min, int *max, int *end)
{
    char c;
    if (pos >= ps->len) return 0;
    c = ps->p[pos];
    if (c == '*') { *min = 0; *max = -1; *end = pos + 1; return 1; }
    if (c == '+') { *min = 1; *max = -1; *end = pos + 1; return 1; }
    if (c == '?') { *min = 0; *max = 1; *end = pos + 1; return 1; }
    if (c == '{') {
        int i = pos + 1, lo = -1, hi = -1, comma = 0;
        while (i < ps->len && isdigit((unsigned char)ps->p[i])) { lo = (lo < 0 ? 0 : lo) * 10 + ps->p[i] - '0'; if (lo > 100000) lo = 100000; i++; }
        if (i < ps->len && ps->p[i] == ',') {
            comma = 1; i++;
            while (i < ps->len && isdigit((unsigned char)ps->p[i])) { hi = (hi < 0 ? 0 : hi) * 10 + ps->p[i] - '0'; if (hi > 100000) hi = 100000; i++; }
        }
        if (i >= ps->len || ps->p[i] != '}') return 0;   /* not a quantifier: literal '{' */
        *min = lo < 0 ? 0 : lo;
        *max = comma ? hi : (lo < 0 ? -1 : lo);
        *end = i + 1;
        return 1;
    }
    return 0;
}

static int parse_atom(rparser *ps, int depth)
{
    int c = (unsigned char)ps->p[ps->pos++], n, named;
    switch (c) {
    case '(': {
        int open = ps->pos - 1;
        if (ps->pos < ps->len && ps->p[ps->pos] == '?') {
            if (ps->pos + 1 < ps->len && ps->p[ps->pos + 1] == ':') ps->pos += 2;
            else return rfail(ps, "unknown extension ?%c at position %d",
                              ps->pos + 1 < ps->len ? ps->p[ps->pos + 1] : '?', open + 1);
        }
        n = parse_alt(ps, depth + 1);
        if (ps->failed) return 0;
        if (ps->pos >= ps->len || ps->p[ps->pos] != ')')
            return rfail(ps, "missing ), unterminated subpattern at position %d", open, 0);
        ps->pos++;
        return n;
    }
    case '[': return parse_class(ps);
    case '.': return rnew(ps, N_ANY);
    case '^': return rnew(ps, N_BOL);
    case '$': return rnew(ps, N_EOL);
    case '\\':
        c = parse_escape(ps, 0, &named);
        if (ps->failed) return 0;
        if (named == 'b') return rnew(ps, N_WB);
        if (named == 'B') return rnew(ps, N_NWB);
        if (named) {
            n = rnew(ps, N_CLASS);
            ps->nodes[n].cls = newcls(ps);
            if (!ps->failed) cls_add_named(ps->re->cls[ps->nodes[n].cls], named);
            return n;
        }
        return char_node(ps, c);
    default:
        return char_node(ps, c);
    }
}

static int parse_repeat(rparser *ps, int depth)
{
    int min, max, end, n;
    if (quant_at(ps, ps->pos, &min, &max, &end))
        return rfail(ps, "nothing to repeat at position %d", ps->pos, 0);
    n = parse_atom(ps, depth);
    if (ps->failed) return 0;
    if (quant_at(ps, ps->pos, &min, &max, &end)) {
        int qpos = ps->pos, r, greedy = 1, m2, x2, e2;
        if (max >= 0 && min > max) return rfail(ps, "min repeat greater than max repeat at position %d", qpos + 1, 0);
        ps->pos = end;
        if (ps->pos < ps->len && ps->p[ps->pos] == '?') { greedy = 0; ps->pos++; }
        if (quant_at(ps, ps->pos, &m2, &x2, &e2))
            return rfail(ps, "multiple repeat at position %d", ps->pos, 0);
        r = rnew2(ps, N_REP, n, 0);
        ps->nodes[r].min = min;
        ps->nodes[r].max = max;
        ps->nodes[r].greedy = greedy;
        return r;
    }
    return n;
}

static int parse_cat(rparser *ps, int depth)
{
    int res = -1;
    while (ps->pos < ps->len && ps->p[ps->pos] != '|' && ps->p[ps->pos] != ')') {
        int n = parse_repeat(ps, depth);
        if (ps->failed) return 0;
        res = res < 0 ? n : rnew2(ps, N_CAT, res, n);
    }
    return res < 0 ? rnew(ps, N_EMPTY) : res;
}

static int parse_alt(rparser *ps, int depth)
{
    int left = parse_cat(ps, depth);
    while (!ps->failed && ps->pos < ps->len && ps->p[ps->pos] == '|') {
        int right;
        ps->pos++;
        right = parse_cat(ps, depth);
        left = rnew2(ps, N_ALT, left, right);
    }
    return left;
}

static int emit(rparser *ps, int op, int c, int x, int y)
{
    srs_re *re = ps->re;
    if (re->n >= REMAX) { rfail(ps, "pattern too large", 0, 0); return 0; }
    if (re->n == re->cap) {
        rinst *np;
        re->cap = re->cap ? re->cap * 2 : 64;
        np = realloc(re->prog, (size_t)re->cap * sizeof *np);
        if (!np) { rfail(ps, "out of memory", 0, 0); return 0; }
        re->prog = np;
    }
    re->prog[re->n].op = (unsigned char)op;
    re->prog[re->n].c = (unsigned char)c;
    re->prog[re->n].x = x;
    re->prog[re->n].y = y;
    return re->n++;
}

static void gen(rparser *ps, int ni)
{
    rnode nd;
    int i;
    if (ps->failed) return;
    nd = ps->nodes[ni];
    switch (nd.t) {
    case N_EMPTY: break;
    case N_CHAR:  emit(ps, I_CHAR, nd.c, 0, 0); break;
    case N_ANY:   emit(ps, I_ANY, 0, 0, 0); break;
    case N_CLASS: emit(ps, I_CLASS, 0, nd.cls, 0); break;
    case N_BOL:   emit(ps, I_BOL, 0, 0, 0); break;
    case N_EOL:   emit(ps, I_EOL, 0, 0, 0); break;
    case N_WB:    emit(ps, I_WB, 0, 0, 0); break;
    case N_NWB:   emit(ps, I_NWB, 0, 0, 0); break;
    case N_CAT:   gen(ps, nd.a); gen(ps, nd.b); break;
    case N_ALT: {
        int sp = emit(ps, I_SPLIT, 0, 0, 0), jp;
        gen(ps, nd.a);
        jp = emit(ps, I_JMP, 0, 0, 0);
        if (ps->failed) return;
        ps->re->prog[sp].x = sp + 1;
        ps->re->prog[sp].y = ps->re->n;
        gen(ps, nd.b);
        if (ps->failed) return;
        ps->re->prog[jp].x = ps->re->n;
        break;
    }
    case N_REP: {
        for (i = 0; i < nd.min && !ps->failed; i++) gen(ps, nd.a);
        if (nd.max < 0) {
            int sp = emit(ps, I_SPLIT, 0, 0, 0);
            gen(ps, nd.a);
            emit(ps, I_JMP, 0, sp, 0);
            if (ps->failed) return;
            ps->re->prog[sp].x = nd.greedy ? sp + 1 : ps->re->n;
            ps->re->prog[sp].y = nd.greedy ? ps->re->n : sp + 1;
        } else {
            int *splits, ns = 0, k;
            splits = malloc(sizeof(int) * (size_t)(nd.max - nd.min + 1));
            if (!splits) { rfail(ps, "out of memory", 0, 0); return; }
            for (i = nd.min; i < nd.max && !ps->failed; i++) {
                splits[ns++] = emit(ps, I_SPLIT, 0, 0, 0);
                gen(ps, nd.a);
            }
            for (k = 0; k < ns && !ps->failed; k++) {
                rinst *in = &ps->re->prog[splits[k]];
                in->x = nd.greedy ? splits[k] + 1 : ps->re->n;
                in->y = nd.greedy ? ps->re->n : splits[k] + 1;
            }
            free(splits);
        }
        break;
    }
    }
}

srs_re *srs_re_compile(const char *pat, int icase, int literal, char *err, size_t errsz)
{
    rparser ps;
    srs_re *re = calloc(1, sizeof *re);
    int root;
    if (!re) { snprintf(err, errsz, "out of memory"); return NULL; }
    memset(&ps, 0, sizeof ps);
    ps.p = pat; ps.len = (int)strlen(pat); ps.icase = icase;
    ps.re = re; ps.err = err; ps.errsz = errsz;
    if (literal) {
        int i;
        root = -1;
        for (i = 0; i < ps.len && !ps.failed; i++) {
            int n = char_node(&ps, (unsigned char)pat[i]);
            root = root < 0 ? n : rnew2(&ps, N_CAT, root, n);
        }
        if (root < 0) root = rnew(&ps, N_EMPTY);
    } else {
        root = parse_alt(&ps, 0);
        if (!ps.failed && ps.pos < ps.len)   /* stray ')' */
            rfail(&ps, "unbalanced parenthesis at position %d", ps.pos, 0);
    }
    gen(&ps, root);
    emit(&ps, I_MATCH, 0, 0, 0);
    free(ps.nodes);
    if (ps.failed) { srs_re_free(re); return NULL; }
    return re;
}

void srs_re_free(srs_re *re)
{
    if (!re) return;
    free(re->prog); free(re->cls); free(re->visit); free(re->stack);
    free(re);
}

static int push(srs_re *re, size_t *sp, int pc, int pos)
{
    if (*sp + 2 > re->stackcap) {
        size_t nc = re->stackcap ? re->stackcap * 2 : 256;
        int *ns = realloc(re->stack, nc * sizeof *ns);
        if (!ns) return 0;
        re->stack = ns; re->stackcap = nc;
    }
    re->stack[(*sp)++] = pc;
    re->stack[(*sp)++] = pos;
    return 1;
}

int srs_re_search(srs_re *re, const char *s, int len, int start, int *so, int *eo)
{
    size_t need = ((size_t)re->n * (size_t)(len + 1) + 7) / 8;
    int st;
    const unsigned char *u = (const unsigned char *)s;
    if (need > re->visitsz) {
        unsigned char *nv = realloc(re->visit, need);
        if (!nv) return 0;
        re->visit = nv; re->visitsz = need;
    }
    /* failure memo is start-independent (no captures), so it is shared by all starts */
    memset(re->visit, 0, need);
    for (st = start; st <= len; st++) {
        size_t sp = 0;
        if (!push(re, &sp, 0, st)) return 0;
        while (sp) {
            int pos = re->stack[--sp], pc = re->stack[--sp];
            for (;;) {
                size_t bit = (size_t)pc * (size_t)(len + 1) + (size_t)pos;
                const rinst *in;
                if (re->visit[bit >> 3] >> (bit & 7) & 1) break;
                re->visit[bit >> 3] |= (unsigned char)(1u << (bit & 7));
                in = &re->prog[pc];
                switch (in->op) {
                case I_CHAR:  if (pos < len && u[pos] == in->c) { pc++; pos++; continue; } break;
                case I_ANY:   if (pos < len && u[pos] != '\n') { pc++; pos++; continue; } break;
                case I_CLASS: if (pos < len && cls_has(re->cls[in->x], u[pos])) { pc++; pos++; continue; } break;
                case I_BOL:   if (pos == 0) { pc++; continue; } break;
                case I_EOL:   if (pos == len || (pos == len - 1 && u[pos] == '\n')) { pc++; continue; } break;
                case I_WB: case I_NWB: {
                    int a = pos > 0 && is_word(u[pos - 1]), b = pos < len && is_word(u[pos]);
                    if ((a != b) == (in->op == I_WB)) { pc++; continue; }
                    break;
                }
                case I_SPLIT: if (!push(re, &sp, in->y, pos)) return 0; pc = in->x; continue;
                case I_JMP:   pc = in->x; continue;
                case I_MATCH: *so = st; *eo = pos; return 1;
                }
                break;
            }
        }
    }
    return 0;
}

/* ======================================================================
 * find across a report
 * ====================================================================== */

const char *srs_reports_root(void)
{
    static char root[1024];
    const char *h = plat_home();
    if (!h) return NULL;
    join(root, sizeof root, h, "sysreports");
    return root;
}

int srs_latest_report(char *out, size_t outsz)
{
    const char *root = srs_reports_root();
    char **names, best[512] = "", path[1024];
    int n, i;
    if (!root) return -1;
    n = plat_listdir(root, &names);
    if (n < 0) return -1;
    for (i = 0; i < n; i++) {
        if (strncmp(names[i], "sysreport-", 10)) continue;
        join(path, sizeof path, root, names[i]);
        if (plat_isdir(path) && strcmp(names[i], best) > 0) snprintf(best, sizeof best, "%s", names[i]);
    }
    plat_freelist(names, n);
    if (!*best) return -1;
    join(out, outsz, root, best);
    return 0;
}

static int ends_with(const char *s, const char *suf)
{
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && !strcmp(s + a - b, suf);
}

int srs_find(const char *report, const char *pattern, int icase, int fixed, int maxshow,
             srs_findres *fr, char *err, size_t errsz)
{
    srs_re *re;
    char **names, path[1200], rerr[256], *buf = NULL;
    size_t cap = 0;
    int n, i;
    double t0 = srs_now_ms();

    memset(fr, 0, sizeof *fr);
    if (report && *report) snprintf(fr->report, sizeof fr->report, "%s", report);
    else if (srs_latest_report(fr->report, sizeof fr->report)) {
        snprintf(err, errsz, "No report found under %s - run srs-sysreport first",
                 srs_reports_root() ? srs_reports_root() : "~/sysreports");
        return 2;
    }
    if (!plat_isdir(fr->report)) {
        if (report && *report) snprintf(err, errsz, "No report directory %s", report);
        else snprintf(err, errsz, "No report found under %s - run srs-sysreport first",
                      srs_reports_root() ? srs_reports_root() : "~/sysreports");
        return 2;
    }
    re = srs_re_compile(pattern, icase, fixed, rerr, sizeof rerr);
    if (!re) {
        snprintf(err, errsz, "Match String [%s] is Invalid: %s", pattern, rerr);
        return 2;
    }
    n = plat_listdir(fr->report, &names);
    for (i = 0; i < n; i++) {
        FILE *f;
        srs_section sec;
        int lineno = 0;
        if (!(ends_with(names[i], ".txt") || ends_with(names[i], ".html")) || !strcmp(names[i], "index.html"))
            continue;
        fr->nfiles++;
        join(path, sizeof path, fr->report, names[i]);
        if (!(f = fopen(path, "rb"))) continue;
        memset(&sec, 0, sizeof sec);
        snprintf(sec.file, sizeof sec.file, "%s", names[i]);
        while (read_line(f, &buf, &cap)) {
            int len = (int)strlen(buf), so, eo, pos = 0, ns = 0;
            srs_span spans[64];
            lineno++;
            if (!srs_re_search(re, buf, len, 0, &so, &eo)) continue;
            sec.count++;
            if (sec.nshown >= maxshow) continue;
            while (pos <= len && ns < 64 && srs_re_search(re, buf, len, pos, &so, &eo)) {
                if (eo > so) { spans[ns].so = so; spans[ns].eo = eo; ns++; }
                pos = eo > so ? eo : so + 1;
            }
            {
                srs_hit *nh = realloc(sec.hits, (size_t)(sec.nshown + 1) * sizeof *nh);
                if (!nh) break;
                sec.hits = nh;
                nh[sec.nshown].lineno = lineno;
                nh[sec.nshown].text = malloc((size_t)len + 1);
                if (nh[sec.nshown].text) memcpy(nh[sec.nshown].text, buf, (size_t)len + 1);
                nh[sec.nshown].nspan = ns;
                nh[sec.nshown].spans = ns ? malloc(sizeof(srs_span) * (size_t)ns) : NULL;
                if (ns && nh[sec.nshown].spans) memcpy(nh[sec.nshown].spans, spans, sizeof(srs_span) * (size_t)ns);
                sec.nshown++;
            }
        }
        fclose(f);
        if (sec.count) {
            srs_section *ns2 = realloc(fr->secs, (size_t)(fr->nsec + 1) * sizeof *ns2);
            if (ns2) { fr->secs = ns2; fr->secs[fr->nsec++] = sec; fr->total += sec.count; }
        }
    }
    plat_freelist(names, n < 0 ? 0 : n);
    free(buf);
    srs_re_free(re);
    fr->ms = srs_now_ms() - t0;
    return 0;
}

void srs_find_free(srs_findres *fr)
{
    int i, j;
    for (i = 0; i < fr->nsec; i++) {
        for (j = 0; j < fr->secs[i].nshown; j++) { free(fr->secs[i].hits[j].text); free(fr->secs[i].hits[j].spans); }
        free(fr->secs[i].hits);
    }
    free(fr->secs);
    memset(fr, 0, sizeof *fr);
}

/* ======================================================================
 * local devices (sysfs; SetupAPI on real Windows)
 * ====================================================================== */

static int read_attr(const char *dir, const char *attr, char *out, size_t outsz)
{
    char path[1200];
    FILE *f;
    join(path, sizeof path, dir, attr);
    if (!(f = fopen(path, "rb"))) return 0;
    if (!fgets(out, (int)outsz, f)) { fclose(f); return 0; }
    fclose(f);
    strip(out);
    return *out != 0;
}

const char *srs_devices_source(void) { return plat_sysfs() ? "sysfs" : "SetupAPI"; }

int srs_devices(enum srs_bus bus, srs_dev **out)
{
    char base[512], dir[1024], **names;
    int n, i, cnt = 0;
    srs_dev *v = NULL;
    const char *sys = plat_sysfs();

    *out = NULL;
    if (!sys) return plat_setupapi_devices(bus, out);
    snprintf(base, sizeof base, "%s%cbus%c%s%cdevices", sys, plat_sep(), plat_sep(),
             bus == BUS_PCI ? "pci" : "usb", plat_sep());
    n = plat_listdir(base, &names);
    if (n < 0) return -1;
    for (i = 0; i < n; i++) {
        char a[64], b[64], c[64], d[64], key[40];
        join(dir, sizeof dir, base, names[i]);
        if (bus == BUS_PCI) {
            if (!read_attr(dir, "vendor", a, sizeof a) || !read_attr(dir, "device", b, sizeof b)) continue;
            snprintf(key, sizeof key, "%s:%s", a + 2, b + 2);
            if (read_attr(dir, "subsystem_vendor", c, sizeof c) && read_attr(dir, "subsystem_device", d, sizeof d) &&
                strcmp(c, "0x0000")) {
                size_t l = strlen(key);
                snprintf(key + l, sizeof key - l, ":%s:%s", c + 2, d + 2);
            }
            if (read_attr(dir, "revision", c, sizeof c)) {
                size_t l = strlen(key);
                snprintf(key + l, sizeof key - l, ":%s", c + 2);
            }
        } else {
            if (!read_attr(dir, "idVendor", a, sizeof a) || !read_attr(dir, "idProduct", b, sizeof b)) continue;
            snprintf(key, sizeof key, "%s:%s", a, b);
        }
        upcase(key);
        {
            srs_dev *nv = realloc(v, (size_t)(cnt + 1) * sizeof *nv);
            if (!nv) break;
            v = nv;
            snprintf(v[cnt].slot, sizeof v[cnt].slot, "%s", names[i]);
            snprintf(v[cnt].key, sizeof v[cnt].key, "%s", key);
            cnt++;
        }
    }
    plat_freelist(names, n);
    *out = v;
    return cnt;
}
