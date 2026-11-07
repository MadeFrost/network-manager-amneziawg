#!/bin/sh
# Builds the signed source package for Launchpad from the last commit.
# usage: packaging/ppa.sh [-n]    -n: do not sign (local test build)
# Result: build/ppa/*_source.changes, uploaded with
#   dput ppa:<user>/<ppa> build/ppa/network-manager-amneziawg_*_source.changes
set -eu
SRC=$(cd "$(dirname "$0")/.." && pwd)
cd "$SRC"
SIGN=
[ "${1:-}" = -n ] && SIGN="-us -uc"

PKG=network-manager-amneziawg
VERSION=$(dpkg-parsechangelog -S Version)
MESON_VERSION=$(sed -n "s/^ *version *: *'\([^']*\)'.*/\1/p" meson.build | head -n 1)
if [ "$VERSION" != "$MESON_VERSION" ]; then
	echo "ppa: debian/changelog has $VERSION, meson.build has $MESON_VERSION" >&2
	exit 1
fi
if [ -n "$(git status --porcelain)" ]; then
	echo "ppa: uncommitted changes are not included (the package is built from HEAD)" >&2
fi

OUT=$SRC/build/ppa
rm -rf "$OUT"
mkdir -p "$OUT"
git archive --format=tar --prefix="$PKG-$VERSION/" HEAD | tar -x -C "$OUT"

cd "$OUT/$PKG-$VERSION"
# -S: source only (Launchpad builds the binaries).
# -d: build dependencies are only needed on the builder.
# shellcheck disable=SC2086
dpkg-buildpackage -S -d $SIGN
echo
echo "Source package: $OUT/${PKG}_${VERSION}_source.changes"
