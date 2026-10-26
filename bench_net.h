/* bench_net.h - Network benchmark (bench_net.c). Written for SRS.
 *
 * Two independent parts:
 *  - On this computer (loopback, 127.0.0.1): TCP throughput with one and with N streams, UDP
 *    throughput and packet loss, TCP and UDP round trip time. Nothing leaves the computer; this
 *    measures the kernel's network stack and the CPU, not a network card or a cable.
 *  - Internet: DNS lookup time (first, repeat = usually cached, made-up name = not cached), TCP
 *    connect time (latency and jitter), HTTPS timings (connect, TLS handshake, server reply) and
 *    download / upload speed with 1 and N parallel streams against a speed test server (default:
 *    Cloudflare's public speed test, GET /__down?bytes=N and POST /__up).
 *    libcurl is loaded at run time (dlopen "libcurl.so.4"); without it the HTTPS and transfer rows
 *    say "not available" and the DNS and connect tests still run.
 *
 * The internet part runs ONLY when cfg->internet is set, and the caller must have asked the user
 * first (net_ask_text() gives the words to show: server, hosts, tests, duration and data use). This
 * file never prompts and never contacts anything on its own; net_probe() sends no packets at all.
 *
 * Privacy (screenshots get published): output and history name the interface, its kind, link
 * speed and MTU and the IP version used - never a MAC address, an IP address (local or public),
 * a Wi-Fi network name (SSID), this computer's host name or a proxy URL. Interface names that
 * contain a MAC address ("enx001122334455", "wlx...") are shortened. Requests carry a plain
 * "srs-sysinfo" user agent, no cookies, no query extras. */
#ifndef BENCH_NET_H
#define BENCH_NET_H

#include "sec.h"
#include <stddef.h>

#define NET_DEFAULT_SERVER "https://speed.cloudflare.com"   /* GET /__down?bytes=N, POST /__up */
#define NET_DEFAULT_HOSTS  ""                               /* extra latency hosts; the server host is always first */
#define NET_MAX_STREAMS 32
#define NET_MAX_HOSTS   8

typedef struct {
    int local;            /* run the loopback tests (no network traffic) */
    int internet;         /* run the internet tests - set ONLY after the user said yes */
    int streams;          /* parallel streams for the "N streams" tests, 1..32 (default 4) */
    double secs;          /* seconds per throughput test, 0.1..60 (default 5; round trip tests use min(secs, 1)) */
    int samples;          /* TCP connect samples per host, 1..100 (default 10) */
    int ipv;              /* 0 = either (system choice), 4 = IPv4 only, 6 = IPv6 only */
    double max_mb;        /* a transfer test stops after this many MB (default 250; 0 = no cap) */
    const char *server;   /* speed test base URL, https only (NULL or "" = NET_DEFAULT_SERVER) */
    const char *hosts;    /* extra latency hosts, comma-separated, "host" or "host:port" (NULL or "" = none) */
} net_cfg;

typedef struct {
    char name[40];        /* "enp14s0", "wlan0"; MAC-based names shortened */
    char kind[48];        /* "wired (Ethernet)", "Wi-Fi", "VPN / tunnel", "bridge", ... */
    char duplex[16];      /* "full", "half" or "" */
    long speed_mbps;      /* link speed, 0 = not reported (usual for Wi-Fi) */
    int mtu;              /* bytes, 0 = unknown */
    int wifi;             /* 1 = wireless */
    int signal_dbm;       /* Wi-Fi signal from /proc/net/wireless, 0 = not known */
} net_iface;

typedef struct {
    char server[512];                     /* base URL used */
    char server_host[256];                /* its host name (shown before a run) */
    char hosts[NET_MAX_HOSTS][256];       /* latency hosts (the server host first) */
    int ports[NET_MAX_HOSTS];             /* their TCP ports (443 unless "host:port") */
    int nhosts;
    char curl[64];                        /* "libcurl 8.11.1", "" = not available */
    char proxy[64];                       /* name of the proxy variable that is set, "" = none */
    net_iface ifc;                        /* the default route's interface (from /proc, sends nothing) */
    double est_mb;                        /* upper bound of the internet tests' data use, MB */
    double est_mb_100;                    /* the same on a 100 Mbit/s line, MB */
    double est_secs;                      /* rough duration of the whole run, s */
    char problem[256];                    /* why the internet tests cannot run ("" = can) */
} net_where;

/* results of the last run in this process (valid after bench_net returned) */
typedef struct { double avg, peak, mb; int done; } net_xfer;   /* Mbit/s from the first byte on; best 0.5 s window; MB moved */
typedef struct { double best, med, p99, mean, jitter; long n, lost; int done; } net_lat;   /* ms */
typedef struct {
    net_xfer tcp1, tcpn, udp;             /* loopback */
    double udp_loss;                      /* % of UDP packets lost on loopback */
    long udp_sent;
    net_lat tcp_rtt, udp_rtt;
    double dns_first, dns_repeat, dns_random;   /* ms, < 0 = failed, 0 = not run */
    net_lat conn[NET_MAX_HOSTS];
    double https_connect, https_tls, https_reply;   /* ms, medians of 3 */
    int https_done;
    net_xfer down1, downn, up1, upn;
    int ipver;                            /* 4 or 6: what the internet tests used, 0 = unknown */
} net_result;

void net_cfg_default(net_cfg *c);
/* fill w from c without any network traffic; returns 1 when the internet tests can run */
int  net_probe(const net_cfg *c, net_where *w);
/* the ask-first text for the internet tests: server, hosts, tests, duration, data use */
void net_ask_text(const net_cfg *c, const net_where *w, char *b, size_t n);
int  net_curl_available(char *ver, size_t n);   /* 1 when libcurl could be loaded; ver = "libcurl 8.x" */
const char *net_stage(void);                    /* "Download, 4 streams" while a run is going on */
double net_live_mbps(void);                     /* current throughput (Mbit/s) during throughput tests, 0 otherwise */
const net_result *net_last(void);
/* run, render into o (may be NULL) and ~/.cache/srs/bench-net.txt, add a history line.
   0 ok, 1 stopped, -1 not started (why says why), -2 a test failed (result shown, why says which) */
int  bench_net(const net_cfg *c, srs_out *o, volatile int *cancel, char *why, size_t wn);

#endif
