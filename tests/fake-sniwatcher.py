#!/usr/bin/env python3
# fake-sniwatcher.py - a stand-in for the desktop's tray (org.kde.StatusNotifierWatcher + a host) on a PRIVATE
# session bus, for tests/perf-tray-test.sh. Never run it on the real session bus (dbus-run-session only).
#
# It records every item that registers, reads its properties the way Plasma does (GetAll after each NewIcon),
# checks IconPixmap (sizes, length, big-endian ARGB), saves the received pixmaps as PNGs, counts NewIcon /
# NewToolTip, and then clicks: Activate, menu GetLayout, a meter check box on, off, and Quit.
#   fake-sniwatcher.py OUTDIR SECONDS
# Writes OUTDIR/watcher.json and prints "ok ..." / "FAIL ..." lines.
#
# SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later.
import json, os, sys, time
import gi
gi.require_version('Gio', '2.0')
from gi.repository import Gio, GLib

OUT, SECS = sys.argv[1], int(sys.argv[2])
XML = """<node><interface name='org.kde.StatusNotifierWatcher'>
 <method name='RegisterStatusNotifierItem'><arg type='s' direction='in'/></method>
 <method name='RegisterStatusNotifierHost'><arg type='s' direction='in'/></method>
 <property name='RegisteredStatusNotifierItems' type='as' access='read'/>
 <property name='IsStatusNotifierHostRegistered' type='b' access='read'/>
 <property name='ProtocolVersion' type='i' access='read'/>
 <signal name='StatusNotifierItemRegistered'><arg type='s'/></signal>
 <signal name='StatusNotifierItemUnregistered'><arg type='s'/></signal>
 <signal name='StatusNotifierHostRegistered'/>
</interface></node>"""

bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
items = {}          # service -> dict
log = {"registered": [], "unregistered": [], "errors": [], "checks": [], "menu": None}
t0 = time.monotonic()
loop = GLib.MainLoop()


def err(msg):
    log["errors"].append(msg)
    print("FAIL", msg, flush=True)


def ok(msg):
    log["checks"].append(msg)
    print("ok", msg, flush=True)


def save_png(raw, w, h, path):
    try:
        from PIL import Image
    except ImportError:
        return
    px = bytearray(w * h * 4)
    for i in range(w * h):
        a, r, g, b = raw[i * 4:i * 4 + 4]
        px[i * 4:i * 4 + 4] = bytes((r, g, b, a))
    Image.frombytes("RGBA", (w, h), bytes(px)).save(path)


def fetch(svc):
    it = items.get(svc)
    if not it or it.get("gone"):
        return
    try:
        r = bus.call_sync(svc, "/StatusNotifierItem", "org.freedesktop.DBus.Properties", "GetAll",
                          GLib.Variant("(s)", ("org.kde.StatusNotifierItem",)), GLib.VariantType("(a{sv})"),
                          Gio.DBusCallFlags.NONE, 2000, None)
    except GLib.Error as e:
        if not log.get("quit_clicked"):   # after Quit, NewIcon signals still queued here may name items already gone
            err("GetAll %s: %s" % (svc, e.message))
        return
    props = r.unpack()[0]
    it["props"] = {k: v for k, v in props.items() if k not in ("IconPixmap",)}
    pix = props.get("IconPixmap", [])
    sizes = []
    for (w, h, data) in pix:
        data = bytes(data)
        sizes.append(w)
        if len(data) != w * h * 4 or w != h:
            err("%s pixmap %dx%d has %d bytes" % (it["id"], w, h, len(data)))
            continue
        # big-endian ARGB: alpha first. The plate's centre pixel is nearly opaque (alpha >= 0xd0).
        c = (h // 2 * w + w // 2) * 4
        if data[c] < 0xd0:
            err("%s %d px centre alpha %d (byte order?)" % (it["id"], w, data[c]))
        it["alpha_center"] = data[c]
        save_png(data, w, h, os.path.join(OUT, "%s-%d-recv.png" % (it["id"], w)))
    it["sizes"] = sizes
    tt = props.get("ToolTip")
    it["tooltip"] = [tt[2], tt[3]] if tt else None


def on_item_signal(conn, sender, path, iface, sig, params, svc):
    it = items.get(svc)
    if not it:
        return
    if sig == "NewIcon":
        it["newicon"].append(round(time.monotonic() - t0, 2))
        fetch(svc)
    elif sig == "NewTitle":
        it["newtitle"] = it.get("newtitle", 0) + 1
    elif sig == "NewToolTip":
        it["newtooltip"] += 1


def on_call(conn, sender, path, iface, method, params, inv):
    if method == "RegisterStatusNotifierItem":
        svc = params.unpack()[0]
        if svc.startswith("/"):
            # registered by object path (libappindicator does this, e.g. SRS's own main tray icon); Plasma accepts it.
            # Recorded only: the meters (srs-*) must use bus names, which the checks below look for.
            log.setdefault("by_path", []).append([sender, svc])
            print("registered-by-path", svc, flush=True)
            inv.return_value(None)
            return
        if svc in items and not items[svc].get("gone"):
            inv.return_value(None)
            return
        items[svc] = {"service": svc, "sender": sender, "newicon": [], "newtooltip": 0, "t": round(time.monotonic() - t0, 2)}
        try:
            idv = bus.call_sync(svc, "/StatusNotifierItem", "org.freedesktop.DBus.Properties", "Get",
                                GLib.Variant("(ss)", ("org.kde.StatusNotifierItem", "Id")), GLib.VariantType("(v)"),
                                Gio.DBusCallFlags.NONE, 2000, None).unpack()[0]
        except GLib.Error as e:
            idv = "?"
            err("Get Id %s: %s" % (svc, e.message))
        items[svc]["id"] = idv
        log["registered"].append([idv, round(time.monotonic() - t0, 2)])
        print("registered", idv, svc, flush=True)
        for s in ("NewIcon", "NewToolTip", "NewTitle"):
            bus.signal_subscribe(svc, "org.kde.StatusNotifierItem", s, "/StatusNotifierItem", None,
                                 Gio.DBusSignalFlags.NONE, on_item_signal, svc)
        inv.return_value(None)
        conn.emit_signal(None, "/StatusNotifierWatcher", "org.kde.StatusNotifierWatcher", "StatusNotifierItemRegistered",
                         GLib.Variant("(s)", (svc,)))
        GLib.idle_add(lambda: (fetch(svc), False)[1])
    else:
        inv.return_value(None)


def on_get(conn, sender, path, iface, prop):
    if prop == "RegisteredStatusNotifierItems":
        return GLib.Variant("as", [s for s, i in items.items() if not i.get("gone")])
    if prop == "IsStatusNotifierHostRegistered":
        return GLib.Variant("b", True)
    return GLib.Variant("i", 0)


def on_owner(conn, sender, path, iface, sig, params):
    name, old, new = params.unpack()
    if name in items and not new and not items[name].get("gone"):
        items[name]["gone"] = True
        log["unregistered"].append([items[name]["id"], round(time.monotonic() - t0, 2)])
        print("unregistered", items[name]["id"], flush=True)


node = Gio.DBusNodeInfo.new_for_xml(XML)
bus.register_object("/StatusNotifierWatcher", node.interfaces[0], on_call, on_get, None)
bus.signal_subscribe("org.freedesktop.DBus", "org.freedesktop.DBus", "NameOwnerChanged", "/org/freedesktop/DBus", None,
                     Gio.DBusSignalFlags.NONE, on_owner)
Gio.bus_own_name_on_connection(bus, "org.kde.StatusNotifierWatcher", Gio.BusNameOwnerFlags.NONE, None, None)


def by_id(i):
    for s, it in items.items():
        if it.get("id") == i and not it.get("gone"):
            return s
    return None


def call(svc, path, iface, method, args, rtype):
    return bus.call_sync(svc, path, iface, method, args, GLib.VariantType(rtype) if rtype else None,
                         Gio.DBusCallFlags.NONE, 3000, None)


def layout_items(svc):
    r = call(svc, "/MenuBar", "com.canonical.dbusmenu", "GetLayout", GLib.Variant("(iias)", (0, -1, [])), "(u(ia{sv}av))")
    rev, (rid, rprops, kids) = r.unpack()
    out = []
    for k in kids:
        cid, cprops, _ = k
        out.append((cid, cprops))
    return rev, rprops, out


def find(entries, label):
    for cid, p in entries:
        if p.get("label") == label:
            return cid, p
    return None, None


def titles(key):
    """Category, Title and Id of every live srs-meter item, read now (Plasma sorts on Category, then Title)."""
    out = []
    for svc, it in items.items():
        if it.get("gone") or not str(it.get("id", "")).startswith("srs-meter-"):
            continue
        r = call(svc, "/StatusNotifierItem", "org.freedesktop.DBus.Properties", "GetAll",
                 GLib.Variant("(s)", ("org.kde.StatusNotifierItem",)), "(a{sv})").unpack()[0]
        out.append([r.get("Category"), r.get("Title"), r.get("Id")])
    log[key] = out


def step_activate():
    svc = by_id("srs-meter-cpu")
    if not svc:
        err("no srs-meter-cpu item to click")
        return False
    call(svc, "/StatusNotifierItem", "org.kde.StatusNotifierItem", "ProvideXdgActivationToken", GLib.Variant("(s)", ("test-token",)), None)
    call(svc, "/StatusNotifierItem", "org.kde.StatusNotifierItem", "Activate", GLib.Variant("(ii)", (10, 10)), None)
    ok("Activate sent to srs-meter-cpu")
    rev, rprops, entries = layout_items(svc)
    log["menu"] = [[cid, {k: (v if isinstance(v, (int, str, bool)) else str(v)) for k, v in p.items()}] for cid, p in entries]
    if rprops.get("children-display") != "submenu":
        err("menu root has no children-display=submenu")
    labels = [p.get("label") for _, p in entries if p.get("type") != "separator"]
    ok("menu: " + " | ".join(str(l) for l in labels))
    ver = call(svc, "/MenuBar", "org.freedesktop.DBus.Properties", "Get", GLib.Variant("(ss)", ("com.canonical.dbusmenu", "Version")), "(v)").unpack()[0]
    if ver != 3:
        err("dbusmenu Version %r" % ver)
    titles("titles_start")
    cid, p = find(entries, "CPU Cores")
    if cid is None or p.get("toggle-type") != "checkmark" or p.get("toggle-state") != 0:
        err("CPU Cores entry missing or already ticked: %r" % (p,))
        return False
    call(svc, "/MenuBar", "com.canonical.dbusmenu", "Event", GLib.Variant("(isvu)", (cid, "clicked", GLib.Variant("i", 0), 0)), None)
    ok("clicked CPU Cores on")
    return False


def step_cores_check():
    if by_id("srs-meter-cores"):
        ok("srs-meter-cores registered after the menu click")
        titles("titles_with_cores")
        svc = by_id("srs-meter-cpu")
        _, _, entries = layout_items(svc)
        cid, p = find(entries, "CPU Cores")
        if p.get("toggle-state") != 1:
            err("CPU Cores not ticked in the rebuilt menu")
        call(svc, "/MenuBar", "com.canonical.dbusmenu", "Event", GLib.Variant("(isvu)", (cid, "clicked", GLib.Variant("i", 0), 0)), None)
        ok("clicked CPU Cores off")
    else:
        err("srs-meter-cores did not appear after ticking it")
    return False


def step_quit():
    if by_id("srs-meter-cores"):
        err("srs-meter-cores still there after unticking it")
    else:
        ok("srs-meter-cores removed after unticking it")
    titles("titles_after")
    svc = by_id("srs-meter-cpu")
    if svc:
        _, _, entries = layout_items(svc)
        oid, _ = find(entries, "_Open SRS")
        if oid is None:
            err("no Open SRS entry")
        else:
            call(svc, "/MenuBar", "com.canonical.dbusmenu", "Event", GLib.Variant("(isvu)", (oid, "clicked", GLib.Variant("i", 0), 0)), None)
            ok("clicked Open SRS")
        cid, _ = find(entries, "_Quit SRS")
        if cid is None:
            err("no Quit entry")
        else:
            log["quit_clicked"] = True
            call(svc, "/MenuBar", "com.canonical.dbusmenu", "Event", GLib.Variant("(isvu)", (cid, "clicked", GLib.Variant("i", 0), 0)), None)
            ok("clicked Quit")
    return False


def finish():
    log["items"] = list(items.values())
    with open(os.path.join(OUT, "watcher.json"), "w") as f:
        json.dump(log, f, indent=1, default=str)
    loop.quit()
    return False


A = max(SECS - 6, 3)
GLib.timeout_add(int(A * 1000), step_activate)
GLib.timeout_add(int((A + 1.5) * 1000), step_cores_check)
GLib.timeout_add(int((A + 3.0) * 1000), step_quit)
GLib.timeout_add(int((A + 5.0) * 1000), finish)
loop.run()
