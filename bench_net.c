/* bench_net.c - Network benchmark: loopback TCP / UDP throughput and round trip time, and (only when
 * the user said yes) internet DNS, connect, HTTPS, download and upload tests (see bench_net.h).
 * Written for SRS.
 *
 * Loopback: sockets on 127.0.0.1 with threads on both ends; nothing leaves the computer.
 * Internet: libcurl is opened with dlopen, so SRS runs and builds without it (the option numbers
 * below are libcurl's stable ABI values; -DNET_CHECK_CURL_ABI checks them against curl.h).
 * Results never contain addresses: errors use curl_easy_strerror(), never curl's error buffer. */
#define _GNU_SOURCE
#include "bench_net.h"
#include "bench.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __linux__
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <sys/socket.h>
#include <unistd.h>

static const char *volatile g_net_stage = "";
static _Atomic uint64_t g_live_kbit;   /* kbit/s: no 64-bit float atomics needed */
static net_result g_res;
const char *net_stage(void) { return g_net_stage; }
double net_live_mbps(void) { return (double)atomic_load(&g_live_kbit) / 1e3; }
const net_result *net_last(void) { return &g_res; }

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void nap_ms(long ms) { struct timespec d = { ms / 1000, (ms % 1000) * 1000000L }; nanosleep(&d, NULL); }
static int stopped(volatile int *cancel) { return cancel && *cancel; }
static uint64_t xs64(uint64_t *s) { *s ^= *s << 13; *s ^= *s >> 7; *s ^= *s << 17; return *s; }
static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }

void net_cfg_default(net_cfg *c)
{
    memset(c, 0, sizeof *c);
    c->local = 1;
    c->streams = 4;
    c->secs = 5;
    c->samples = 10;
    c->max_mb = 250;
}

/* samples (ms, in the order taken) -> best, median, 99%, mean, jitter (mean change between neighbours) */
static void lat_stats(double *v, long n, long lost, net_lat *r)
{
    long i;
    double sum = 0, jit = 0;
    memset(r, 0, sizeof *r);
    r->lost = lost;
    r->n = n;
    if (n <= 0) return;
    for (i = 0; i < n; i++) { sum += v[i]; if (i) jit += v[i] > v[i - 1] ? v[i] - v[i - 1] : v[i - 1] - v[i]; }
    r->mean = sum / (double)n;
    r->jitter = n > 1 ? jit / (double)(n - 1) : 0;
    qsort(v, (size_t)n, sizeof *v, cmp_d);
    r->best = v[0];
    r->med = v[n / 2];
    r->p99 = v[(long)((double)n * 0.99) < n ? (long)((double)n * 0.99) : n - 1];
    r->done = 1;
}

/* ======================================================================
 * throughput watcher (loopback and internet): average from the first byte, best 0.5 s window
 * ====================================================================== */
static void watch(_Atomic uint64_t *bytes, _Atomic int *alive, double secs, double cap_mb, double first_wait,
                  volatile int *cancel, net_xfer *r)
{
    double t0 = now_s(), tf = 0, t = t0, wt = 0, peak = 0;
    uint64_t b = 0, wb = 0;
    memset(r, 0, sizeof *r);
    for (;;) {
        nap_ms(50);
        t = now_s();
        b = atomic_load(bytes);
        if (!tf && b) { tf = wt = t; wb = b; }
        if (tf && t - wt >= 0.5) {
            double v = (double)(b - wb) * 8 / (t - wt) / 1e6;
            if (v > peak) peak = v;
            atomic_store(&g_live_kbit, (uint64_t)(v * 1e3));
            wt = t;
            wb = b;
        }
        if (stopped(cancel) || !atomic_load(alive)) break;
        if (tf ? t - tf >= secs : t - t0 >= first_wait) break;
        if (cap_mb > 0 && (double)b >= cap_mb * 1e6) break;
    }
    atomic_store(&g_live_kbit, 0);
    if (!tf || t <= tf) return;
    r->mb = (double)b / 1e6;
    r->avg = (double)b * 8 / (t - tf) / 1e6;
    r->peak = peak > r->avg ? peak : r->avg;
    r->done = 1;
}

/* ======================================================================
 * loopback
 * ====================================================================== */
static void set_timeo(int fd, long ms)
{
    struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
}

/* a socket bound to 127.0.0.1 on a free port (listening for TCP); a = its address */
static int lo_socket(int type, struct sockaddr_in *a)
{
    socklen_t al = sizeof *a;
    int fd = socket(AF_INET, type | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    memset(a, 0, sizeof *a);
    a->sin_family = AF_INET;
    a->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd, (struct sockaddr *)a, sizeof *a) || getsockname(fd, (struct sockaddr *)a, &al) ||
        (type == SOCK_STREAM && listen(fd, NET_MAX_STREAMS))) { close(fd); return -1; }
    return fd;
}

/* n connected TCP pairs on loopback: cl[i] -> sv[i]; 0 ok */
static int lo_tcp_pairs(int n, int *cl, int *sv)
{
    struct sockaddr_in a;
    int ls = lo_socket(SOCK_STREAM, &a), i, ok = 0;
    for (i = 0; i < n; i++) cl[i] = sv[i] = -1;
    if (ls < 0) return -1;
    set_timeo(ls, 2000);
    for (i = 0; i < n; i++) {
        if ((cl[i] = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)) < 0 || connect(cl[i], (struct sockaddr *)&a, sizeof a) ||
            (sv[i] = accept4(ls, NULL, NULL, SOCK_CLOEXEC)) < 0) break;
        set_timeo(cl[i], 200);
        set_timeo(sv[i], 200);
        ok++;
    }
    close(ls);
    if (ok == n) return 0;
    for (i = 0; i < n; i++) { if (cl[i] >= 0) close(cl[i]); if (sv[i] >= 0) close(sv[i]); }
    return -1;
}

typedef struct {
    int fd, udp;
    _Atomic uint64_t *bytes;   /* receiver: bytes in */
    _Atomic int *stop, *alive;
    long pkts;                 /* UDP: packets sent / received */
    pthread_t t;
} lo_job;

static void *lo_send(void *arg)
{
    lo_job *j = arg;
    size_t len = j->udp ? 1472 : 128 * 1024;
    unsigned char *buf = malloc(len);
    uint64_t s = 0x9E3779B97F4A7C15ull;
    size_t i;
    if (buf) {
        for (i = 0; i < len; i++) buf[i] = (unsigned char)xs64(&s);
        while (!atomic_load(j->stop)) {
            ssize_t n = send(j->fd, buf, len, MSG_NOSIGNAL);
            if (n > 0) { j->pkts++; continue; }
            if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == ENOBUFS || errno == ECONNREFUSED)) continue;
            break;
        }
        free(buf);
    }
    if (!j->udp) shutdown(j->fd, SHUT_WR);   /* the receiver sees the end */
    return NULL;
}

static void *lo_recv(void *arg)
{
    lo_job *j = arg;
    size_t len = j->udp ? 2048 : 128 * 1024;
    unsigned char *buf = malloc(len);
    int idle = 0;
    if (buf)
        for (;;) {
            ssize_t n = recv(j->fd, buf, len, 0);
            if (n > 0) { atomic_fetch_add(j->bytes, (uint64_t)n); j->pkts++; idle = 0; continue; }
            if (n == 0 && !j->udp) break;
            if (n < 0 && errno != EINTR && errno != EAGAIN) break;
            if (atomic_load(j->stop) && ++idle >= (j->udp ? 1 : 10)) break;   /* UDP: drained once 200 ms stay quiet */
        }
    free(buf);
    atomic_fetch_sub(j->alive, 1);
    return NULL;
}

/* TCP with n streams, or UDP (one stream of 1472-byte packets) */
static void lo_throughput(int n, int udp, double secs, volatile int *cancel, net_xfer *r, double *loss, long *sent)
{
    int cl[NET_MAX_STREAMS], sv[NET_MAX_STREAMS], i;
    lo_job snd[NET_MAX_STREAMS], rcv[NET_MAX_STREAMS];
    _Atomic uint64_t bytes = 0;
    _Atomic int stop = 0, alive = 0;
    memset(r, 0, sizeof *r);
    if (udp) {
        struct sockaddr_in a, b;
        int bufsz = 4 << 20;
        n = 1;
        sv[0] = lo_socket(SOCK_DGRAM, &a);
        cl[0] = lo_socket(SOCK_DGRAM, &b);
        if (sv[0] < 0 || cl[0] < 0 || connect(cl[0], (struct sockaddr *)&a, sizeof a) || connect(sv[0], (struct sockaddr *)&b, sizeof b)) {
            if (sv[0] >= 0) close(sv[0]);
            if (cl[0] >= 0) close(cl[0]);
            return;
        }
        setsockopt(sv[0], SOL_SOCKET, SO_RCVBUF, &bufsz, sizeof bufsz);
        set_timeo(sv[0], 200);
        set_timeo(cl[0], 200);
    } else if (lo_tcp_pairs(n, cl, sv)) return;
    memset(snd, 0, sizeof snd);
    memset(rcv, 0, sizeof rcv);
    atomic_store(&alive, n);
    for (i = 0; i < n; i++) {
        rcv[i].fd = sv[i]; rcv[i].udp = udp; rcv[i].bytes = &bytes; rcv[i].stop = &stop; rcv[i].alive = &alive;
        snd[i].fd = cl[i]; snd[i].udp = udp; snd[i].stop = &stop; snd[i].alive = &alive;
        pthread_create(&rcv[i].t, NULL, lo_recv, &rcv[i]);
        pthread_create(&snd[i].t, NULL, lo_send, &snd[i]);
    }
    watch(&bytes, &alive, secs, 0, 2, cancel, r);
    atomic_store(&stop, 1);
    for (i = 0; i < n; i++) { pthread_join(snd[i].t, NULL); pthread_join(rcv[i].t, NULL); close(cl[i]); close(sv[i]); }
    if (udp && snd[0].pkts > 0) {
        *loss = 100.0 * (double)(snd[0].pkts - rcv[0].pkts) / (double)snd[0].pkts;
        if (*loss < 0) *loss = 0;
        *sent = snd[0].pkts;
    }
}

/* round trips: the echo thread sends back what it gets (UDP: 8-byte sequence numbers) */
typedef struct { int fd, udp; _Atomic int *stop; pthread_t t; } echo_job;

static void *lo_echo(void *arg)
{
    echo_job *j = arg;
    unsigned char b[64];
    while (!atomic_load(j->stop)) {
        ssize_t n = recv(j->fd, b, sizeof b, 0);
        if (n > 0) { if (send(j->fd, b, (size_t)n, MSG_NOSIGNAL) < 0 && !j->udp) break; continue; }
        if (n == 0 || (errno != EINTR && errno != EAGAIN)) break;
    }
    return NULL;
}

static void lo_rtt(int udp, double secs, volatile int *cancel, net_lat *r)
{
    int cfd = -1, sfd = -1, one = 1;
    long n = 0, lost = 0, cap = 500000;
    double *v = malloc((size_t)cap * sizeof *v), t0;
    echo_job e;
    _Atomic int stop = 0;
    uint64_t seq = 0;
    memset(r, 0, sizeof *r);
    if (!v) return;
    if (udp) {
        struct sockaddr_in a, b;
        sfd = lo_socket(SOCK_DGRAM, &a);
        cfd = lo_socket(SOCK_DGRAM, &b);
        if (sfd < 0 || cfd < 0 || connect(cfd, (struct sockaddr *)&a, sizeof a) || connect(sfd, (struct sockaddr *)&b, sizeof b)) goto out;
        set_timeo(sfd, 100);
        set_timeo(cfd, 200);
    } else {
        if (lo_tcp_pairs(1, &cfd, &sfd)) goto out;
        setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        setsockopt(sfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        set_timeo(cfd, 1000);
    }
    e.fd = sfd; e.udp = udp; e.stop = &stop;
    pthread_create(&e.t, NULL, lo_echo, &e);
    t0 = now_s();
    while (n < cap && now_s() - t0 < secs && !stopped(cancel)) {
        unsigned char b[8], g[64];
        double ts = now_s();
        ssize_t k;
        seq++;
        memcpy(b, &seq, 8);
        if (send(cfd, b, 8, MSG_NOSIGNAL) != 8) { lost++; if (!udp) break; continue; }
        for (;;) {   /* UDP: skip a late answer to an earlier packet */
            size_t got = 0;
            k = recv(cfd, g, udp ? sizeof g : 8, 0);
            if (!udp) while (k > 0 && (got += (size_t)k) < 8) k = recv(cfd, g + got, 8 - got, 0);
            if (k <= 0 || !udp || !memcmp(g, b, 8)) break;
        }
        if (k <= 0) { lost++; if (!udp) break; continue; }
        v[n++] = (now_s() - ts) * 1e3;
    }
    atomic_store(&stop, 1);
    shutdown(cfd, SHUT_RDWR);
    shutdown(sfd, SHUT_RDWR);
    pthread_join(e.t, NULL);
    lat_stats(v, n, lost, r);
out:
    if (cfd >= 0) close(cfd);
    if (sfd >= 0) close(sfd);
    free(v);
}

/* ======================================================================
 * where: default route interface, proxy, libcurl (reads /proc and /sys only)
 * ====================================================================== */
static void rd1(const char *path, char *out, size_t n)
{
    FILE *f = fopen(path, "r");
    out[0] = 0;
    if (!f) return;
    if (fgets(out, (int)n, f)) out[strcspn(out, "\n")] = 0;
    fclose(f);
}

static void iface_info(net_iface *ifc)
{
    char l[512], name[64] = "", p[160], v[64];
    unsigned long dst, flags;
    int metric, best = 1 << 30;
    FILE *f;
    memset(ifc, 0, sizeof *ifc);
    if ((f = fopen("/proc/net/route", "r"))) {   /* Iface Destination Gateway Flags RefCnt Use Metric ... */
        while (fgets(l, sizeof l, f)) {
            char nm[64];
            if (sscanf(l, "%63s %lx %*x %lx %*d %*d %d", nm, &dst, &flags, &metric) == 4 && dst == 0 && (flags & 1) && metric < best) {
                best = metric;
                snprintf(name, sizeof name, "%s", nm);
            }
        }
        fclose(f);
    }
    if (!name[0] && (f = fopen("/proc/net/ipv6_route", "r"))) {   /* dest plen src plen next metric ref use flags iface */
        while (fgets(l, sizeof l, f)) {
            char d[40], nm[64];
            int plen;
            if (sscanf(l, "%39s %x %*s %*x %*s %x %*x %*x %lx %63s", d, (unsigned *)&plen, (unsigned *)&metric, &flags, nm) == 5 &&
                plen == 0 && !strcmp(d, "00000000000000000000000000000000") && strcmp(nm, "lo") && (flags & 1) && metric < best) {
                best = metric;
                snprintf(name, sizeof name, "%s", nm);
            }
        }
        fclose(f);
    }
    if (!name[0]) return;
    /* names with a MAC address inside (enx001122334455, wlx...) are shortened */
    if (strlen(name) == 15 && (!strncmp(name, "enx", 3) || !strncmp(name, "wlx", 3))) snprintf(ifc->name, sizeof ifc->name, "%.3s...", name);
    else snprintf(ifc->name, sizeof ifc->name, "%.39s", name);
    snprintf(p, sizeof p, "/sys/class/net/%s/wireless", name);
    ifc->wifi = access(p, F_OK) == 0;
    snprintf(p, sizeof p, "/sys/class/net/%s/phy80211", name);
    if (access(p, F_OK) == 0) ifc->wifi = 1;
    snprintf(p, sizeof p, "/sys/class/net/%s/type", name);
    rd1(p, v, sizeof v);
    if (ifc->wifi) snprintf(ifc->kind, sizeof ifc->kind, "Wi-Fi");
    else {
        int type = atoi(v), usb;
        char real[1024];
        snprintf(p, sizeof p, "/sys/class/net/%s/bridge", name);
        if (access(p, F_OK) == 0) snprintf(ifc->kind, sizeof ifc->kind, "bridge");
        else if (snprintf(p, sizeof p, "/sys/class/net/%s/bonding", name), access(p, F_OK) == 0) snprintf(ifc->kind, sizeof ifc->kind, "bonded links");
        else if (type == 512) snprintf(ifc->kind, sizeof ifc->kind, "PPP (dial-up / DSL)");
        else if (type == 65534 || type == 768 || type == 776 || !strncmp(name, "wg", 2) || !strncmp(name, "tun", 3) ||
                 !strncmp(name, "tailscale", 9)) snprintf(ifc->kind, sizeof ifc->kind, "VPN / tunnel");
        else if (type == 1) {
            snprintf(p, sizeof p, "/sys/class/net/%s/device", name);
            usb = realpath(p, real) && strstr(real, "/usb");
            snprintf(ifc->kind, sizeof ifc->kind, usb ? "wired (USB Ethernet)" : "wired (Ethernet)");
        } else snprintf(ifc->kind, sizeof ifc->kind, "other (type %d)", type);
    }
    snprintf(p, sizeof p, "/sys/class/net/%s/speed", name);
    rd1(p, v, sizeof v);
    ifc->speed_mbps = atol(v) > 0 ? atol(v) : 0;
    snprintf(p, sizeof p, "/sys/class/net/%s/duplex", name);
    rd1(p, v, sizeof v);
    if (!strcmp(v, "full") || !strcmp(v, "half")) snprintf(ifc->duplex, sizeof ifc->duplex, "%.4s", v);
    snprintf(p, sizeof p, "/sys/class/net/%s/mtu", name);
    rd1(p, v, sizeof v);
    ifc->mtu = atoi(v);
    if (ifc->wifi && (f = fopen("/proc/net/wireless", "r"))) {   /* "wlan0: 0000   70.  -40.  -256 ..." */
        while (fgets(l, sizeof l, f)) {
            char nm[64];
            double q, lev;
            if (sscanf(l, " %63[^:]: %*x %lf %lf", nm, &q, &lev) == 3 && !strcmp(nm, name) && lev < 0) ifc->signal_dbm = (int)lev;
        }
        fclose(f);
    }
}

/* host name of an https URL; 0 = not an https URL with a plain host name */
static int url_host(const char *url, char *host, size_t hn)
{
    size_t i = 0;
    const char *s;
    if (strncmp(url, "https://", 8)) return 0;
    for (s = url + 8; *s && *s != '/' && *s != ':' && *s != '?'; s++) {
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '.' || *s == '-')) return 0;
        if (i + 1 < hn) host[i++] = *s;
    }
    host[i] = 0;
    return i > 0;
}

static void add_host(net_where *w, const char *h, int port)
{
    int i;
    char c[256];
    size_t k;
    snprintf(c, sizeof c, "%s", h);
    for (k = 0; c[k]; k++)
        if (!((c[k] >= 'a' && c[k] <= 'z') || (c[k] >= 'A' && c[k] <= 'Z') || (c[k] >= '0' && c[k] <= '9') || c[k] == '.' || c[k] == '-')) return;
    if (!c[0] || w->nhosts >= NET_MAX_HOSTS || port <= 0 || port > 65535) return;
    for (i = 0; i < w->nhosts; i++) if (!strcmp(w->hosts[i], c) && w->ports[i] == port) return;
    memcpy(w->hosts[w->nhosts], c, sizeof c);
    w->ports[w->nhosts++] = port;
}

/* ---- libcurl through dlopen ---- */
typedef long long net_off;   /* curl_off_t (64-bit) */
enum {
    CO_WRITEDATA = 10001, CO_URL = 10002, CO_READDATA = 10009, CO_WRITEFUNCTION = 20011, CO_READFUNCTION = 20012,
    CO_USERAGENT = 10018, CO_LOW_SPEED_LIMIT = 19, CO_LOW_SPEED_TIME = 20, CO_HTTPHEADER = 10023, CO_NOPROGRESS = 43,
    CO_POST = 47, CO_XFERINFODATA = 10057, CO_FRESH_CONNECT = 74, CO_FORBID_REUSE = 75, CO_NOSIGNAL = 99, CO_IPRESOLVE = 113,
    CO_POSTFIELDSIZE_LARGE = 30120, CO_TIMEOUT_MS = 155, CO_CONNECTTIMEOUT_MS = 156, CO_XFERINFOFUNCTION = 20219,
    CO_PROTOCOLS_STR = 10318,
    CI_RESPONSE_CODE = 0x200000 + 2, CI_PRIMARY_IP = 0x100000 + 32, CI_NAMELOOKUP_TIME_T = 0x600000 + 51,
    CI_CONNECT_TIME_T = 0x600000 + 52, CI_STARTTRANSFER_TIME_T = 0x600000 + 54, CI_APPCONNECT_TIME_T = 0x600000 + 56,
    CE_OK = 0, CE_ABORTED_BY_CALLBACK = 42, CG_DEFAULT = 3
};
#if defined(NET_CHECK_CURL_ABI) && __has_include(<curl/curl.h>)
#include <curl/curl.h>
_Static_assert((int)CO_WRITEDATA == (int)CURLOPT_WRITEDATA && (int)CO_URL == (int)CURLOPT_URL && (int)CO_READDATA == (int)CURLOPT_READDATA &&
               (int)CO_WRITEFUNCTION == (int)CURLOPT_WRITEFUNCTION && (int)CO_READFUNCTION == (int)CURLOPT_READFUNCTION && (int)CO_USERAGENT == (int)CURLOPT_USERAGENT &&
               (int)CO_LOW_SPEED_LIMIT == (int)CURLOPT_LOW_SPEED_LIMIT && (int)CO_LOW_SPEED_TIME == (int)CURLOPT_LOW_SPEED_TIME &&
               (int)CO_HTTPHEADER == (int)CURLOPT_HTTPHEADER && (int)CO_NOPROGRESS == (int)CURLOPT_NOPROGRESS && (int)CO_POST == (int)CURLOPT_POST &&
               (int)CO_XFERINFODATA == (int)CURLOPT_XFERINFODATA && (int)CO_FRESH_CONNECT == (int)CURLOPT_FRESH_CONNECT && (int)CO_FORBID_REUSE == (int)CURLOPT_FORBID_REUSE &&
               (int)CO_NOSIGNAL == (int)CURLOPT_NOSIGNAL && (int)CO_IPRESOLVE == (int)CURLOPT_IPRESOLVE && (int)CO_POSTFIELDSIZE_LARGE == (int)CURLOPT_POSTFIELDSIZE_LARGE &&
               (int)CO_TIMEOUT_MS == (int)CURLOPT_TIMEOUT_MS && (int)CO_CONNECTTIMEOUT_MS == (int)CURLOPT_CONNECTTIMEOUT_MS &&
               (int)CO_XFERINFOFUNCTION == (int)CURLOPT_XFERINFOFUNCTION && (int)CO_PROTOCOLS_STR == (int)CURLOPT_PROTOCOLS_STR, "CURLOPT values");
_Static_assert((int)CI_RESPONSE_CODE == (int)CURLINFO_RESPONSE_CODE && (int)CI_PRIMARY_IP == (int)CURLINFO_PRIMARY_IP &&
               (int)CI_NAMELOOKUP_TIME_T == (int)CURLINFO_NAMELOOKUP_TIME_T && (int)CI_CONNECT_TIME_T == (int)CURLINFO_CONNECT_TIME_T &&
               (int)CI_STARTTRANSFER_TIME_T == (int)CURLINFO_STARTTRANSFER_TIME_T && (int)CI_APPCONNECT_TIME_T == (int)CURLINFO_APPCONNECT_TIME_T, "CURLINFO values");
_Static_assert((int)CE_ABORTED_BY_CALLBACK == (int)CURLE_ABORTED_BY_CALLBACK && (int)CG_DEFAULT == (int)CURL_GLOBAL_DEFAULT && sizeof(curl_off_t) == sizeof(net_off),
               "curl codes");
#endif

static struct {
    int ok;
    char ver[64];
    int (*global_init)(long);
    void *(*easy_init)(void);
    int (*easy_setopt)(void *, int, ...);
    int (*easy_perform)(void *);
    int (*easy_getinfo)(void *, int, ...);
    void (*easy_cleanup)(void *);
    const char *(*easy_strerror)(int);
    char *(*version)(void);
    void *(*slist_append)(void *, const char *);
    void (*slist_free_all)(void *);
} cu;
static pthread_once_t cu_once = PTHREAD_ONCE_INIT;

static void cu_load(void)
{
    static const char *const libs[] = { "libcurl.so.4", "libcurl.so", "libcurl-gnutls.so.4", NULL };
    void *h = NULL, *p;
    int i, miss = 0;
    for (i = 0; libs[i] && !h; i++) h = dlopen(libs[i], RTLD_NOW | RTLD_LOCAL);
    if (!h) return;
#define CU_SYM(field, name) (p = dlsym(h, name), p ? (void)memcpy(&cu.field, &p, sizeof p) : (void)(miss = 1))
    CU_SYM(global_init, "curl_global_init");
    CU_SYM(easy_init, "curl_easy_init");
    CU_SYM(easy_setopt, "curl_easy_setopt");
    CU_SYM(easy_perform, "curl_easy_perform");
    CU_SYM(easy_getinfo, "curl_easy_getinfo");
    CU_SYM(easy_cleanup, "curl_easy_cleanup");
    CU_SYM(easy_strerror, "curl_easy_strerror");
    CU_SYM(version, "curl_version");
    CU_SYM(slist_append, "curl_slist_append");
    CU_SYM(slist_free_all, "curl_slist_free_all");
#undef CU_SYM
    if (miss || cu.global_init(CG_DEFAULT) != CE_OK) { dlclose(h); memset(&cu, 0, sizeof cu); return; }   /* kept loaded from here on */
    {
        const char *v = cu.version(), *sl = v ? strchr(v, '/') : NULL;   /* "libcurl/8.11.1 OpenSSL/3.4 ..." */
        snprintf(cu.ver, sizeof cu.ver, "libcurl %.*s", sl ? (int)strcspn(sl + 1, " ") : 0, sl ? sl + 1 : "");
    }
    cu.ok = 1;
}

int net_curl_available(char *ver, size_t n)
{
    pthread_once(&cu_once, cu_load);
    if (ver && n) snprintf(ver, n, "%s", cu.ok ? cu.ver : "");
    return cu.ok;
}

static const char *server_of(const net_cfg *c) { return c->server && c->server[0] ? c->server : NET_DEFAULT_SERVER; }

int net_probe(const net_cfg *c, net_where *w)
{
    static const char *const pv[] = { "https_proxy", "HTTPS_PROXY", "all_proxy", "ALL_PROXY", NULL };
    const char *s = server_of(c);
    int i;
    double secs = c->secs > 0 ? c->secs : 5, rtt = secs < 1 ? secs : 1, per;
    memset(w, 0, sizeof *w);
    snprintf(w->server, sizeof w->server, "%s", s);
    while (w->server[0] && w->server[strlen(w->server) - 1] == '/') w->server[strlen(w->server) - 1] = 0;
    if (!url_host(w->server, w->server_host, sizeof w->server_host))
        snprintf(w->problem, sizeof w->problem, "the speed test server must be an https:// address with a plain host name");
    else add_host(w, w->server_host, 443);
    if (c->hosts && c->hosts[0]) {
        char list[2048], *t, *save = NULL;
        snprintf(list, sizeof list, "%s", c->hosts);
        for (t = strtok_r(list, ", ", &save); t; t = strtok_r(NULL, ", ", &save)) {
            char *colon = strchr(t, ':');
            int port = 443;
            if (colon) { *colon = 0; port = atoi(colon + 1); }
            add_host(w, t, port);
        }
    }
    net_curl_available(w->curl, sizeof w->curl);
    for (i = 0; pv[i]; i++) if (getenv(pv[i]) && getenv(pv[i])[0]) { snprintf(w->proxy, sizeof w->proxy, "%s", pv[i]); break; }
    iface_info(&w->ifc);
    if (!w->problem[0] && !w->ifc.name[0]) snprintf(w->problem, sizeof w->problem, "this computer has no network connection (no default route)");
    /* 4 transfer tests, each stops at secs or max_mb */
    per = c->max_mb > 0 ? c->max_mb : 1e9;
    w->est_mb = w->curl[0] ? 4 * (c->max_mb > 0 ? c->max_mb : secs * 1250) : 0;   /* no cap: up to 10 Gbit/s */
    w->est_mb_100 = w->curl[0] ? 4 * (secs * 12.5 < per ? secs * 12.5 : per) : 0;
    w->est_secs = (c->local ? 3 * secs + 2 * rtt : 0) +
                  (c->internet ? 2 + w->nhosts * (c->samples > 0 ? c->samples : 10) * 0.08 + (w->curl[0] ? 1.5 + 4 * (secs + 1) : 0) : 0);
    return !w->problem[0];
}

void net_ask_text(const net_cfg *c, const net_where *w, char *b, size_t n)
{
    char hosts[1200] = "";
    size_t k = 0;
    int i, st = c->streams > 0 ? c->streams : 4;
    double secs = c->secs > 0 ? c->secs : 5, isecs = 2 + w->nhosts * (c->samples > 0 ? c->samples : 10) * 0.08 + (w->curl[0] ? 1.5 + 4 * (secs + 1) : 0);
    for (i = 0; i < w->nhosts && k < sizeof hosts; i++)
        k += (size_t)snprintf(hosts + k, sizeof hosts - k, "%s%s", i ? ", " : "", w->hosts[i]);
    if (w->problem[0]) { snprintf(b, n, "The internet test cannot run: %s.", w->problem); return; }
    if (w->curl[0])
        snprintf(b, n,
                 "The internet test connects to %s (%s).\n"
                 "It measures: DNS lookup time (including one made-up name under %s), connect time (%d connections to %s), "
                 "HTTPS response time, then download and upload speed with 1 and %d parallel streams, %.0f s each.\n"
                 "Data use: up to about %.0f MB in all (each of the 4 transfer tests stops after %.0f s or %.0f MB); "
                 "on a 100 Mbit/s line about %.0f MB. Time: about %.0f s.\n"
                 "The server sees your public IP address, as with any web page; SRS does not show or save it.%s%s%s\n"
                 "On a metered or mobile connection, choose No.",
                 w->server_host, w->server, w->server_host, c->samples > 0 ? c->samples : 10, hosts, st, secs, w->est_mb, secs,
                 c->max_mb > 0 ? c->max_mb : secs * 1250, w->est_mb_100, isecs,
                 w->proxy[0] ? " The transfers use the proxy set in " : "", w->proxy, w->proxy[0] ? "." : "");
    else
        snprintf(b, n,
                 "The internet test contacts %s.\n"
                 "libcurl is not installed, so only DNS lookup time (including one made-up name under %s) and connect time "
                 "(%d connections) run; no download or upload. Data use: a few KB. Time: about %.0f s.",
                 hosts, w->server_host, c->samples > 0 ? c->samples : 10, isecs);
}

/* ======================================================================
 * internet: DNS, TCP connect, HTTPS timings, transfers
 * ====================================================================== */
static double dns_ms(const char *host, int ipv, int *ok)
{
    struct addrinfo hi, *ai = NULL;
    double t = now_s();
    int rc;
    memset(&hi, 0, sizeof hi);
    hi.ai_family = ipv == 4 ? AF_INET : ipv == 6 ? AF_INET6 : AF_UNSPEC;
    hi.ai_socktype = SOCK_STREAM;
    rc = getaddrinfo(host, "443", &hi, &ai);
    t = (now_s() - t) * 1e3;
    if (ai) freeaddrinfo(ai);
    *ok = rc == 0 || rc == EAI_NONAME || rc == EAI_NODATA;   /* "no such name" is an answer too */
    return t;
}

static void dns_tests(const net_where *w, int ipv, volatile int *cancel, net_result *r)
{
    double v[5];
    int ok, i, n = 0;
    char rnd[320];
    uint64_t s = (uint64_t)now_s() * 1000003u ^ ((uint64_t)getpid() << 20);
    r->dns_first = dns_ms(w->server_host, ipv, &ok);
    if (!ok) r->dns_first = -1;
    for (i = 0; i < 5 && !stopped(cancel); i++) { v[n] = dns_ms(w->server_host, ipv, &ok); if (ok) n++; }
    if (n) { qsort(v, (size_t)n, sizeof *v, cmp_d); r->dns_repeat = v[n / 2]; } else r->dns_repeat = -1;
    if (stopped(cancel)) return;
    xs64(&s);
    snprintf(rnd, sizeof rnd, "srs-%010llx.%s", (unsigned long long)(xs64(&s) & 0xffffffffffull), w->server_host);
    r->dns_random = dns_ms(rnd, ipv, &ok);
    if (!ok) r->dns_random = -1;
}

/* time one TCP connect (ms), < 0 = failed */
static double connect_ms(const struct addrinfo *a)
{
    int fd = socket(a->ai_family, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0), err = 0;
    socklen_t el = sizeof err;
    double t = now_s();
    struct pollfd p;
    if (fd < 0) return -1;
    if (connect(fd, a->ai_addr, a->ai_addrlen) && errno != EINPROGRESS) { close(fd); return -1; }
    p.fd = fd;
    p.events = POLLOUT;
    if (poll(&p, 1, 3000) != 1 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el) || err) { close(fd); return -1; }
    t = (now_s() - t) * 1e3;
    close(fd);
    return t;
}

static void connect_tests(const net_where *w, int ipv, int samples, volatile int *cancel, net_result *r)
{
    int h, i;
    double v[100];
    for (h = 0; h < w->nhosts && !stopped(cancel); h++) {
        struct addrinfo hi, *ai = NULL;
        char port[8];
        long n = 0, lost = 0;
        memset(&hi, 0, sizeof hi);
        hi.ai_family = ipv == 4 ? AF_INET : ipv == 6 ? AF_INET6 : AF_UNSPEC;
        hi.ai_socktype = SOCK_STREAM;
        snprintf(port, sizeof port, "%d", w->ports[h]);
        if (getaddrinfo(w->hosts[h], port, &hi, &ai) || !ai) { r->conn[h].lost = samples; continue; }
        if (!r->ipver) r->ipver = ai->ai_family == AF_INET6 ? 6 : 4;
        for (i = 0; i < samples && !stopped(cancel); i++) {
            double t = connect_ms(ai);
            if (t >= 0) v[n++] = t; else lost++;
            nap_ms(20);
        }
        freeaddrinfo(ai);
        lat_stats(v, n, lost, &r->conn[h]);
    }
}

typedef struct xfer_ctx xfer_ctx;
typedef struct { xfer_ctx *x; net_off last, sent; int err; long code; pthread_t t; } stream;
struct xfer_ctx {
    int up, ipv;
    char url[600];
    const unsigned char *body;   /* upload data, 1 MiB, cycled */
    _Atomic uint64_t bytes;
    _Atomic int stop, alive;
};
#define NET_CHUNK 25000000LL     /* bytes per request; a stream repeats requests until time is up */

static size_t cu_sink(char *p, size_t sz, size_t nm, void *u) { (void)p; (void)u; return sz * nm; }

static size_t cu_source(char *p, size_t sz, size_t nm, void *u)
{
    stream *s = u;
    size_t want = sz * nm, off = (size_t)(s->sent % (1 << 20)), k;
    if ((net_off)want > NET_CHUNK - s->sent) want = (size_t)(NET_CHUNK - s->sent);
    if (want > (size_t)(1 << 20) - off) want = (size_t)(1 << 20) - off;
    for (k = 0; k < want; k++) p[k] = (char)s->x->body[off + k];
    s->sent += (net_off)want;
    return want;
}

static int cu_progress(void *u, net_off dlt, net_off dln, net_off ult, net_off uln)
{
    stream *s = u;
    net_off cur = s->x->up ? uln : dln;
    (void)dlt; (void)ult;
    if (cur > s->last) { atomic_fetch_add(&s->x->bytes, (uint64_t)(cur - s->last)); s->last = cur; }
    return atomic_load(&s->x->stop) ? 1 : 0;
}

static void cu_common(void *e, int ipv)
{
    cu.easy_setopt(e, CO_USERAGENT, "srs-sysinfo");
    cu.easy_setopt(e, CO_NOSIGNAL, 1L);
    cu.easy_setopt(e, CO_PROTOCOLS_STR, "https");   /* older libcurl ignores it (unknown option) */
    cu.easy_setopt(e, CO_CONNECTTIMEOUT_MS, 10000L);
    cu.easy_setopt(e, CO_LOW_SPEED_LIMIT, 1L);
    cu.easy_setopt(e, CO_LOW_SPEED_TIME, 15L);
    cu.easy_setopt(e, CO_WRITEFUNCTION, cu_sink);
    if (ipv == 4 || ipv == 6) cu.easy_setopt(e, CO_IPRESOLVE, ipv == 4 ? 1L : 2L);
}

/* a dying peer must give an error, not kill the program with SIGPIPE (signals stay blocked in workers) */
static void block_sigpipe(sigset_t *old)
{
    sigset_t sp;
    sigemptyset(&sp);
    sigaddset(&sp, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &sp, old);
}

static void unblock_sigpipe(const sigset_t *old)
{
    sigset_t sp;
    struct timespec zero = { 0, 0 };
    sigemptyset(&sp);
    sigaddset(&sp, SIGPIPE);
    while (sigtimedwait(&sp, NULL, &zero) > 0) ;
    pthread_sigmask(SIG_SETMASK, old, NULL);
}

static void *stream_main(void *arg)
{
    stream *s = arg;
    xfer_ctx *x = s->x;
    void *e = cu.easy_init(), *hdr = NULL;
    sigset_t old;
    block_sigpipe(&old);
    if (!e) { s->err = -1; goto out; }
    cu_common(e, x->ipv);
    cu.easy_setopt(e, CO_URL, x->url);
    cu.easy_setopt(e, CO_NOPROGRESS, 0L);
    cu.easy_setopt(e, CO_XFERINFOFUNCTION, cu_progress);
    cu.easy_setopt(e, CO_XFERINFODATA, s);
    if (x->up) {
        hdr = cu.slist_append(NULL, "Expect:");   /* no 100-continue round trip */
        cu.easy_setopt(e, CO_HTTPHEADER, hdr);
        cu.easy_setopt(e, CO_POST, 1L);
        cu.easy_setopt(e, CO_POSTFIELDSIZE_LARGE, (net_off)NET_CHUNK);
        cu.easy_setopt(e, CO_READFUNCTION, cu_source);
        cu.easy_setopt(e, CO_READDATA, s);
    }
    while (!atomic_load(&x->stop)) {
        int rc;
        s->last = s->sent = 0;
        rc = cu.easy_perform(e);
        if (atomic_load(&x->stop)) break;
        if (rc != CE_OK) { s->err = rc; break; }
        cu.easy_getinfo(e, CI_RESPONSE_CODE, &s->code);
        if (s->code != 200) { s->err = -2; break; }
    }
    cu.easy_cleanup(e);
    if (hdr) cu.slist_free_all(hdr);
out:
    unblock_sigpipe(&old);
    atomic_fetch_sub(&x->alive, 1);
    return NULL;
}

static void xfer_test(const net_where *w, int up, int n, double secs, double cap_mb, int ipv, const unsigned char *body,
                      volatile int *cancel, net_xfer *r, char *err, size_t en)
{
    xfer_ctx *x = calloc(1, sizeof *x);
    stream st[NET_MAX_STREAMS];
    int i, started = 0;
    memset(r, 0, sizeof *r);
    if (!x) return;
    x->up = up;
    x->ipv = ipv;
    x->body = body;
    if (up) snprintf(x->url, sizeof x->url, "%s/__up", w->server);
    else snprintf(x->url, sizeof x->url, "%s/__down?bytes=%lld", w->server, NET_CHUNK);
    memset(st, 0, sizeof st);
    for (i = 0; i < n; i++) {
        st[i].x = x;
        atomic_fetch_add(&x->alive, 1);
        if (pthread_create(&st[i].t, NULL, stream_main, &st[i])) { atomic_fetch_sub(&x->alive, 1); break; }
        started++;
    }
    watch(&x->bytes, &x->alive, secs, cap_mb, secs + 15, cancel, r);
    atomic_store(&x->stop, 1);
    for (i = 0; i < started; i++) pthread_join(st[i].t, NULL);
    for (i = 0; i < started && err && !err[0]; i++)
        if (st[i].err)
            snprintf(err, en, "%s: %s", up ? "upload" : "download",
                     st[i].err == -2 ? "the server did not answer 200 OK" : st[i].err == -1 ? "libcurl could not start" : cu.easy_strerror(st[i].err));
    if (r->done && err) err[0] = 0;   /* one stream failing while others moved data still gives a result */
    free(x);
}

/* HTTPS: TCP connect, TLS handshake and server reply time of a 0-byte request (medians of 3, new connection each) */
static void https_tests(const net_where *w, int ipv, volatile int *cancel, net_result *r, char *err, size_t en)
{
    double c[3], t[3], s[3];
    int i, n = 0;
    char url[600];
    sigset_t old;
    snprintf(url, sizeof url, "%s/__down?bytes=0", w->server);
    block_sigpipe(&old);
    for (i = 0; i < 3 && !stopped(cancel); i++) {
        void *e = cu.easy_init();
        net_off nl = 0, co = 0, ap = 0, stt = 0;
        long code = 0;
        char *ip = NULL;
        int rc;
        if (!e) break;
        cu_common(e, ipv);
        cu.easy_setopt(e, CO_URL, url);
        cu.easy_setopt(e, CO_TIMEOUT_MS, 15000L);
        cu.easy_setopt(e, CO_FRESH_CONNECT, 1L);
        cu.easy_setopt(e, CO_FORBID_REUSE, 1L);
        rc = cu.easy_perform(e);
        if (rc == CE_OK) {
            cu.easy_getinfo(e, CI_RESPONSE_CODE, &code);
            cu.easy_getinfo(e, CI_NAMELOOKUP_TIME_T, &nl);
            cu.easy_getinfo(e, CI_CONNECT_TIME_T, &co);
            cu.easy_getinfo(e, CI_APPCONNECT_TIME_T, &ap);
            cu.easy_getinfo(e, CI_STARTTRANSFER_TIME_T, &stt);
            if (cu.easy_getinfo(e, CI_PRIMARY_IP, &ip) == CE_OK && ip && ip[0] && !r->ipver) r->ipver = strchr(ip, ':') ? 6 : 4;   /* only the version is kept */
        }
        cu.easy_cleanup(e);
        if (rc != CE_OK || code != 200) {
            if (err && !err[0]) snprintf(err, en, "HTTPS: %s", rc != CE_OK ? cu.easy_strerror(rc) : "the server did not answer 200 OK");
            continue;
        }
        c[n] = (double)(co - nl) / 1e3;
        t[n] = (double)(ap - co) / 1e3;
        s[n] = (double)(stt - ap) / 1e3;
        n++;
    }
    unblock_sigpipe(&old);
    if (!n) return;
    qsort(c, (size_t)n, sizeof *c, cmp_d);
    qsort(t, (size_t)n, sizeof *t, cmp_d);
    qsort(s, (size_t)n, sizeof *s, cmp_d);
    r->https_connect = c[n / 2];
    r->https_tls = t[n / 2];
    r->https_reply = s[n / 2];
    r->https_done = 1;
    if (err) err[0] = 0;
}

/* ======================================================================
 * the benchmark
 * ====================================================================== */
typedef struct {
    net_cfg c;
    net_where w;
    net_result *r;
    int cancelled, ran_local, ran_net;
    char err[200], when[32];
} netctx;

static void fmt_mbit(char *b, size_t n, const net_xfer *x, int peak)
{
    double v = peak ? x->peak : x->avg;
    if (!x->done) snprintf(b, n, "-");
    else snprintf(b, n, v >= 100 ? "%.0f Mbit/s" : "%.1f Mbit/s", v);
}

static void fmt_ms(char *b, size_t n, double v)
{
    if (v < 0) snprintf(b, n, "failed");
    else if (v == 0) snprintf(b, n, "-");
    else if (v < 1) snprintf(b, n, "%.1f µs", v * 1e3);   /* loopback: microseconds */
    else snprintf(b, n, v < 10 ? "%.2f ms" : "%.1f ms", v);
}

static void xfer_row(srs_out *o, const char *name, const net_xfer *x, const char *extra)
{
    char a[32], p[32], d[96];
    fmt_mbit(a, sizeof a, x, 0);
    fmt_mbit(p, sizeof p, x, 1);
    if (x->done) snprintf(d, sizeof d, "%.0f MB moved%s", x->mb, extra ? extra : "");
    else snprintf(d, sizeof d, "%s", extra && extra[0] ? extra + 2 : "not measured");
    so_row(o, 4, (const char *const[]){ name, a, p, d }, (const int[]){ C_LABEL, C_GREEN, C_BLACK, C_GRAY });
}

static void lat_row(srs_out *o, const char *name, const net_lat *l)
{
    char m[32], b[32], p[32], j[32], d[96];
    fmt_ms(j, sizeof j, l->jitter > 0 ? l->jitter : 1e-9);
    fmt_ms(m, sizeof m, l->done ? l->med : 0);
    fmt_ms(b, sizeof b, l->done ? l->best : 0);
    fmt_ms(p, sizeof p, l->done ? l->p99 : 0);
    if (l->done && l->lost) snprintf(d, sizeof d, "jitter %s, %ld measured, %ld lost", j, l->n, l->lost);
    else if (l->done) snprintf(d, sizeof d, "jitter %s, %ld measured", j, l->n);
    else snprintf(d, sizeof d, l->lost ? "no answer" : "not measured");
    so_row(o, 5, (const char *const[]){ name, m, b, p, d }, (const int[]){ C_LABEL, C_GREEN, C_BLACK, C_BLACK, C_GRAY });
}

static void net_render(srs_out *o, void *c)
{
    static const int tabs[] = { 230, 340, 450, 560 }, tabs5[] = { 230, 330, 430, 530 }, tabs1[] = { 230 };
    netctx *x = c;
    net_result *r = x->r;
    char t[700], name[300];
    int i;
    /* the shared header names the computer; this one leaves the host name out (results get published) */
    snprintf(t, sizeof t, "srs-gui - Network benchmark (loopback and internet speed and latency) - %s", sys_os_name());
    so_t(o, C_VALUE, t);
    so_par(o);
    so_par(o);
    if (x->ran_local) {
        so_b(o, C_TEAL, "On this computer (loopback: nothing leaves the computer)");
        so_par(o);
        so_tabs(o, tabs, 4);
        so_row(o, 4, (const char *const[]){ "Throughput", "Average", "Peak", "" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL });
        xfer_row(o, "TCP, 1 stream", &r->tcp1, NULL);
        snprintf(name, sizeof name, "TCP, %d streams", x->c.streams);
        xfer_row(o, name, &r->tcpn, NULL);
        snprintf(t, sizeof t, ", %.2f%% of %ld packets lost", r->udp_loss, r->udp_sent);
        xfer_row(o, "UDP, 1472-byte packets", &r->udp, r->udp_sent ? t : NULL);
        so_par(o);
        so_tabs(o, tabs5, 4);
        so_row(o, 5, (const char *const[]){ "Round trip", "Median", "Best", "99% under", "" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
        lat_row(o, "TCP round trip", &r->tcp_rtt);
        lat_row(o, "UDP round trip", &r->udp_rtt);
        so_par(o);
    }
    if (x->ran_net) {
        char a[32], b[32], d[32];
        snprintf(t, sizeof t, "Internet (speed test server: %s)", x->w.server_host);
        so_b(o, C_TEAL, t);
        so_par(o);
        so_tabs(o, tabs1, 1);
        fmt_ms(a, sizeof a, r->dns_first);
        fmt_ms(b, sizeof b, r->dns_repeat);
        fmt_ms(d, sizeof d, r->dns_random);
        so_kvf(o, "DNS lookup", C_VALUE, "first %s, repeated %s (usually cached), made-up name %s (not cached)", a, b, d);
        if (r->https_done)
            so_kvf(o, "HTTPS request", C_VALUE, "TCP connect %.1f ms, TLS handshake %.1f ms, server reply %.1f ms (medians of 3)",
                   r->https_connect, r->https_tls, r->https_reply);
        else so_kv(o, "HTTPS request", C_GRAY, x->w.curl[0] ? "not measured" : "not available (libcurl is not installed)");
        so_par(o);
        so_tabs(o, tabs5, 4);
        so_row(o, 5, (const char *const[]){ "Connect time (TCP)", "Median", "Best", "99% under", "" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
        for (i = 0; i < x->w.nhosts; i++) {
            if (x->w.ports[i] != 443) snprintf(name, sizeof name, "%.240s port %d", x->w.hosts[i], x->w.ports[i]);
            else snprintf(name, sizeof name, "%s", x->w.hosts[i]);
            lat_row(o, name, &r->conn[i]);
        }
        so_par(o);
        so_tabs(o, tabs, 4);
        so_row(o, 4, (const char *const[]){ "Speed (HTTPS)", "Average", "Peak", "" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL });
        if (x->w.curl[0]) {
            xfer_row(o, "Download, 1 stream", &r->down1, NULL);
            snprintf(name, sizeof name, "Download, %d streams", x->c.streams);
            xfer_row(o, name, &r->downn, NULL);
            xfer_row(o, "Upload, 1 stream", &r->up1, NULL);
            snprintf(name, sizeof name, "Upload, %d streams", x->c.streams);
            xfer_row(o, name, &r->upn, NULL);
        } else so_row(o, 2, (const char *const[]){ "Download / upload", "not available (libcurl is not installed)" }, (const int[]){ C_LABEL, C_GRAY });
        so_par(o);
        so_tabs(o, tabs1, 1);
        if (x->w.ifc.name[0]) {
            char sp[64] = "", sig[48] = "";
            if (x->w.ifc.speed_mbps) snprintf(sp, sizeof sp, ", link %ld Mbit/s%s%s", x->w.ifc.speed_mbps, x->w.ifc.duplex[0] ? " " : "",
                                              x->w.ifc.duplex[0] ? (x->w.ifc.duplex[0] == 'f' ? "full duplex" : "half duplex") : "");
            if (x->w.ifc.signal_dbm) snprintf(sig, sizeof sig, ", signal %d dBm", x->w.ifc.signal_dbm);
            so_kvf(o, "Connection", C_VALUE, "%s (%s)%s%s, MTU %d bytes%s", x->w.ifc.kind, x->w.ifc.name, sp, sig, x->w.ifc.mtu,
                   r->ipver ? (r->ipver == 6 ? ", IPv6" : ", IPv4") : "");
        }
        so_kv(o, "libcurl", C_VALUE, x->w.curl[0] ? x->w.curl : "not installed (libcurl.so.4)");
        if (x->w.proxy[0]) so_kvf(o, "Proxy", C_ORANGE, "%s is set: the HTTPS tests went through the proxy", x->w.proxy);
    } else {
        so_tabs(o, tabs1, 1);
        so_kv(o, "Internet", C_GRAY, "not run (it contacts a speed test server and asks first)");
    }
    if (x->cancelled) so_kv(o, "Status", C_ORANGE, "Stopped before the end - the empty rows were not measured");
    if (x->err[0]) so_kv(o, "Error", C_RED, x->err);
    so_kvf(o, "Settings", C_VALUE, "%.1f s per throughput test, %d streams%s", x->c.secs, x->c.streams,
           x->c.ipv == 4 ? ", IPv4 only" : x->c.ipv == 6 ? ", IPv6 only" : "");
    so_kv(o, "Run", C_GRAY, x->when);
    so_kv(o, "Build", C_GRAY, bench_build_tag());
    so_note(o, "1 Mbit/s = 1,000,000 bits per second; 1 MB = 1,000,000 bytes; 1 ms = 1,000 µs (microseconds). Average counts from the first byte that arrived; "
               "peak = the best half second. Loopback measures the operating system and CPU, not the network card. "
               "Internet results depend on the line, the Wi-Fi, other traffic and the server, so they vary from run to run.");
    so_board_footer(o);
}

int bench_net(const net_cfg *cfg, srs_out *o, volatile int *cancel, char *why, size_t wn)
{
    netctx *x;
    net_result *r = &g_res;
    time_t now = time(NULL);
    char params[400], res[400], st_buf[64];
    static char stage[64];
    unsigned char *body = NULL;
    int rc;
    double rtt;
    if (why && wn) why[0] = 0;
    if (!cfg->local && !cfg->internet) { if (why) snprintf(why, wn, "nothing to run: choose the loopback test, the internet test or both"); return -1; }
    if (!(x = calloc(1, sizeof *x))) return -1;
    x->c = *cfg;
    if (x->c.streams < 1) x->c.streams = 1;
    if (x->c.streams > NET_MAX_STREAMS) x->c.streams = NET_MAX_STREAMS;
    if (x->c.secs < 0.1) x->c.secs = 0.1;
    if (x->c.secs > 60) x->c.secs = 60;
    if (x->c.samples < 1) x->c.samples = 1;
    if (x->c.samples > 100) x->c.samples = 100;
    if (x->c.ipv != 4 && x->c.ipv != 6) x->c.ipv = 0;
    net_probe(&x->c, &x->w);
    if (cfg->internet && x->w.problem[0]) {
        if (why) snprintf(why, wn, "%s", x->w.problem);
        free(x);
        return -1;
    }
    memset(r, 0, sizeof *r);
    x->r = r;
    rtt = x->c.secs < 1 ? x->c.secs : 1;
    if (cfg->local) {
        x->ran_local = 1;
        g_net_stage = "Loopback TCP, 1 stream";
        lo_throughput(1, 0, x->c.secs, cancel, &r->tcp1, NULL, NULL);
        if (!stopped(cancel)) {
            snprintf(stage, sizeof stage, "Loopback TCP, %d streams", x->c.streams);
            g_net_stage = stage;
            lo_throughput(x->c.streams, 0, x->c.secs, cancel, &r->tcpn, NULL, NULL);
        }
        if (!stopped(cancel)) { g_net_stage = "Loopback UDP"; lo_throughput(1, 1, x->c.secs, cancel, &r->udp, &r->udp_loss, &r->udp_sent); }
        if (!stopped(cancel)) { g_net_stage = "Loopback TCP round trip"; lo_rtt(0, rtt, cancel, &r->tcp_rtt); }
        if (!stopped(cancel)) { g_net_stage = "Loopback UDP round trip"; lo_rtt(1, rtt, cancel, &r->udp_rtt); }
        if (!stopped(cancel) && (!r->tcp1.done || !r->tcp_rtt.done)) snprintf(x->err, sizeof x->err, "a loopback test could not open its sockets");
    }
    if (cfg->internet && !stopped(cancel)) {
        x->ran_net = 1;
        g_net_stage = "DNS lookups";
        dns_tests(&x->w, x->c.ipv, cancel, r);
        if (!stopped(cancel)) { g_net_stage = "Connect time"; connect_tests(&x->w, x->c.ipv, x->c.samples, cancel, r); }
        if (x->w.curl[0] && !stopped(cancel)) {
            char e2[200] = "";
            g_net_stage = "HTTPS request";
            https_tests(&x->w, x->c.ipv, cancel, r, e2, sizeof e2);
            if (e2[0] && !x->err[0]) snprintf(x->err, sizeof x->err, "%s", e2);
            if ((body = malloc(1 << 20))) {
                uint64_t s = 0x243F6A8885A308D3ull;
                size_t i;
                for (i = 0; i < (1u << 20) / 8; i++) { uint64_t v = xs64(&s); memcpy(body + i * 8, &v, 8); }   /* incompressible */
            }
            {
                static const struct { int up, multi; const char *what; } t4[4] = { { 0, 0, "Download" }, { 0, 1, "Download" }, { 1, 0, "Upload" }, { 1, 1, "Upload" } };
                net_xfer *dst[4] = { &r->down1, &r->downn, &r->up1, &r->upn };
                int k;
                for (k = 0; k < 4 && body && !stopped(cancel); k++) {
                    int n = t4[k].multi ? x->c.streams : 1;
                    char e3[200] = "";
                    snprintf(stage, sizeof stage, "%s, %d stream%s", t4[k].what, n, n > 1 ? "s" : "");
                    g_net_stage = stage;
                    if (t4[k].multi && n == 1) { *dst[k] = *dst[k - 1]; continue; }   /* streams = 1: same test */
                    xfer_test(&x->w, t4[k].up, n, x->c.secs, x->c.max_mb, x->c.ipv, body, cancel, dst[k], e3, sizeof e3);
                    if (e3[0] && !x->err[0]) snprintf(x->err, sizeof x->err, "%s", e3);
                }
            }
            free(body);
        }
    }
    x->cancelled = stopped(cancel);
    g_net_stage = "";
    strftime(x->when, sizeof x->when, "%Y-%m-%d %H:%M:%S", localtime(&now));
    bench_save_render("net", net_render, x, o);
    snprintf(params, sizeof params, "%s%s streams=%d seconds=%.1f%s%s%s", x->ran_local ? "loopback" : "", x->ran_net ? (x->ran_local ? "+internet" : "internet") : "",
             x->c.streams, x->c.secs, x->c.ipv ? (x->c.ipv == 4 ? " ipv4" : " ipv6") : "", x->ran_net ? " server=" : "", x->ran_net ? x->w.server_host : "");
    res[0] = 0;
    if (x->ran_local)
        snprintf(res, sizeof res, "loopback TCP %.0f/%.0f Mbit/s (1/%d streams), UDP %.0f Mbit/s, RTT TCP %.3f UDP %.3f ms",
                 r->tcp1.avg, r->tcpn.avg, x->c.streams, r->udp.avg, r->tcp_rtt.med, r->udp_rtt.med);
    if (x->ran_net) {
        size_t k = strlen(res);
        snprintf(st_buf, sizeof st_buf, "%.1f", r->conn[0].done ? r->conn[0].med : 0);
        snprintf(res + k, sizeof res - k, "%sdown %.0f/%.0f, up %.0f/%.0f Mbit/s, connect %s ms, DNS %.1f ms", k ? "; " : "",
                 r->down1.avg, r->downn.avg, r->up1.avg, r->upn.avg, st_buf, r->dns_first > 0 ? r->dns_first : 0);
    }
    if (x->cancelled) strncat(res, " STOPPED", sizeof res - strlen(res) - 1);
    else if (x->err[0]) strncat(res, " ERROR", sizeof res - strlen(res) - 1);
    bench_history_add("net", params, res);
    rc = x->cancelled ? 1 : x->err[0] ? -2 : 0;
    if (x->err[0] && why) snprintf(why, wn, "%s", x->err);
    free(x);
    return rc;
}

#else   /* not Linux */
static net_result g_res;
void net_cfg_default(net_cfg *c) { memset(c, 0, sizeof *c); c->local = 1; c->streams = 4; c->secs = 5; c->samples = 10; c->max_mb = 250; }
int net_probe(const net_cfg *c, net_where *w) { (void)c; memset(w, 0, sizeof *w); snprintf(w->problem, sizeof w->problem, "Linux only"); return 0; }
void net_ask_text(const net_cfg *c, const net_where *w, char *b, size_t n) { (void)c; (void)w; snprintf(b, n, "Linux only"); }
int net_curl_available(char *ver, size_t n) { if (ver && n) ver[0] = 0; return 0; }
const char *net_stage(void) { return ""; }
double net_live_mbps(void) { return 0; }
const net_result *net_last(void) { return &g_res; }
int bench_net(const net_cfg *c, srs_out *o, volatile int *cancel, char *why, size_t wn)
{
    (void)c; (void)o; (void)cancel;
    if (why) snprintf(why, wn, "Linux only");
    return -1;
}
#endif
