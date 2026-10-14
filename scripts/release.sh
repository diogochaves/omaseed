#!/usr/bin/env bash
set -euo pipefail

# scripts/release.sh <version> [options]
#
# Cuts an omaseed release. packaging/arch/PKGBUILD downloads GitHub's own archive of the tag
# (https://github.com/diogochaves/omaseed/archive/refs/tags/vX.Y.Z.tar.gz). GitHub makes that
# archive with `git archive --prefix=omaseed-X.Y.Z/ vX.Y.Z | gzip -n`, honouring the tree's
# .gitattributes export-ignore, so this script builds the very same bytes locally and its
# sha256 is the PKGBUILD's checksum. --publish also downloads GitHub's archive after the push
# and uses its checksum, so a difference can never reach the PKGBUILD.
#
# Each release also carries the binary package, so people install with one line:
# `curl -fLO https://github.com/diogochaves/omaseed/releases/latest/download/omaseed-x86_64.pkg.tar.zst
# && sudo pacman -U omaseed-x86_64.pkg.tar.zst`. (pacman -U on the URL itself wants a .sig
# beside it under Arch's default SigLevel; a file on disk does not need one.)
# makepkg builds it in a fresh directory under dist/ from the tag's PKGBUILD and the tag's
# archive (never the working tree), and the release gets it twice: as
# omaseed-<ver>-<rel>-x86_64.pkg.tar.zst and as the versionless omaseed-x86_64.pkg.tar.zst,
# the name that keeps releases/latest/download/ stable. The -debug package stays out.
# --add-package does just that for a release that is already out.
#
# The checksum cannot live in the tagged tree (it would be part of what it checksums), so a
# publish is: tag the release commit (PKGBUILD at sha256sums=('SKIP')), push, then commit
# "Set the vX.Y.Z checksum in PKGBUILD" on main after the tag. That commit's PKGBUILD is the
# one that goes to omarchy-pkgs.
#
# --dry-run (the default) has no side effects on git: no tag, no commit, no file edited.
# Nothing touches the network unless --publish is given. See docs/RELEASING.md.
#
# Test hooks (tests/release/test_release.sh): OMASEED_RELEASE_REMOTE (default origin),
# OMASEED_RELEASE_ARCHIVE_URL (default GitHub's tag archive URL; curl, so file:// works) and
# OMASEED_RELEASE_MAKEPKG (default makepkg).

usage() {
    cat <<'EOF'
Usage: scripts/release.sh <version> [options]

  <version>       Release version, e.g. 0.1.0 (no leading "v").

Options:
  --bump          If CMakeLists.txt's project() VERSION differs from <version>, update it and
                   both PKGBUILDs' pkgver (pkgrel=1, sha256sums=('SKIP')). With --publish
                   this is committed as "Release v<version>" before tagging; with --dry-run
                   it is only reported.
  --dry-run       Check everything and build dist/ (tarball, SHA256SUMS, release notes) from
                   HEAD, exactly as the tag would be, but create no tag, make no commit, edit
                   no file and push nothing. The default.
  --publish       Tag, build dist/ (source tarball and binary package) from the tag, push
                   main and the tag, create the GitHub release, then write GitHub's archive
                   checksum into packaging/arch/PKGBUILD and commit and push that.
  --add-package   For a release already out: build the binary package from the tag and
                   upload it (versioned and versionless) with a SHA256SUMS that lists it.
                   Touches neither the tag nor the source archive; refuses if the release
                   already has a package. With --dry-run it builds and checks, and uploads
                   nothing.
  --omarchy-pr    Print (do not run) the steps to open the PR against omacom/omarchy-pkgs.
                   Exits immediately after printing; skips every other step.
  -h, --help      Show this help.
EOF
}

log() {
    printf '==> %s\n' "$*"
}

fail() {
    printf 'error: %s\n' "$*" >&2
    if [[ -n "${UNPUSHED_TAG:-}" ]]; then
        printf 'error: %s is tagged here but nothing was pushed; drop it with: git tag -d %s\n' \
            "$UNPUSHED_TAG" "$UNPUSHED_TAG" >&2
    fi
    exit 1
}

VERSION=""
DO_BUMP=0
DO_DRYRUN=0
DO_PUBLISH=0
DO_OMARCHY_PR=0
DO_ADD_PACKAGE=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --bump) DO_BUMP=1; shift ;;
        --dry-run) DO_DRYRUN=1; shift ;;
        --publish) DO_PUBLISH=1; shift ;;
        --omarchy-pr) DO_OMARCHY_PR=1; shift ;;
        --add-package) DO_ADD_PACKAGE=1; shift ;;
        -h|--help) usage; exit 0 ;;
        --) shift; break ;;
        -*) fail "unknown option: $1" ;;
        *)
            if [[ -n "$VERSION" ]]; then
                fail "unexpected argument: $1"
            fi
            VERSION="$1"
            shift
            ;;
    esac
done

[[ -n "$VERSION" ]] || { usage >&2; fail "missing <version>"; }
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || fail "version must be X.Y.Z, got: $VERSION"
[[ $DO_DRYRUN -eq 1 && $DO_PUBLISH -eq 1 ]] && fail "--dry-run and --publish are mutually exclusive"
[[ $DO_ADD_PACKAGE -eq 1 && $((DO_PUBLISH + DO_BUMP)) -gt 0 ]] &&
    fail "--add-package takes only <version> and, to build without uploading, --dry-run"
[[ $DO_PUBLISH -eq 0 && $DO_ADD_PACKAGE -eq 0 ]] && DO_DRYRUN=1

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

GITHUB_REPO="diogochaves/omaseed"
OMARCHY_PKGS_REPO="omacom/omarchy-pkgs"
TAG="v${VERSION}"
REMOTE="${OMASEED_RELEASE_REMOTE:-origin}"
ARCHIVE_URL="${OMASEED_RELEASE_ARCHIVE_URL:-https://github.com/${GITHUB_REPO}/archive/refs/tags/${TAG}.tar.gz}"
DIST_DIR="$ROOT/dist"
TARBALL_NAME="omaseed-${VERSION}.tar.gz"
TARBALL_PATH="$DIST_DIR/$TARBALL_NAME"
SUMS_PATH="$DIST_DIR/SHA256SUMS"
NOTES_PATH="$DIST_DIR/RELEASE_NOTES.md"
ARCHIVE_PREFIX="omaseed-${VERSION}/"
PKGBUILD_PATH="packaging/arch/PKGBUILD"
PKGBUILD_LOCAL_PATH="packaging/arch/PKGBUILD.local"
MAKEPKG="${OMASEED_RELEASE_MAKEPKG:-makepkg}"
PKG_BUILD_DIR="$DIST_DIR/pkgbuild"
PKG_LATEST_NAME="omaseed-x86_64.pkg.tar.zst"
PKG_NAME="" # omaseed-<ver>-<rel>-x86_64.pkg.tar.zst, set by build_package

print_omarchy_pr_steps() {
    cat <<EOF
omarchy-pkgs PR steps for ${TAG} (charter §6). Print-only in v1: nothing below is run.
Run them after --publish, so packaging/arch/PKGBUILD on main carries the release checksum.

Builds there run only for collaborators, people in .github/VOUCHED.td, or a PR a
maintainer labels "build-approved" -- opening the PR is necessary but not sufficient.

1. gh repo fork ${OMARCHY_PKGS_REPO} --clone=false            # once, if not already forked
2. git clone git@github.com:<you>/omarchy-pkgs.git /tmp/omarchy-pkgs-omaseed
3. cd /tmp/omarchy-pkgs-omaseed && git checkout -b omaseed-${TAG}
4. mkdir -p pkgbuilds/omaseed/.omarchy
5. cp ${ROOT}/packaging/arch/PKGBUILD pkgbuilds/omaseed/PKGBUILD
6. cp ${ROOT}/packaging/omarchy/package.json pkgbuilds/omaseed/.omarchy/package.json
7. git add pkgbuilds/omaseed
8. git commit -m "Add omaseed ${TAG}"
9. git push -u origin omaseed-${TAG}
10. gh pr create --repo ${OMARCHY_PKGS_REPO} --title "Add omaseed ${TAG}" \\
      --body "Adds omaseed, a native Qt torrent client for Omarchy. Release: https://github.com/${GITHUB_REPO}/releases/tag/${TAG}"
EOF
}

if [[ $DO_OMARCHY_PR -eq 1 ]]; then
    print_omarchy_pr_steps
    exit 0
fi

# pkgbuild_field FILE NAME: the value of a top-level NAME= line.
pkgbuild_field() {
    sed -n -E "s/^$2=//p" "$1" | head -n1
}

# bump_files: the version edits --bump makes (CMakeLists.txt and both PKGBUILDs).
bump_files() {
    sed -i -E "0,/^([[:space:]]*VERSION[[:space:]]+)[0-9]+\.[0-9]+\.[0-9]+/s//\1${VERSION}/" CMakeLists.txt
    local f
    for f in "$PKGBUILD_PATH" "$PKGBUILD_LOCAL_PATH"; do
        [[ -f "$f" ]] || continue
        sed -i -E "s/^pkgver=.*/pkgver=${VERSION}/; s/^pkgrel=.*/pkgrel=1/" "$f"
    done
    sed -i -E "s/^sha256sums=\([^)]*\)/sha256sums=('SKIP')/" "$PKGBUILD_PATH"
}

# unwrap_markdown: join each paragraph's and list item's wrapped lines into one. CHANGELOG.md
# is wrapped for the terminal, but a GitHub release body turns every newline into a line break.
# Headings, list item starts, blank lines and code blocks stay as they are.
unwrap_markdown() {
    awk '
        function flush() { if (buf != "") print buf; buf = "" }
        /^```/ { flush(); print; code = !code; next }
        code { print; next }
        /^[[:space:]]*$/ || /^#/ { flush(); print; next }
        /^[[:space:]]*([-*+] |[0-9]+\. |> )/ { flush(); buf = $0; next }
        { line = $0; sub(/^[[:space:]]+/, "", line); buf = (buf == "" ? line : buf " " line) }
        END { flush() }
    '
}

# build_tarball REV: GitHub's archive of REV, byte for byte (see the header).
build_tarball() {
    git archive --format=tar --prefix="$ARCHIVE_PREFIX" "$1" | gzip -n > "$TARBALL_PATH"
}

# build_package REV: the binary package, built by makepkg in a fresh directory from REV's
# PKGBUILD and dist/'s tarball of REV (the bytes makepkg would download for the tag). The
# PKGBUILD gets that tarball's checksum, so makepkg verifies what it builds. The build is
# dated at REV's commit (SOURCE_DATE_EPOCH). Copies the package into dist/ under its own
# name and the versionless one; the -debug package is left in the build directory.
build_package() {
    local rev="$1" pkgrel sha
    rm -rf "$PKG_BUILD_DIR"
    rm -f "$DIST_DIR"/*.pkg.tar.zst
    mkdir -p "$PKG_BUILD_DIR"/{src,pkg,out,logs}
    git show "${rev}:${PKGBUILD_PATH}" >"$PKG_BUILD_DIR/PKGBUILD"
    [[ "$(pkgbuild_field "$PKG_BUILD_DIR/PKGBUILD" pkgver)" == "$VERSION" ]] ||
        fail "the PKGBUILD at ${rev} is not at pkgver=${VERSION}"
    pkgrel="$(pkgbuild_field "$PKG_BUILD_DIR/PKGBUILD" pkgrel)"
    sha="$(sha256sum "$TARBALL_PATH" | cut -d' ' -f1)"
    sed -i -E "s/^sha256sums=\([^)]*\)/sha256sums=('${sha}')/" "$PKG_BUILD_DIR/PKGBUILD"
    # The name the PKGBUILD's source=() saves the download as, so makepkg uses this file.
    cp "$TARBALL_PATH" "$PKG_BUILD_DIR/src/omaseed-${VERSION}.tar.gz"
    (
        cd "$PKG_BUILD_DIR"
        SRCDEST="$PKG_BUILD_DIR/src" BUILDDIR="$PKG_BUILD_DIR/pkg" PKGDEST="$PKG_BUILD_DIR/out" \
            LOGDEST="$PKG_BUILD_DIR/logs" \
            PACKAGER="$(git -C "$ROOT" config user.name) <$(git -C "$ROOT" config user.email)>" \
            SOURCE_DATE_EPOCH="$(git -C "$ROOT" log -1 --format=%ct "$rev")" \
            "$MAKEPKG" --cleanbuild --noconfirm --noprogressbar
    ) || fail "makepkg failed (build directory: $PKG_BUILD_DIR)"
    PKG_NAME="omaseed-${VERSION}-${pkgrel}-x86_64.pkg.tar.zst"
    [[ -f "$PKG_BUILD_DIR/out/$PKG_NAME" ]] || fail "makepkg made no $PKG_NAME"
    cp "$PKG_BUILD_DIR/out/$PKG_NAME" "$DIST_DIR/$PKG_NAME"
    cp "$PKG_BUILD_DIR/out/$PKG_NAME" "$DIST_DIR/$PKG_LATEST_NAME"
}

# write_sums: SHA256SUMS for the tarball and, once built, both package names.
write_sums() {
    local files=("$TARBALL_NAME")
    [[ -n "$PKG_NAME" ]] && files+=("$PKG_NAME" "$PKG_LATEST_NAME")
    (cd "$DIST_DIR" && sha256sum "${files[@]}" >SHA256SUMS)
}

if [[ $DO_ADD_PACKAGE -eq 1 ]]; then
    git rev-parse -q --verify "refs/tags/${TAG}" >/dev/null || fail "no tag ${TAG} here (git fetch --tags?)"
    mkdir -p "$DIST_DIR"
    log "Checking the ${TAG} release"
    ASSETS="$(gh release view "$TAG" --repo "$GITHUB_REPO" --json assets --jq '.assets[].name')" ||
        fail "no GitHub release ${TAG}"
    grep -qxF "$TARBALL_NAME" <<<"$ASSETS" || fail "the ${TAG} release has no ${TARBALL_NAME}"
    if grep -q '\.pkg\.tar\.zst$' <<<"$ASSETS"; then
        fail "the ${TAG} release already has a package: $(grep '\.pkg\.tar\.zst$' <<<"$ASSETS" | tr '\n' ' ')"
    fi
    log "Building the source tarball from ${TAG} and checking it is the release's"
    build_tarball "$TAG"
    TARBALL_SHA="$(sha256sum "$TARBALL_PATH" | cut -d' ' -f1)"
    RELEASE_SUMS="$(gh release download "$TAG" --repo "$GITHUB_REPO" --pattern SHA256SUMS --output -)" ||
        fail "could not download the ${TAG} release's SHA256SUMS"
    grep -qxF "${TARBALL_SHA}  ${TARBALL_NAME}" <<<"$RELEASE_SUMS" ||
        fail "dist/${TARBALL_NAME} (${TARBALL_SHA}) is not the release's: ${RELEASE_SUMS}"
    log "Building the package from ${TAG}'s PKGBUILD"
    build_package "$TAG"
    write_sums
    log "SHA256SUMS:"
    sed 's/^/  /' "$SUMS_PATH"
    if [[ $DO_DRYRUN -eq 1 ]]; then
        log "Dry run: would upload dist/${PKG_NAME}, dist/${PKG_LATEST_NAME} and dist/SHA256SUMS to ${TAG}"
        exit 0
    fi
    log "Uploading ${PKG_NAME}, ${PKG_LATEST_NAME} and SHA256SUMS to ${TAG}"
    gh release upload "$TAG" --repo "$GITHUB_REPO" "dist/${PKG_NAME}" "dist/${PKG_LATEST_NAME}"
    gh release upload "$TAG" --repo "$GITHUB_REPO" --clobber dist/SHA256SUMS
    log "Done: https://github.com/${GITHUB_REPO}/releases/download/${TAG}/${PKG_NAME}"
    exit 0
fi

log "Checking the working tree is clean"
[[ -z "$(git status --porcelain)" ]] || { git status --short >&2; fail "working tree is not clean"; }

log "Checking the current branch"
BRANCH="$(git rev-parse --abbrev-ref HEAD)"
[[ "$BRANCH" == "main" ]] || fail "must be on main, currently on $BRANCH"

log "Checking the tag is new"
if git rev-parse -q --verify "refs/tags/${TAG}" >/dev/null; then
    fail "tag ${TAG} already exists"
fi

log "Checking the project version"
CMAKE_VERSION_LINE="$(grep -m1 -E '^[[:space:]]*VERSION[[:space:]]+[0-9]+\.[0-9]+\.[0-9]+' CMakeLists.txt || true)"
CMAKE_VERSION="$(printf '%s\n' "$CMAKE_VERSION_LINE" | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' || true)"
[[ -n "$CMAKE_VERSION" ]] || fail "could not find project VERSION in CMakeLists.txt"
[[ -f "$PKGBUILD_PATH" ]] || fail "$PKGBUILD_PATH is missing"
PKGBUILD_VERSION="$(pkgbuild_field "$PKGBUILD_PATH" pkgver)"

NEEDS_BUMP=0
if [[ "$CMAKE_VERSION" != "$VERSION" || "$PKGBUILD_VERSION" != "$VERSION" ]]; then
    [[ $DO_BUMP -eq 1 ]] ||
        fail "CMakeLists.txt is at ${CMAKE_VERSION} and PKGBUILD at ${PKGBUILD_VERSION}, expected ${VERSION} (pass --bump to update both)"
    NEEDS_BUMP=1
elif [[ "$(pkgbuild_field "$PKGBUILD_PATH" sha256sums)" != "('SKIP')" ]]; then
    fail "$PKGBUILD_PATH already carries a checksum; the tagged PKGBUILD must say sha256sums=('SKIP') (pass --bump, or reset it by hand)"
fi

log "Checking CHANGELOG.md"
CHANGELOG="$ROOT/CHANGELOG.md"
[[ -f "$CHANGELOG" ]] || fail "CHANGELOG.md is missing"
grep -q "^## v${VERSION}\([[:space:]]\|\$\)" "$CHANGELOG" || fail "CHANGELOG.md has no '## v${VERSION}' section"

extract_changelog_section() {
    awk -v ver="v${VERSION}" '
        $0 ~ "^## " ver "([[:space:]]|$)" { found=1; print; next }
        found && /^## / { exit }
        found { print }
    ' "$CHANGELOG"
}
CHANGELOG_SECTION="$(extract_changelog_section)"
[[ -n "$CHANGELOG_SECTION" ]] || fail "could not extract the '## v${VERSION}' section from CHANGELOG.md"

mkdir -p "$DIST_DIR"
TAG_MSG_FILE="$(mktemp)"
trap 'rm -f "$TAG_MSG_FILE"' EXIT

if [[ $DO_PUBLISH -eq 1 ]]; then
    if [[ $NEEDS_BUMP -eq 1 ]]; then
        log "Bumping CMakeLists.txt ${CMAKE_VERSION} and PKGBUILD ${PKGBUILD_VERSION} -> ${VERSION}"
        bump_files
        git add CMakeLists.txt "$PKGBUILD_PATH" "$PKGBUILD_LOCAL_PATH"
        git commit -q -m "Release v${VERSION}" -m "Bump the project and package version for the v${VERSION} release."
    fi
    log "Tagging ${TAG}"
    printf '%s\n' "$CHANGELOG_SECTION" > "$TAG_MSG_FILE"
    git tag -a "$TAG" -F "$TAG_MSG_FILE"
    UNPUSHED_TAG="$TAG"
    ARCHIVE_REV="$TAG"
else
    if [[ $NEEDS_BUMP -eq 1 ]]; then
        log "Dry run: would bump CMakeLists.txt ${CMAKE_VERSION} and PKGBUILD ${PKGBUILD_VERSION} -> ${VERSION} and commit it"
        log "Dry run: the tarball below is HEAD without that bump, so its checksum is not the release's"
    fi
    log "Dry run: would tag ${TAG} at $(git rev-parse --short HEAD)"
    ARCHIVE_REV="HEAD"
fi

log "Building the source tarball (git archive ${ARCHIVE_REV} | gzip -n, as GitHub does)"
build_tarball "$ARCHIVE_REV"

TARBALL_SHA="$(sha256sum "$TARBALL_PATH" | cut -d' ' -f1)"

if [[ $NEEDS_BUMP -eq 1 && $DO_PUBLISH -eq 0 ]]; then
    log "Dry run: no package build, since HEAD's PKGBUILD is not at ${VERSION} yet"
    rm -f "$DIST_DIR"/*.pkg.tar.zst
else
    log "Building the package (makepkg, ${ARCHIVE_REV}'s PKGBUILD and tarball, a fresh build directory)"
    build_package "$ARCHIVE_REV"
fi

log "Writing SHA256SUMS"
write_sums

log "Writing release notes"
{
    printf '# omaseed %s\n\n' "$TAG"
    printf '%s\n\n' "$CHANGELOG_SECTION" | unwrap_markdown
    printf '## Install\n\n'
    if [[ -n "$PKG_NAME" ]]; then
        printf 'Arch / Omarchy, the prebuilt package (pacman pulls Qt and the rest from the Arch repos):\n\n'
        printf '```\n'
        printf 'curl -fLO https://github.com/%s/releases/download/%s/%s && sudo pacman -U %s\n' \
            "$GITHUB_REPO" "$TAG" "$PKG_NAME" "$PKG_NAME"
        printf '```\n\n'
        printf 'Or build it from the source tarball:\n\n'
    else
        # shellcheck disable=SC2016 # backticks are Markdown, not command substitution
        printf 'Arch / Omarchy, from the source tarball (before the `[omarchy]` repo package lands):\n\n'
    fi
    printf '```\n'
    printf 'tar xf %s\n' "$TARBALL_NAME"
    printf 'cd omaseed-%s\n' "$VERSION"
    printf 'makepkg -si -p packaging/arch/PKGBUILD\n'
    printf '```\n\n'
    # shellcheck disable=SC2016 # backticks are Markdown, not command substitution
    printf 'Once accepted into the omarchy-pkgs `[omarchy]` repo:\n\n'
    printf '```\n'
    printf 'pacman -S omaseed\n'
    printf '```\n\n'
    printf '## SHA256SUMS\n\n'
    printf '```\n'
    cat "$SUMS_PATH"
    printf '```\n'
} > "$NOTES_PATH"

RELEASE_FILES=("dist/${TARBALL_NAME}")
[[ -n "$PKG_NAME" ]] && RELEASE_FILES+=("dist/${PKG_NAME}" "dist/${PKG_LATEST_NAME}")
RELEASE_FILES+=(dist/SHA256SUMS)

log "Release artefacts in dist/:"
printf '  %s\n' "${RELEASE_FILES[@]}" dist/RELEASE_NOTES.md

RELEASE_ARGS=(release create "$TAG" "${RELEASE_FILES[@]}" --notes-file dist/RELEASE_NOTES.md)
log "GitHub release command:"
printf '  gh %s\n' "${RELEASE_ARGS[*]}"

if [[ $DO_PUBLISH -eq 0 ]]; then
    log "Dry run: the PKGBUILD checksum would be ${TARBALL_SHA} (confirmed against GitHub's archive on --publish)"
    log "Dry run: no tag, no commit, nothing pushed, no GitHub release. Re-run with --publish to do that."
    exit 0
fi

log "Publishing: pushing main and ${TAG} to ${REMOTE}"
git push "$REMOTE" main "refs/tags/${TAG}"
UNPUSHED_TAG=""

log "Publishing: creating the GitHub release"
gh "${RELEASE_ARGS[@]}"

log "Fetching GitHub's archive of ${TAG} (what makepkg downloads)"
ARCHIVE_SHA=""
FETCHED="$(mktemp)"
trap 'rm -f "$TAG_MSG_FILE" "$FETCHED"' EXIT
for attempt in 1 2 3 4 5; do
    if curl -fsSL --max-time 60 -o "$FETCHED" "$ARCHIVE_URL"; then
        ARCHIVE_SHA="$(sha256sum "$FETCHED" | cut -d' ' -f1)"
        break
    fi
    log "  not yet (attempt ${attempt}); retrying"
    sleep $((attempt * 2))
done
PKGBUILD_SHA="$TARBALL_SHA"
if [[ -z "$ARCHIVE_SHA" ]]; then
    log "WARNING: could not download ${ARCHIVE_URL}; writing the local tarball's checksum."
    log "WARNING: check it before the omarchy-pkgs PR: curl -sL ${ARCHIVE_URL} | sha256sum"
elif [[ "$ARCHIVE_SHA" != "$TARBALL_SHA" ]]; then
    log "WARNING: GitHub's archive differs from dist/${TARBALL_NAME} (${ARCHIVE_SHA} vs ${TARBALL_SHA})."
    log "WARNING: the PKGBUILD gets GitHub's, since that is the file makepkg downloads."
    PKGBUILD_SHA="$ARCHIVE_SHA"
else
    log "GitHub's archive matches dist/${TARBALL_NAME}"
fi

log "Writing the checksum into ${PKGBUILD_PATH} and committing it after the tag"
sed -i -E "s/^sha256sums=\([^)]*\)/sha256sums=('${PKGBUILD_SHA}')/" "$PKGBUILD_PATH"
git add "$PKGBUILD_PATH"
git commit -q -m "Set the ${TAG} checksum in PKGBUILD" \
    -m "The checksum of GitHub's ${TAG} archive, which the PKGBUILD downloads. It cannot be part of the tagged tree it checksums, so it lands on main right after the tag."
git push "$REMOTE" main

log "Released ${TAG}. Next: scripts/release.sh ${VERSION} --omarchy-pr"
