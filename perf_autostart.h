/* perf_autostart.h - everything that starts at login, and safe per-user ways to change it
 * (the Startup Apps page of the Performance tab; GLib only, no GTK, so tests can drive it).
 *
 * Sources: XDG autostart entries (~/.config/autostart over $XDG_CONFIG_DIRS/autostart and
 * /etc/xdg/autostart, merged by file name the way the XDG spec says), systemd user units that are
 * enabled (systemctl --user), Plasma login scripts (the *.sh files in ~/.config/plasma-workspace/env) and the
 * legacy ~/.config/autostart-scripts.
 *
 * Changes are per user only: nothing under /etc (or any system directory) is ever written, and
 * nothing needs root. Files are written atomically (temporary file, fsync, rename) with GKeyFile
 * keeping comments and translations; the first change to a file the user already had keeps a copy
 * "<name>.desktop.srs-bak" once. "Remove" moves a user entry to ~/.config/autostart/.srs-trash
 * (restorable). Every change is recorded in $XDG_STATE_HOME/srs/autostart-undo.log so Undo works
 * across restarts.
 *
 * Tests: SRS_TEST_SYSTEMCTL=/path/to/fake replaces systemctl; SRS_TEST_AUTOSTART_NO_ETC=1 leaves
 * /etc/xdg/autostart out unless it is listed in XDG_CONFIG_DIRS.
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#ifndef SRS_PERF_AUTOSTART_H
#define SRS_PERF_AUTOSTART_H

#include <glib.h>

typedef enum {
    AS_SRC_XDG_USER,               /* only in ~/.config/autostart */
    AS_SRC_XDG_SYSTEM,             /* a system entry, no user file */
    AS_SRC_XDG_OVERRIDE,           /* a system entry with a user file of the same name (SRS's or the user's own copy) */
    AS_SRC_SYSTEMD,                /* systemd --user unit */
    AS_SRC_PLASMA_ENV,             /* a .sh file in ~/.config/plasma-workspace/env */
    AS_SRC_SCRIPT                  /* ~/.config/autostart-scripts/ (legacy) */
} as_source;

typedef struct {
    char *key;                     /* unique: "xdg:foo.desktop", "systemd:foo.service", "env:foo.sh", "script:foo" */
    char *id;                      /* file / unit name */
    as_source source;
    char *name, *comment, *icon;
    char *exec;                    /* the command without the delay wrapper */
    char *raw_exec;                /* Exec= as written */
    int delay;                     /* seconds (X-GNOME-Autostart-Delay or the sh -c "sleep N && ..." wrapper), 0 none */
    int enabled;                   /* the on/off switch: not Hidden=true, X-GNOME-Autostart-enabled not false, unit enabled */
    int starts;                    /* will really start at the next login (enabled and nothing else stops it) */
    int hidden;                    /* hidden for any reason (the Show Hidden filter) */
    int prog_missing;              /* the program cannot be found */
    int not_this_desktop;          /* OnlyShowIn / NotShowIn exclude $XDG_CURRENT_DESKTOP */
    char *notes;                   /* why it is hidden / will not start, plain words */
    char *path;                    /* the file in effect (user file, system file, script, unit file) */
    char *sys_path;                /* the system entry under a user override */
    int srs_override;              /* the user file is SRS's override copy (X-SRS-Override=true) */
    int srs_added;                 /* X-SRS-Added=true */
    char *wanted_by;               /* systemd: WantedBy */
} as_entry;

typedef struct { GPtrArray *items; char *desktop; } as_list;   /* items: as_entry*, sorted by name */

as_list *as_load(void);
void as_list_free(as_list *l);
const as_entry *as_find(const as_list *l, const char *key_or_id);

/* the source as shown in the Source column */
const char *as_source_name(as_source s);
/* "Remove" for this entry, or "Disable for Me" (system entries) / "Disable" (systemd units) */
const char *as_remove_label(const as_entry *e);

/* each returns TRUE on success; *err (may be NULL) gets a plain-words message otherwise.
   The caller re-reads with as_load() afterwards and shows the real result. */
gboolean as_set_enabled(const as_entry *e, gboolean on, char **err);
gboolean as_remove(const as_entry *e, char **err);
/* name / cmd may be NULL (unchanged); delay < 0 unchanged; enabled < 0 unchanged */
gboolean as_edit(const as_entry *e, const char *name, const char *cmd, int delay, int enabled, char **err);
/* creates ~/.config/autostart/<id>.desktop; icon / comment may be NULL; out_key gets the new key */
gboolean as_add(const char *name, const char *cmd, const char *icon, const char *comment, int delay, gboolean only_this_desktop,
                const char *base_id, char **out_key, char **err);

/* undo the last recorded change (also one from an earlier run); describe gets what it was */
gboolean as_can_undo(char **describe);
gboolean as_undo(char **describe, char **err);

/* removed entries in ~/.config/autostart/.srs-trash: file names; name gets the entry's Name */
GPtrArray *as_trash_list(void);    /* char* file names, free with g_ptr_array_unref */
char *as_trash_name(const char *file);
gboolean as_restore(const char *file, char **err);

/* Restore Default: what it would do for this entry (NULL = nothing to restore): a system entry's user copy is removed,
   a user entry gets its original back (<file>.srs-bak, or the old manager's startup-backup/<id>.orig), a systemd unit
   gets systemctl --user preset. Undo-logged like every change. */
const char *as_default_what(const as_entry *e);
gboolean as_restore_default(const as_entry *e, char **err);

/* the old (2.2) Startup manager's backups in ~/.config/srs/startup-backup ("<id>.desktop.orig" / ".<date>"):
   listed in Restore Removed; bringing one back COPIES it into ~/.config/autostart (the backup is never deleted).
   as_old_backups_offer_once() returns how many there are the first time it is called (then 0 for good). */
GPtrArray *as_old_backups(void);
char *as_old_backup_name(const char *file);
gboolean as_import_old_backup(const char *file, char **err);
int as_old_backups_offer_once(void);

/* NULL when the command looks runnable, else why not ("Program not found: foo") */
char *as_check_cmd(const char *cmd);
/* the delay wrapper KDE needs (it ignores X-GNOME-Autostart-Delay): sh -c "sleep N && cmd" */
char *as_wrap_delay(const char *cmd, int delay);
/* the opposite: returns the inner command and sets *delay, or NULL when exec is not wrapped */
char *as_unwrap_delay(const char *exec, int *delay);

/* the folder that holds the entry (for Open File Location) */
char *as_folder(const as_entry *e);

/* installed applications (for Add): visible .desktop files of XDG_DATA_DIRS + ~/.local/share */
typedef struct { char *id, *name, *exec, *icon, *comment; } as_app;
GPtrArray *as_apps(void);          /* as_app*, sorted by name */
void as_app_free(as_app *a);

#endif
