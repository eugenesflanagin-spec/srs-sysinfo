/* plat_posix.c - Linux platform layer for the native CLI image. */
#define _POSIX_C_SOURCE 200809L
#include "srs.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

double srs_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static int SRS_CDECL name_cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

int plat_listdir(const char *dir, char ***names)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    char **v = NULL;
    int n = 0;
    *names = NULL;
    if (!d) return -1;
    while ((e = readdir(d))) {
        char **nv;
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        nv = realloc(v, (size_t)(n + 1) * sizeof *nv);
        if (!nv) break;
        v = nv;
        v[n] = strdup(e->d_name);
        if (v[n]) n++;
    }
    closedir(d);
    if (n > 1) qsort(v, (size_t)n, sizeof *v, name_cmp);
    *names = v;
    return n;
}

void plat_freelist(char **names, int n)
{
    int i;
    for (i = 0; i < n; i++) free(names[i]);
    free(names);
}

int plat_isdir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int plat_exedir(char *out, size_t outsz)
{
    ssize_t n = readlink("/proc/self/exe", out, outsz - 1);
    char *s;
    if (n <= 0) return -1;
    out[n] = 0;
    if (!(s = strrchr(out, '/'))) return -1;
    *s = 0;
    return 0;
}

const char *plat_hwdata_dir(void) { return "/usr/share/hwdata"; }
const char *plat_sysfs(void) { return plat_isdir("/sys/bus") ? "/sys" : NULL; }
const char *plat_home(void) { return getenv("HOME"); }
char plat_sep(void) { return '/'; }
int  plat_is_wine(void) { return 0; }

int plat_run_capture(const char *const argv[], char *out, size_t outsz)
{
    int fd[2], st;
    size_t got = 0;
    pid_t pid;
    out[0] = 0;
    if (pipe(fd)) return -1;
    if ((pid = fork()) < 0) { close(fd[0]); close(fd[1]); return -1; }
    if (pid == 0) {
        dup2(fd[1], 1);
        close(fd[0]); close(fd[1]);
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    close(fd[1]);
    for (;;) {
        ssize_t r = read(fd[0], out + got, outsz - 1 - got);
        if (r <= 0) break;
        got += (size_t)r;
        if (got >= outsz - 1) break;
    }
    out[got] = 0;
    close(fd[0]);
    waitpid(pid, &st, 0);
    if (!WIFEXITED(st) || WEXITSTATUS(st) == 127) return -1;
    return WEXITSTATUS(st);
}

int plat_setupapi_devices(enum srs_bus bus, srs_dev **out) { (void)bus; *out = NULL; return -1; }
int plat_setupapi_info(enum srs_bus bus, const char *id, srs_info *out) { (void)bus; (void)id; (void)out; return -1; }

const char *plat_build_info(void)
{
    static char b[256];
    snprintf(b, sizeof b, "ELF %s, %s, glibc (system C runtime)",
#if defined(__x86_64__)
             "x86-64",
#elif defined(__i386__)
             "i686",
#elif defined(__aarch64__)
             "aarch64",
#else
             "native",
#endif
#if defined(__clang__)
             "clang " __clang_version__
#else
             "gcc " __VERSION__
#endif
    );
    return b;
}
