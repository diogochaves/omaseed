# packaging/omarchy

`package.json` is the `.omarchy/package.json` that
[omarchy-pkgs](https://github.com/omacom/omarchy-pkgs) keeps beside each PKGBUILD. In that repo's
layout it sits at `pkgbuilds/omaseed/.omarchy/package.json`, next to a copy of
[`packaging/arch/PKGBUILD`](../arch/PKGBUILD).

It says the package is built from the PKGBUILD (`"source": "local"`), and that new releases are
found by watching this repository's tags (`vX.Y.Z`).
