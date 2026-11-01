/* perf_procs_harness.c - drives the plain part of perf_procs.c for tests/perf-view-test.sh. The test only ever points it
 * at processes the test itself started.
 *   stat PID | signal PID SIG(TERM|KILL|STOP|CONT) [START] | renice PID NICE | aff-get PID | aff-set PID LIST |
 *   tree PID (children of PID in a live snapshot, deepest first) | untouchable PID PPID KTHREAD | critical PID NAME |
 *   elevated kill|renice|taskset ARG PID
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#include "../perf_procs.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int sig_of(const char *s)
{
    return !strcmp(s, "KILL") ? SIGKILL : !strcmp(s, "STOP") ? SIGSTOP : !strcmp(s, "CONT") ? SIGCONT : SIGTERM;
}

int main(int argc, char **argv)
{
    char err[256] = "", buf[512];
    const char *c = argc > 1 ? argv[1] : "";
    int pid = argc > 2 ? atoi(argv[2]) : 0;
    unsigned long long st;
    char state;
    int ppid;
    if (!strcmp(c, "stat")) {
        if (!pp_stat(pid, &st, &state, &ppid)) { printf("gone\n"); return 0; }
        printf("%llu %c %d\n", st, state, ppid);
        return 0;
    }
    if (!strcmp(c, "signal") && argc > 3) {
        unsigned long long start = argc > 4 ? strtoull(argv[4], NULL, 10) : 0;
        if (!start && pp_stat(pid, &st, NULL, NULL)) start = st;
        printf("%d %s\n", pp_signal(pid, start, sig_of(argv[3]), err, sizeof err), err);
        return 0;
    }
    if (!strcmp(c, "renice") && argc > 3) {
        pp_stat(pid, &st, NULL, NULL);
        printf("%d %s\n", pp_renice(pid, st, atoi(argv[3]), err, sizeof err), err);
        return 0;
    }
    if (!strcmp(c, "aff-get")) { pp_get_affinity(pid, buf, sizeof buf); printf("%s\n", buf); return 0; }
    if (!strcmp(c, "aff-set") && argc > 3) {
        pp_stat(pid, &st, NULL, NULL);
        printf("%d %s\n", pp_set_affinity(pid, st, argv[3], err, sizeof err), err);
        return 0;
    }
    if (!strcmp(c, "tree")) {
        pd_sampler *s = pd_new(0);
        const pd_snap *sn = pd_sample(s, 1.0);
        int out[256], n = pp_tree(sn, pid, out, 256);
        for (int i = 0; i < n; i++) printf("%s%d", i ? " " : "", out[i]);
        printf("\n");
        pd_free(s);
        return 0;
    }
    if (!strcmp(c, "untouchable") && argc > 4) { const char *w = pp_untouchable(pid, atoi(argv[3]), atoi(argv[4])); printf("%s\n", w ? w : "ok"); return 0; }
    if (!strcmp(c, "critical") && argc > 3) { const char *w = pp_critical(pid, argv[3]); printf("%s\n", w ? w : "ok"); return 0; }
    if (!strcmp(c, "elevated") && argc > 4) {
        char **a = pp_elevated_argv(argv[2], argv[3], atoi(argv[4]));
        for (int i = 0; a[i]; i++) printf("%s%s", i ? " " : "", a[i]);
        printf("\n");
        g_strfreev(a);
        return 0;
    }
    fprintf(stderr, "usage: see the top of perf_procs_harness.c\n");
    return 2;
}
