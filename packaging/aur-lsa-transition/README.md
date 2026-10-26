# Transitional AUR package: lsa-sysinfo 1.0.0 (prepared, NOT pushed)

Linux System Audit (LSA) is part of SRS from **srs-sysinfo 2.2.0**: its Cache & Memory benchmark
(same measuring engine) is on SRS's Benchmarks & stress page and in `sysreport-search cachemem`.

This folder holds the AUR files for the last `lsa-sysinfo` release:

| File | Purpose |
|---|---|
| `PKGBUILD`, `.SRCINFO` | `lsa-sysinfo` 1.0.0, `arch=any`, `depends=('srs-sysinfo>=2.2.0')` |
| `lsa` | wrapper: `lsa cachemem ...` → `sysreport-search cachemem ...` (same options), `lsa guide` → SRS guide |
| `lsa-gui` | wrapper: opens SRS's Cache & Memory window (`srs-gui --gui-open=cachemem`) |

What an update does on a user's PC: `lsa-sysinfo` 0.2.0 → 1.0.0 removes the old LSA program, its
two menu entries and its guide, pulls in `srs-sysinfo`, and keeps `lsa` / `lsa-gui` working.
No desktop file is shipped (SRS has its own menu entries and a "Cache & Memory benchmark" jump-list
action), and no file overlaps with `srs-sysinfo`, so neither package needs `conflicts` or
`replaces`.

## When to publish (in this order, all by hand, after SRS 2.2.0 is on the AUR)

1. Push SRS 2.2.0 (with the Cache & Memory benchmark) to the AUR.
2. Copy `PKGBUILD`, `.SRCINFO`, `lsa`, `lsa-gui` into the `lsa-sysinfo` AUR clone, commit, push.
3. LSA GitHub repo: add a README pointer to SRS and a final release note "LSA is now part of
   SRS"; keep the repo and all releases (including the Windows build) downloadable.
4. After a few months: file an AUR **merge** request `lsa-sysinfo` → `srs-sysinfo` (keeps votes
   and comments), if wanted.

Test done on the branch: `lsa cachemem --only l1 --tests latency --seconds 0.2 --yes` with the
branch's `sysreport-search` in `PATH` gives the L1 latency (0.8 ns); `lsa` without arguments prints
the mapping and exits 2. `lsa guide` hands over to `sysreport-search guide cache-memory`, which opens
the guide in the PDF viewer / browser.
