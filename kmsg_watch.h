/* kmsg_watch.h - watch the kernel log for GPU resets while a GPU test runs (gpu-burn --watch-kernel, new in SRS 2.6).
 *
 * Why: a GPU that is undervolted or overclocked too far often does not fail inside the test program. The driver
 * notices that the GPU stopped answering one of its work queues ("ring timeout"), resets the GPU, and the program that
 * was hit may be the desktop (the window manager), not the test. The kernel log is where that is written.
 *
 * Read-only: /dev/kmsg is opened for reading, at its end (only lines that appear after the start count). Nothing is
 * ever written to it. Tests set SRS_TEST_KMSG=<file>: that text file is read instead, from its start, and followed.
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#ifndef KMSG_WATCH_H
#define KMSG_WATCH_H

#include <stddef.h>

#define KW_MAXEV 32
#define KW_MAXOTHER 8

typedef struct {
    double t_s;            /* seconds after the watch began, when the line was seen */
    const char *kind;      /* "ring-timeout", "gpu-reset", "gpu-reset-done", "device-lost", "parser-error", "nvidia-xid", "gpu-hang" */
    char pci[16];          /* the card the line names ("0000:03:00.0"; NVIDIA: "0000:01:00"), "" when it names none */
    char process[48];      /* the program the driver blames, "" when the log does not say */
    char detail[48];       /* ring name, reset mode ("MODE1"), Xid number */
    int vram_lost;         /* a reset that wiped the video memory */
    char text[200];        /* the kernel's line (message part) */
} kw_event;

typedef struct kw kw;

/* start watching. NULL = the kernel log cannot be read here (why says so, in plain words) */
kw *kw_open(char *why, size_t wn);
/* read what is new (never blocks); call a few times a second. Returns the number of events so far */
int kw_poll(kw *k);
/* the events seen so far; other: driver error lines that are not a known reset line (shown, not counted) */
int kw_events(const kw *k, const kw_event **ev);
int kw_others(const kw *k, const char *const **lines);
/* 1 when the event is about this card, or cannot be told apart from it (a line with no address, or pci is "") */
int kw_event_for(const kw_event *e, const char *pci);
/* a short plain-English summary of the events of one card, for the result's reason ("" = none) */
void kw_summary(const kw *k, const char *pci, char *out, size_t n);
void kw_close(kw *k);

/* one kernel line -> its kind (NULL = not a GPU reset line). Pure; the pattern table lives here. */
const char *kw_classify(const char *line, char *pci, size_t pn, char *detail, size_t dn);

#endif
