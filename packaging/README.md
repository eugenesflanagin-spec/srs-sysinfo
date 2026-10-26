# Packaging and releases

This folder builds the release files for SRS: packages for Arch, Debian 13,
Ubuntu 24.04, Fedora 44 and openSUSE Tumbleweed, the generic Linux tarball,
the AppImage, and the checksums. GitHub builds them for you when you push a version tag
(see `.github/workflows/release.yml`). You can run the same build on your own
PC with `build-all.sh`.

Every package is built in a fresh Docker container of its distro, then
installed with that distro's own package manager in a second fresh container
and checked by `test.sh` (programs start, version is right, user guide is
installed, menu entries are valid). The AppImage is built in an Ubuntu 22.04
container and started in fresh Debian 12 and Fedora 44 containers (see
[The AppImage](#the-appimage) below).

## Making a release

1. **Set the version.** Change `SRS_VERSION` in `srs.h` (for example to `1.2.0`).
   That is the only place the version is written. The Debian changelog, the
   RPM spec and the man pages get it automatically at build time.
2. **Update `CHANGELOG.md`.** Rename `## Unreleased` to `## 1.2.0 - YYYY-MM-DD`
   (the date is used in the package changelogs).
3. **Write the release notes** (optional) in `packaging/release-notes/v1.2.0.md`.
   Its first `# ` line becomes the release title. If there is no such file,
   the `## 1.2.0` section of `CHANGELOG.md` is used instead. Name the `.deb`
   files with a dot, as GitHub shows them: `srs-sysinfo_1.2.0-1.deb13_amd64.deb`.
4. **Commit and push** to `main`.
5. **Push the tag:**
   ```sh
   git tag v1.2.0
   git push origin v1.2.0
   ```
6. **Watch it run:** on GitHub, open the **Actions** tab and click the
   "Release packages" run (or `gh run watch`). It takes about 10 to 15 minutes.
   If the tag does not match `SRS_VERSION`, it stops in the first step.
7. **Check the release** on the **Releases** page: 5 packages, the
   `-linux-x86_64.tar.gz`, the `-x86_64.AppImage` and its `.AppImage.zsync`,
   `SRS-User-Guide.pdf` and `SHA256SUMS` (10 files).
   The release is only created if every build and every install test passed.
8. **Update the AUR package** as before (`~/aur/srs-sysinfo`: new `pkgver`,
   `updpkgsums`, `makepkg --printsrcinfo > .SRCINFO`, push). The workflow does
   not touch the AUR.

## Test run without a release

On GitHub: **Actions** tab, "Release packages", **Run workflow**, pick a
branch. Or from a terminal:

```sh
gh workflow run release.yml --ref main
gh run watch
```

This builds and tests everything but creates no release. The files are on the
run's page under **Artifacts**: `release-files` holds exactly what a release
would get (with `SHA256SUMS` and the release notes), and `pkg-<distro>` holds
each package with its build logs (lintian / rpmlint output, file list).

## Building on your own PC

Needs Docker. From the repository folder:

```sh
packaging/build-all.sh                     # everything, from the committed HEAD
packaging/build-all.sh fedora-44 arch      # only some distros
packaging/build-all.sh path/to/srs-sysinfo-1.2.0.tar.gz   # from a source tarball
```

Only **committed** files are built (it uses `git archive HEAD`), so commit
first. Results go to `dist/release-<version>/`:

- `<distro>/pkg/` the package, `<distro>/build.log`, `<distro>/test.log`,
  `lintian.txt` or `rpmlint.txt`, `contents.txt` (file list)
- `appimage/` the AppImage, its `.zsync`, `contents.txt`; `appimage.log` and
  `appimage-test.log` beside it (`SRS_NO_APPIMAGE=1` skips the AppImage)
- `release/` all release files plus `SHA256SUMS`
- `release-notes.md` the notes the release would use

The single steps can also be run on their own, exactly as the workflow does:

```sh
packaging/build.sh debian-13 srs-sysinfo-1.2.0.tar.gz out/debian-13
packaging/test-in-container.sh debian-13 out/debian-13/pkg 1.2.0
packaging/make-tarball.sh out/arch/pkg/srs-sysinfo-1.2.0-1-x86_64.pkg.tar.zst srs-sysinfo-1.2.0.tar.gz out/tarball
packaging/build-appimage.sh srs-sysinfo-1.2.0.tar.gz out/appimage
packaging/test-appimage.sh out/appimage/srs-sysinfo-1.2.0-x86_64.AppImage 1.2.0
packaging/test-appimage.sh -s shots/ out/appimage/srs-sysinfo-1.2.0-x86_64.AppImage 1.2.0 ubuntu:22.04 archlinux:latest
```

## What is in this folder

| File | What it does |
|---|---|
| `distros.sh` | The list of distros and which Docker images they use |
| `build.sh` | Builds one distro's package in a fresh container |
| `build-arch.sh`, `build-deb.sh`, `build-rpm.sh` | The build steps that run inside that container |
| `test-in-container.sh` | Installs a package in a fresh container and runs `test.sh` |
| `test.sh`, `test-common.sh` | The install checks |
| `make-tarball.sh`, `INSTALL.txt.in` | The generic Linux tarball and its INSTALL.txt |
| `build-appimage.sh` | The AppImage and its `.zsync` (Ubuntu 22.04 container, pinned tools) |
| `test-appimage.sh` | Starts the AppImage in fresh containers of other distros (CLI + window under Xvfb) |
| `appimage/AppRun` | The AppImage's start script (GUI, or the command-line programs by name) |
| `appimage/*.metainfo.xml` | AppStream metadata (name, description, screenshots) for software centres |
| `appimage/SUBMIT.md` | Ready-made text for listing the AppImage in AppImageHub and AM |
| `release-info.sh` | Reads the version, checks the tag, prints the release notes |
| `lib-version.sh` | Writes the version into the Debian changelog, spec and man pages |
| `build-all.sh` | Runs all of the above on your PC |
| `arch/PKGBUILD`, `arch/makepkg.conf` | Arch build (a copy of the AUR PKGBUILD that builds from the local source) with generic x86-64 flags |
| `debian/`, `rpm/`, `man/` | Debian/Ubuntu and Fedora/openSUSE packaging, man pages |
| `release-notes/` | Release notes per tag |

The generic tarball is the Arch package unpacked, so it needs a glibc at least
as new as Arch's (`INSTALL.txt` in the tarball states the exact version). For
Debian, Ubuntu, Fedora and openSUSE the distro packages are the ones to use.

## The AppImage

`srs-sysinfo-VER-x86_64.AppImage` is the whole program in one file, for any
distribution. It is built by `build-appimage.sh` in a fresh **Ubuntu 22.04**
container, the oldest Ubuntu LTS still supported, so it runs on glibc 2.35 and
newer (the build prints the exact number: the highest `GLIBC_` symbol version
of any program or library inside).

How it is made:

1. `make native` + `make install-system` into an AppDir, as for the packages.
   The boot-time unit and the extra menu entries are left out; the man pages,
   the user guide (PDF + HTML), the icon, `srs-gui.desktop` and the AppStream
   file `appimage/io.github.eugenesflanagin_spec.srs_sysinfo.metainfo.xml` go in.
2. `pci.ids`, `usb.ids` and `pnp.ids` from the hwdata project (pinned) go into
   `usr/share/hwdata`. SRS uses the system's own copy first and this one only
   when the system has none.
3. **linuxdeploy** with **linuxdeploy-plugin-gtk** copies GTK 3 and every other
   library SRS needs (except the ones every distro has, such as glibc and X11),
   plus the tray library `libayatana-appindicator3`. The gtk plugin's hook makes
   GTK use X11 (XWayland on Wayland desktops) unless `GDK_BACKEND` is set.
   Not bundled on purpose: the optional libraries the benchmarks load at run time
   with `dlopen` - zlib, zstd, lz4 and xz/liblzma (Everyday Work) and libcurl
   (the Network benchmark's internet test) - come from the host system; a
   missing one only skips its rows.
4. `appimage/AppRun` replaces linuxdeploy's: `sysreport-search` and
   `srs-sysreport` (as first argument, or as the name of a link to the AppImage)
   start without the GTK settings; otherwise it runs `srs-gui` with the
   arguments, which is the window, or the CLI when there are arguments. It also
   remembers your desktop's own values of the variables the hook changes
   (`SRS_HOSTENV_*`), so the PDF viewer SRS opens for the guide gets them back.
5. **Licences:** for every bundled library, the Ubuntu package it came from is
   looked up (`dpkg -S`) and that package's `copyright` file is copied to
   `usr/share/doc/<package>/`. `usr/share/doc/srs-sysinfo/BUNDLED-LIBRARIES.txt`
   lists library, package and version. The build stops if fewer than 10 licence
   files were found.
6. **appimagetool** packs it with the static type2 runtime and embeds the update
   information `gh-releases-zsync|eugenesflanagin-spec|srs-sysinfo|latest|srs-sysinfo-*x86_64.AppImage.zsync`,
   and writes the `.zsync` file. AppImageUpdate reads that, looks at the newest
   GitHub release and downloads only the changed blocks.
7. A self-check extracts the result and checks the files, the update
   information, `--version` and the glibc it needs.

Containers have no FUSE, so every AppImage there runs with
`APPIMAGE_EXTRACT_AND_RUN=1` (it unpacks itself to /tmp instead of mounting).

**The tools are pinned.** `build-appimage.sh` downloads linuxdeploy,
linuxdeploy-plugin-gtk, appimagetool, the type2 runtime and the ID files from
their GitHub releases (or a fixed commit), each with its sha256. To update one,
change its URL and sum in the `TOOLS` / `IDS` lists at the top. GitHub shows the
sha256 of each release file: `gh api repos/OWNER/REPO/releases/tags/TAG --jq
'.assets[] | .name + " " + .digest'`.

**Run as root from the AppImage.** pkexec runs the helper as root, but root
cannot read inside the AppImage while it runs: the FUSE mount is visible only
to the user who started it. So `srs-gui` copies `srs-root` (a short read-only
shell script) right before the prompt into a new folder only the user can open
(`$XDG_RUNTIME_DIR/srs-root-XXXXXX`, mode 0700, a local tmpfs root can read;
`~/.cache/srs` if that is missing or mounted noexec), runs `pkexec <copy>` and
deletes the copy afterwards. Why this is safe: polkit asks for the administrator
password exactly as for the packaged helper, and the prompt shows the file that
runs; nothing is setuid, no polkit rule or system file is added. The copy is
the same script the user's own AppImage contains; the user can already change
that file (and could run anything through pkexec with the password), so no new
trust is involved, and other users cannot read or change the private folder.
The boot-time unit cannot be installed from an AppImage, so **Refresh root data
at boot** stays greyed out there (its tooltip says why).

**Listing it in catalogs** (AppImageHub, AM) is done by hand once per project:
see `appimage/SUBMIT.md`.

## Troubleshooting

- **"tag v1.2.0 does not match SRS_VERSION"**: the tag and `srs.h` disagree.
  Delete the tag (`git tag -d v1.2.0; git push origin :refs/tags/v1.2.0`),
  fix `srs.h`, commit, and tag again.
- **"CHANGELOG.md has no '## 1.2.0 - DATE' section"**: add it (step 2) and re-tag as above.
- **A build fails**: open the failed job on the Actions page and read the log of
  the "Build" step, or run `packaging/build-all.sh <distro>` locally and read
  `dist/release-<version>/<distro>/build.log`. Distros update their images
  often, so a package name may have changed in `debian/control` or
  `rpm/srs-sysinfo.spec`.
- **An install test fails**: the log shows lines starting with `FAIL:`. Run it
  locally with `packaging/build-all.sh <distro>` and read `<distro>/test.log`.
- **"release v1.2.0 is already published"**: the workflow never replaces files
  of a published release. Delete the release on GitHub (or `gh release delete
  v1.2.0`, keeping the tag) and re-run the workflow from the Actions page.
  A *draft* left over from a half-finished run is completed automatically.
- **Re-running only the failed jobs** is fine: on the run's page use
  "Re-run failed jobs".
- **The AppImage build fails in "linuxdeploy" or "appimagetool"**: read
  `dist/release-<version>/appimage.log`. A failed sha256 check means a pinned
  download changed: check the release on GitHub before changing the sum.
- **The AppImage test fails**: `appimage-test.log` shows which check failed (`FAIL:`)
  and the GUI's messages. Run one image with a screenshot:
  `packaging/test-appimage.sh -s shots/ path/to.AppImage VER debian:12`.
- **Docker permission errors on your PC**: your user must be in the `docker`
  group, or the Docker service is not running (`systemctl start docker`).
