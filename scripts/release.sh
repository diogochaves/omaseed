#!/usr/bin/env bash
set -euo pipefail

# scripts/release.sh <version> [options]
#
# Cuts an omaseed release: checks the tree is clean and on main, checks the version
# against CMakeLists.txt (or bumps it), tags, builds a source tarball + SHA256SUMS,
# rewrites the checksum into packaging/ if it exists yet, and writes release notes.
# Nothing touches the network unless --publish is given. See docs/RELEASING.md for the
# full human checklist this script is one step of.

usage() {
    cat <<'EOF'
Usage: scripts/release.sh <version> [options]

  <version>       Release version, e.g. 0.1.0 (no leading "v").

Options:
  --bump          If CMakeLists.txt's project() VERSION differs from <version>, update it
                   and commit "Release v<version>" before tagging.
  --dry-run       Tag, build the tarball/SHA256SUMS/release notes, but do not push or
                   create a GitHub release. This is the default when neither --dry-run
                   nor --publish is given.
  --publish       Push main and the tag (git push --follow-tags) and run
                   `gh release create` with the tarball, SHA256SUMS and release notes.
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
    exit 1
}

VERSION=""
DO_BUMP=0
DO_DRYRUN=0
DO_PUBLISH=0
DO_OMARCHY_PR=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --bump) DO_BUMP=1; shift ;;
        --dry-run) DO_DRYRUN=1; shift ;;
        --publish) DO_PUBLISH=1; shift ;;
        --omarchy-pr) DO_OMARCHY_PR=1; shift ;;
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
[[ $DO_PUBLISH -eq 0 ]] && DO_DRYRUN=1

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

TAG="v${VERSION}"
DIST_DIR="$ROOT/dist"
TARBALL_NAME="omaseed-${VERSION}.tar.gz"
TARBALL_PATH="$DIST_DIR/$TARBALL_NAME"
SUMS_PATH="$DIST_DIR/SHA256SUMS"
NOTES_PATH="$DIST_DIR/RELEASE_NOTES.md"
ARCHIVE_PREFIX="omaseed-${VERSION}/"

print_omarchy_pr_steps() {
    cat <<EOF
omarchy-pkgs PR steps for ${TAG} (charter §6). Print-only in v1: nothing below is run.

Builds there run only for collaborators, people in .github/VOUCHED.td, or a PR a
maintainer labels "build-approved" -- opening the PR is necessary but not sufficient.

1. gh repo fork omacom/omarchy-pkgs --clone=false            # once, if not already forked
2. git clone git@github.com:<you>/omarchy-pkgs.git /tmp/omarchy-pkgs-omaseed
3. cd /tmp/omarchy-pkgs-omaseed && git checkout -b omaseed-${TAG}
4. mkdir -p pkgbuilds/omaseed/.omarchy
5. cp ${ROOT}/packaging/arch/PKGBUILD pkgbuilds/omaseed/PKGBUILD
6. cp ${ROOT}/packaging/omarchy/package.json pkgbuilds/omaseed/.omarchy/package.json
7. git add pkgbuilds/omaseed
8. git commit -m "Add omaseed ${TAG}"
9. git push -u origin omaseed-${TAG}
10. gh pr create --repo omacom/omarchy-pkgs --title "Add omaseed ${TAG}" \\
      --body "Adds omaseed, a native Qt torrent client for Omarchy. Release: https://github.com/diogochaves/omaseed/releases/tag/${TAG}"
EOF
}

if [[ $DO_OMARCHY_PR -eq 1 ]]; then
    print_omarchy_pr_steps
    exit 0
fi

log "Checking the working tree is clean"
[[ -z "$(git status --porcelain)" ]] || { git status --short >&2; fail "working tree is not clean"; }

log "Checking the current branch"
BRANCH="$(git rev-parse --abbrev-ref HEAD)"
[[ "$BRANCH" == "main" ]] || fail "must be on main, currently on $BRANCH"

log "Checking the project version"
CMAKE_VERSION_LINE="$(grep -m1 -E '^[[:space:]]*VERSION[[:space:]]+[0-9]+\.[0-9]+\.[0-9]+' CMakeLists.txt || true)"
CMAKE_VERSION="$(printf '%s\n' "$CMAKE_VERSION_LINE" | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' || true)"
[[ -n "$CMAKE_VERSION" ]] || fail "could not find project VERSION in CMakeLists.txt"

if [[ "$CMAKE_VERSION" != "$VERSION" ]]; then
    if [[ $DO_BUMP -eq 1 ]]; then
        log "Bumping CMakeLists.txt project version $CMAKE_VERSION -> $VERSION"
        sed -i -E "0,/^([[:space:]]*VERSION[[:space:]]+)[0-9]+\.[0-9]+\.[0-9]+/s//\1${VERSION}/" CMakeLists.txt
        git add CMakeLists.txt
        git commit -m "Release v${VERSION}" -m "Bump the project version for the v${VERSION} release."
    else
        fail "CMakeLists.txt is at ${CMAKE_VERSION}, expected ${VERSION} (pass --bump to update it)"
    fi
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

log "Tagging ${TAG}"
if git rev-parse -q --verify "refs/tags/${TAG}" >/dev/null; then
    fail "tag ${TAG} already exists"
fi
TAG_MSG_FILE="$(mktemp)"
trap 'rm -f "$TAG_MSG_FILE"' EXIT
printf '%s\n' "$CHANGELOG_SECTION" > "$TAG_MSG_FILE"
git tag -a "$TAG" -F "$TAG_MSG_FILE"

log "Building the source tarball"
git archive --format=tar --prefix="$ARCHIVE_PREFIX" "$TAG" -- \
    '.' \
    ':(exclude,glob)docs/evidence' \
    ':(exclude,glob)docs/evidence/**' \
    ':(exclude,glob)build*' \
    ':(exclude,glob)build*/**' \
    ':(exclude,glob).github' \
    ':(exclude,glob).github/**' \
    | gzip -n > "$TARBALL_PATH"

log "Writing SHA256SUMS"
(cd "$DIST_DIR" && sha256sum "$TARBALL_NAME" > SHA256SUMS)
TARBALL_SHA="$(cut -d' ' -f1 "$SUMS_PATH")"

PKGBUILD_PATH="$ROOT/packaging/arch/PKGBUILD"
if [[ -f "$PKGBUILD_PATH" ]]; then
    if grep -q '^sha256sums=' "$PKGBUILD_PATH"; then
        log "Rewriting sha256sums= in packaging/arch/PKGBUILD"
        sed -i -E "s/^sha256sums=\([^)]*\)/sha256sums=('${TARBALL_SHA}')/" "$PKGBUILD_PATH"
    else
        log "packaging/arch/PKGBUILD has no sha256sums= line, leaving it alone"
    fi
else
    log "packaging/arch/PKGBUILD not present yet (T5.1), skipping"
fi

# packaging/omarchy/package.json follows the live omarchy-pkgs schema (docs/QUESTIONS.md Q18):
# the repo's own sync derives the checksum from the PKGBUILD, so nothing is written there.

log "Writing release notes"
{
    printf '# omaseed %s\n\n' "$TAG"
    printf '%s\n\n' "$CHANGELOG_SECTION"
    printf '## Install\n\n'
    printf 'Arch / Omarchy, from the source tarball (before the `[omarchy]` repo package lands):\n\n'
    printf '```\n'
    printf 'tar xf %s\n' "$TARBALL_NAME"
    printf 'cd omaseed-%s\n' "$VERSION"
    printf 'makepkg -si -p packaging/arch/PKGBUILD\n'
    printf '```\n\n'
    printf 'Once accepted into the omarchy-pkgs `[omarchy]` repo:\n\n'
    printf '```\n'
    printf 'pacman -S omaseed\n'
    printf '```\n\n'
    printf '## SHA256SUMS\n\n'
    printf '```\n'
    cat "$SUMS_PATH"
    printf '```\n'
} > "$NOTES_PATH"

log "Release artefacts in dist/:"
printf '  %s\n' "$TARBALL_PATH" "$SUMS_PATH" "$NOTES_PATH"

RELEASE_CMD="gh release create ${TAG} dist/${TARBALL_NAME} dist/SHA256SUMS --notes-file dist/RELEASE_NOTES.md"
log "GitHub release command:"
printf '  %s\n' "$RELEASE_CMD"

if [[ $DO_PUBLISH -eq 1 ]]; then
    log "Publishing: pushing main and ${TAG}"
    git push --follow-tags
    log "Publishing: creating the GitHub release"
    eval "$RELEASE_CMD"
else
    log "Dry run: not pushing, not creating a GitHub release. Re-run with --publish to do that."
fi
