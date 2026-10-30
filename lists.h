/* lists.h - "Update hardware lists" (2.4): the user's own, newer copies of pci.ids, usb.ids and pnp.ids.
 * See lists.c. Internal to SRS. */
#ifndef SRS_LISTS_H
#define SRS_LISTS_H

#include <stddef.h>

#define SRS_NLISTS 3

typedef struct {
    const char *file, *name, *url;
    const char *magic;     /* a line near the top of a real list, NULL = none */
    int min_entries;       /* fewer means a broken download */
} srs_list;

extern const srs_list srs_lists[SRS_NLISTS];

/* ~/.local/share/srs/lists ($XDG_DATA_HOME/srs/lists); 0 = ok */
int srs_lists_dir(char *out, size_t n);
/* the list's "Version:" date (2026.10.01), "" when it has none (pnp.ids); -1 when it is missing */
int srs_ids_version(const char *path, char *out, size_t n);
/* entries (non-comment lines) of a list, for lists without a version line; -1 when missing */
long srs_ids_entries(const char *path);
/* the newer of the user's and the system's copy of FILE (pci.ids ...); NULL if neither. *src says which */
const char *srs_ids_path(const char *file, char *out, size_t n, const char **src);
/* download and check the lists (which = index into srs_lists, -1 = all); report gets one line per list.
   Returns the number updated, -1 when there is nowhere to keep them. Blocks: run it off the GUI thread. */
int srs_lists_update(int which, char *report, size_t rn);

#endif
