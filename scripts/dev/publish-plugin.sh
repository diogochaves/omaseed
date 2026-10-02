#!/usr/bin/env bash
# omaseed — publish the bar widget (#17, docs/07 §6): plugin/ is split out of this repository
# with `git subtree split` and pushed to its own small public repository, the one
# `omarchy plugin add` clones. One source, two distributions: the package ships the same
# folder at /usr/share/omaseed/plugin for `omaseed --install-bar-widget`.
#
#   scripts/dev/publish-plugin.sh VERSION                 # dry run: split, print the pushes
#   scripts/dev/publish-plugin.sh --push VERSION          # tag, and push to the public repository
#   scripts/dev/publish-plugin.sh --remote URL [--push] VERSION   # another remote (a local test)
#
# VERSION must equal plugin/manifest.json's "version" at HEAD. The split is of HEAD (commit
# first; a dirty plugin/ is refused). A dry run writes no ref. With --push the split becomes the
# local branch `omaseed-bar` and the tag `bar-vVERSION` (this repository's own vX.Y.Z tags are
# the app's), pushed to the public repository as `main` and `vVERSION`. Re-running for a
# version already tagged at the same split is harmless; a tag that points elsewhere is refused.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo"

REMOTE="git@github.com:diogochaves/omaseed-bar.git"
PUSH=0
VERSION=""
while [ $# -gt 0 ]; do
    case "$1" in
    --push) PUSH=1 ;;
    --remote)
        [ $# -ge 2 ] || { echo "--remote needs a URL" >&2; exit 2; }
        REMOTE="$2"
        shift
        ;;
    -h | --help) sed -n '2,/^set -e/p' "$0" | sed '$d'; exit 0 ;;
    -*) echo "unknown option: $1" >&2; exit 2 ;;
    *) VERSION="$1" ;;
    esac
    shift
done

[ -n "$VERSION" ] || { echo "usage: $0 [--remote URL] [--push] VERSION" >&2; exit 2; }
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "VERSION is X.Y.Z, not $VERSION" >&2; exit 2; }

if [ -n "$(git status --porcelain -- plugin)" ]; then
    echo "plugin/ has uncommitted changes; the split is of HEAD, so commit them first" >&2
    exit 1
fi

manifest_version="$(git show HEAD:plugin/manifest.json \
    | python3 -c 'import json, sys; print(json.load(sys.stdin)["version"])')"
if [ "$manifest_version" != "$VERSION" ]; then
    echo "plugin/manifest.json says $manifest_version at HEAD, not $VERSION" >&2
    exit 1
fi

if command -v omarchy >/dev/null 2>&1; then
    omarchy plugin validate plugin >/dev/null || { echo "omarchy plugin validate plugin failed" >&2; exit 1; }
fi

echo "splitting plugin/ at $(git rev-parse --short HEAD)…"
split="$(git subtree split -q --prefix=plugin HEAD)"

tag="bar-v$VERSION"
existing="$(git rev-parse -q --verify "refs/tags/$tag^{commit}" || true)"
if [ -n "$existing" ] && [ "$existing" != "$split" ]; then
    echo "$tag already points at ${existing:0:12}, not this split ${split:0:12}" >&2
    exit 1
fi

echo "split ${split:0:12}"
if [ "$PUSH" -eq 0 ]; then
    echo "dry run, nothing written; with --push this tags $tag, moves the branch omaseed-bar and runs:"
    printf '  %s\n' "git push $REMOTE $split:refs/heads/main" \
        "git push $REMOTE refs/tags/$tag:refs/tags/v$VERSION"
    exit 0
fi

git branch -f omaseed-bar "$split" >/dev/null
[ -n "$existing" ] || git tag -a "$tag" -m "omaseed bar widget v$VERSION" "$split"
git push "$REMOTE" "$split:refs/heads/main"
git push "$REMOTE" "refs/tags/$tag:refs/tags/v$VERSION"
echo "published v$VERSION to $REMOTE"
echo "install with: omarchy plugin add ${REMOTE/git@github.com:/https://github.com/} --enable"
