/* cuda_toolkit.c - the optional NVIDIA CUDA toolkit: detection, the plain-English text, the install command per
 * distribution, the "don't ask again" setting and the checks before an install (see cuda_toolkit.h).
 *
 * What SRS needs from the toolkit is only its runtime compiler, NVRTC (libnvrtc). Since 2.3 SRS's CUDA kernels are
 * built in as PTX, so NVRTC is a fallback for drivers that refuse that PTX. The packages (checked 2026-10):
 *   Arch Linux (extra):   cuda             - the whole toolkit in /opt/cuda (libnvrtc.so.13 via /etc/ld.so.conf.d/cuda.conf),
 *                                            about 2.4 GB download / 5.2 GB installed. It also puts NVIDIA's own cuBLAS on
 *                                            the library path, ahead of ZLUDA's, and pulls opencl-nvidia.
 *   Debian / Ubuntu:      libnvrtc12 (Debian 13 trixie: 12.4, Depends only libc6 + libnvrtc-builtins12.4, 17 MB download /
 *                         59 MB installed - packages.debian.org/trixie/libnvrtc12) or libnvrtc11.2 (Debian 11/12) from the
 *                         non-free nvidia-cuda-toolkit source; Ubuntu ships the same source in multiverse (24.04: CUDA 12.0,
 *                         22.04: 11.5 - launchpad.net/ubuntu/+source/nvidia-cuda-toolkit; the binary names there were not
 *                         looked up one by one). Whichever of the two apt-cache knows is used; else nvidia-cuda-toolkit.
 *   Fedora, openSUSE, others: NVRTC is only in NVIDIA's own CUDA repository (cuda-nvrtc-<version> packages), which SRS
 *                         does not add by itself: instructions and the link to NVIDIA's download page instead.
 * Not run by any test: package managers. The tests use a fake PATH, a fake /etc/os-release (SRS_OS_RELEASE), a fake
 * PCI tree (SRS_PCI_SYSFS), a fake route table (SRS_CTK_ROUTE), a fake lock file (SRS_CTK_PACMAN_LOCK) and
 * SRS_CTK_DRY_RUN=1 (with SRS_CTK_TOOL_DIRS = the folder of fake tools, read only in a dry run).
 * What runs as root: only a package manager, sudo and pkexec found in /usr/bin, /usr/sbin, /bin or /sbin, run by that full
 * path; PATH is never searched for them (a planted ~/.local/bin/pacman would otherwise run as root after the password).
 * Written for SRS. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "cuda_toolkit.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static const char explain[] =
    "The NVIDIA CUDA toolkit is NVIDIA's free set of developer tools for running programs on NVIDIA graphics cards. "
    "SRS only needs one small part of it: the runtime compiler (NVRTC), which turns SRS's test code into instructions "
    "the graphics card understands while SRS is running.\n\n"
    "Since SRS 2.3 the CUDA tests come pre-built (as PTX, NVIDIA's portable GPU code), so most people do not need the "
    "toolkit: GPU Burnin and every CUDA row of the GPU Compute comparison run without it. SRS only uses the compiler "
    "when the graphics driver refuses the pre-built tests; then the CUDA rows say \"not available\" until it is installed.\n\n"
    "It can be large (on Arch Linux the whole toolkit: about 2.4 GB to download, 5.2 GB on disk). On AMD computers that "
    "use ZLUDA (which runs CUDA programs on AMD graphics cards), installing it can break or remove ZLUDA.";

static const char zluda_warn[] =
    "This computer uses ZLUDA. Installing the NVIDIA CUDA toolkit is NOT recommended here: it can replace parts of "
    "ZLUDA (its cuBLAS library) with NVIDIA's own, which do not work on an AMD graphics card, and packages that bring "
    "NVIDIA's driver libraries (nvidia-utils) remove ZLUDA altogether. SRS's built-in CUDA tests already work with ZLUDA.";

const char *ctk_explain(void) { return explain; }
const char *ctk_zluda_warning(void) { return zluda_warn; }
const char *ctk_later_hint(void) { return "You can install it later from " CTK_MENU_PATH " (or: " CTK_CLI ")."; }
int ctk_dry_run(void) { const char *e = getenv("SRS_CTK_DRY_RUN"); return e && *e && strcmp(e, "0"); }

static int rd_line(const char *path, char *b, size_t n)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    if (!fgets(b, (int)n, f)) b[0] = 0;
    fclose(f);
    b[strcspn(b, "\n")] = 0;
    return 1;
}

/* an NVIDIA (vendor 0x10de) display controller (class 0x03xxxx) */
static int nvidia_on_pci(void)
{
    const char *root = getenv("SRS_PCI_SYSFS");
    DIR *d;
    struct dirent *e;
    int hit = 0;
    if (!root || !*root) root = "/sys/bus/pci/devices";
    if (!(d = opendir(root))) return 0;
    while (!hit && (e = readdir(d))) {
        char p[600], v[32], c[32];
        if (e->d_name[0] == '.') continue;
        snprintf(p, sizeof p, "%s/%s/vendor", root, e->d_name);
        if (!rd_line(p, v, sizeof v) || strtoul(v, NULL, 16) != 0x10de) continue;
        snprintf(p, sizeof p, "%s/%s/class", root, e->d_name);
        if (rd_line(p, c, sizeof c) && (strtoul(c, NULL, 16) >> 16) == 0x03) hit = 1;
    }
    closedir(d);
    return hit;
}

void ctk_detect(ctk_state *s, int recheck)
{
    memset(s, 0, sizeof *s);
    gb_cuda_probe(&s->cu, recheck);
    s->nvidia_gpu = nvidia_on_pci();
    s->relevant = (s->nvidia_gpu || s->cu.libcuda) && !s->cu.nvrtc;
}

/* ---------------------------------------------------------------- the install command */
/* name in one of the folders of list (colon-separated); only absolute folders count (an empty or relative PATH entry
   would mean the current folder), and only a regular executable file */
static int in_dirs(const char *list, const char *name, char *full, size_t n)
{
    char buf[4096], *t, *sp;
    if (!list) return 0;
    snprintf(buf, sizeof buf, "%s", list);
    for (t = strtok_r(buf, ":", &sp); t; t = strtok_r(NULL, ":", &sp)) {
        char p[1024];
        struct stat st;
        if (*t != '/') continue;
        if ((size_t)snprintf(p, sizeof p, "%s/%s", t, name) >= sizeof p) continue;
        if (!stat(p, &st) && S_ISREG(st.st_mode) && !access(p, X_OK)) { if (full) snprintf(full, n, "%s", p); return 1; }
    }
    return 0;
}

/* a program that runs as the user (terminal emulators): PATH, absolute entries only */
static int in_path(const char *name, char *full, size_t n) { return in_dirs(getenv("PATH"), name, full, n); }

/* a program that runs as root or asks for the password (package manager, sudo, pkexec, and apt-cache with them):
   only from the system folders, by full path, whatever PATH says. SRS_CTK_TOOL_DIRS replaces the folders only in a
   dry run (tests), when nothing is ever run. */
#define CTK_SYS_DIRS "/usr/bin:/usr/sbin:/bin:/sbin"
static int trusted_tool(const char *name, char *full, size_t n)
{
    const char *t = ctk_dry_run() ? getenv("SRS_CTK_TOOL_DIRS") : NULL;
    return in_dirs(t && *t ? t : CTK_SYS_DIRS, name, full, n);
}

/* os-release: ID, ID_LIKE and PRETTY_NAME */
static void os_release(char *id, size_t in, char *like, size_t ln, char *pretty, size_t pn)
{
    const char *f = getenv("SRS_OS_RELEASE");
    FILE *fp;
    char l[512];
    id[0] = like[0] = pretty[0] = 0;
    if (!f || !*f) f = access("/etc/os-release", R_OK) ? "/usr/lib/os-release" : "/etc/os-release";
    if (!(fp = fopen(f, "r"))) return;
    while (fgets(l, sizeof l, fp)) {
        char *v = strchr(l, '='), *dst = NULL;
        size_t dn = 0, k;
        if (!v) continue;
        *v++ = 0;
        v[strcspn(v, "\n")] = 0;
        if (*v == '"' || *v == '\'') { v++; k = strlen(v); if (k && (v[k - 1] == '"' || v[k - 1] == '\'')) v[k - 1] = 0; }
        if (!strcmp(l, "ID")) { dst = id; dn = in; }
        else if (!strcmp(l, "ID_LIKE")) { dst = like; dn = ln; }
        else if (!strcmp(l, "PRETTY_NAME")) { dst = pretty; dn = pn; }
        if (dst) snprintf(dst, dn, "%s", v);
    }
    fclose(fp);
}

static int word_in(const char *list, const char *w)
{
    char b[256], *t, *sp;
    snprintf(b, sizeof b, "%s", list);
    for (t = strtok_r(b, " ", &sp); t; t = strtok_r(NULL, " ", &sp)) if (!strcmp(t, w)) return 1;
    return 0;
}

/* does apt know this package? (apt-cache show: a read-only question, no root). It can take a second or two, so each
   answer is kept for the session (ctk_plan_refresh forgets them); one question at a time */
static pthread_mutex_t g_apt_mu = PTHREAD_MUTEX_INITIALIZER;
static struct { char pkg[40]; int known; } g_apt[6];
static int g_napt;

void ctk_plan_refresh(void)
{
    pthread_mutex_lock(&g_apt_mu);
    g_napt = 0;
    pthread_mutex_unlock(&g_apt_mu);
}

static int apt_ask(const char *pkg)
{
    char exe[1024];
    pid_t p;
    int st = 0;
    if (!trusted_tool("apt-cache", exe, sizeof exe)) return 0;
    if ((p = fork()) < 0) return 0;
    if (p == 0) {
        int nul = open("/dev/null", O_WRONLY);
        if (nul >= 0) { dup2(nul, 1); dup2(nul, 2); }
        execl(exe, "apt-cache", "show", "--no-all-versions", pkg, (char *)NULL);
        _exit(127);
    }
    while (waitpid(p, &st, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

static int apt_knows(const char *pkg)
{
    int i, k = -1;
    pthread_mutex_lock(&g_apt_mu);
    for (i = 0; i < g_napt; i++) if (!strcmp(g_apt[i].pkg, pkg)) { k = g_apt[i].known; break; }
    if (k < 0) {
        k = apt_ask(pkg);
        if (g_napt < (int)(sizeof g_apt / sizeof g_apt[0])) { snprintf(g_apt[g_napt].pkg, sizeof g_apt[g_napt].pkg, "%s", pkg); g_apt[g_napt++].known = k; }
    }
    pthread_mutex_unlock(&g_apt_mu);
    return k;
}

static void set_argv(ctk_plan *p, const char *const *a)
{
    int i;
    size_t l = 0;
    p->argc = 0;
    p->cmd[0] = 0;
    for (i = 0; a[i] && i < 8; i++) {
        snprintf(p->argv[i], sizeof p->argv[i], "%s", a[i]);
        l += (size_t)snprintf(p->cmd + l, l < sizeof p->cmd ? sizeof p->cmd - l : 0, "%s%s", i ? " " : "", a[i]);
        if (l >= sizeof p->cmd) l = sizeof p->cmd - 1;
        p->argc++;
    }
}

int ctk_plan_install(ctk_plan *p)
{
    char id[64], like[192], both[300];
    memset(p, 0, sizeof *p);
    os_release(id, sizeof id, like, sizeof like, p->distro, sizeof p->distro);
    if (!p->distro[0]) snprintf(p->distro, sizeof p->distro, "%s", id[0] ? id : "this Linux");
    snprintf(both, sizeof both, "%s %s", id, like);
    if (word_in(both, "arch") || word_in(both, "archlinux")) {
        static const char *const a[] = { "pacman", "-S", "--needed", "cuda", NULL };
        p->pm = CTK_PM_PACMAN;
        if (!trusted_tool("pacman", p->exe, sizeof p->exe)) {
            p->manual = 1;
            snprintf(p->manual_text, sizeof p->manual_text,
                     "pacman was not found in /usr/bin, /usr/sbin, /bin or /sbin, so SRS does not run it (a program of that name in "
                     "another folder, for example your home folder, could be anything). If your pacman is somewhere else on purpose, "
                     "run it yourself in a terminal: sudo pacman -S --needed cuda");
            return 0;
        }
        set_argv(p, a);
        snprintf(p->pkg, sizeof p->pkg, "cuda");
        snprintf(p->noninteractive, sizeof p->noninteractive, "--noconfirm");
        snprintf(p->size, sizeof p->size, "the whole CUDA toolkit: about 2.4 GB to download, 5.2 GB on disk (Arch has no smaller package)");
        snprintf(p->manual_text, sizeof p->manual_text, "Arch Linux's cuda package installs to /opt/cuda; SRS finds libnvrtc through /etc/ld.so.conf.d/cuda.conf.");
        return 0;
    }
    if (word_in(both, "debian") || word_in(both, "ubuntu")) {
        static const char *const cand[] = { "libnvrtc12", "libnvrtc11.2", NULL };
        int i;
        p->pm = CTK_PM_APT;
        if (!trusted_tool("apt-get", p->exe, sizeof p->exe)) {
            p->manual = 1;
            snprintf(p->manual_text, sizeof p->manual_text,
                     "apt-get was not found in /usr/bin, /usr/sbin, /bin or /sbin, so SRS does not run it (a program of that name in "
                     "another folder, for example your home folder, could be anything). Install it yourself in a terminal: "
                     "sudo apt-get install libnvrtc12 (or libnvrtc11.2 on older releases).");
            return 0;
        }
        snprintf(p->noninteractive, sizeof p->noninteractive, "-y");
        for (i = 0; cand[i]; i++) if (apt_knows(cand[i])) break;
        if (cand[i]) {
            const char *a[] = { "apt-get", "install", cand[i], NULL };
            set_argv(p, a);
            snprintf(p->pkg, sizeof p->pkg, "%s", cand[i]);
            snprintf(p->size, sizeof p->size, "only the compiler library: about 20 MB to download, about 70 MB on disk");
        } else if (apt_knows("nvidia-cuda-toolkit")) {
            static const char *const a[] = { "apt-get", "install", "nvidia-cuda-toolkit", NULL };
            set_argv(p, a);
            snprintf(p->pkg, sizeof p->pkg, "nvidia-cuda-toolkit");
            snprintf(p->size, sizeof p->size, "the whole CUDA toolkit: several GB");
        } else {
            p->manual = 1;
            snprintf(p->manual_text, sizeof p->manual_text,
                     "apt does not know a package with NVRTC. On Debian, turn on the contrib and non-free components; on Ubuntu, "
                     "multiverse. Then run: sudo apt-get update, and use this entry again (it then installs libnvrtc12 or libnvrtc11.2). "
                     "NVIDIA's own packages: https://developer.nvidia.com/cuda-downloads");
        }
        return 0;
    }
    p->manual = 1;
    p->pm = word_in(both, "fedora") || word_in(both, "rhel") ? CTK_PM_DNF : word_in(both, "suse") || word_in(both, "opensuse") ? CTK_PM_ZYPPER : CTK_PM_NONE;
    snprintf(p->manual_text, sizeof p->manual_text,
             "%s has no NVRTC package of its own that SRS knows. NVIDIA's CUDA repository has it: follow the steps for %s at "
             "https://developer.nvidia.com/cuda-downloads and install only the cuda-nvrtc package of the newest CUDA version "
             "(for example cuda-nvrtc-12-8 with %s), not the whole toolkit. SRS finds libnvrtc when it is on the library path.",
             p->distro, p->distro, p->pm == CTK_PM_DNF ? "dnf install" : p->pm == CTK_PM_ZYPPER ? "zypper install" : "your package manager");
    return 0;
}

void ctk_status_text(const ctk_state *s, const ctk_plan *p, char *b, size_t n)
{
    size_t l = 0;
#define ADD(...) do { l += (size_t)snprintf(b + l, l < n ? n - l : 0, __VA_ARGS__); if (l >= n) l = n - 1; } while (0)
    b[0] = 0;
    ADD("NVIDIA graphics card: %s\n", s->nvidia_gpu ? "found" : "none found");
    if (!s->cu.libcuda) ADD("CUDA driver library (libcuda): not installed\n");
    else ADD("CUDA driver library (libcuda): %s%s\n", s->cu.zluda ? "ZLUDA (CUDA translated to an AMD graphics card)" : "found",
             s->cu.runtime_ok ? "" : ", but too old or damaged");
    if (s->cu.nvrtc) {
        if (s->cu.nvrtc_major) ADD("Runtime compiler (NVRTC): installed, version %d.%d (%s)\n", s->cu.nvrtc_major, s->cu.nvrtc_minor, s->cu.nvrtc_lib);
        else ADD("Runtime compiler (NVRTC): installed (%s)\n", s->cu.nvrtc_lib);
    } else ADD("Runtime compiler (NVRTC): not installed%s\n", s->cu.libcuda ? " - SRS uses its built-in CUDA tests" : "");
    if (p) {
        if (p->manual) ADD("Install on %s: by hand (see below)\n", p->distro);
        else {
            ADD("Install on %s: %s %s\n  (%s)\n", p->distro, ctk_root_tool() ? ctk_root_tool() : "sudo", p->cmd, p->size);
            ADD("Programs used: %s, %s\n", ctk_root_path() ? ctk_root_path() : "(no sudo or pkexec in /usr/bin, /usr/sbin, /bin or /sbin)", p->exe);
        }
    }
#undef ADD
}

/* ---------------------------------------------------------------- "don't ask again" (SRSDEF.ini, shared with the GUI) */
const char *ctk_ini_path(char *b, size_t n)
{
    const char *x = getenv("XDG_CONFIG_HOME"), *h = getenv("HOME");
    if (x && *x == '/') snprintf(b, n, "%s/srs/SRSDEF.ini", x);
    else snprintf(b, n, "%s/.config/srs/SRSDEF.ini", h && *h ? h : ".");
    return b;
}

#define ASK_KEY "Ask CUDA Toolkit"

int ctk_ask_get(void)
{
    char path[1100], l[1024];
    FILE *f;
    int in = 0, v = 1;
    if (!(f = fopen(ctk_ini_path(path, sizeof path), "r"))) return 1;
    while (fgets(l, sizeof l, f)) {
        char *k = l;
        l[strcspn(l, "\r\n")] = 0;
        while (*k == ' ' || *k == '\t') k++;
        if (*k == '[') { in = !strncmp(k, "[SRSDEF]", 8); continue; }
        if (in && !strncmp(k, ASK_KEY, sizeof ASK_KEY - 1)) {
            char *e = k + sizeof ASK_KEY - 1;
            while (*e == ' ') e++;
            if (*e == '=') v = atoi(e + 1) != 0;
        }
    }
    fclose(f);
    return v;
}

/* rewrite SRSDEF.ini with the key set; every other line kept as it is */
int ctk_ask_set(int on)
{
    char path[1100], tmp[1200], dir[1100], l[1024], *sl;
    FILE *f, *o;
    int in = 0, done = 0, seen = 0;
    ctk_ini_path(path, sizeof path);
    snprintf(dir, sizeof dir, "%s", path);
    if ((sl = strrchr(dir, '/'))) {   /* mkdir -p */
        char *q;
        *sl = 0;
        for (q = dir + 1; *q; q++) if (*q == '/') { *q = 0; mkdir(dir, 0755); *q = '/'; }
        mkdir(dir, 0755);
    }
    snprintf(tmp, sizeof tmp, "%s.%ld.tmp", path, (long)getpid());
    if (!(o = fopen(tmp, "w"))) return -1;
    if ((f = fopen(path, "r"))) {
        while (fgets(l, sizeof l, f)) {
            char *k = l;
            while (*k == ' ' || *k == '\t') k++;
            if (*k == '[') {
                if (in && !done) { fprintf(o, ASK_KEY "=%d\n", on); done = 1; }
                in = !strncmp(k, "[SRSDEF]", 8);
                seen |= in;
            } else if (in && !strncmp(k, ASK_KEY, sizeof ASK_KEY - 1) && strchr(" \t=", k[sizeof ASK_KEY - 1]) && k[sizeof ASK_KEY - 1] &&
                       strchr(k, '=')) {   /* this key, not a longer one that starts the same */
                if (!done) fprintf(o, ASK_KEY "=%d\n", on);
                done = 1;
                continue;
            }
            fputs(l, o);
            if (l[0] && l[strlen(l) - 1] != '\n') fputc('\n', o);
        }
        fclose(f);
    }
    if (!done) {
        if (!seen) fprintf(o, "[SRSDEF]\n");   /* (a [SRSDEF] that ended before another section got the key above) */
        fprintf(o, ASK_KEY "=%d\n", on);
    }
    if (fclose(o) || rename(tmp, path)) { unlink(tmp); return -1; }
    return 0;
}

/* ---------------------------------------------------------------- before an install */
/* sudo, else pkexec, from a system folder only (see trusted_tool) */
static char g_root[1100], g_pkexec[1100];
const char *ctk_pkexec_path(void) { return trusted_tool("pkexec", g_pkexec, sizeof g_pkexec) ? g_pkexec : NULL; }
const char *ctk_root_path(void)
{
    if (trusted_tool("sudo", g_root, sizeof g_root)) return g_root;
    if (trusted_tool("pkexec", g_root, sizeof g_root)) return g_root;
    return NULL;
}
const char *ctk_root_tool(void)
{
    const char *p = ctk_root_path();
    return !p ? NULL : !strcmp(strrchr(p, '/') + 1, "sudo") ? "sudo" : "pkexec";
}

const char *ctk_find_terminal(char *b, size_t n)
{
    static const char *const t[] = { "konsole", "gnome-terminal", "kgx", "xfce4-terminal", "mate-terminal", "lxterminal", "kitty",
                                     "alacritty", "foot", "wezterm", "xterm", "x-terminal-emulator", NULL };
    int i;
    for (i = 0; t[i]; i++) if (in_path(t[i], NULL, 0)) { snprintf(b, n, "%s", t[i]); return b; }
    return NULL;
}

int ctk_terminal_argv(const char *term, const char *script, const char **argv, int max)
{
    int n = 0;
    if (max < 7) return 0;
    argv[n++] = term;
    if (!strcmp(term, "gnome-terminal") || !strcmp(term, "kgx")) argv[n++] = "--";
    else if (!strcmp(term, "xfce4-terminal") || !strcmp(term, "mate-terminal")) argv[n++] = "-x";
    else if (!strcmp(term, "wezterm")) { argv[n++] = "start"; argv[n++] = "--"; }
    else if (strcmp(term, "kitty") && strcmp(term, "foot")) argv[n++] = "-e";   /* konsole, lxterminal, alacritty, xterm, x-terminal-emulator */
    argv[n++] = "/bin/sh";
    argv[n++] = "-c";
    argv[n++] = script;
    argv[n] = NULL;
    return n;
}

/* a default route (IPv4 or IPv6) that is not on lo */
static int have_route(void)
{
    const char *f = getenv("SRS_CTK_ROUTE");
    FILE *fp;
    char l[512];
    int hit = 0;
    if ((fp = fopen(f && *f ? f : "/proc/net/route", "r"))) {
        while (!hit && fgets(l, sizeof l, fp)) {
            char ifn[64], dst[64];
            if (sscanf(l, "%63s %63s", ifn, dst) == 2 && !strcmp(dst, "00000000") && strcmp(ifn, "lo")) hit = 1;
        }
        fclose(fp);
    }
    if (!hit && !(f && *f) && (fp = fopen("/proc/net/ipv6_route", "r"))) {
        while (!hit && fgets(l, sizeof l, fp)) {
            char dst[64], len[8], ifn[64] = "";
            if (sscanf(l, "%63s %7s %*s %*s %*s %*s %*s %*s %*s %63s", dst, len, ifn) >= 2 && !strcmp(dst, "00000000000000000000000000000000") &&
                !strcmp(len, "00") && ifn[0] && strcmp(ifn, "lo")) hit = 1;
        }
        fclose(fp);
    }
    return hit;
}

int ctk_precheck(const ctk_plan *p, int have_terminal, char *why, size_t wn)
{
    const char *lock = getenv("SRS_CTK_PACMAN_LOCK");
    if (p->manual || !p->argc) { snprintf(why, wn, "SRS has no install command for %s: see the instructions", p->distro); return 1; }
    if (p->pm == CTK_PM_PACMAN && !access(lock && *lock ? lock : "/var/lib/pacman/db.lck", F_OK)) {
        snprintf(why, wn, "the package database is locked: another install or update is running (pacman's db.lck exists). "
                          "Let it finish, then try again. If nothing is running, the lock was left behind by a crash: "
                          "see the Arch wiki on removing /var/lib/pacman/db.lck");
        return 1;
    }
    if (!have_route()) { snprintf(why, wn, "this computer has no network connection (no default route), and the package has to be downloaded"); return 1; }
    if (!p->exe[0]) { snprintf(why, wn, "the package manager was not found in /usr/bin, /usr/sbin, /bin or /sbin, so SRS does not run it"); return 1; }
    if (have_terminal && !ctk_root_path()) {
        snprintf(why, wn, "neither sudo nor pkexec was found in /usr/bin, /usr/sbin, /bin or /sbin, so the install cannot ask for the "
                          "administrator password (SRS does not use a sudo or pkexec from any other folder)");
        return 1;
    }
    if (!have_terminal && !ctk_pkexec_path()) {
        snprintf(why, wn, "no terminal window program and no pkexec (polkit) in /usr/bin, /usr/sbin, /bin or /sbin was found, so the install "
                          "cannot ask for the administrator password. Run the command yourself in a terminal: sudo %s", p->cmd);
        return 1;
    }
    return 0;
}

/* append s to b as one shell word in single quotes (a ' inside becomes '\'') */
static void shq(char *b, size_t n, const char *s)
{
    size_t l = strlen(b);
    if (l + 1 >= n) return;
    b[l++] = '\'';
    for (; *s && l + 5 < n; s++) {
        if (*s == '\'') { memcpy(b + l, "'\\''", 4); l += 4; }
        else b[l++] = *s;
    }
    b[l++] = '\'';
    b[l] = 0;
}
#define SCAT(...) do { size_t l_ = strlen(b); snprintf(b + l_, l_ < n ? n - l_ : 0, __VA_ARGS__); } while (0)

/* every path and word is quoted for sh (a ' in XDG_CACHE_HOME is fine); the root tool and the package manager by full path */
void ctk_terminal_script(const ctk_plan *p, const char *statusfile, char *b, size_t n)
{
    const char *root = ctk_root_path();
    char msg[600];
    int k;
    if (!n) return;
    b[0] = 0;
    snprintf(msg, sizeof msg, "SRS: installing the NVIDIA CUDA runtime compiler (%s). Command: %s %s", p->pkg, ctk_root_tool() ? ctk_root_tool() : "sudo", p->cmd);
    SCAT("printf '%%s\\n\\n' ");
    shq(b, n, msg);
    SCAT("; ");
    if (root && p->exe[0]) {
        shq(b, n, root);
        SCAT(" ");
        shq(b, n, p->exe);
        for (k = 1; k < p->argc; k++) { SCAT(" "); shq(b, n, p->argv[k]); }
        SCAT("; s=$?; ");
    } else SCAT("echo 'No sudo or pkexec in a system folder.'; s=127; ");
    SCAT("echo $s > ");
    shq(b, n, statusfile);
    SCAT("; echo; "
         "if [ $s = 0 ]; then echo 'Finished. SRS checks for the compiler now.'; else echo \"The install stopped with exit status $s (see above).\"; fi; "
         "echo 'Press Enter to close this window.'; read x");
}
#undef SCAT

void ctk_result_text(const ctk_plan *p, int status, const ctk_state *after, char *b, size_t n)
{
    char ver[32] = "";
    if (after->cu.nvrtc_major) snprintf(ver, sizeof ver, " (NVRTC %d.%d)", after->cu.nvrtc_major, after->cu.nvrtc_minor);
    if (status == 0 && after->cu.nvrtc)
        snprintf(b, n, "Installed. SRS found the runtime compiler%s; GPU Burnin and the GPU Compute comparison use it when the "
                       "driver refuses the built-in CUDA tests (no restart needed).", ver);
    else if (status == 0)
        snprintf(b, n, "The package manager finished without an error, but SRS still cannot find libnvrtc. It may be outside the library "
                       "path: run \"sudo ldconfig\", or log out and back in, then look at %s again.", CTK_MENU_PATH);
    else if (status == -1)
        snprintf(b, n, "The install could not be started (the terminal or password program did not run). Nothing was changed. "
                       "You can run it yourself in a terminal: sudo %s", p->cmd);
    else if (status == -3)
        snprintf(b, n, "An install SRS started earlier is still running (SRS stopped waiting for it). Let it finish first.");
    else if (status == -2)
        snprintf(b, n, "SRS stopped waiting for the install. If the terminal window is still open, it may still be running; "
                       "look at %s again when it has finished.", CTK_MENU_PATH);
    else if (status == 126)
        snprintf(b, n, "The password prompt was cancelled, so nothing was installed.");
    else if (status == 127)
        snprintf(b, n, "Not allowed: the administrator password was not accepted (or polkit refused), so nothing was installed.");
    else
        snprintf(b, n, "The package manager stopped with an error (exit status %d), so the install did not finish; the terminal window "
                       "showed why. Common causes: no internet connection or a mirror that does not answer, the package database locked "
                       "by another update (wait for it, then try again), a conflict with an installed package (for example ZLUDA), or "
                       "a \"no\" answer to one of its questions. Nothing else in SRS changed; the built-in CUDA tests still work.", status);
}
