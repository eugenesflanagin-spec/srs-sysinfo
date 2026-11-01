/* perf_sni.c - StatusNotifierItem + com.canonical.dbusmenu over GDBus (see perf_sni.h).
 *
 * Protocols: the freedesktop StatusNotifierItem specification (as implemented by KDE Plasma) and the
 * dbusmenu protocol (com.canonical.dbusmenu, version 3). Written from the published interface
 * descriptions; no code taken from other implementations.
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#include "perf_sni.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SNI_PATH "/StatusNotifierItem"
#define MENU_PATH "/MenuBar"
#define WATCHER "org.kde.StatusNotifierWatcher"

static const char sni_xml[] =
    "<node>"
    " <interface name='org.kde.StatusNotifierItem'>"
    "  <property name='Category' type='s' access='read'/>"
    "  <property name='Id' type='s' access='read'/>"
    "  <property name='Title' type='s' access='read'/>"
    "  <property name='Status' type='s' access='read'/>"
    "  <property name='WindowId' type='i' access='read'/>"
    "  <property name='IconThemePath' type='s' access='read'/>"
    "  <property name='IconName' type='s' access='read'/>"
    "  <property name='IconPixmap' type='a(iiay)' access='read'/>"
    "  <property name='OverlayIconName' type='s' access='read'/>"
    "  <property name='OverlayIconPixmap' type='a(iiay)' access='read'/>"
    "  <property name='AttentionIconName' type='s' access='read'/>"
    "  <property name='AttentionIconPixmap' type='a(iiay)' access='read'/>"
    "  <property name='AttentionMovieName' type='s' access='read'/>"
    "  <property name='ToolTip' type='(sa(iiay)ss)' access='read'/>"
    "  <property name='ItemIsMenu' type='b' access='read'/>"
    "  <property name='Menu' type='o' access='read'/>"
    "  <method name='ContextMenu'><arg name='x' type='i' direction='in'/><arg name='y' type='i' direction='in'/></method>"
    "  <method name='Activate'><arg name='x' type='i' direction='in'/><arg name='y' type='i' direction='in'/></method>"
    "  <method name='SecondaryActivate'><arg name='x' type='i' direction='in'/><arg name='y' type='i' direction='in'/></method>"
    "  <method name='Scroll'><arg name='delta' type='i' direction='in'/><arg name='orientation' type='s' direction='in'/></method>"
    "  <method name='ProvideXdgActivationToken'><arg name='token' type='s' direction='in'/></method>"
    "  <signal name='NewTitle'/>"
    "  <signal name='NewIcon'/>"
    "  <signal name='NewAttentionIcon'/>"
    "  <signal name='NewOverlayIcon'/>"
    "  <signal name='NewToolTip'/>"
    "  <signal name='NewStatus'><arg name='status' type='s'/></signal>"
    " </interface>"
    "</node>";

static const char menu_xml[] =
    "<node>"
    " <interface name='com.canonical.dbusmenu'>"
    "  <property name='Version' type='u' access='read'/>"
    "  <property name='TextDirection' type='s' access='read'/>"
    "  <property name='Status' type='s' access='read'/>"
    "  <property name='IconThemePath' type='as' access='read'/>"
    "  <method name='GetLayout'>"
    "   <arg type='i' name='parentId' direction='in'/><arg type='i' name='recursionDepth' direction='in'/>"
    "   <arg type='as' name='propertyNames' direction='in'/>"
    "   <arg type='u' name='revision' direction='out'/><arg type='(ia{sv}av)' name='layout' direction='out'/>"
    "  </method>"
    "  <method name='GetGroupProperties'>"
    "   <arg type='ai' name='ids' direction='in'/><arg type='as' name='propertyNames' direction='in'/>"
    "   <arg type='a(ia{sv})' name='properties' direction='out'/>"
    "  </method>"
    "  <method name='GetProperty'>"
    "   <arg type='i' name='id' direction='in'/><arg type='s' name='name' direction='in'/><arg type='v' name='value' direction='out'/>"
    "  </method>"
    "  <method name='Event'>"
    "   <arg type='i' name='id' direction='in'/><arg type='s' name='eventId' direction='in'/>"
    "   <arg type='v' name='data' direction='in'/><arg type='u' name='timestamp' direction='in'/>"
    "  </method>"
    "  <method name='EventGroup'>"
    "   <arg type='a(isvu)' name='events' direction='in'/><arg type='ai' name='idErrors' direction='out'/>"
    "  </method>"
    "  <method name='AboutToShow'><arg type='i' name='id' direction='in'/><arg type='b' name='needUpdate' direction='out'/></method>"
    "  <method name='AboutToShowGroup'>"
    "   <arg type='ai' name='ids' direction='in'/><arg type='ai' name='updatesNeeded' direction='out'/>"
    "   <arg type='ai' name='idErrors' direction='out'/>"
    "  </method>"
    "  <signal name='ItemsPropertiesUpdated'><arg type='a(ia{sv})' name='updatedProps'/><arg type='a(ias)' name='removedProps'/></signal>"
    "  <signal name='LayoutUpdated'><arg type='u' name='revision'/><arg type='i' name='parent'/></signal>"
    "  <signal name='ItemActivationRequested'><arg type='i' name='id'/><arg type='u' name='timestamp'/></signal>"
    " </interface>"
    "</node>";

static GDBusNodeInfo *g_sni_info, *g_menu_info;
static int g_serial;

typedef struct { int id; char *label; int check, enabled; } mitem;

struct psni {
    GDBusConnection *conn;
    guint obj_id, menu_id, own_id, watch_id;
    int name_owned, watcher_up, registered;
    char *bus, *id, *title, *tt_title, *tt_body, *category;
    psni_cbs cb;
    void *user;
    GVariant *pix;                 /* a(iiay), owned */
    GBytes *last;                  /* the pixels last sent, for change detection */
    unsigned icon_sends;
    mitem *menu;
    int nmenu;
    guint rev;
    char token[256];
    int dead;                      /* being freed: callbacks ignored */
    int calls;                     /* D-Bus calls in flight; the last reply frees a dead item */
};

static int log_on(void) { const char *e = getenv("SRS_TEST_TRAY_LOG"); return e && *e && strcmp(e, "0"); }

static void emit(psni *it, const char *path, const char *iface, const char *sig, GVariant *args)
{
    if (!it->conn || g_dbus_connection_is_closed(it->conn)) { if (args) g_variant_unref(g_variant_ref_sink(args)); return; }
    g_dbus_connection_emit_signal(it->conn, NULL, path, iface, sig, args, NULL);
}

static GVariant *empty_pix(void) { return g_variant_new_array(G_VARIANT_TYPE("(iiay)"), NULL, 0); }

/* ---------------------------------------------------------------- StatusNotifierItem */
static GVariant *sni_get(GDBusConnection *c, const char *sender, const char *path, const char *iface, const char *prop,
                         GError **err, gpointer d)
{
    psni *it = d;
    (void)c; (void)sender; (void)path; (void)iface; (void)err;
    if (!strcmp(prop, "Category")) return g_variant_new_string(it->category ? it->category : "Hardware");
    if (!strcmp(prop, "Id")) return g_variant_new_string(it->id);
    if (!strcmp(prop, "Title")) return g_variant_new_string(it->title ? it->title : "");
    if (!strcmp(prop, "Status")) return g_variant_new_string("Active");
    if (!strcmp(prop, "WindowId")) return g_variant_new_int32(0);
    if (!strcmp(prop, "IconPixmap")) return it->pix ? g_variant_ref(it->pix) : empty_pix();
    if (!strcmp(prop, "OverlayIconPixmap") || !strcmp(prop, "AttentionIconPixmap")) return empty_pix();
    if (!strcmp(prop, "ToolTip"))
        return g_variant_new("(s@a(iiay)ss)", "", empty_pix(), it->tt_title ? it->tt_title : (it->title ? it->title : ""),
                             it->tt_body ? it->tt_body : "");
    if (!strcmp(prop, "ItemIsMenu")) return g_variant_new_boolean(FALSE);
    if (!strcmp(prop, "Menu")) return g_variant_new_object_path(MENU_PATH);
    /* IconThemePath, IconName, OverlayIconName, AttentionIconName, AttentionMovieName */
    return g_variant_new_string("");
}

static void sni_call(GDBusConnection *c, const char *sender, const char *path, const char *iface, const char *method,
                     GVariant *args, GDBusMethodInvocation *inv, gpointer d)
{
    psni *it = d;
    (void)c; (void)sender; (void)path; (void)iface;
    if (log_on()) fprintf(stderr, "SRS-TEST: tray %s %s\n", it->id, method);
    if (!strcmp(method, "ProvideXdgActivationToken")) {
        const char *t = NULL;
        g_variant_get(args, "(&s)", &t);
        snprintf(it->token, sizeof it->token, "%s", t ? t : "");
        g_dbus_method_invocation_return_value(inv, NULL);
        return;
    }
    if (!strcmp(method, "Activate") || !strcmp(method, "SecondaryActivate") || !strcmp(method, "ContextMenu")) {
        int x = 0, y = 0;
        char tok[256];
        g_variant_get(args, "(ii)", &x, &y);
        g_dbus_method_invocation_return_value(inv, NULL);
        snprintf(tok, sizeof tok, "%s", it->token);
        it->token[0] = 0;
        if (it->dead) return;
        /* ContextMenu is only called by trays that ignore the Menu property: treat it like a left click */
        if (!strcmp(method, "SecondaryActivate")) { if (it->cb.secondary) it->cb.secondary(it, x, y, tok, it->user); }
        else if (it->cb.activate) it->cb.activate(it, x, y, tok, it->user);
        return;
    }
    g_dbus_method_invocation_return_value(inv, NULL);   /* Scroll: nothing to do */
}

/* ---------------------------------------------------------------- dbusmenu */
static int want_prop(GVariant *names, const char *p)
{
    gsize n = names ? g_variant_n_children(names) : 0, i;
    if (!n) return 1;
    for (i = 0; i < n; i++) {
        const char *s = NULL;
        g_variant_get_child(names, i, "&s", &s);
        if (s && !strcmp(s, p)) return 1;
    }
    return 0;
}

static GVariant *item_props(const psni *it, int id, GVariant *names)
{
    GVariantBuilder b;
    int i;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
    if (id == 0) {
        if (want_prop(names, "children-display")) g_variant_builder_add(&b, "{sv}", "children-display", g_variant_new_string("submenu"));
        return g_variant_builder_end(&b);
    }
    for (i = 0; i < it->nmenu; i++) {
        const mitem *m = &it->menu[i];
        if (m->id != id) continue;
        if (!m->label) {
            if (want_prop(names, "type")) g_variant_builder_add(&b, "{sv}", "type", g_variant_new_string("separator"));
            break;
        }
        if (want_prop(names, "label")) g_variant_builder_add(&b, "{sv}", "label", g_variant_new_string(m->label));
        if (!m->enabled && want_prop(names, "enabled")) g_variant_builder_add(&b, "{sv}", "enabled", g_variant_new_boolean(FALSE));
        if (m->check >= 0) {
            if (want_prop(names, "toggle-type")) g_variant_builder_add(&b, "{sv}", "toggle-type", g_variant_new_string("checkmark"));
            if (want_prop(names, "toggle-state")) g_variant_builder_add(&b, "{sv}", "toggle-state", g_variant_new_int32(m->check ? 1 : 0));
        }
        break;
    }
    return g_variant_builder_end(&b);
}

static int has_item(const psni *it, int id)
{
    int i;
    if (id == 0) return 1;
    for (i = 0; i < it->nmenu; i++) if (it->menu[i].id == id) return 1;
    return 0;
}

static GVariant *layout(const psni *it, int id, int depth, GVariant *names)
{
    GVariantBuilder kids;
    int i;
    g_variant_builder_init(&kids, G_VARIANT_TYPE("av"));
    if (id == 0 && depth != 0)
        for (i = 0; i < it->nmenu; i++)
            g_variant_builder_add(&kids, "v", layout(it, it->menu[i].id, depth < 0 ? -1 : depth - 1, names));
    return g_variant_new("(i@a{sv}av)", id, item_props(it, id, names), &kids);
}

static void menu_event(psni *it, int id, const char *ev)
{
    int i;
    char tok[256];
    if (strcmp(ev, "clicked") || it->dead) return;
    for (i = 0; i < it->nmenu; i++)
        if (it->menu[i].id == id && it->menu[i].label && it->menu[i].enabled) {
            if (log_on()) fprintf(stderr, "SRS-TEST: tray %s menu %d\n", it->id, id);
            snprintf(tok, sizeof tok, "%s", it->token);
            it->token[0] = 0;
            if (it->cb.menu) it->cb.menu(it, id, tok, it->user);
            return;
        }
}

static void menu_call(GDBusConnection *c, const char *sender, const char *path, const char *iface, const char *method,
                      GVariant *args, GDBusMethodInvocation *inv, gpointer d)
{
    psni *it = d;
    (void)c; (void)sender; (void)path; (void)iface;
    if (!strcmp(method, "GetLayout")) {
        int parent = 0, depth = -1;
        GVariant *names = NULL;
        g_variant_get(args, "(ii@as)", &parent, &depth, &names);
        if (!has_item(it, parent)) {
            g_dbus_method_invocation_return_dbus_error(inv, "com.canonical.dbusmenu.Error", "no such item");
        } else {
            g_dbus_method_invocation_return_value(inv, g_variant_new("(u@(ia{sv}av))", it->rev, layout(it, parent, depth, names)));
        }
        g_variant_unref(names);
    } else if (!strcmp(method, "GetGroupProperties")) {
        GVariant *ids = NULL, *names = NULL;
        GVariantBuilder b;
        gsize i, n;
        g_variant_get(args, "(@ai@as)", &ids, &names);
        g_variant_builder_init(&b, G_VARIANT_TYPE("a(ia{sv})"));
        n = g_variant_n_children(ids);
        if (!n) {   /* empty list = every item */
            int k;
            g_variant_builder_add(&b, "(i@a{sv})", 0, item_props(it, 0, names));
            for (k = 0; k < it->nmenu; k++) g_variant_builder_add(&b, "(i@a{sv})", it->menu[k].id, item_props(it, it->menu[k].id, names));
        }
        for (i = 0; i < n; i++) {
            gint32 id;
            g_variant_get_child(ids, i, "i", &id);
            if (has_item(it, id)) g_variant_builder_add(&b, "(i@a{sv})", id, item_props(it, id, names));
        }
        g_dbus_method_invocation_return_value(inv, g_variant_new("(a(ia{sv}))", &b));
        g_variant_unref(ids);
        g_variant_unref(names);
    } else if (!strcmp(method, "GetProperty")) {
        int id = 0;
        const char *name = NULL;
        GVariant *props, *v;
        g_variant_get(args, "(i&s)", &id, &name);
        props = item_props(it, id, NULL);
        g_variant_ref_sink(props);
        v = g_variant_lookup_value(props, name, NULL);
        if (v) g_dbus_method_invocation_return_value(inv, g_variant_new("(v)", v));
        else g_dbus_method_invocation_return_dbus_error(inv, "com.canonical.dbusmenu.Error", "no such property");
        if (v) g_variant_unref(v);
        g_variant_unref(props);
    } else if (!strcmp(method, "Event")) {
        int id = 0;
        const char *ev = NULL;
        g_variant_get(args, "(i&svu)", &id, &ev, NULL, NULL);
        g_dbus_method_invocation_return_value(inv, NULL);
        menu_event(it, id, ev);   /* may free or rebuild the menu: after the reply */
    } else if (!strcmp(method, "EventGroup")) {
        GVariant *evs = NULL;
        GVariantBuilder err;
        gsize i, n;
        int *ids, nids = 0;
        char **names;
        g_variant_get(args, "(@a(isvu))", &evs);
        n = g_variant_n_children(evs);
        ids = g_new0(int, n + 1);
        names = g_new0(char *, n + 1);
        g_variant_builder_init(&err, G_VARIANT_TYPE("ai"));
        for (i = 0; i < n; i++) {
            gint32 id;
            const char *ev;
            g_variant_get_child(evs, i, "(i&svu)", &id, &ev, NULL, NULL);
            if (!has_item(it, id)) g_variant_builder_add(&err, "i", id);
            else { ids[nids] = id; names[nids++] = g_strdup(ev); }
        }
        g_dbus_method_invocation_return_value(inv, g_variant_new("(ai)", &err));
        for (i = 0; i < (gsize)nids; i++) { menu_event(it, ids[i], names[i]); g_free(names[i]); }
        g_free(ids);
        g_free(names);
        g_variant_unref(evs);
    } else if (!strcmp(method, "AboutToShow")) {
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", FALSE));
    } else if (!strcmp(method, "AboutToShowGroup")) {
        g_dbus_method_invocation_return_value(inv, g_variant_new("(@ai@ai)", g_variant_new_array(G_VARIANT_TYPE_INT32, NULL, 0),
                                                                 g_variant_new_array(G_VARIANT_TYPE_INT32, NULL, 0)));
    } else {
        g_dbus_method_invocation_return_dbus_error(inv, "org.freedesktop.DBus.Error.UnknownMethod", method);
    }
}

static GVariant *menu_get(GDBusConnection *c, const char *sender, const char *path, const char *iface, const char *prop,
                          GError **err, gpointer d)
{
    (void)c; (void)sender; (void)path; (void)iface; (void)err; (void)d;
    if (!strcmp(prop, "Version")) return g_variant_new_uint32(3);
    if (!strcmp(prop, "TextDirection")) return g_variant_new_string(gtk_widget_get_default_direction() == GTK_TEXT_DIR_RTL ? "rtl" : "ltr");
    if (!strcmp(prop, "Status")) return g_variant_new_string("normal");
    return g_variant_new_strv(NULL, 0);   /* IconThemePath */
}

static const GDBusInterfaceVTable sni_vt = { sni_call, sni_get, NULL, { 0 } };
static const GDBusInterfaceVTable menu_vt = { menu_call, menu_get, NULL, { 0 } };

/* ---------------------------------------------------------------- registration with the tray */
static void set_registered(psni *it, int on)
{
    if (it->registered == on) return;
    it->registered = on;
    if (log_on()) fprintf(stderr, "SRS-TEST: tray %s registered=%d\n", it->id, on);
    if (!it->dead && it->cb.registered) it->cb.registered(it, on, it->user);
}

static void reg_done(GObject *src, GAsyncResult *res, gpointer d)
{
    psni *it = d;
    GError *e = NULL;
    GVariant *r = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, &e);
    it->calls--;
    if (r) { g_variant_unref(r); if (!it->dead) set_registered(it, 1); }
    else {
        if (!it->dead && log_on()) fprintf(stderr, "SRS-TEST: tray %s register failed: %s\n", it->id, e ? e->message : "?");
        g_clear_error(&e);
    }
    if (it->dead && !it->calls) g_free(it);   /* psni_free left the shell for the last reply */
}

static void try_register(psni *it)
{
    if (!it->name_owned || !it->watcher_up || it->dead) return;
    it->calls++;
    g_dbus_connection_call(it->conn, WATCHER, "/StatusNotifierWatcher", WATCHER, "RegisterStatusNotifierItem",
                           g_variant_new("(s)", it->bus), NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL, reg_done, it);
}

static void name_got(GDBusConnection *c, const char *name, gpointer d)
{
    psni *it = d;
    (void)c; (void)name;
    it->name_owned = 1;
    try_register(it);
}

static void name_lost(GDBusConnection *c, const char *name, gpointer d)
{
    psni *it = d;
    (void)c; (void)name;
    it->name_owned = 0;
    set_registered(it, 0);
}

static void watcher_up(GDBusConnection *c, const char *name, const char *owner, gpointer d)
{
    psni *it = d;
    (void)c; (void)name; (void)owner;
    it->watcher_up = 1;
    try_register(it);   /* also after a tray restart */
}

static void watcher_down(GDBusConnection *c, const char *name, gpointer d)
{
    psni *it = d;
    (void)c; (void)name;
    it->watcher_up = 0;
    set_registered(it, 0);
}

psni *psni_new(const char *id, const char *title, const psni_cbs *cb, void *user)
{
    psni *it;
    char *addr;
    GError *e = NULL;
    GDBusConnection *conn;
    if (!g_sni_info) {
        g_sni_info = g_dbus_node_info_new_for_xml(sni_xml, NULL);
        g_menu_info = g_dbus_node_info_new_for_xml(menu_xml, NULL);
        if (!g_sni_info || !g_menu_info) return NULL;
    }
    addr = g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SESSION, NULL, NULL);
    if (!addr) return NULL;
    conn = g_dbus_connection_new_for_address_sync(addr, G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                                  G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION, NULL, NULL, &e);
    g_free(addr);
    if (!conn) {
        if (log_on()) fprintf(stderr, "SRS-TEST: tray %s no bus: %s\n", id, e ? e->message : "?");
        g_clear_error(&e);
        return NULL;
    }
    g_dbus_connection_set_exit_on_close(conn, FALSE);
    it = g_new0(psni, 1);
    it->conn = conn;
    it->id = g_strdup(id);
    it->title = g_strdup(title);
    if (cb) it->cb = *cb;
    it->user = user;
    it->rev = 1;
    it->bus = g_strdup_printf("org.kde.StatusNotifierItem-%d-%d", (int)getpid(), ++g_serial);
    it->obj_id = g_dbus_connection_register_object(conn, SNI_PATH, g_sni_info->interfaces[0], &sni_vt, it, NULL, NULL);
    it->menu_id = g_dbus_connection_register_object(conn, MENU_PATH, g_menu_info->interfaces[0], &menu_vt, it, NULL, NULL);
    it->own_id = g_bus_own_name_on_connection(conn, it->bus, G_BUS_NAME_OWNER_FLAGS_DO_NOT_QUEUE, name_got, name_lost, it, NULL);
    it->watch_id = g_bus_watch_name_on_connection(conn, WATCHER, G_BUS_NAME_WATCHER_FLAGS_NONE, watcher_up, watcher_down, it, NULL);
    return it;
}

void psni_free(psni *it)
{
    if (!it) return;
    it->dead = 1;
    if (it->watch_id) g_bus_unwatch_name(it->watch_id);
    if (it->own_id) g_bus_unown_name(it->own_id);
    if (it->obj_id) g_dbus_connection_unregister_object(it->conn, it->obj_id);
    if (it->menu_id) g_dbus_connection_unregister_object(it->conn, it->menu_id);
    /* closing the connection drops our bus name, so the tray removes the icon now, not at exit */
    g_dbus_connection_close_sync(it->conn, NULL, NULL);
    g_object_unref(it->conn);
    it->conn = NULL;
    if (it->pix) g_variant_unref(it->pix);
    if (it->last) g_bytes_unref(it->last);
    for (int i = 0; i < it->nmenu; i++) g_free(it->menu[i].label);
    g_free(it->menu);
    g_free(it->bus); g_free(it->id); g_free(it->title); g_free(it->tt_title); g_free(it->tt_body); g_free(it->category);
    it->bus = it->id = it->title = it->tt_title = it->tt_body = it->category = NULL;
    it->menu = NULL;
    it->nmenu = 0;
    if (!it->calls) g_free(it);   /* else reg_done frees the shell */
}

int psni_registered(const psni *it) { return it && it->registered; }
unsigned psni_icon_sends(const psni *it) { return it ? it->icon_sends : 0; }
const char *psni_bus_name(const psni *it) { return it ? it->bus : ""; }

/* ---------------------------------------------------------------- content */
int psni_set_icon(psni *it, cairo_surface_t *const *surf, int n)
{
    GVariantBuilder b;
    GByteArray *all = g_byte_array_new();
    GBytes *now;
    int k;
    if (!it || it->dead) { g_byte_array_unref(all); return 0; }
    g_variant_builder_init(&b, G_VARIANT_TYPE("a(iiay)"));
    for (k = 0; k < n; k++) {
        int w, h, stride, x, y;
        const unsigned char *src;
        guint8 *px;
        GBytes *bytes;
        cairo_surface_flush(surf[k]);
        w = cairo_image_surface_get_width(surf[k]);
        h = cairo_image_surface_get_height(surf[k]);
        stride = cairo_image_surface_get_stride(surf[k]);
        src = cairo_image_surface_get_data(surf[k]);
        px = g_malloc((size_t)w * h * 4);
        for (y = 0; y < h; y++) {
            const guint32 *row = (const guint32 *)(src + (size_t)y * stride);
            for (x = 0; x < w; x++) {
                guint32 p = row[x];
                unsigned a = p >> 24, r = (p >> 16) & 255, g = (p >> 8) & 255, bl = p & 255;
                guint8 *o = px + ((size_t)y * w + x) * 4;
                if (a && a < 255) { r = r * 255 / a; g = g * 255 / a; bl = bl * 255 / a; }   /* straight alpha */
                o[0] = (guint8)a; o[1] = (guint8)r; o[2] = (guint8)g; o[3] = (guint8)bl;   /* ARGB, network order */
            }
        }
        g_byte_array_append(all, px, (guint)((size_t)w * h * 4));
        bytes = g_bytes_new_take(px, (size_t)w * h * 4);
        g_variant_builder_add(&b, "(ii@ay)", w, h, g_variant_new_from_bytes(G_VARIANT_TYPE_BYTESTRING, bytes, TRUE));
        g_bytes_unref(bytes);
    }
    now = g_byte_array_free_to_bytes(all);
    if (it->last && g_bytes_equal(it->last, now)) {
        g_bytes_unref(now);
        g_variant_unref(g_variant_ref_sink(g_variant_builder_end(&b)));
        return 0;
    }
    if (it->last) g_bytes_unref(it->last);
    it->last = now;
    if (it->pix) g_variant_unref(it->pix);
    it->pix = g_variant_ref_sink(g_variant_builder_end(&b));
    it->icon_sends++;
    emit(it, SNI_PATH, "org.kde.StatusNotifierItem", "NewIcon", NULL);
    return 1;
}

void psni_set_tooltip(psni *it, const char *title, const char *body)
{
    if (!it || it->dead) return;
    if (!g_strcmp0(it->tt_title, title) && !g_strcmp0(it->tt_body, body)) return;
    g_free(it->tt_title); g_free(it->tt_body);
    it->tt_title = g_strdup(title);
    it->tt_body = g_strdup(body);
    emit(it, SNI_PATH, "org.kde.StatusNotifierItem", "NewToolTip", NULL);
}

void psni_set_title(psni *it, const char *title)
{
    if (!it || it->dead > 0 || !g_strcmp0(it->title, title)) return;
    g_free(it->title);
    it->title = g_strdup(title);
    emit(it, SNI_PATH, "org.kde.StatusNotifierItem", "NewTitle", NULL);
}

void psni_set_category(psni *it, const char *category)
{
    if (!it || it->dead) return;
    g_free(it->category);
    it->category = g_strdup(category);
}

void psni_set_menu(psni *it, const psni_menu_item *items, int n)
{
    int i;
    if (!it || it->dead) return;
    for (i = 0; i < it->nmenu; i++) g_free(it->menu[i].label);
    g_free(it->menu);
    it->menu = g_new0(mitem, n > 0 ? n : 1);
    it->nmenu = n;
    for (i = 0; i < n; i++) {
        it->menu[i].id = items[i].id;
        it->menu[i].label = items[i].label ? g_strdup(items[i].label) : NULL;
        it->menu[i].check = items[i].check;
        it->menu[i].enabled = items[i].enabled;
    }
    it->rev++;
    emit(it, MENU_PATH, "com.canonical.dbusmenu", "LayoutUpdated", g_variant_new("(ui)", it->rev, 0));
}
