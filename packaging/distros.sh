# shellcheck shell=bash
# Distro table shared by build.sh, test-in-container.sh, build-all.sh and the
# GitHub workflow (.github/workflows/release.yml). Sourced, not run.
#
#   KIND         which build script runs in the build container (build-KIND.sh)
#   BUILD_IMAGE  image the package is built in (toolchain installed fresh each run)
#   TEST_IMAGE   fresh image the finished package is installed and tested in
#   BUILD_ARGS   extra arguments for build-KIND.sh after VER
#                (deb: version suffix + distribution, rpm: %dist; "" = distro default)
# shellcheck disable=SC2034
ALL_DISTROS=(arch debian-13 ubuntu-24.04 fedora-44 opensuse-tumbleweed)
declare -A KIND=(        [arch]=arch                [debian-13]=deb            [ubuntu-24.04]=deb                [fedora-44]=rpm      [opensuse-tumbleweed]=rpm )
declare -A BUILD_IMAGE=( [arch]=archlinux:base-devel [debian-13]=debian:trixie [ubuntu-24.04]=ubuntu:24.04       [fedora-44]=fedora:44 [opensuse-tumbleweed]=opensuse/tumbleweed:latest )
declare -A TEST_IMAGE=(  [arch]=archlinux:latest     [debian-13]=debian:trixie [ubuntu-24.04]=ubuntu:24.04       [fedora-44]=fedora:44 [opensuse-tumbleweed]=opensuse/tumbleweed:latest )
declare -A BUILD_ARGS=(  [arch]=""                   [debian-13]="~deb13 trixie" [ubuntu-24.04]="~ubuntu24.04 noble" [fedora-44]=""      [opensuse-tumbleweed]=".tw" )

known_distro() { [ -n "${KIND[$1]:-}" ] || { echo "unknown distro '$1' (known: ${ALL_DISTROS[*]})" >&2; return 1; }; }
