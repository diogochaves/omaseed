# Releasing omaseed

Human checklist for cutting a release. Run it from a clean `main` checkout with both dev
servers up (`~/.config/omaseed/dev.env` loaded). `scripts/release.sh` automates steps 5–7;
this file is the order to run them in and what to check by hand around them.

1. **Full test pass, both test servers up.**
   ```
   set -a; . ~/.config/omaseed/dev.env; set +a
   scripts/dev/test-server.sh start
   scripts/dev/test-server.sh start --strict
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DOMASEED_WERROR=ON
   cmake --build build
   ctest --test-dir build --output-on-failure
   scripts/dev/lint.sh build
   ```
2. **Theme sweep (T5.4).** Screenshot every theme via `--demo` and `omarchy-theme-set`
   against the spec figures. Note the starting theme first
   (`cat ~/.local/state/omarchy/current/theme.name`) and restore it afterwards — never
   leave the desktop changed.
3. **Bump the version, if the release differs from `CMakeLists.txt`'s `project(VERSION …)`.**
   Either let the release script do it (step 6, `--bump`: `CMakeLists.txt`, and `pkgver`
   with `pkgrel=1` and `sha256sums=('SKIP')` in both PKGBUILDs), or edit all three by hand
   and commit.
4. **CHANGELOG.md.** Add or check a `## vX.Y.Z` section that summarises what shipped in
   terms a user cares about (features), not the task IDs that built them. Fold anything
   sitting under `## Unreleased` into it.
5. **Dry run.**
   ```
   scripts/release.sh X.Y.Z --dry-run          # or: --bump --dry-run
   ```
   Checks the tree is clean and on `main`, that `vX.Y.Z` is not tagged yet, the version in
   `CMakeLists.txt` and `packaging/arch/PKGBUILD`, and the changelog section. Writes
   `dist/omaseed-X.Y.Z.tar.gz` (from `HEAD`), `dist/SHA256SUMS` and `dist/RELEASE_NOTES.md`
   (`dist/` is gitignored). It creates **no tag and no commit and edits no file**, so step 6
   can follow straight away. Inspect `dist/` before moving on; `tar tf
   dist/omaseed-X.Y.Z.tar.gz | head` should show the `omaseed-X.Y.Z/` prefix and no
   `docs/evidence` entries. With `--bump` the dry run only reports the bump, so its tarball
   still carries the old version.
6. **Publish.**
   ```
   scripts/release.sh X.Y.Z --publish          # or: --bump --publish
   ```
   In order: commits the bump (`Release vX.Y.Z`, with `--bump`), tags `vX.Y.Z`, rebuilds
   `dist/` from the tag, pushes `main` and the tag, runs
   `gh release create vX.Y.Z dist/omaseed-X.Y.Z.tar.gz dist/SHA256SUMS --notes-file dist/RELEASE_NOTES.md`,
   downloads GitHub's archive of the tag, and commits its sha256 into
   `packaging/arch/PKGBUILD` (`Set the vX.Y.Z checksum in PKGBUILD`) and pushes that.
7. **omarchy-pkgs PR.**
   ```
   scripts/release.sh X.Y.Z --omarchy-pr
   ```
   Prints — does not run — the fork/clone/copy/commit/`gh pr create` steps for
   `pkgbuilds/omaseed/` in `omacom/omarchy-pkgs` (charter §6). Copy the PKGBUILD from
   `main` after step 6, the one with the checksum. Builds there run only for
   collaborators, people in `.github/VOUCHED.td`, or a PR a maintainer labels
   `build-approved`; opening the PR is necessary but not sufficient for it to build.
8. **The bar widget (#17).** Nothing to do for a release: the widget is part of the install
   (ruled 2026-10-02). The package ships `plugin/` at `/usr/share/omaseed/plugin`, and the app
   links `~/.config/omarchy/plugins/chaves.omaseed` to it (first run, Settings › Appearance,
   `omaseed --install-bar-widget`), so every package upgrade upgrades the widget. Keep
   `"version"` in `plugin/manifest.json` equal to the app's.

   **Parked: a separate repository for the marketplace.** A public `diogochaves/omaseed-bar`
   that `omarchy plugin add` clones only matters for a marketplace listing (charter §9), and a
   widget installed that way without the app only says "omaseed is not running". If it is ever
   wanted, the steps below are ready.
   - **Once, with Diogo's OK:** create the public repository, empty (no README, licence or
     .gitignore: the split brings its own).
     ```
     gh repo create diogochaves/omaseed-bar --public \
       --description "omaseed in the Omarchy bar: torrent activity, alt-speed, adding and seeding stats"
     ```
   - **Each release:** set `"version"` in `plugin/manifest.json` (the widget's own version; it
     may differ from the app's) and commit, then
     ```
     scripts/dev/publish-plugin.sh X.Y.Z           # dry run: splits, writes no ref, prints the pushes
     scripts/dev/publish-plugin.sh --push X.Y.Z    # tags bar-vX.Y.Z, pushes the split as main and vX.Y.Z
     ```
     The local tag is `bar-vX.Y.Z` because this repository's `vX.Y.Z` tags are the app's.
   - **Check:** in a clean omabox, `omarchy plugin add https://github.com/diogochaves/omaseed-bar.git --enable`
     puts the widget in the bar; `omarchy plugin update chaves.omaseed` later pulls the next
     release.
   - **Marketplace (charter §9):** the app cannot be a shell plugin, but the companion widget
     can be listed in the Omarchy plugin marketplace. Submit `omaseed-bar` there once the first
     release is public, following the marketplace's own submission steps at the time.

## How the tarball and the checksum fit together

- `packaging/arch/PKGBUILD` downloads GitHub's own archive of the tag
  (`https://github.com/diogochaves/omaseed/archive/refs/tags/vX.Y.Z.tar.gz`). GitHub
  builds it as `git archive --prefix=omaseed-X.Y.Z/ vX.Y.Z | gzip -n`, leaving out what
  `.gitattributes` marks `export-ignore` (`docs/evidence`). `release.sh` builds the same
  bytes into `dist/`, uploads them as the release asset, and after the push checks them
  against GitHub's download; if they ever differ, the PKGBUILD gets GitHub's checksum,
  since that is the file makepkg fetches, and the script says so.
- The tagged tree's PKGBUILD says `sha256sums=('SKIP')`: a file cannot contain the checksum
  of an archive it is part of. The real checksum lands in the commit right after the tag.
- `packaging/omarchy/package.json` has no checksum field (omarchy-pkgs derives it from the
  PKGBUILD), so the script never writes to it.

## What the script checks for you, and what it does not

- It refuses to run on a dirty tree or off `main`, refuses to re-tag a version that
  already has a tag, and refuses a PKGBUILD whose `pkgver` does not match (without
  `--bump`) or that already carries a checksum.
- It does not run `ctest` or the theme sweep — those are step 1 and 2 above, by hand,
  because they need the dev servers and a live Hyprland session this script cannot see.
- It does not merge or rebase anything; `main` must already have everything the release
  needs.
