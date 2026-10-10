#!/usr/bin/env bash
set -euo pipefail

# Exercises scripts/release.sh end to end in a throwaway clone, with a synthetic version
# (99.0.0) so the test keeps working after every real release and bump: all of the clone's
# tags are dropped, a CHANGELOG section is added there, and "origin" is a bare repository in
# the scratch directory. `gh` is a stub on PATH that records its arguments and plays GitHub:
# it writes GitHub's tag archive (git archive | gzip -n of the pushed tag) where release.sh
# is told to download it from. No network, and this checkout's git state is never touched.

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/omaseed-release-test.XXXXXX")"
CLONE="$WORK/omaseed"
ORIGIN="$WORK/origin.git"
VERSION=99.0.0
TAG="v$VERSION"

cleanup() {
    rm -rf "$WORK"
}
trap cleanup EXIT

fail() {
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

SOURCE_STATUS_BEFORE="$(git -C "$REPO_ROOT" status --porcelain)"
SOURCE_TAGS_BEFORE="$(git -C "$REPO_ROOT" tag -l)"

echo "==> Cloning $REPO_ROOT into $CLONE"
git clone --quiet --no-hardlinks "$REPO_ROOT" "$CLONE"
cd "$CLONE"
git config user.name "omaseed release test"
git config user.email "release-test@invalid"
git config commit.gpgsign false
git config tag.gpgsign false
# release.sh requires main; the checkout may be on a task branch.
[[ "$(git rev-parse --abbrev-ref HEAD)" == main ]] || git branch -m main
git tag -l | xargs -r git tag -d >/dev/null
git init --quiet --bare "$ORIGIN"
git remote set-url origin "$ORIGIN"
git push --quiet origin main

printf '\n## %s — test\n\nA synthetic release for tests/release/test_release.sh.\n' "$TAG" >>CHANGELOG.md
git commit --quiet -am "Add a synthetic $TAG changelog section"
git push --quiet origin main

# ---- dry run --------------------------------------------------------------------------------
echo "==> A dry run without --bump refuses the version mismatch"
if bash scripts/release.sh "$VERSION" --dry-run >/dev/null 2>&1; then
    fail "the dry run accepted $VERSION while CMakeLists.txt is at another version"
fi

HEAD_BEFORE="$(git rev-parse HEAD)"
echo "==> scripts/release.sh $VERSION --bump --dry-run"
OUTPUT="$(bash scripts/release.sh "$VERSION" --bump --dry-run)"
echo "$OUTPUT"

TARBALL="$CLONE/dist/omaseed-$VERSION.tar.gz"
SUMS="$CLONE/dist/SHA256SUMS"
NOTES="$CLONE/dist/RELEASE_NOTES.md"
[[ -f "$TARBALL" ]] || fail "tarball not created: $TARBALL"
[[ -f "$SUMS" ]] || fail "SHA256SUMS not created: $SUMS"
[[ -f "$NOTES" ]] || fail "RELEASE_NOTES.md not created: $NOTES"

echo "==> The dry run left git alone: no tag, no commit, a clean tree"
git rev-parse -q --verify "refs/tags/$TAG" >/dev/null && fail "the dry run created $TAG"
[[ "$(git rev-parse HEAD)" == "$HEAD_BEFORE" ]] || fail "the dry run made a commit"
[[ -z "$(git status --porcelain)" ]] || fail "the dry run dirtied the tree: $(git status --porcelain)"

TAR_LISTING="$(tar tf "$TARBALL")"
echo "==> The tarball's prefix"
FIRST_ENTRY="${TAR_LISTING%%$'\n'*}" # no pipe: head exiting early SIGPIPEs printf under pipefail
[[ "$FIRST_ENTRY" == "omaseed-$VERSION/"* ]] || fail "tarball's first entry is not prefixed omaseed-$VERSION/: $FIRST_ENTRY"

echo "==> docs/evidence is export-ignored"
if grep -qE "^omaseed-$VERSION/(docs/evidence/|build|dist/)" <<<"$TAR_LISTING"; then
    fail "docs/evidence, build*/ or dist/ leaked into the tarball"
fi
grep -qx "omaseed-$VERSION/CMakeLists.txt" <<<"$TAR_LISTING" || fail "CMakeLists.txt is missing from the tarball"

echo "==> SHA256SUMS"
grep -qE "^[0-9a-f]{64}  omaseed-${VERSION//./\\.}\.tar\.gz$" "$SUMS" || fail "SHA256SUMS has the wrong line format"
(cd "$CLONE/dist" && sha256sum --check --status SHA256SUMS) || fail "SHA256SUMS does not verify against the tarball"
head -n1 "$NOTES" | grep -qxF "# omaseed $TAG" || fail "RELEASE_NOTES.md title is wrong: $(head -n1 "$NOTES")"
echo "$OUTPUT" | grep -qF \
    "gh release create $TAG dist/omaseed-$VERSION.tar.gz dist/SHA256SUMS --notes-file dist/RELEASE_NOTES.md" ||
    fail "did not print the expected gh release create command"

# ---- publish --------------------------------------------------------------------------------
mkdir "$WORK/bin"
cat >"$WORK/bin/gh" <<EOF
#!/usr/bin/env bash
printf '%s\n' "\$*" >>"$WORK/gh.log"
# GitHub: the tag's archive, as served at /archive/refs/tags/<tag>.tar.gz.
if [[ "\$1 \$2" == "release create" ]]; then
    git -C "$ORIGIN" archive --format=tar --prefix="omaseed-$VERSION/" "\$3" | gzip -n >"$WORK/github-archive.tar.gz"
fi
EOF
chmod +x "$WORK/bin/gh"

echo "==> scripts/release.sh $VERSION --bump --publish (stub gh, local origin)"
PATH="$WORK/bin:$PATH" OMASEED_RELEASE_ARCHIVE_URL="file://$WORK/github-archive.tar.gz" \
    bash scripts/release.sh "$VERSION" --bump --publish

echo "==> The tag, on origin, carries the bumped versions and no checksum"
git -C "$ORIGIN" rev-parse -q --verify "refs/tags/$TAG" >/dev/null || fail "$TAG was not pushed"
git -C "$ORIGIN" show "$TAG:CMakeLists.txt" | grep -qE "^[[:space:]]*VERSION[[:space:]]+$VERSION\$" ||
    fail "the tagged CMakeLists.txt is not at $VERSION"
TAGGED_PKGBUILD="$(git -C "$ORIGIN" show "$TAG:packaging/arch/PKGBUILD")"
grep -qx "pkgver=$VERSION" <<<"$TAGGED_PKGBUILD" || fail "the tagged PKGBUILD is not at pkgver=$VERSION"
grep -qx "pkgrel=1" <<<"$TAGGED_PKGBUILD" || fail "the tagged PKGBUILD is not at pkgrel=1"
grep -qx "sha256sums=('SKIP')" <<<"$TAGGED_PKGBUILD" || fail "the tagged PKGBUILD carries a checksum"
git -C "$ORIGIN" show "$TAG:packaging/arch/PKGBUILD.local" | grep -qx "pkgver=$VERSION" ||
    fail "the tagged PKGBUILD.local is not at pkgver=$VERSION"

echo "==> main, on origin, has GitHub's archive checksum in the PKGBUILD, after the tag"
ARCHIVE_SHA="$(sha256sum "$WORK/github-archive.tar.gz" | cut -d' ' -f1)"
git -C "$ORIGIN" show "main:packaging/arch/PKGBUILD" | grep -qx "sha256sums=('$ARCHIVE_SHA')" ||
    fail "origin's main PKGBUILD does not carry the archive checksum $ARCHIVE_SHA"
[[ "$(git -C "$ORIGIN" rev-parse "main^")" == "$(git -C "$ORIGIN" rev-parse "$TAG^{commit}")" ]] ||
    fail "the checksum commit is not the one right after the tag"
[[ "$(git rev-parse HEAD)" == "$(git -C "$ORIGIN" rev-parse main)" ]] || fail "local main and origin differ"

echo "==> The local tarball is GitHub's archive, byte for byte"
[[ "$(cut -d' ' -f1 "$SUMS")" == "$ARCHIVE_SHA" ]] || fail "dist/ tarball and GitHub's archive differ"

echo "==> gh was asked for the release"
grep -qxF "release create $TAG dist/omaseed-$VERSION.tar.gz dist/SHA256SUMS --notes-file dist/RELEASE_NOTES.md" "$WORK/gh.log" ||
    fail "gh was not called as expected: $(cat "$WORK/gh.log")"
[[ -z "$(git status --porcelain)" ]] || fail "publish left the tree dirty: $(git status --porcelain)"

echo "==> A second publish of the same version is refused"
if PATH="$WORK/bin:$PATH" bash scripts/release.sh "$VERSION" --publish >/dev/null 2>&1; then
    fail "a second publish of $TAG went through"
fi

echo "==> This checkout is untouched"
[[ "$(git -C "$REPO_ROOT" status --porcelain)" == "$SOURCE_STATUS_BEFORE" ]] || fail "the source repo's working tree changed"
[[ "$(git -C "$REPO_ROOT" tag -l)" == "$SOURCE_TAGS_BEFORE" ]] || fail "the source repo's tags changed"

echo "ALL CHECKS PASSED"
