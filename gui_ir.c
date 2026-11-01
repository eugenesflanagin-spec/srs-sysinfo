/* gui_ir.c - the IR Blaster tab's Remotes view (srs-gui main window, USB & PCI devices page).
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. The behaviour and wording follow
 * the author's IR Remote app (MIT); the dongle protocol is elksmart-comm's (ir.c, 0BSD, Přemysl Eric Janouch).
 *
 * The dongle (ELK Smart / OCRUSTAR, 045c:0132 / 0184 / 0195 / 0134) is looked for in sysfs every 2 s, so plugging it
 * in or out shows at once; without one the dongle controls are greyed and a line says what to plug in. The codes are
 * IR Remote's remotes.json: every change reads the file again first, changes it and writes it back at once (IR Remote
 * may have changed it meanwhile), and the view follows changes IR Remote makes. Learn, Send, Test and Reset run in a
 * worker thread; only one at a time (IR Remote's lock file keeps SRS and IR Remote from using the dongle together). */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "gui_ir.h"
#include "ir.h"

#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

enum { RC_NAME, RC_COUNT, RC_N };
enum { BC_NAME, BC_TYPE, BC_BYTES, BC_FREQ, BC_SHORTCUT, BC_IDX, BC_DIM, BC_TIP, BC_N };
enum { RESP_DONE = 100, RESP_RETRY };
enum { J_SEND, J_PROBE, J_RESET, J_LEARN };

typedef struct irm {
    int refs, dead, state, busy, broken;
    GtkWidget *box, *icon, *status, *fixrow;
    GtkWidget *b_test, *b_reset, *b_open;
    GtkWidget *rview, *bview, *codes_title;
    GtkListStore *rstore, *bstore;
    GtkWidget *b_radd, *b_rren, *b_rdel;
    GtkWidget *b_send, *repeat, *dry, *b_learn, *b_type, *b_bren, *b_bdel, *b_imp, *b_exp;
    GtkTextBuffer *logbuf;
    GtkWidget *logview;
    guint poll;
    char remote[256];          /* the selected remote's name */
    gint64 mtime;
    goffset size;
    void (*on_state)(int, gpointer);
    gpointer on_state_data;
} irm;

typedef struct {
    int kind, ok, finished;
    irm *m;
    volatile int cancel;
    char hex[8192], label[400];
    int nec, repeat;
    unsigned long freq;
    char code[8192], hint[256], msg[1000];
    GtkWidget *dlg;            /* learn: its dialog while it is open */
} job;

static void irm_unref(irm *m)
{
    if (--m->refs > 0) return;
    g_free(m);
}

static void sensitivity(irm *m);
static void reload(irm *m);

/* ---- the log line at the bottom ---- */
static void say(irm *m, const char *fmt, ...) G_GNUC_PRINTF(2, 3);
static void say(irm *m, const char *fmt, ...)
{
    va_list ap;
    char *s, t[16];
    GtkTextIter end;
    time_t now = time(NULL);
    if (m->dead) return;
    va_start(ap, fmt);
    s = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    strftime(t, sizeof t, "%H:%M:%S", localtime(&now));
    gtk_text_buffer_get_end_iter(m->logbuf, &end);
    if (gtk_text_buffer_get_char_count(m->logbuf)) gtk_text_buffer_insert(m->logbuf, &end, "\n", 1);
    gtk_text_buffer_insert(m->logbuf, &end, t, -1);
    gtk_text_buffer_insert(m->logbuf, &end, "  ", 2);
    gtk_text_buffer_insert(m->logbuf, &end, s, -1);
    {   /* follow the newest line (a mark: scrolling to an iter does nothing before the view has its size) */
        GtkTextMark *mk = gtk_text_buffer_get_mark(m->logbuf, "srs-ir-end");
        gtk_text_buffer_get_end_iter(m->logbuf, &end);
        if (!mk) mk = gtk_text_buffer_create_mark(m->logbuf, "srs-ir-end", &end, FALSE);
        else gtk_text_buffer_move_mark(m->logbuf, mk, &end);
        gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(m->logview), mk, 0, TRUE, 0, 1);
    }
    g_free(s);
}

static GtkWindow *top(irm *m)
{
    GtkWidget *t = gtk_widget_get_toplevel(m->box);
    return GTK_IS_WINDOW(t) ? GTK_WINDOW(t) : NULL;
}

static void message(irm *m, GtkMessageType type, const char *head, const char *body)
{
    GtkWidget *d = gtk_message_dialog_new(top(m), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, type, GTK_BUTTONS_CLOSE, "%s", head);
    if (body && *body) gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "%s", body);
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}

static int confirm(irm *m, const char *head, const char *body, const char *yes)
{
    GtkWidget *d = gtk_message_dialog_new(top(m), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "%s", head);
    int r;
    if (body && *body) gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "%s", body);
    gtk_dialog_add_buttons(GTK_DIALOG(d), "_Cancel", GTK_RESPONSE_CANCEL, yes, GTK_RESPONSE_OK, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_CANCEL);
    r = gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
    return r == GTK_RESPONSE_OK;
}

/* a one-line question; NULL = cancelled */
static char *ask_text(irm *m, const char *title, const char *prompt, const char *initial, const char *ok)
{
    GtkWidget *d = gtk_dialog_new_with_buttons(title, top(m), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                               "_Cancel", GTK_RESPONSE_CANCEL, ok, GTK_RESPONSE_OK, NULL);
    GtkWidget *c = gtk_dialog_get_content_area(GTK_DIALOG(d)), *l = gtk_label_new(prompt), *e = gtk_entry_new();
    char *res = NULL;
    gtk_container_set_border_width(GTK_CONTAINER(c), 10);
    gtk_box_set_spacing(GTK_BOX(c), 6);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_entry_set_text(GTK_ENTRY(e), initial ? initial : "");
    gtk_entry_set_activates_default(GTK_ENTRY(e), TRUE);
    gtk_entry_set_width_chars(GTK_ENTRY(e), 32);
    gtk_box_pack_start(GTK_BOX(c), l, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(c), e, FALSE, FALSE, 0);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
    gtk_widget_show_all(d);
    if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) res = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(e))));
    gtk_widget_destroy(d);
    if (res && !*res) { g_free(res); res = NULL; }
    return res;
}

/* ---- the codes file: read fresh, change, write at once ---- */
static ir_doc *doc_read(irm *m, int quiet)
{
    char err[400] = "";
    int exists = 0;
    ir_doc *d = ir_doc_load(ir_codes_path(), &exists, err, sizeof err);
    if (!d && !quiet) message(m, GTK_MESSAGE_ERROR, "The codes file cannot be read",
                              "SRS leaves it as it is (nothing is written). Fix or move it, then press Refresh.");
    return d;
}

static void file_stamp(irm *m)
{
    GStatBuf st;
    if (g_stat(ir_codes_path(), &st) == 0) { m->mtime = (gint64)st.st_mtime * 1000000000 + st.st_mtim.tv_nsec; m->size = st.st_size; }
    else { m->mtime = 0; m->size = -1; }
}

static int doc_write(irm *m, ir_doc *d, const char *what)
{
    char err[600] = "";
    int ok = ir_doc_save(d, ir_codes_path(), err, sizeof err);
    if (ok) say(m, "%s (saved in %s)", what, ir_codes_path());
    else { say(m, "Not saved: %s", err); message(m, GTK_MESSAGE_ERROR, "The codes could not be saved", err); }
    reload(m);
    return ok;
}

/* ---- selection ---- */
static int sel_remote(irm *m, const ir_doc *d)
{
    return *m->remote ? ir_find_remote(d, m->remote) : -1;
}

static int sel_button(irm *m)
{
    GtkTreeModel *model;
    GtkTreeIter it;
    int idx = -1;
    if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(m->bview)), &model, &it))
        gtk_tree_model_get(model, &it, BC_IDX, &idx, -1);
    return idx;
}

static void fill_buttons(irm *m, const ir_doc *d)
{
    int r = d ? sel_remote(m, d) : -1, b, keep = sel_button(m);
    char t[400];
    GtkTreeIter it;
    gtk_list_store_clear(m->bstore);
    if (r < 0) snprintf(t, sizeof t, "Codes");
    else snprintf(t, sizeof t, "Codes of %s", ir_remote_name(d, r));
    gtk_label_set_text(GTK_LABEL(m->codes_title), t);
    for (b = 0; r >= 0 && b < ir_nbuttons(d, r); b++) {
        ir_button bt;
        char by[24], fq[32], tip[600];
        ir_button_at(d, r, b, &bt);
        snprintf(by, sizeof by, "%zu", strlen(bt.code) / 2);
        snprintf(fq, sizeof fq, "%lu Hz", bt.freq);
        snprintf(tip, sizeof tip, "%s\n%s%s", bt.name, *bt.code ? "Double-click or Send: point the dongle at the device first. Code: " : "Not learned yet: Learn Code with this name.",
                 *bt.code ? (strlen(bt.code) > 120 ? "(long, see Copy in the report)" : bt.code) : "");
        gtk_list_store_insert_with_values(m->bstore, &it, -1, BC_NAME, bt.name, BC_TYPE, *bt.code ? (strcmp(bt.type, "nec") ? "Learned" : "NEC") : "Not learned",
                                          BC_BYTES, *bt.code ? by : "-", BC_FREQ, fq, BC_SHORTCUT, bt.shortcut, BC_IDX, b, BC_DIM, !*bt.code, BC_TIP, tip, -1);
        if (b == keep) gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(m->bview)), &it);
    }
}

/* read the file again and show it (keeps the selected remote by name) */
static void reload(irm *m)
{
    ir_doc *d;
    char err[400] = "";
    int exists = 0, r, sel = -1;
    GtkTreeIter it;
    if (m->dead) return;
    d = ir_doc_load(ir_codes_path(), &exists, err, sizeof err);
    file_stamp(m);
    m->broken = d == NULL;
    gtk_list_store_clear(m->rstore);
    if (!d) {
        say(m, "Codes file %s: %s - nothing will be written until it is fixed.", ir_codes_path(), err);
        fill_buttons(m, NULL);
        sensitivity(m);
        return;
    }
    if (*m->remote) sel = ir_find_remote(d, m->remote);
    if (sel < 0 && ir_nremotes(d)) { sel = 0; g_strlcpy(m->remote, ir_remote_name(d, 0), sizeof m->remote); }
    if (sel < 0) m->remote[0] = 0;
    for (r = 0; r < ir_nremotes(d); r++) {
        char c[40];
        int n = ir_nbuttons(d, r);
        snprintf(c, sizeof c, "%d", n);
        gtk_list_store_insert_with_values(m->rstore, &it, -1, RC_NAME, ir_remote_name(d, r), RC_COUNT, c, -1);
        if (r == sel) gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(m->rview)), &it);
    }
    fill_buttons(m, d);
    ir_doc_free(d);
    sensitivity(m);
}

static void on_remote_sel(GtkTreeSelection *s, gpointer data)
{
    irm *m = data;
    GtkTreeModel *model;
    GtkTreeIter it;
    char *name = NULL;
    ir_doc *d;
    if (!gtk_tree_selection_get_selected(s, &model, &it)) return;
    gtk_tree_model_get(model, &it, RC_NAME, &name, -1);
    if (name && strcmp(name, m->remote)) {
        g_strlcpy(m->remote, name, sizeof m->remote);
        if ((d = doc_read(m, 1))) { fill_buttons(m, d); ir_doc_free(d); }
    }
    g_free(name);
    sensitivity(m);
}

static void on_button_sel(GtkTreeSelection *s, gpointer data) { (void)s; sensitivity(data); }

/* ---- dongle status ---- */
static void show_state(irm *m)
{
    ir_dongle d;
    char s[800];
    static const char *const icons[] = { "action-unavailable-symbolic", "dialog-warning-symbolic", "emblem-ok-symbolic" };
    GtkStyleContext *sc = gtk_widget_get_style_context(m->status);
    ir_detect(&d);
    ir_status_text(&d, s, sizeof s);
    gtk_label_set_text(GTK_LABEL(m->status), s);
    gtk_image_set_from_icon_name(GTK_IMAGE(m->icon), icons[d.state], GTK_ICON_SIZE_LARGE_TOOLBAR);
    if (d.state == IR_ABSENT) gtk_style_context_add_class(sc, "dim-label"); else gtk_style_context_remove_class(sc, "dim-label");
    gtk_widget_set_visible(m->fixrow, d.state == IR_NOPERM && !d.nonode);
    if (d.state != m->state) {
        int was = m->state;
        m->state = d.state;
        if (was >= 0) say(m, "%s", d.state == IR_READY ? "IR blaster plugged in and ready." : d.state == IR_NOPERM ? "IR blaster plugged in, but no permission to use it (see the fix above)."
                                                                                                 : "IR blaster unplugged.");
        if (m->on_state) m->on_state(m->state, m->on_state_data);
    }
    sensitivity(m);
}

static gboolean on_poll(gpointer data)
{
    irm *m = data;
    GStatBuf st;
    gint64 mt = 0;
    goffset sz = -1;
    if (m->dead) return G_SOURCE_REMOVE;
    show_state(m);
    if (g_stat(ir_codes_path(), &st) == 0) { mt = (gint64)st.st_mtime * 1000000000 + st.st_mtim.tv_nsec; sz = st.st_size; }
    if ((mt != m->mtime || sz != m->size) && !m->busy) {   /* IR Remote (or sysreport-search ir) changed the codes */
        reload(m);
        say(m, "The codes file changed (IR Remote?) - shown again.");
    }
    return G_SOURCE_CONTINUE;
}

static void on_map(GtkWidget *w, gpointer data) { (void)w; show_state(data); }

static void sensitivity(irm *m)
{
    int ready = m->state == IR_READY && !m->busy, r = *m->remote != 0, b = sel_button(m) >= 0, learned = 0, dry;
    GtkTreeModel *model;
    GtkTreeIter it;
    if (m->dead) return;
    dry = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->dry));
    if (b && gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(m->bview)), &model, &it)) {
        gboolean dim = TRUE;
        gtk_tree_model_get(model, &it, BC_DIM, &dim, -1);
        learned = !dim;
    }
    gtk_widget_set_sensitive(m->b_test, ready);
    gtk_widget_set_sensitive(m->b_reset, ready);
    gtk_widget_set_sensitive(m->b_learn, ready && !m->broken);
    gtk_widget_set_sensitive(m->b_send, learned && !m->busy && (dry || m->state == IR_READY));
    gtk_widget_set_sensitive(m->repeat, m->state == IR_READY || dry);
    gtk_widget_set_sensitive(m->b_radd, !m->broken);
    gtk_widget_set_sensitive(m->b_rren, !m->broken && r);
    gtk_widget_set_sensitive(m->b_rdel, !m->broken && r);
    gtk_widget_set_sensitive(m->b_type, !m->broken);
    gtk_widget_set_sensitive(m->b_bren, !m->broken && b);
    gtk_widget_set_sensitive(m->b_bdel, !m->broken && b);
    gtk_widget_set_sensitive(m->b_imp, !m->broken);
    gtk_widget_set_sensitive(m->b_exp, !m->broken);
    gtk_widget_set_tooltip_text(m->b_send, m->state == IR_READY || dry ? "Send the selected code through the IR blaster (double-click a code does the same). "
                                                                         "Point the dongle at the device first."
                                                                       : "Plug in the IR blaster to send (or turn on Dry Run to see the USB packets only)");
}

/* ---- worker jobs: one at a time ---- */
static gboolean job_done(gpointer data)
{
    job *j = data;
    irm *m = j->m;
    j->finished = 1;
    m->busy = 0;
    if (j->kind == J_LEARN && j->dlg) {   /* the dialog takes it from here */
        if (!m->dead) sensitivity(m);
        gtk_dialog_response(GTK_DIALOG(j->dlg), RESP_DONE);
        irm_unref(m);
        return G_SOURCE_REMOVE;
    }   /* the dialog takes it from here */
    if (!m->dead) {
        if (j->kind == J_SEND) say(m, j->ok ? "%s %s" : "Not sent (%2$s): %1$s", j->msg, j->label);
        else if (j->kind == J_LEARN) say(m, "%s", j->msg);
        else say(m, "%s", j->msg);
        if (!j->ok && j->kind != J_LEARN) {
            if (j->kind == J_SEND) message(m, GTK_MESSAGE_WARNING, "The code was not sent", j->msg);
        }
        sensitivity(m);
    }
    irm_unref(m);
    g_free(j);
    return G_SOURCE_REMOVE;
}

static gpointer job_thread(gpointer data)
{
    job *j = data;
    switch (j->kind) {
    case J_SEND: j->ok = ir_send(j->hex, j->nec, j->freq, j->repeat, &j->cancel, j->msg, sizeof j->msg); break;
    case J_PROBE: j->ok = ir_probe(j->msg, sizeof j->msg); break;
    case J_RESET: j->ok = ir_reset(j->msg, sizeof j->msg); break;
    case J_LEARN: j->ok = ir_learn(20, &j->cancel, j->code, sizeof j->code, j->hint, sizeof j->hint, j->msg, sizeof j->msg); break;
    }
    g_idle_add(job_done, j);
    return NULL;
}

static job *job_start(irm *m, int kind)
{
    job *j = g_new0(job, 1);
    j->kind = kind;
    j->m = m;
    m->refs++;
    m->busy = 1;
    sensitivity(m);
    if (kind == J_PROBE) say(m, "Asking the dongle to identify itself (no infrared is sent)...");
    if (kind == J_RESET) say(m, "Resetting the dongle (USB reset), then asking it to identify itself...");
    return j;
}
static void job_go(job *j) { GThread *t = g_thread_new("srs-ir", job_thread, j); g_thread_unref(t); }

static void on_test(GtkButton *b, gpointer data) { (void)b; job_go(job_start(data, J_PROBE)); }
static void on_reset(GtkButton *b, gpointer data) { (void)b; job_go(job_start(data, J_RESET)); }

static void on_send(GtkButton *bt, gpointer data)
{
    irm *m = data;
    ir_doc *d;
    ir_button b;
    int r, i = sel_button(m), rep = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(m->repeat));
    (void)bt;
    if (i < 0 || m->busy || !(d = doc_read(m, 0))) return;
    if ((r = sel_remote(m, d)) < 0 || !ir_button_at(d, r, i, &b) || !*b.code) { ir_doc_free(d); return; }
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->dry))) {
        char msg[300], *out = ir_dry_run(b.code, !strcmp(b.type, "nec"), b.freq, -1, msg, sizeof msg);
        if (out) {
            say(m, "Dry run: %s / %s (%s, %lu Hz) x%d - %s%s\n%s", ir_remote_name(d, r), b.name, b.type, b.freq, rep, msg,
                ir_last_protocol() < 0 ? " (the dongle has not said which protocol it uses yet: plain shown; Test asks it)" : "", g_strchomp(out));
            free(out);
        } else say(m, "Dry run: %s", msg);
        ir_doc_free(d);
        return;
    }
    if (m->state != IR_READY) { ir_doc_free(d); return; }
    {
        job *j = job_start(m, J_SEND);
        g_strlcpy(j->hex, b.code, sizeof j->hex);
        j->nec = !strcmp(b.type, "nec");
        j->freq = b.freq;
        j->repeat = rep;
        snprintf(j->label, sizeof j->label, "%s / %s", ir_remote_name(d, r), b.name);
        say(m, "Sending %s%s...", j->label, rep > 1 ? " several times" : "");
        job_go(j);
    }
    ir_doc_free(d);
}

static void on_code_activated(GtkTreeView *v, GtkTreePath *p, GtkTreeViewColumn *c, gpointer data)
{
    irm *m = data;
    (void)v; (void)p; (void)c;
    if (gtk_widget_is_sensitive(m->b_send)) on_send(NULL, m);
}

static void on_dry(GtkToggleButton *t, gpointer data) { (void)t; sensitivity(data); }

/* ---- naming a code (after Learn, or typed in) ---- */
static GtkWidget *remote_combo(irm *m, const ir_doc *d)
{
    GtkWidget *c = gtk_combo_box_text_new_with_entry();
    int r;
    for (r = 0; d && r < ir_nremotes(d); r++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c), ir_remote_name(d, r));
    gtk_entry_set_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(c))), *m->remote ? m->remote : "TV");
    gtk_widget_set_tooltip_text(c, "The remote to keep the code in: pick one, or type a new name");
    return c;
}

static GtkWidget *grid_row(GtkWidget *g, int row, const char *label, GtkWidget *w)
{
    GtkWidget *l = gtk_label_new_with_mnemonic(label);
    gtk_label_set_xalign(GTK_LABEL(l), 1);
    gtk_label_set_mnemonic_widget(GTK_LABEL(l), GTK_IS_COMBO_BOX(w) ? gtk_bin_get_child(GTK_BIN(w)) : w);
    gtk_grid_attach(GTK_GRID(g), l, 0, row, 1, 1);
    gtk_widget_set_hexpand(w, TRUE);
    gtk_grid_attach(GTK_GRID(g), w, 1, row, 1, 1);
    return w;
}

/* save code into remote / name (asks before replacing an existing code); 1 = saved */
static int save_code(irm *m, const char *remote, const char *name, const char *code, int nec, unsigned long freq)
{
    ir_doc *d;
    int r, b, ok;
    char what[700];
    if (!(d = doc_read(m, 0))) return 0;
    r = ir_find_remote(d, remote);
    b = r >= 0 ? ir_find_button(d, r, name) : -1;
    if (b >= 0) {
        ir_button bt;
        ir_button_at(d, r, b, &bt);
        if (*bt.code) {
            char q[600];
            snprintf(q, sizeof q, "%s already has a code called \"%s\". Replace its code with the new one? (Its name and shortcuts stay.)", ir_remote_name(d, r), bt.name);
            if (!confirm(m, "Replace the code?", q, "_Replace")) { ir_doc_free(d); return 0; }
        }
    }
    if (r < 0) r = ir_remote_add(d, remote);
    b = r >= 0 ? ir_button_set(d, r, name, code, nec, freq) : -1;
    if (b < 0) { ir_doc_free(d); message(m, GTK_MESSAGE_ERROR, "Not saved", "A remote and a code need a name."); return 0; }
    g_strlcpy(m->remote, ir_remote_name(d, r), sizeof m->remote);
    snprintf(what, sizeof what, "Saved %s / %s (%zu bytes)", ir_remote_name(d, r), name, strlen(code) / 2);
    ok = doc_write(m, d, what);
    ir_doc_free(d);
    return ok;
}


static gboolean learn_tick(gpointer data)
{
    GtkWidget *bar = data;
    gint64 t0 = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(bar), "t0"));
    double left = 20 - (g_get_monotonic_time() - t0) / 1e6;
    char s[80];
    if (left < 0) left = 0;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(bar), left / 20);
    snprintf(s, sizeof s, "Waiting for the remote... %.0f s left", left);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(bar), s);
    return G_SOURCE_CONTINUE;
}

static void on_learn(GtkButton *bt, gpointer data)
{
    irm *m = data;
    (void)bt;
    for (;;) {
        GtkWidget *d, *c, *l, *bar;
        job *j;
        guint tick;
        int r, again = 0;
        if (m->busy || m->state != IR_READY) return;
    /* MNEMONIC-MENU: ir-learn-wait */
        d = gtk_dialog_new_with_buttons("Learn a Code", top(m), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel", GTK_RESPONSE_CANCEL, NULL);
    /* MNEMONIC-END */
        c = gtk_dialog_get_content_area(GTK_DIALOG(d));
        gtk_container_set_border_width(GTK_CONTAINER(c), 12);
        gtk_box_set_spacing(GTK_BOX(c), 10);
        l = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(l), "<b>Press the button on your remote now.</b>\n\nHold the remote within 1 cm of the dongle, aim it straight at "
                                           "the tip and press the button once, holding it for about a second.");
        gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(l), 52);
        gtk_label_set_xalign(GTK_LABEL(l), 0);
        gtk_box_pack_start(GTK_BOX(c), l, FALSE, FALSE, 0);
        bar = gtk_progress_bar_new();
        gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(bar), TRUE);
        g_object_set_data(G_OBJECT(bar), "t0", GSIZE_TO_POINTER((gsize)g_get_monotonic_time()));
        learn_tick(bar);
        gtk_box_pack_start(GTK_BOX(c), bar, FALSE, FALSE, 0);
        gtk_widget_show_all(d);
        tick = g_timeout_add(250, learn_tick, bar);
        j = job_start(m, J_LEARN);
        j->dlg = d;
        say(m, "Learning: waiting up to 20 s for a remote button...");
        job_go(j);
        r = gtk_dialog_run(GTK_DIALOG(d));
        g_source_remove(tick);
        if (r != RESP_DONE) {   /* Cancel or closed: the worker stops, resets the dongle, and says so in the log */
            j->cancel = 1;
            j->dlg = NULL;
            j->kind = J_LEARN;
            gtk_widget_destroy(d);
            say(m, "Cancelling (the dongle is reset so it leaves learn mode)...");
            return;
        }
        gtk_widget_destroy(d);
        if (!j->ok) {
            GtkWidget *q = gtk_message_dialog_new(top(m), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
                                                  "No code was learned");
            gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(q), "%s", j->msg);
    /* MNEMONIC-MENU: ir-learn-failed */
            gtk_dialog_add_buttons(GTK_DIALOG(q), "_Close", GTK_RESPONSE_CANCEL, "_Try Again", RESP_RETRY, NULL);
    /* MNEMONIC-END */
            gtk_dialog_set_default_response(GTK_DIALOG(q), RESP_RETRY);
            say(m, "Learn: %s", j->msg);
            again = gtk_dialog_run(GTK_DIALOG(q)) == RESP_RETRY;
            gtk_widget_destroy(q);
        } else {
            ir_doc *doc = doc_read(m, 1);
            GtkWidget *g = gtk_grid_new(), *rc, *ne, *info;
            char s[700];
            say(m, "Learned a code: %zu bytes%s%s", strlen(j->code) / 2, *j->hint ? ", attempted pulse decode " : "", j->hint);
    /* MNEMONIC-MENU: ir-learn-save */
            d = gtk_dialog_new_with_buttons("Save the Code", top(m), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                            "_Discard", GTK_RESPONSE_CANCEL, "_Try Again", RESP_RETRY, "_Save", GTK_RESPONSE_OK, NULL);
            c = gtk_dialog_get_content_area(GTK_DIALOG(d));
            gtk_container_set_border_width(GTK_CONTAINER(c), 12);
            gtk_box_set_spacing(GTK_BOX(c), 10);
            snprintf(s, sizeof s, "Got it! %zu bytes%s%s%s. Name the button and pick the remote to keep it in.", strlen(j->code) / 2,
                     *j->hint ? " (attempted pulse decode: " : "", j->hint, *j->hint ? ")" : "");
            info = gtk_label_new(s);
            gtk_label_set_line_wrap(GTK_LABEL(info), TRUE);
            gtk_label_set_max_width_chars(GTK_LABEL(info), 52);
            gtk_label_set_xalign(GTK_LABEL(info), 0);
            gtk_box_pack_start(GTK_BOX(c), info, FALSE, FALSE, 0);
            gtk_grid_set_row_spacing(GTK_GRID(g), 6);
            gtk_grid_set_column_spacing(GTK_GRID(g), 8);
            rc = grid_row(g, 0, "_Remote", remote_combo(m, doc));
            ne = grid_row(g, 1, "_Button name", gtk_entry_new());
    /* MNEMONIC-END */
            gtk_entry_set_placeholder_text(GTK_ENTRY(ne), "Power, Volume Up, Input ...");
            gtk_entry_set_activates_default(GTK_ENTRY(ne), TRUE);
            gtk_box_pack_start(GTK_BOX(c), g, FALSE, FALSE, 0);
            gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
            gtk_widget_show_all(d);
            gtk_widget_grab_focus(ne);
            ir_doc_free(doc);
            for (;;) {
                int rr = gtk_dialog_run(GTK_DIALOG(d));
                char *rn = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(rc)))))),
                     *bn = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(ne))));
                if (rr == RESP_RETRY) again = 1;
                if (rr != GTK_RESPONSE_OK) {
                    if (rr != RESP_RETRY) say(m, "The learned code was not saved.");
                    g_free(rn); g_free(bn);
                    break;
                }
                if (!*rn || !*bn) {
                    message(m, GTK_MESSAGE_INFO, "Name it first", "Type a name for the button (and the remote) to save the code.");
                    g_free(rn); g_free(bn);
                    continue;
                }
                rr = save_code(m, rn, bn, j->code, 0, IR_DEFAULT_FREQ);
                g_free(rn); g_free(bn);
                if (rr) break;
            }
            gtk_widget_destroy(d);
        }
        g_free(j);
        sensitivity(m);   /* failed, discarded or saved: the buttons at once, not at the next 2 s check */
        if (!again) break;
    }
}

static void on_type_code(GtkButton *bt, gpointer data)
{
    irm *m = data;
    ir_doc *doc = doc_read(m, 0);
    GtkWidget *d, *c, *g, *rc, *ne, *he, *nec, *fq, *l;
    (void)bt;
    if (!doc) return;
    /* MNEMONIC-MENU: ir-type-code */
    d = gtk_dialog_new_with_buttons("Type a Code", top(m), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                    "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_OK, NULL);
    c = gtk_dialog_get_content_area(GTK_DIALOG(d));
    gtk_container_set_border_width(GTK_CONTAINER(c), 12);
    gtk_box_set_spacing(GTK_BOX(c), 10);
    l = gtk_label_new("A code from a list or another program: a learned code (hex, as IR Remote stores it), or an NEC code as "
                      "address + command pairs (4 hex digits per press, for example 20df10ef).");
    gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(l), 56);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_box_pack_start(GTK_BOX(c), l, FALSE, FALSE, 0);
    g = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(g), 6);
    gtk_grid_set_column_spacing(GTK_GRID(g), 8);
    rc = grid_row(g, 0, "_Remote", remote_combo(m, doc));
    ne = grid_row(g, 1, "_Button name", gtk_entry_new());
    he = grid_row(g, 2, "Co_de (hex)", gtk_entry_new());
    gtk_entry_set_width_chars(GTK_ENTRY(he), 40);
    nec = gtk_check_button_new_with_mnemonic("_NEC code (address + command pairs)");
    gtk_grid_attach(GTK_GRID(g), nec, 1, 3, 1, 1);
    fq = grid_row(g, 4, "_Frequency (Hz)", gtk_spin_button_new_with_range(10000, 100000, 1000));
    /* MNEMONIC-END */
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(fq), IR_DEFAULT_FREQ);
    gtk_box_pack_start(GTK_BOX(c), g, FALSE, FALSE, 0);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
    gtk_widget_show_all(d);
    ir_doc_free(doc);
    while (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
        char *rn = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(rc)))))),
             *bn = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(ne))));
        const char *hx = gtk_entry_get_text(GTK_ENTRY(he)), *pr;
        int isnec = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(nec)), done = 0;
        if (!*rn || !*bn) message(m, GTK_MESSAGE_INFO, "Name it first", "Type a name for the button and the remote.");
        else if ((pr = ir_code_problem(hx, isnec))) message(m, GTK_MESSAGE_INFO, "That code does not look right", pr);
        else done = save_code(m, rn, bn, hx, isnec, (unsigned long)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(fq)));
        g_free(rn);
        g_free(bn);
        if (done) break;
    }
    gtk_widget_destroy(d);
}


/* ---- remotes and codes: add, rename, delete ---- */
static void on_remote_add(GtkButton *bt, gpointer data)
{
    irm *m = data;
    char *n = ask_text(m, "Add a Remote", "Name of the remote (for example TV, Soundbar, Fan):", "", "_Add");
    ir_doc *d;
    (void)bt;
    if (!n) return;
    if ((d = doc_read(m, 0))) {
        if (ir_find_remote(d, n) >= 0) message(m, GTK_MESSAGE_INFO, "That remote is already there", n);
        else if (ir_remote_add(d, n) >= 0) {
            char w[400];
            g_strlcpy(m->remote, n, sizeof m->remote);
            snprintf(w, sizeof w, "Added the remote %s", n);
            doc_write(m, d, w);
        }
        ir_doc_free(d);
    }
    g_free(n);
}

static void on_remote_rename(GtkButton *bt, gpointer data)
{
    irm *m = data;
    ir_doc *d;
    char *n;
    int r;
    (void)bt;
    if (!*m->remote || !(n = ask_text(m, "Rename the Remote", "New name:", m->remote, "_Rename"))) return;
    if ((d = doc_read(m, 0)) && (r = sel_remote(m, d)) >= 0) {
        char w[600];
        snprintf(w, sizeof w, "Renamed the remote %s to %s", m->remote, n);
        if (!ir_remote_rename(d, r, n)) message(m, GTK_MESSAGE_INFO, "There is already a remote with that name", n);
        else { g_strlcpy(m->remote, n, sizeof m->remote); doc_write(m, d, w); }
    }
    ir_doc_free(d);
    g_free(n);
}

static void on_remote_delete(GtkButton *bt, gpointer data)
{
    irm *m = data;
    ir_doc *d;
    int r;
    char q[600], w[400];
    (void)bt;
    if (!*m->remote || !(d = doc_read(m, 0))) return;
    if ((r = sel_remote(m, d)) >= 0) {
        snprintf(q, sizeof q, "%s and its %d code%s are removed from the codes file (IR Remote shares it). "
                              "The file as it was before SRS first changed it is kept as remotes.json.before-srs.",
                 ir_remote_name(d, r), ir_nbuttons(d, r), ir_nbuttons(d, r) == 1 ? "" : "s");
        if (confirm(m, "Delete this remote?", q, "_Delete")) {
            snprintf(w, sizeof w, "Deleted the remote %s", ir_remote_name(d, r));
            ir_remote_delete(d, r);
            m->remote[0] = 0;
            doc_write(m, d, w);
        }
    }
    ir_doc_free(d);
}

static void on_code_rename(GtkButton *bt, gpointer data)
{
    irm *m = data;
    ir_doc *d;
    ir_button b;
    int r, i = sel_button(m);
    char *n = NULL;
    (void)bt;
    if (i < 0 || !(d = doc_read(m, 0))) return;
    if ((r = sel_remote(m, d)) >= 0 && ir_button_at(d, r, i, &b) && (n = ask_text(m, "Rename the Code", "New name:", b.name, "_Rename"))) {
        char w[600];
        snprintf(w, sizeof w, "Renamed %s / %s to %s", ir_remote_name(d, r), b.name, n);
        if (!ir_button_rename(d, r, i, n)) message(m, GTK_MESSAGE_INFO, "There is already a code with that name", n);
        else doc_write(m, d, w);
    }
    g_free(n);
    ir_doc_free(d);
}

static void on_code_delete(GtkButton *bt, gpointer data)
{
    irm *m = data;
    ir_doc *d;
    ir_button b;
    int r, i = sel_button(m);
    char q[600], w[600];
    (void)bt;
    if (i < 0 || !(d = doc_read(m, 0))) return;
    if ((r = sel_remote(m, d)) >= 0 && ir_button_at(d, r, i, &b)) {
        snprintf(q, sizeof q, "%s / %s is removed from the codes file (IR Remote shares it).", ir_remote_name(d, r), b.name);
        if (confirm(m, "Delete this code?", q, "_Delete")) {
            snprintf(w, sizeof w, "Deleted %s / %s", ir_remote_name(d, r), b.name);
            ir_button_delete(d, r, i);
            doc_write(m, d, w);
        }
    }
    ir_doc_free(d);
}

/* ---- import / export ---- */
static void add_json_filter(GtkWidget *fc)
{
    GtkFileFilter *f = gtk_file_filter_new();
    gtk_file_filter_set_name(f, "IR Remote codes (*.json)");
    gtk_file_filter_add_pattern(f, "*.json");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fc), f);
    f = gtk_file_filter_new();
    gtk_file_filter_set_name(f, "All files");
    gtk_file_filter_add_pattern(f, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fc), f);
}

static void on_import(GtkButton *bt, gpointer data)
{
    irm *m = data;
    GtkWidget *fc = gtk_file_chooser_dialog_new("Import Codes", top(m), GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_OK, NULL);
    char *path = NULL, err[400] = "";
    (void)bt;
    add_json_filter(fc);
    if (gtk_dialog_run(GTK_DIALOG(fc)) == GTK_RESPONSE_OK) path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc));
    gtk_widget_destroy(fc);
    if (path) {
        int exists = 0, r, n = 0;
        ir_doc *in = ir_doc_load(path, &exists, err, sizeof err), *d;
        if (!in || !exists) message(m, GTK_MESSAGE_ERROR, "That file cannot be imported", in ? "No such file." : err);
        else if ((d = doc_read(m, 0))) {
            GtkWidget *q = gtk_message_dialog_new(top(m), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
                                                  "Import %d remote%s?", ir_nremotes(in), ir_nremotes(in) == 1 ? "" : "s");
            int how;
            char w[1200];
            for (r = 0; r < ir_nremotes(in); r++) n += ir_nbuttons(in, r);
            gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(q), "%s holds %d remote%s with %d code%s.\n\nAdd: remotes and codes are added; "
                                                     "a code with the same name as one of yours replaces it. Replace all: your codes become the file's "
                                                     "(the file as it was before SRS first changed it is kept as remotes.json.before-srs).",
                                                     path, ir_nremotes(in), ir_nremotes(in) == 1 ? "" : "s", n, n == 1 ? "" : "s");
            gtk_dialog_add_buttons(GTK_DIALOG(q), "_Cancel", GTK_RESPONSE_CANCEL, "Replace _All", RESP_RETRY, "_Add", GTK_RESPONSE_OK, NULL);
            gtk_dialog_set_default_response(GTK_DIALOG(q), GTK_RESPONSE_OK);
            how = gtk_dialog_run(GTK_DIALOG(q));
            gtk_widget_destroy(q);
            snprintf(w, sizeof w, "Imported %d remote%s from %s%s", ir_nremotes(in), ir_nremotes(in) == 1 ? "" : "s", path, how == RESP_RETRY ? " (replacing all)" : "");
            if (how == GTK_RESPONSE_OK) { if (ir_doc_merge(d, in)) doc_write(m, d, w); }
            else if (how == RESP_RETRY) doc_write(m, in, w);
            ir_doc_free(d);
        }
        ir_doc_free(in);
        g_free(path);
    }
}

static void on_export(GtkButton *bt, gpointer data)
{
    irm *m = data;
    GtkWidget *fc = gtk_file_chooser_dialog_new("Export Codes", top(m), GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_OK, NULL);
    char *path = NULL, err[600] = "";
    ir_doc *d;
    (void)bt;
    add_json_filter(fc);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(fc), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(fc), "remotes.json");
    if (gtk_dialog_run(GTK_DIALOG(fc)) == GTK_RESPONSE_OK) path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc));
    gtk_widget_destroy(fc);
    if (path && (d = doc_read(m, 0))) {
        if (ir_doc_export(d, path, err, sizeof err)) say(m, "Exported %d remote%s to %s", ir_nremotes(d), ir_nremotes(d) == 1 ? "" : "s", path);
        else message(m, GTK_MESSAGE_ERROR, "The codes could not be exported", err);
        ir_doc_free(d);
    }
    g_free(path);
}

static void on_open_app(GtkButton *bt, gpointer data)
{
    irm *m = data;
    char *exe = g_find_program_in_path("irremote");
    GError *e = NULL;
    (void)bt;
    if (exe) {
        char *argv[] = { exe, NULL };
        if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL, NULL, &e)) { say(m, "Could not start IR Remote: %s", e->message); g_error_free(e); }
        else say(m, "Started IR Remote (%s). It shares the codes: changes made there show here.", exe);
    }
    g_free(exe);
}

static void on_copy_rule(GtkButton *bt, gpointer data)
{
    irm *m = data;
    (void)bt;
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),
                           "echo '" IR_UDEV_LINE "' | sudo tee /etc/udev/rules.d/70-elksmart-ir.rules && sudo udevadm control --reload", -1);
    say(m, "Copied the command that installs the rule. Run it in a terminal, then unplug and replug the dongle.");
}

/* ---- building the view ---- */
static void on_destroy(GtkWidget *w, gpointer data)
{
    irm *m = data;
    (void)w;
    m->dead = 1;
    if (m->poll) g_source_remove(m->poll);
    m->poll = 0;
    irm_unref(m);
}

static GtkWidget *btn(const char *label, const char *tip, GCallback cb, irm *m)
{
    GtkWidget *b = gtk_button_new_with_mnemonic(label);
    gtk_widget_set_tooltip_text(b, tip);
    g_signal_connect(b, "clicked", cb, m);
    return b;
}

static GtkWidget *heading(const char *text)
{
    GtkWidget *l = gtk_label_new(NULL);
    char *mk = g_markup_printf_escaped("<b>%s</b>", text);
    gtk_label_set_markup(GTK_LABEL(l), mk);
    g_free(mk);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
    return l;
}

static void cell_dim(GtkTreeViewColumn *c, GtkCellRenderer *r, GtkTreeModel *model, GtkTreeIter *it, gpointer d)
{
    gboolean dim = FALSE;
    (void)c; (void)d;
    gtk_tree_model_get(model, it, BC_DIM, &dim, -1);
    g_object_set(r, "sensitive", !dim, NULL);
}

GtkWidget *ir_manager_new(void (*on_state)(int state, gpointer data), gpointer data)
{
    irm *m = g_new0(irm, 1);
    GtkWidget *row, *paned, *left, *right, *sw, *rows, *l, *frow;
    GtkCellRenderer *r;
    GtkTreeViewColumn *c;
    static const char *const bheads[] = { "Button", "Type", "Bytes", "Frequency", "Shortcut" };
    char *app = g_find_program_in_path("irremote");
    int i;
    m->refs = 1;
    m->state = -1;
    m->on_state = on_state;
    m->on_state_data = data;
    m->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(m->box), 8);

    /* MNEMONIC-MENU: item-tab, ir-page (on the IR Blaster tab, beside the tab's own Refresh / Copy / Guide row) */
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    m->icon = gtk_image_new_from_icon_name("action-unavailable-symbolic", GTK_ICON_SIZE_LARGE_TOOLBAR);
    gtk_widget_set_valign(m->icon, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(row), m->icon, FALSE, FALSE, 0);
    m->status = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(m->status), TRUE);
    gtk_label_set_xalign(GTK_LABEL(m->status), 0);
    gtk_label_set_selectable(GTK_LABEL(m->status), TRUE);
    gtk_widget_set_can_focus(m->status, FALSE);
    gtk_box_pack_start(GTK_BOX(row), m->status, TRUE, TRUE, 0);
    m->b_open = btn("_Open IR Remote", app ? "Open the IR Remote app (remote pictures, global shortcuts). It uses the same codes file."
                                           : "IR Remote is not installed (github.com/eugenesflanagin-spec/irremote). SRS reads and writes its codes file either way.",
                    G_CALLBACK(on_open_app), m);
    gtk_widget_set_sensitive(m->b_open, app != NULL);
    g_free(app);
    gtk_box_pack_end(GTK_BOX(row), m->b_open, FALSE, FALSE, 0);
    m->b_reset = btn("Reset _Dongle", "USB reset, for a dongle that stopped answering (for example one left waiting in learn mode). Sends no infrared.",
                     G_CALLBACK(on_reset), m);
    gtk_box_pack_end(GTK_BOX(row), m->b_reset, FALSE, FALSE, 0);
    m->b_test = btn("_Test", "Ask the dongle to identify itself: shows it answers, and which protocol it uses. Sends no infrared.", G_CALLBACK(on_test), m);
    gtk_box_pack_end(GTK_BOX(row), m->b_test, FALSE, FALSE, 0);
    gtk_widget_set_valign(m->b_test, GTK_ALIGN_START);
    gtk_widget_set_valign(m->b_reset, GTK_ALIGN_START);
    gtk_widget_set_valign(m->b_open, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(m->box), row, FALSE, FALSE, 0);

    m->fixrow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);   /* only when the dongle may not be opened */
    l = gtk_label_new("Rule: " IR_UDEV_LINE);
    gtk_label_set_selectable(GTK_LABEL(l), TRUE);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
    gtk_style_context_add_class(gtk_widget_get_style_context(l), "monospace");
    gtk_widget_set_margin_start(l, 32);
    gtk_box_pack_start(GTK_BOX(m->fixrow), l, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(m->fixrow), btn("Copy Install Command", "Copy a command that writes this rule to /etc/udev/rules.d (asks for your password "
                                                                     "in the terminal) - or install elksmart-comm-git, which ships it",
                                             G_CALLBACK(on_copy_rule), m), FALSE, FALSE, 0);
    gtk_widget_show_all(m->fixrow);
    gtk_widget_set_no_show_all(m->fixrow, TRUE);   /* show_state shows it only while the dongle may not be opened */
    gtk_widget_hide(m->fixrow);
    gtk_box_pack_start(GTK_BOX(m->box), m->fixrow, FALSE, FALSE, 0);

    paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_wide_handle(GTK_PANED(paned), TRUE);
    /* the remotes */
    left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(GTK_BOX(left), heading("Remotes"), FALSE, FALSE, 0);
    m->rstore = gtk_list_store_new(RC_N, G_TYPE_STRING, G_TYPE_STRING);
    m->rview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(m->rstore));
    g_object_unref(m->rstore);
    r = gtk_cell_renderer_text_new();
    g_object_set(r, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    c = gtk_tree_view_column_new_with_attributes("Remote", r, "text", RC_NAME, NULL);
    gtk_tree_view_column_set_expand(c, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(m->rview), c);
    r = gtk_cell_renderer_text_new();
    g_object_set(r, "xalign", 1.0, NULL);
    c = gtk_tree_view_column_new_with_attributes("Codes", r, "text", RC_COUNT, NULL);
    gtk_tree_view_column_set_alignment(c, 1.0);
    gtk_tree_view_append_column(GTK_TREE_VIEW(m->rview), c);
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(m->rview)), "changed", G_CALLBACK(on_remote_sel), m);
    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw), GTK_SHADOW_IN);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sw), m->rview);
    gtk_box_pack_start(GTK_BOX(left), sw, TRUE, TRUE, 0);
    rows = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    m->b_radd = btn("_Add Remote...", "Add an empty remote (Learn Code also makes one when you type a new name)", G_CALLBACK(on_remote_add), m);
    m->b_rren = btn("Re_name...", "Rename the selected remote", G_CALLBACK(on_remote_rename), m);
    m->b_rdel = btn("Delete...", "Delete the selected remote and its codes (asks first)", G_CALLBACK(on_remote_delete), m);
    gtk_box_pack_start(GTK_BOX(rows), m->b_radd, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rows), m->b_rren, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rows), m->b_rdel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left), rows, FALSE, FALSE, 0);
    gtk_widget_set_size_request(left, 220, -1);
    gtk_paned_set_position(GTK_PANED(paned), 230);
    gtk_paned_pack1(GTK_PANED(paned), left, FALSE, FALSE);

    /* the codes of the selected remote */
    right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    m->codes_title = heading("Codes");
    gtk_box_pack_start(GTK_BOX(right), m->codes_title, FALSE, FALSE, 0);
    m->bstore = gtk_list_store_new(BC_N, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_BOOLEAN, G_TYPE_STRING);
    m->bview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(m->bstore));
    g_object_unref(m->bstore);
    gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(m->bview), BC_TIP);
    for (i = 0; i < 5; i++) {
        int num = i == 2 || i == 3;
        r = gtk_cell_renderer_text_new();
        if (num) g_object_set(r, "xalign", 1.0, NULL);
        if (i == 0) g_object_set(r, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
        c = gtk_tree_view_column_new_with_attributes(bheads[i], r, "text", BC_NAME + i, NULL);
        gtk_tree_view_column_set_cell_data_func(c, r, cell_dim, NULL, NULL);
        gtk_tree_view_column_set_resizable(c, TRUE);
        if (num) gtk_tree_view_column_set_alignment(c, 1.0);
        if (i == 0) { gtk_tree_view_column_set_expand(c, TRUE); gtk_tree_view_column_set_min_width(c, 160); }
        gtk_tree_view_append_column(GTK_TREE_VIEW(m->bview), c);
    }
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(m->bview)), "changed", G_CALLBACK(on_button_sel), m);
    g_signal_connect(m->bview, "row-activated", G_CALLBACK(on_code_activated), m);
    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(sw), m->bview);
    gtk_box_pack_start(GTK_BOX(right), sw, TRUE, TRUE, 0);
    rows = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    m->b_send = btn("_Send", "", G_CALLBACK(on_send), m);
    gtk_style_context_add_class(gtk_widget_get_style_context(m->b_send), "suggested-action");
    gtk_box_pack_start(GTK_BOX(rows), m->b_send, FALSE, FALSE, 0);
    frow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    l = gtk_label_new_with_mnemonic("Re_peat");
    m->repeat = gtk_spin_button_new_with_range(1, 20, 1);
    gtk_label_set_mnemonic_widget(GTK_LABEL(l), m->repeat);
    gtk_widget_set_tooltip_text(m->repeat, "Send the code this many times, 0.1 s apart (for volume steps)");
    gtk_box_pack_start(GTK_BOX(frow), l, FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(frow), m->repeat, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rows), frow, FALSE, FALSE, 4);
    m->dry = gtk_check_button_new_with_mnemonic("Dry R_un");
    gtk_widget_set_tooltip_text(m->dry, "Send only shows the USB packets it would write, below; nothing goes to the dongle (works without one)");
    g_signal_connect(m->dry, "toggled", G_CALLBACK(on_dry), m);
    gtk_box_pack_start(GTK_BOX(rows), m->dry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right), rows, FALSE, FALSE, 0);
    rows = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);   /* a second row: the window stays narrow enough for a laptop */
    m->b_learn = btn("Lea_rn Code...", "Read a button of your remote with the dongle (within 1 cm), then name it and keep it", G_CALLBACK(on_learn), m);
    gtk_box_pack_start(GTK_BOX(rows), m->b_learn, FALSE, FALSE, 0);
    m->b_type = btn("T_ype Code...", "Save a code you have as text: a learned code (hex) or an NEC address + command code", G_CALLBACK(on_type_code), m);
    gtk_box_pack_start(GTK_BOX(rows), m->b_type, FALSE, FALSE, 0);
    m->b_bren = btn("Rena_me...", "Rename the selected code", G_CALLBACK(on_code_rename), m);
    gtk_box_pack_start(GTK_BOX(rows), m->b_bren, FALSE, FALSE, 0);
    m->b_bdel = btn("Delete...", "Delete the selected code (asks first)", G_CALLBACK(on_code_delete), m);
    gtk_box_pack_start(GTK_BOX(rows), m->b_bdel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right), rows, FALSE, FALSE, 0);
    gtk_paned_pack2(GTK_PANED(paned), right, TRUE, FALSE);
    gtk_box_pack_start(GTK_BOX(m->box), paned, TRUE, TRUE, 0);

    /* the log, and the file's own buttons */
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    l = gtk_label_new(NULL);
    {
        char *mk = g_markup_printf_escaped("<small>Codes file: %s (shared with IR Remote)</small>", ir_codes_path());
        gtk_label_set_markup(GTK_LABEL(l), mk);
        g_free(mk);
    }
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_selectable(GTK_LABEL(l), TRUE);
    gtk_widget_set_can_focus(l, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(l), "dim-label");
    gtk_box_pack_start(GTK_BOX(row), l, TRUE, TRUE, 0);
    m->b_exp = btn("_Export...", "Save all remotes and codes to a file (IR Remote's format), to share or keep", G_CALLBACK(on_export), m);
    gtk_box_pack_end(GTK_BOX(row), m->b_exp, FALSE, FALSE, 0);
    m->b_imp = btn("_Import...", "Add remotes and codes from a file (IR Remote's format)", G_CALLBACK(on_import), m);
    gtk_box_pack_end(GTK_BOX(row), m->b_imp, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(m->box), row, FALSE, FALSE, 0);
    /* MNEMONIC-END */
    m->logview = gtk_text_view_new();
    m->logbuf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->logview));
    gtk_text_view_set_editable(GTK_TEXT_VIEW(m->logview), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(m->logview), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(m->logview), GTK_WRAP_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(m->logview), 4);
    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw), GTK_SHADOW_IN);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(sw), 90);
    gtk_container_add(GTK_CONTAINER(sw), m->logview);
    gtk_box_pack_start(GTK_BOX(m->box), sw, FALSE, FALSE, 0);

    g_signal_connect(m->box, "destroy", G_CALLBACK(on_destroy), m);
    g_signal_connect(m->box, "map", G_CALLBACK(on_map), m);
    m->poll = g_timeout_add_seconds(2, on_poll, m);
    reload(m);
    show_state(m);
    say(m, "The codes are shared with IR Remote. Learn and Send need the dongle; nothing is sent until you press Send.");
    return m->box;
}
