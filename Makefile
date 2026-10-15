# sysreport-search / SRS - one C source set, one image per platform, the way SIV
# ships SIV64X.exe, SIV32X.exe, ... from a single code base.
#
#   make            all images into dist/
#   make check      parity + GUI smoke tests are in the job scripts; this checks headers
#   make install    native CLI + native GTK GUI + Windows images under ~/.local
#
# Full paths for compilers: /opt/aocc/bin shadows bare gcc/clang on this box.

CC        := /usr/bin/gcc
CC32      := /usr/bin/gcc -m32
CCA64     := /usr/bin/aarch64-linux-gnu-gcc
MINGW64   := /usr/bin/x86_64-w64-mingw32-gcc
MINGW32   := /usr/bin/i686-w64-mingw32-gcc
WINDRES64 := /usr/bin/x86_64-w64-mingw32-windres
WINDRES32 := /usr/bin/i686-w64-mingw32-windres

WFLAGS    := -std=c11 -Wall -Wextra -Wno-format-truncation
CFLAGS    ?= -O2
BASE      = $(WFLAGS) $(CFLAGS)
CORE      := core.c cli.c
HDRS      := srs.h
# the SIV section engine (Linux images): every SIV item, the burner and the benchmarks
SECS      := sec_core.c sec_stubs.c sec_cpu.c sec_machine.c sec_sensors.c sec_storage.c sec_network.c \
             sec_os.c sec_gpu.c sec_bus.c sec_tools.c sec_siv.c burner.c bench.c
SECHDRS   := sec.h burner.h bench.h sec_default_save.inc
LINUX     := -DSRS_HAVE_SECTIONS -D_DEFAULT_SOURCE -pthread

# --- PE layout copied field-for-field from SIV64X.exe / SIV32X.exe (objdump -p) ---
#   system msvcrt.dll (not UCRT), GUI subsystem, ImageBase 0x1000000,
#   stack 0x1800000 reserve / 0x200000 commit, OS version 6.00, TS-aware + dynamic base.
PE_COMMON := -mwindows -mcrtdll=msvcrt-os -static-libgcc -s \
             -Wl,--image-base,0x1000000 -Xlinker --stack -Xlinker 0x1800000,0x200000 \
             -Wl,--major-os-version,6 -Wl,--minor-os-version,0 \
             -Wl,--tsaware -Wl,--dynamicbase
PE64      := $(PE_COMMON) -Wl,--major-subsystem-version,5 -Wl,--minor-subsystem-version,2 \
             -Wl,--disable-nxcompat -Wl,--disable-high-entropy-va
PE32      := $(PE_COMMON) -Wl,--major-subsystem-version,5 -Wl,--minor-subsystem-version,0 \
             -Wl,--nxcompat
WINLIBS   := -lcomdlg32 -lshell32 -lole32 -lsetupapi -lgdi32 -luser32 -lkernel32
WINSRC    := gui.c plat_win32.c $(CORE)

DIST := dist

all: cli-x86_64 cli-i686 cli-aarch64 gui win64 win32 data

cli-x86_64: $(DIST)/linux-x86_64/sysreport-search
cli-i686: $(DIST)/linux-i686/sysreport-search
cli-aarch64: $(DIST)/linux-aarch64/sysreport-search
win64: $(DIST)/win/SRS64X.exe
win32: $(DIST)/win/SRS32X.exe
gui: $(DIST)/linux-x86_64/srs-gui

$(DIST)/linux-x86_64/sysreport-search: $(CORE) plat_posix.c $(SECS) $(HDRS) $(SECHDRS)
	@mkdir -p $(@D)
	$(CC) $(BASE) $(LINUX) -s -o $@ $(CORE) plat_posix.c $(SECS) -lm

$(DIST)/linux-i686/sysreport-search: $(CORE) plat_posix.c $(SECS) $(HDRS) $(SECHDRS)
	@mkdir -p $(@D)
	$(CC32) $(BASE) $(LINUX) -s -o $@ $(CORE) plat_posix.c $(SECS) -lm

$(DIST)/linux-aarch64/sysreport-search: $(CORE) plat_posix.c $(SECS) $(HDRS) $(SECHDRS)
	@mkdir -p $(@D)
	$(CCA64) $(BASE) $(LINUX) -s -o $@ $(CORE) plat_posix.c $(SECS) -lm

$(DIST)/win/srs64.res.o: srs.rc srs.ico srs.manifest
	@mkdir -p $(@D)
	$(WINDRES64) srs.rc -O coff -o $@

$(DIST)/win/srs32.res.o: srs.rc srs.ico srs.manifest
	@mkdir -p $(@D)
	$(WINDRES32) srs.rc -O coff -o $@

$(DIST)/win/SRS64X.exe: $(WINSRC) $(HDRS) $(DIST)/win/srs64.res.o
	$(MINGW64) $(BASE) -DSRS_NO_MAIN -o $@ $(WINSRC) $(DIST)/win/srs64.res.o $(PE64) $(WINLIBS)

$(DIST)/win/SRS32X.exe: $(WINSRC) $(HDRS) $(DIST)/win/srs32.res.o
	$(MINGW32) $(BASE) -DSRS_NO_MAIN -o $@ $(WINSRC) $(DIST)/win/srs32.res.o $(PE32) $(WINLIBS)

# native Linux GUI: plain C + GTK3, same core, glibc + GTK only (no wine)
GTKFLAGS := $(shell pkg-config --cflags gtk+-3.0)
GTKLIBS  := $(shell pkg-config --libs gtk+-3.0)
$(DIST)/linux-x86_64/srs-gui: gui_gtk.c plat_posix.c $(CORE) $(SECS) $(HDRS) $(SECHDRS)
	@mkdir -p $(@D)
	$(CC) $(BASE) $(LINUX) -Wno-deprecated-declarations -DSRS_NO_MAIN $(GTKFLAGS) -s -o $@ gui_gtk.c plat_posix.c $(CORE) $(SECS) $(GTKLIBS) -lm

# ship the open data files beside the Windows images, the way SIV ships PCIDEVS.txt
data: $(DIST)/win/pci.ids $(DIST)/win/usb.ids
$(DIST)/win/%.ids: /usr/share/hwdata/%.ids
	@mkdir -p $(@D)
	cp $< $@

PREFIX ?= $(HOME)/.local
install: cli-x86_64 gui win64 win32 data
	install -Dm755 $(DIST)/linux-x86_64/sysreport-search $(PREFIX)/bin/sysreport-search
	install -Dm755 $(DIST)/linux-x86_64/srs-gui $(PREFIX)/bin/srs-gui
	install -Dm755 srs-root.sh $(PREFIX)/bin/srs-root
	install -Dm644 $(DIST)/win/SRS64X.exe $(PREFIX)/lib/srs/win/SRS64X.exe
	install -Dm644 $(DIST)/win/SRS32X.exe $(PREFIX)/lib/srs/win/SRS32X.exe
	install -Dm644 $(DIST)/win/pci.ids $(PREFIX)/lib/srs/win/pci.ids
	install -Dm644 $(DIST)/win/usb.ids $(PREFIX)/lib/srs/win/usb.ids
	install -Dm644 srs.png $(PREFIX)/share/icons/hicolor/256x256/apps/srs.png
	install -d $(PREFIX)/share/applications
	sed 's|^Exec=srs-gui|Exec=$(PREFIX)/bin/srs-gui|' srs-gui.desktop > $(PREFIX)/share/applications/srs-gui.desktop

# --- distribution packages (AUR): native build with the distro's CFLAGS/LDFLAGS, no cross tools ---
NATIVE := $(DIST)/native
SYSPREFIX ?= /usr
native: $(NATIVE)/srs-gui $(NATIVE)/sysreport-search

$(NATIVE)/sysreport-search: $(CORE) plat_posix.c $(SECS) $(HDRS) $(SECHDRS)
	@mkdir -p $(@D)
	$(CC) $(BASE) $(LINUX) $(CPPFLAGS) $(LDFLAGS) -o $@ $(CORE) plat_posix.c $(SECS) -lm

$(NATIVE)/srs-gui: gui_gtk.c plat_posix.c $(CORE) $(SECS) $(HDRS) $(SECHDRS)
	@mkdir -p $(@D)
	$(CC) $(BASE) $(LINUX) -Wno-deprecated-declarations -DSRS_NO_MAIN $(GTKFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ gui_gtk.c plat_posix.c $(CORE) $(SECS) $(GTKLIBS) -lm

install-system: native
	install -Dm755 $(NATIVE)/srs-gui $(DESTDIR)$(SYSPREFIX)/bin/srs-gui
	install -Dm755 $(NATIVE)/sysreport-search $(DESTDIR)$(SYSPREFIX)/bin/sysreport-search
	install -Dm755 srs-root.sh $(DESTDIR)$(SYSPREFIX)/bin/srs-root
	install -Dm755 sysreport.sh $(DESTDIR)$(SYSPREFIX)/bin/srs-sysreport
	install -Dm644 srs.png $(DESTDIR)$(SYSPREFIX)/share/icons/hicolor/256x256/apps/srs.png
	install -Dm644 srs-gui.desktop $(DESTDIR)$(SYSPREFIX)/share/applications/srs-gui.desktop
	install -Dm644 srs-burnin.desktop $(DESTDIR)$(SYSPREFIX)/share/applications/srs-burnin.desktop
	install -Dm644 srs-benchmark.desktop $(DESTDIR)$(SYSPREFIX)/share/applications/srs-benchmark.desktop
	install -Dm644 srs-sysreport.desktop $(DESTDIR)$(SYSPREFIX)/share/applications/srs-sysreport.desktop
	install -Dm644 README.md $(DESTDIR)$(SYSPREFIX)/share/doc/srs-sysinfo/README.md
	install -Dm644 EVIDENCE.md $(DESTDIR)$(SYSPREFIX)/share/doc/srs-sysinfo/EVIDENCE.md
	install -Dm644 CHECKLIST.tsv $(DESTDIR)$(SYSPREFIX)/share/doc/srs-sysinfo/CHECKLIST.tsv
	install -Dm644 LICENSE $(DESTDIR)$(SYSPREFIX)/share/licenses/srs-sysinfo/LICENSE

headers: win64 win32
	x86_64-w64-mingw32-objdump -p $(DIST)/win/SRS64X.exe | grep -E "^(Magic|ImageBase|Major|Minor|Subsystem|DllChar|SizeOfStack)|DLL Name"
	i686-w64-mingw32-objdump -p $(DIST)/win/SRS32X.exe | grep -E "^(Magic|ImageBase|Major|Minor|Subsystem|DllChar|SizeOfStack)|DLL Name"

clean:
	rm -rf $(DIST)

.PHONY: native install-system all cli-x86_64 cli-i686 cli-aarch64 gui win64 win32 data install headers clean
