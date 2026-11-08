# tests/private-bus.sh - sourced first by every test that starts srs-gui (2.6.0). It runs the test again on a PRIVATE
# D-Bus session bus, so no SRS a test starts can reach the real desktop: no tray icons or menus in the desktop's tray,
# no notifications, no portals. Everything the test starts (Xvfb, xvfb-run, srs-gui, helpers) inherits that bus.
# Without dbus-run-session the test still runs, with DBUS_SESSION_BUS_ADDRESS unset.
#   . "$(dirname "$0")/private-bus.sh"
if [ -z "${SRS_TEST_PRIVATE_BUS:-}" ]; then
    export SRS_TEST_PRIVATE_BUS=1
    if command -v dbus-run-session >/dev/null; then
        exec env -u DBUS_SESSION_BUS_ADDRESS dbus-run-session -- bash "$0" "$@"
    fi
    unset DBUS_SESSION_BUS_ADDRESS
fi
