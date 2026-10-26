# Listing the AppImage in AppImage catalogs

Ready-to-use material for two catalogs. Nothing here is submitted automatically. Do it once,
after a release with the AppImage is **published** on GitHub (both catalogs fetch the AppImage
from the newest release and test it).

Before submitting, check the release:

```sh
gh release view --json assets -q '.assets[].name' | grep AppImage
# srs-sysinfo-2.0.0-x86_64.AppImage
# srs-sysinfo-2.0.0-x86_64.AppImage.zsync
```

## 1. AppImageHub (appimage.github.io)

The catalog behind <https://appimage.github.io>, which app centres such as AppImagePool and
Gear Lever read. One pull request adds one file; their test downloads the AppImage, starts it on
the oldest supported Ubuntu LTS without network and posts a screenshot in the pull request.

What their test needs, and how SRS meets it:

| Requirement | SRS |
|---|---|
| Type 2 AppImage, SquashFS, on GitHub Releases | yes, `packaging/build-appimage.sh` |
| Runs on the oldest supported Ubuntu LTS | built on Ubuntu 22.04 (glibc 2.35) |
| Works with X11, without network, main window within 30 s | yes (checked in CI under Xvfb) |
| One `.desktop` file that passes `desktop-file-validate` | `srs-gui.desktop` |
| AppStream metainfo in `usr/share/metainfo` that passes `appstreamcli` | `io.github.eugenesflanagin_spec.srs_sysinfo.metainfo.xml` |
| Light theme unless the desktop asks for dark | the main window follows the desktop's colour scheme |
| English interface in the C locale | yes |

**File name:** `data/SRS_System_Information` (the `Name=` of the desktop file, blanks as `_`).

**File content** (one line, nothing else):

```
https://github.com/eugenesflanagin-spec/srs-sysinfo
```

**How:** open <https://github.com/AppImage/appimage.github.io/new/master/data>, name the file
`SRS_System_Information`, paste the line, and choose "Propose new file" / "Create pull request".

**Pull request title:**

```
Add SRS System Information
```

**Pull request text:**

```
SRS System Information is a system information viewer for Linux (GTK 3). It shows the
processor, memory, motherboard and BIOS, graphics, storage, network, sensors, USB and PCI
devices and the operating system, with an overview of temperatures, drive health and
hardware errors.

- Repository and releases: https://github.com/eugenesflanagin-spec/srs-sysinfo
- Licence: GPL-2.0-or-later
- The AppImage is built on Ubuntu 22.04 with linuxdeploy and linuxdeploy-plugin-gtk,
  contains AppStream metadata, and carries update information for AppImageUpdate
  (gh-releases-zsync).
- It runs without network access. Root-only details (DMI, SMART, SPD) are optional and
  are not needed for the main window.
```

After the test comments: check that the screenshot shows the main window. `/retest` runs the
test again (for example after a new release).

## 2. AM / AppMan (github.com/ivan-hc/AM)

AM is a command-line package manager for AppImages (`am -i srs-sysinfo`, or `appman` for a
per-user install). An app is added with an install script in `programs/x86_64/` plus one line
in each of two lists. The script below is AM's own template `templates/AM-SAMPLE-AppImage`
(script version 3.6) with the app name, the repository and the download-link function filled in.
Before the pull request, compare it with the current template in their repository in case it has
changed.

**Files in the pull request:**

1. `programs/x86_64/srs-sysinfo` (the script below, mode 755)
2. a line in `programs/x86_64-apps`, in alphabetical order:
   ```
   ◆ srs-sysinfo : System information viewer for Linux, shows hardware, sensors, drive health and more.
   ```
3. the same line in `programs/x86_64-appimages`

**`programs/x86_64/srs-sysinfo`:**

```sh
#!/bin/sh

# AM INSTALL SCRIPT VERSION 3.6
set -u
APP=srs-sysinfo
SITE="eugenesflanagin-spec/srs-sysinfo"

# CREATE DIRECTORIES AND ADD REMOVER
[ -n "$APP" ] && mkdir -p "/opt/$APP/tmp" "/opt/$APP/icons" && cd "/opt/$APP/tmp" || exit 1
printf "#!/bin/sh\nset -e\nrm -f /usr/local/bin/$APP\nrm -R -f /opt/$APP" > ../remove
printf '\n%s' "rm -f /usr/local/share/applications/$APP-AM.desktop" >> ../remove
chmod a+x ../remove || exit 1

# DOWNLOAD AND PREPARE THE APP, $version is also used for updates
version=$(curl -Ls https://api.github.com/repos/"$SITE"/releases | sed 's/[()",{}]/ /g; s/ /\n/g' | grep -oi 'https.*releases.*srs-sysinfo-.*x86_64\.AppImage$' | head -1)
wget "$version" || exit 1
# Keep this space in sync with other installation scripts
# Use tar fx ./*tar* here for example in this line in case a compressed file is downloaded.
cd ..
mv ./tmp/*mage ./"$APP"
# Keep this space in sync with other installation scripts
rm -R -f ./tmp || exit 1
echo "$version" > ./version
chmod a+x ./"$APP" || exit 1

# LINK TO PATH
ln -s "/opt/$APP/$APP" "/usr/local/bin/$APP"

# SCRIPT TO UPDATE THE PROGRAM
cat >> ./AM-updater << 'EOF'
#!/bin/sh
set -u
APP=srs-sysinfo
SITE="eugenesflanagin-spec/srs-sysinfo"
version0=$(cat "/opt/$APP/version")
version=$(curl -Ls https://api.github.com/repos/"$SITE"/releases | sed 's/[()",{}]/ /g; s/ /\n/g' | grep -oi 'https.*releases.*srs-sysinfo-.*x86_64\.AppImage$' | head -1)
[ -n "$version" ] || { echo "Error getting link"; exit 1; }
if command -v appimageupdatetool >/dev/null 2>&1; then
	cd "/opt/$APP" || exit 1
	appimageupdatetool -Or ./"$APP" && chmod a+x ./"$APP" && echo "$version" > ./version && exit 0
fi
if [ "$version" != "$version0" ]; then
	mkdir "/opt/$APP/tmp" && cd "/opt/$APP/tmp" || exit 1
	notify-send "A new version of $APP is available, please wait"
	wget "$version" || exit 1
	# Use tar fx ./*tar* here for example in this line in case a compressed file is downloaded.
	cd ..
	mv --backup=t ./tmp/*mage ./"$APP"
	chmod a+x ./"$APP" || exit 1
	echo "$version" > ./version
	rm -R -f ./*zs-old ./*.part ./tmp ./*~
	notify-send "$APP is updated!"
else
	echo "Update not needed!"
fi
EOF
chmod a+x ./AM-updater || exit 1

# LAUNCHER & ICON
./"$APP" --appimage-extract *.desktop 1>/dev/null
./"$APP" --appimage-extract .DirIcon 1>/dev/null && mv ./squashfs-root/.DirIcon ./DirIcon
COUNT=0
while [ "$COUNT" -lt 10 ]; do # Tries to get the actual icon/desktop if it is a symlink to another symlink
	for f in squashfs-root/*.desktop; do
		FILE="$f"
		if [ -L "$f" ]; then
			LINKPATH="$(readlink "$f" | sed 's|^\./||' 2>/dev/null)"
			./"$APP" --appimage-extract "$LINKPATH" 1>/dev/null
			FILE="./squashfs-root/$LINKPATH"
		fi
		if grep -qi '^NoDisplay=true$' "$FILE" 2>/dev/null; then mv "$FILE" ./"$APP"-handler-AM.desktop && printf '\n%s' "rm -f /usr/local/share/applications/$APP-handler-AM.desktop" >> ./remove
		else [ ! -f ./"$APP"-AM.desktop ] && mv "$FILE" ./"$APP"-AM.desktop || mv "$FILE" ./"$APP"-"$COUNT"-AM.desktop && printf '\n%s' "rm -f /usr/local/share/applications/$APP-$COUNT-AM.desktop" >> ./remove
		fi
	done
	if [ -L ./DirIcon ]; then
		LINKPATH="$(readlink ./DirIcon | sed 's|^\./||' 2>/dev/null)"
		./"$APP" --appimage-extract "$LINKPATH" 1>/dev/null && mv ./squashfs-root/"$LINKPATH" ./DirIcon
	fi
	[ ! -L ./"$APP"-AM.desktop ] && [ ! -L ./DirIcon ] && break
	COUNT=$((COUNT + 1))
done
for f in ./*-AM.desktop; do [ "$f" != ./"$APP"-AM.desktop ] && [ -f "$f" ] && [ "$(sort ./"$APP"-AM.desktop)" = "$(sort "$f")" ] && rm -f "$f"; done
sed -i "s#Exec=[^ ]*#Exec=$APP#g; s#Icon=.*#Icon=/opt/$APP/icons/$APP#g; s# --no-sandbox##g" ./*-AM.desktop
mv ./*-AM.desktop /usr/local/share/applications/ && mv ./DirIcon ./icons/"$APP" 1>/dev/null
rm -R -f ./squashfs-root
```

The menu entry's `Exec=srs-gui` becomes `Exec=srs-sysinfo` (the AppImage), and its actions
(`--gui-open=sensors`, ...) keep working, because the AppImage starts `srs-gui` with the same
arguments. `srs-sysinfo --version` and `srs-sysinfo find ...` give the command line.

**Pull request title:**

```
Add srs-sysinfo
```

**Pull request text:**

```
Adds srs-sysinfo (SRS System Information), a system information viewer for Linux (GTK 3):
processor, memory, motherboard and BIOS, graphics, storage, network, sensors, USB and PCI
devices and the operating system, with an overview of temperatures, drive health and
hardware errors, plus a command-line report search.

- Upstream: https://github.com/eugenesflanagin-spec/srs-sysinfo (GPL-2.0-or-later)
- Downloads the x86_64 AppImage from the newest GitHub release
- The AppImage carries update information (gh-releases-zsync), so the updater can use
  appimageupdatetool
- The script is the AM-SAMPLE-AppImage template with the name, site and download
  function filled in; the line is added to x86_64-apps and x86_64-appimages
```

## Checking the result

- AppImageHub: the entry appears as `https://appimage.github.io/SRS_System_Information/` after
  the pull request is merged.
- AM: `am -i srs-sysinfo` (or `appman -i srs-sysinfo`), then `srs-sysinfo --version`.
