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
   Either edit it by hand and commit, or let the release script do it (step 5, `--bump`).
4. **CHANGELOG.md.** Add or check a `## vX.Y.Z` section that summarises what shipped in
   terms a user cares about (features), not the task IDs that built them. Fold anything
   sitting under `## Unreleased` into it.
5. **Dry run.**
   ```
   scripts/release.sh X.Y.Z --dry-run          # or: --bump --dry-run
   ```
   Checks the tree is clean and on `main`, the version, and the changelog section; tags
   `vX.Y.Z` locally; writes `dist/omaseed-X.Y.Z.tar.gz`, `dist/SHA256SUMS`,
   `dist/RELEASE_NOTES.md`; rewrites the checksum into `packaging/arch/PKGBUILD` and
   `packaging/omarchy/package.json` if those files exist yet. Nothing is pushed and no
   GitHub release is created. Inspect `dist/` before moving on; `tar tf
   dist/omaseed-X.Y.Z.tar.gz | head` should show the `omaseed-X.Y.Z/` prefix and no
   `docs/evidence` or `build*` entries.
6. **Publish.**
   ```
   scripts/release.sh X.Y.Z --publish
   ```
   Pushes `main` and the tag (`git push --follow-tags`) and runs
   `gh release create vX.Y.Z dist/omaseed-X.Y.Z.tar.gz dist/SHA256SUMS --notes-file dist/RELEASE_NOTES.md`.
7. **omarchy-pkgs PR.**
   ```
   scripts/release.sh X.Y.Z --omarchy-pr
   ```
   Prints — does not run — the fork/clone/copy/commit/`gh pr create` steps for
   `pkgbuilds/omaseed/` in `omacom/omarchy-pkgs` (charter §6). Builds there run only for
   collaborators, people in `.github/VOUCHED.td`, or a PR a maintainer labels
   `build-approved`; opening the PR is necessary but not sufficient for it to build.

## What the script checks for you, and what it does not

- It refuses to run on a dirty tree or off `main`, and refuses to re-tag a version that
  already has a tag.
- It does not run `ctest` or the theme sweep — those are step 1 and 2 above, by hand,
  because they need the dev servers and a live Hyprland session this script cannot see.
- It does not merge or rebase anything; `main` must already have everything the release
  needs.
