# Packaging and releases

This folder builds the release files for SRS: packages for Arch, Debian 13,
Ubuntu 24.04, Fedora 44 and openSUSE Tumbleweed, the generic Linux tarball,
and the checksums. GitHub builds them for you when you push a version tag
(see `.github/workflows/release.yml`). You can run the same build on your own
PC with `build-all.sh`.

Every package is built in a fresh Docker container of its distro, then
installed with that distro's own package manager in a second fresh container
and checked by `test.sh` (programs start, version is right, user guide is
installed, menu entries are valid).

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
   `-linux-x86_64.tar.gz`, `SRS-User-Guide.pdf` and `SHA256SUMS`.
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
- `release/` all release files plus `SHA256SUMS`
- `release-notes.md` the notes the release would use

The single steps can also be run on their own, exactly as the workflow does:

```sh
packaging/build.sh debian-13 srs-sysinfo-1.2.0.tar.gz out/debian-13
packaging/test-in-container.sh debian-13 out/debian-13/pkg 1.2.0
packaging/make-tarball.sh out/arch/pkg/srs-sysinfo-1.2.0-1-x86_64.pkg.tar.zst srs-sysinfo-1.2.0.tar.gz out/tarball
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
| `release-info.sh` | Reads the version, checks the tag, prints the release notes |
| `lib-version.sh` | Writes the version into the Debian changelog, spec and man pages |
| `build-all.sh` | Runs all of the above on your PC |
| `arch/PKGBUILD`, `arch/makepkg.conf` | Arch build (a copy of the AUR PKGBUILD that builds from the local source) with generic x86-64 flags |
| `debian/`, `rpm/`, `man/` | Debian/Ubuntu and Fedora/openSUSE packaging, man pages |
| `release-notes/` | Release notes per tag |

The generic tarball is the Arch package unpacked, so it needs a glibc at least
as new as Arch's (`INSTALL.txt` in the tarball states the exact version). For
Debian, Ubuntu, Fedora and openSUSE the distro packages are the ones to use.

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
- **Docker permission errors on your PC**: your user must be in the `docker`
  group, or the Docker service is not running (`systemctl start docker`).
