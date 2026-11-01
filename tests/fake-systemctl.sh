#!/bin/sh
# fake-systemctl.sh - stands in for systemctl in the tests (SRS_TEST_SYSTEMCTL / SRS_TEST_LOGINCTL):
# logs its arguments to $FAKE_SYSTEMCTL_LOG and answers from small state files in $FAKE_SYSTEMCTL_DIR.
# It never touches the real system.
# SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later.
log=${FAKE_SYSTEMCTL_LOG:-/dev/null}
dir=${FAKE_SYSTEMCTL_DIR:-/nonexistent}
echo "$(basename "$0") $*" >> "$log"
[ "$1" = "--user" ] && shift
# "verb -- unit": drop the end-of-options marker so $2 is the unit, as SRS passes it
if [ "$2" = "--" ]; then v=$1; shift 2; set -- "$v" "$@"; fi
case "$1" in
list-sessions)
    [ -f "$dir/sessions.txt" ] && cat "$dir/sessions.txt"; exit 0 ;;
show-session)
    [ -f "$dir/show-sessions.txt" ] && cat "$dir/show-sessions.txt"; exit 0 ;;
list-unit-files)
    case "$*" in *--type=service*) [ -f "$dir/unit-files.txt" ] && cat "$dir/unit-files.txt"; exit 0 ;; esac
    [ -f "$dir/enabled" ] && while read -r u; do printf '%s enabled enabled\n' "$u"; done < "$dir/enabled"
    exit 0 ;;
show)
    shift
    case "$*" in *'*.service'*) [ -f "$dir/show-services.txt" ] && cat "$dir/show-services.txt"; exit 0 ;; esac
    first=1
    for a in "$@"; do
        case "$a" in -p|--no-pager|Id|Description|WantedBy|UnitFileState|FragmentPath|ExecStart|ActiveState|--property=*) continue ;; esac
        [ $first = 1 ] || echo
        first=0
        st=disabled; grep -qx "$a" "$dir/enabled" 2>/dev/null && st=enabled
        printf 'Id=%s\nDescription=Fake %s\nWantedBy=default.target\nUnitFileState=%s\nFragmentPath=/fake/%s\nExecStart={ path=/usr/bin/true ; argv[]=/usr/bin/true --fake ; }\nActiveState=inactive\n' "$a" "$a" "$st" "$a"
    done
    exit 0 ;;
enable)
    grep -qx "$2" "$dir/enabled" 2>/dev/null || echo "$2" >> "$dir/enabled"; exit 0 ;;
disable)
    if [ -f "$dir/enabled" ]; then grep -vx "$2" "$dir/enabled" > "$dir/enabled.new"; mv "$dir/enabled.new" "$dir/enabled"; fi; exit 0 ;;
start|stop|restart|terminate-session)
    [ -f "$dir/fail" ] && { echo "Failed to $1 $2: fake failure" >&2; exit 1; }
    exit 0 ;;
*)  exit 0 ;;
esac
