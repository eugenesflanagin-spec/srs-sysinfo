/* cuda_toolkit.h - the optional NVIDIA CUDA toolkit (only its runtime compiler, NVRTC, matters to SRS): whether it is
 * worth offering, the plain-English explanation, the install command for this distribution, the "don't ask again"
 * setting, and the checks before an install. Shared by the GUI (ask dialog, Options entry) and the CLI
 * (sysreport-search cuda-toolkit). Linux only. Nothing here installs anything by itself: the GUI and the CLI run the
 * command only after the user has seen it and said yes. SRS_CTK_DRY_RUN=1 never runs a package manager (tests).
 * The package manager, sudo and pkexec are only ever taken from /usr/bin, /usr/sbin, /bin or /sbin, by their full path,
 * never looked up in PATH (a program of the same name in the user's own folders could be anything). Only with
 * SRS_CTK_DRY_RUN=1, SRS_CTK_TOOL_DIRS (folders, colon-separated) replaces those four folders, so the tests can point at
 * fake tools that are never run. */
#ifndef CUDA_TOOLKIT_H
#define CUDA_TOOLKIT_H

#include <stddef.h>
#include "gpu_backend.h"

/* where the menu entry is; every message that mentions "later" uses these */
#define CTK_MENU_LABEL "NVIDIA CUDA toolkit for GPU tests..."
#define CTK_MENU_PATH "Options > NVIDIA CUDA toolkit for GPU tests..."
#define CTK_CLI "sysreport-search cuda-toolkit --install"

typedef struct {
    gb_cuda_state cu;          /* libcuda / ZLUDA / NVRTC (gpuburn_rt.c) */
    int nvidia_gpu;            /* an NVIDIA display device on the PCI bus */
    int relevant;              /* worth offering: (an NVIDIA GPU or a libcuda) and no NVRTC */
} ctk_state;

enum { CTK_PM_NONE, CTK_PM_PACMAN, CTK_PM_APT, CTK_PM_DNF, CTK_PM_ZYPPER };

typedef struct {
    int pm;                    /* CTK_PM_* found in PATH (and matching /etc/os-release) */
    int manual;                /* 1 = no command: only instructions (manual[]) */
    char distro[96];           /* PRETTY_NAME from os-release */
    char pkg[64];              /* the package that will be installed */
    char size[120];            /* about how big, plain words */
    char exe[512];             /* the package manager's full path (a system folder only), e.g. /usr/bin/pacman: what runs */
    char argv[8][64];          /* the package manager command WITHOUT sudo / pkexec, e.g. pacman -S --needed cuda
                                  (argv[0] is the plain name, to show; exe replaces it when it runs) */
    int argc;
    char noninteractive[24];   /* the extra flag when there is no terminal to answer questions (pkexec), or "" */
    char cmd[300];             /* argv joined, to show */
    char manual_text[700];     /* instructions when manual = 1 (also extra notes otherwise) */
} ctk_plan;

void ctk_detect(ctk_state *s, int recheck);
int ctk_plan_install(ctk_plan *p);
/* ctk_plan_install asks apt-cache (Debian / Ubuntu) once per package and remembers the answer for the session, so the
   GUI's Run button never waits for it; ctk_plan_refresh forgets the answers (after an install). Thread-safe: the GUI
   asks ahead on a worker thread. */
void ctk_plan_refresh(void);
const char *ctk_explain(void);        /* what the toolkit is, plain words first */
const char *ctk_zluda_warning(void);  /* why installing it is risky with ZLUDA */
const char *ctk_later_hint(void);     /* "You can install it later from ..." */
void ctk_status_text(const ctk_state *s, const ctk_plan *p, char *b, size_t n);

/* SRSDEF.ini [SRSDEF] "Ask CUDA Toolkit": 1 = the GUI may ask (default), 0 = the user said don't ask again */
int ctk_ask_get(void);
int ctk_ask_set(int on);
const char *ctk_ini_path(char *b, size_t n);

/* before an install: 0 = go on, else why not (package database locked, no network, nothing to ask for the password) */
int ctk_precheck(const ctk_plan *p, int have_terminal, char *why, size_t wn);
const char *ctk_find_terminal(char *b, size_t n);    /* a terminal emulator in PATH, or NULL */
const char *ctk_root_tool(void);                      /* "sudo", "pkexec" or NULL: the name, to show */
const char *ctk_root_path(void);                      /* its full path in a system folder (what runs), or NULL */
const char *ctk_pkexec_path(void);                    /* pkexec's full path in a system folder, or NULL */
int ctk_dry_run(void);
/* the shell line a terminal window runs: root tool + command, the exit status written to statusfile, then a pause */
void ctk_terminal_script(const ctk_plan *p, const char *statusfile, char *b, size_t n);
/* argv for a terminal emulator to run /bin/sh -c script; returns argc (0 = unknown terminal) */
int ctk_terminal_argv(const char *term, const char *script, const char **argv, int max);
/* plain words for a finished install: exit status of the package manager (or -1 = could not start) */
void ctk_result_text(const ctk_plan *p, int status, const ctk_state *after, char *b, size_t n);

#endif
