/* perf_data_harness.c - prints what perf_data.c samples, for tests/perf-view-test.sh.
 *   perf_data_harness [--gpu] [--no-procs] [--live N] [ROOT_T0 ROOT_T1]
 * With two roots (SRS_TEST_PROC_ROOT trees) it samples ROOT_T0, then ROOT_T1 with dt = 1 s and
 * prints the second snapshot (rates and percentages come from the difference). --live N samples
 * the real system N times one second apart and prints the last one.
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#include "../perf_data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    int gpu = 0, live = 0, a = 1, noprocs = 0;
    static char buf[1 << 22];
    const pd_snap *s = NULL;
    pd_sampler *ps;
    for (; a < argc && argv[a][0] == '-'; a++) {
        if (!strcmp(argv[a], "--gpu")) gpu = 1;
        else if (!strcmp(argv[a], "--no-procs")) noprocs = 1;
        else if (!strcmp(argv[a], "--live") && a + 1 < argc) live = atoi(argv[++a]);
    }
    if (!live && argc - a == 2) pd_set_root(argv[a]);
    ps = pd_new(gpu);
    if (!ps) return 1;
    if (noprocs) pd_set_procs(ps, 0);
    if (live) {
        for (int i = 0; i < live; i++) { if (i) sleep(1); s = pd_sample(ps, 0); }
    } else if (argc - a == 2) {
        pd_sample(ps, 1.0);
        pd_set_root(argv[a + 1]);
        s = pd_sample(ps, 1.0);
    } else {
        fprintf(stderr, "usage: %s [--gpu] [--live N] [ROOT_T0 ROOT_T1]\n", argv[0]);
        return 2;
    }
    pd_dump(s, buf, sizeof buf);
    fputs(buf, stdout);
    pd_free(ps);
    return 0;
}
