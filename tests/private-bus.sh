# tests/private-bus.sh - sourced first by every test that starts srs-gui (2.6.0). It runs the test again on a PRIVATE
# D-Bus session bus, so no SRS a test starts can reach the real desktop: no tray icons or menus in the desktop's tray,
# no notifications, no portals. Everything the test starts (Xvfb, xvfb-run, srs-gui, helpers) inherits that bus.
# Without dbus-run-session the test still runs, with the bus address pointed at a path that does not exist.
# Never just unset DBUS_SESSION_BUS_ADDRESS (here or in a test's env -u): GLib then falls back to
# $XDG_RUNTIME_DIR/bus, which is the REAL desktop bus.
#   . "$(dirname "$0")/private-bus.sh"
# 2.6.0: the same for Tux Burner's service socket (the GPU Tuning tab reads it): a test that does not name its own fake
# service gets a path that does not exist. (SRS also refuses the real socket whenever SRS_TEST_PRIVATE_BUS is set.)
export SRS_TUX_BURNER_SOCKET=${SRS_TUX_BURNER_SOCKET:-/nonexistent/srs-test-no-tux-burner.sock}
if [ -z "${SRS_TEST_PRIVATE_BUS:-}" ]; then
    export SRS_TEST_PRIVATE_BUS=1
    if command -v dbus-run-session >/dev/null; then
        exec env -u DBUS_SESSION_BUS_ADDRESS dbus-run-session -- bash "$0" "$@"
    fi
    export DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent/srs-test-no-session-bus
fi
