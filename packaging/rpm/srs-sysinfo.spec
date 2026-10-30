Name:           srs-sysinfo
Version:        1.1.0
Release:        1%{?dist}
Summary:        Linux system information viewer inspired by SIV64X
License:        GPL-2.0-or-later
URL:            https://github.com/eugenesflanagin-spec/srs-sysinfo
Source0:        %{url}/archive/refs/tags/v%{version}.tar.gz#/%{name}-%{version}.tar.gz
Source1:        srs-gui.1
Source2:        sysreport-search.1
Source3:        srs-sysreport.1
Source4:        srs-root.1
ExclusiveArch:  x86_64

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  pkgconfig(gtk+-3.0)
BuildRequires:  pkgconfig(sqlite3)
BuildRequires:  desktop-file-utils
BuildRequires:  systemd-rpm-macros
Requires:       hwdata
Requires:       xdg-utils
%if !0%{?suse_version}
Requires:       hicolor-icon-theme
%endif
Recommends:     polkit
Recommends:     dmidecode
Recommends:     pciutils
Suggests:       i2c-tools
Suggests:       smartmontools
Suggests:       nvme-cli
Suggests:       usbutils
Suggests:       lm_sensors
Suggests:       vulkan-tools
Suggests:       clinfo
Suggests:       iw
Suggests:       inxi
Suggests:       lshw
Suggests:       hwinfo
# loaded at run time (dlopen) by the Everyday Work benchmark; a missing one only skips its rows
%if 0%{?suse_version}
Suggests:       libz1
Suggests:       libzstd1
Suggests:       liblz4-1
Suggests:       liblzma5
%else
Suggests:       zlib
Suggests:       libzstd
Suggests:       lz4-libs
Suggests:       xz-libs
%endif
# libcurl, loaded at run time by the Network benchmark's optional internet speed test
%if 0%{?suse_version}
Suggests:       libcurl4
%else
Suggests:       libcurl
%endif
# the curl command (or wget): Tools > Update hardware lists downloads pci.ids, usb.ids and pnp.ids with it (2.4)
Suggests:       curl
# the Vulkan loader, loaded at run time by the GPU benchmarks (with the GPU's own Vulkan driver)
%if 0%{?suse_version}
Suggests:       libvulkan1
%else
Suggests:       vulkan-loader
%endif
# The NVIDIA runtime compiler (NVRTC) is optional since 2.3 (the CUDA kernels are built in) and is not in Fedora's or
# openSUSE's own repositories: only NVIDIA's CUDA repository has it (cuda-nvrtc-<version>), with a version in every
# package name, so no weak dependency can name it. Options > NVIDIA CUDA toolkit (and sysreport-search cuda-toolkit)
# shows the instructions instead.
# ROCm / HIP, OpenCL and CLBlast, loaded at run time by GPU Burnin and the GPU Compute comparison
%if 0%{?suse_version}
Suggests:       libOpenCL1
%else
Suggests:       ocl-icd
Suggests:       rocm-hip
Suggests:       clblast
%endif
%if 0%{?suse_version}
Suggests:       libayatana-appindicator3-1
%else
Suggests:       libayatana-appindicator-gtk3
%endif

%description
SRS is a system information viewer for Linux that follows SIV64X, the System
Information Viewer by Ray Hinchliffe (RH Software), built to improve on it and
free for the Linux and open-source community. It is an independent project,
not affiliated with SIV or its author, and contains no SIV code or data.

It provides a GTK 3 GUI (srs-gui) with 558 report items, a CPU burn-in /
stress tester, a GPU burn-in, Dhrystone/Whetstone, memory-latency, CPU,
GPU and other benchmarks, a
command-line report search (sysreport-search), a full text system report
(srs-sysreport) and an optional root-only data collector (srs-root, run
through pkexec or the srs-root@.service boot unit).

A built-in user guide (PDF and HTML, in /usr/share/doc/srs-sysinfo) explains
every window, option and item; F1 or the Guide button in each panel opens it
at that item's section.

%prep
%autosetup -n %{name}-%{version}

%build
%set_build_flags
# the Makefile links without -pie; ask for a PIE explicitly (default on Fedora, not on openSUSE)
export CFLAGS="$CFLAGS -fPIE" LDFLAGS="$LDFLAGS -pie"
%make_build CC=gcc native

%install
%make_build CC=gcc DESTDIR=%{buildroot} SYSPREFIX=%{_prefix} install-system
# the licence is packaged with %%license below. The doc tree installed by
# install-system (README, CHANGELOG, user guide PDF/HTML/Markdown, images) is
# kept as it is: the app opens /usr/share/doc/srs-sysinfo/SRS-User-Guide.pdf
# and html/SRS-User-Guide.html by these exact paths (on openSUSE too, where
# %%{_docdir} is /usr/share/doc/packages).
rm -rf %{buildroot}%{_datadir}/licenses
%if "%{_unitdir}" != "/usr/lib/systemd/system"
install -Dm644 %{buildroot}/usr/lib/systemd/system/srs-root@.service %{buildroot}%{_unitdir}/srs-root@.service
rm -rf %{buildroot}/usr/lib/systemd
%endif

install -Dm644 -t %{buildroot}%{_mandir}/man1 %{SOURCE1} %{SOURCE2} %{SOURCE3} %{SOURCE4}

%check
make CC=gcc test-power
for f in %{buildroot}%{_datadir}/applications/*.desktop; do desktop-file-validate "$f"; done

# srs-root@.service is a per-user template; the GUI enables it on request,
# so it is not preset or started here.
%preun
%systemd_preun 'srs-root@*.service'

%files
%license LICENSE
%doc %{_datadir}/doc/%{name}/
%{_bindir}/srs-gui
%{_bindir}/srs-root
%{_bindir}/srs-sysreport
%{_bindir}/sysreport-search
%{_unitdir}/srs-root@.service
%{_datadir}/applications/srs-gui.desktop
%{_datadir}/applications/srs-burnin.desktop
%{_datadir}/applications/srs-benchmark.desktop
%{_datadir}/applications/srs-sysreport.desktop
%{_datadir}/applications/srs-user-guide.desktop
%{_datadir}/icons/hicolor/256x256/apps/srs.png
%{_datadir}/srs-sysinfo/
%{_mandir}/man1/srs-gui.1*
%{_mandir}/man1/sysreport-search.1*
%{_mandir}/man1/srs-sysreport.1*
%{_mandir}/man1/srs-root.1*

%changelog
* Sun Oct 25 2026 Eugene Flanagin <eflanagin@pm.me> - 1.1.0-1
- Package of upstream release 1.1.0 (see CHANGELOG.md)

* Sun Oct 25 2026 Eugene Flanagin <eflanagin@pm.me> - 1.0.8-1
- Native package of upstream release 1.0.8
