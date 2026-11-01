/* autostart_harness.c - drives perf_autostart.c from the command line for tests/autostart-test.sh.
 *   list | enable KEY | disable KEY | remove KEY | restore FILE | trash | undo | can-undo |
 *   add NAME CMD DELAY ONLY_THIS_DESKTOP(0/1) | edit KEY NAME CMD DELAY | check CMD | wrap CMD N | unwrap EXEC |
 *   default KEY | default-what KEY | old-list | old-import FILE | old-offer
 * Exit 0 on success, 1 when the action failed (the message goes to stderr), 2 on usage errors.
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#include "../perf_autostart.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(char *err) { fprintf(stderr, "error: %s\n", err ? err : "?"); g_free(err); return 1; }

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    char *err = NULL;
    as_list *l;
    const as_entry *e;
    if (!strcmp(c, "list")) {
        l = as_load();
        for (guint i = 0; i < l->items->len; i++) {
            e = l->items->pdata[i];
            printf("%s|on=%d|starts=%d|hidden=%d|missing=%d|desk=%d|%s|delay=%d|%s|%s|%s\n", e->key, e->enabled, e->starts, e->hidden,
                   e->prog_missing, e->not_this_desktop, as_source_name(e->source), e->delay, e->name, e->exec, e->notes);
        }
        as_list_free(l);
        return 0;
    }
    if (!strcmp(c, "trash")) {
        GPtrArray *a = as_trash_list();
        for (guint i = 0; i < a->len; i++) { char *n = as_trash_name(a->pdata[i]); printf("%s|%s\n", (char *)a->pdata[i], n); g_free(n); }
        g_ptr_array_unref(a);
        return 0;
    }
    if (!strcmp(c, "old-list")) {
        GPtrArray *a = as_old_backups();
        for (guint i = 0; i < a->len; i++) { char *n = as_old_backup_name(a->pdata[i]); printf("%s|%s\n", (char *)a->pdata[i], n); g_free(n); }
        g_ptr_array_unref(a);
        return 0;
    }
    if (!strcmp(c, "old-offer")) { printf("%d\n", as_old_backups_offer_once()); return 0; }
    if (!strcmp(c, "old-import") && argc > 2) return as_import_old_backup(argv[2], &err) ? 0 : fail(err);
    if (!strcmp(c, "undo")) { char *d = NULL; if (!as_undo(&d, &err)) return fail(err); printf("undone: %s\n", d); g_free(d); return 0; }
    if (!strcmp(c, "can-undo")) { char *d = NULL; int ok = as_can_undo(&d); printf("%s\n", ok ? d : "nothing"); g_free(d); return 0; }
    if (!strcmp(c, "restore") && argc > 2) return as_restore(argv[2], &err) ? 0 : fail(err);
    if (!strcmp(c, "check") && argc > 2) { char *w = as_check_cmd(argv[2]); printf("%s\n", w ? w : "ok"); g_free(w); return 0; }
    if (!strcmp(c, "wrap") && argc > 3) { char *w = as_wrap_delay(argv[2], atoi(argv[3])); printf("%s\n", w); g_free(w); return 0; }
    if (!strcmp(c, "unwrap") && argc > 2) { int d = 0; char *w = as_unwrap_delay(argv[2], &d); printf("%d|%s\n", d, w ? w : "(none)"); g_free(w); return 0; }
    if (!strcmp(c, "add") && argc > 5) {
        char *key = NULL;
        if (!as_add(argv[2], argv[3], NULL, NULL, atoi(argv[4]), atoi(argv[5]), NULL, &key, &err)) return fail(err);
        printf("%s\n", key);
        g_free(key);
        return 0;
    }
    if (argc < 3) { fprintf(stderr, "usage: see the comment at the top of autostart_harness.c\n"); return 2; }
    l = as_load();
    e = as_find(l, argv[2]);
    if (!e) { fprintf(stderr, "error: no entry %s\n", argv[2]); as_list_free(l); return 1; }
    {
        int ok;
        if (!strcmp(c, "enable")) ok = as_set_enabled(e, TRUE, &err);
        else if (!strcmp(c, "disable")) ok = as_set_enabled(e, FALSE, &err);
        else if (!strcmp(c, "remove")) ok = as_remove(e, &err);
        else if (!strcmp(c, "label")) { printf("%s\n", as_remove_label(e)); ok = 1; }
        else if (!strcmp(c, "default")) ok = as_restore_default(e, &err);
        else if (!strcmp(c, "default-what")) { const char *w = as_default_what(e); printf("%s\n", w ? w : "none"); ok = 1; }
        else if (!strcmp(c, "edit") && argc > 5) ok = as_edit(e, argv[3], argv[4], atoi(argv[5]), -1, &err);
        else { as_list_free(l); return 2; }
        as_list_free(l);
        return ok ? 0 : fail(err);
    }
}
