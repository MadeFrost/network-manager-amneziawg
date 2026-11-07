#!/bin/sh
# Builds the .deb through debian/, as Launchpad does, from the working tree
# (uncommitted changes included). DEB_BUILD_OPTIONS=nocheck skips the unit tests.
# usage: packaging/build-deb.sh    result: build/network-manager-amneziawg_<version>_<arch>.deb
set -eu
SRC=$(cd "$(dirname "$0")/.." && pwd)
OUT=$SRC/build/deb

# dpkg-buildpackage writes into the parent directory, so build in a copy.
rm -rf "$OUT"
mkdir -p "$OUT/src"
tar -C "$SRC" --exclude=./build --exclude=./.git --exclude='./obj-*' -cf - . | tar -x -C "$OUT/src"
(cd "$OUT/src" && dpkg-buildpackage -b -us -uc)

VERSION=$(cd "$OUT/src" && dpkg-parsechangelog -S Version)
DEB=network-manager-amneziawg_${VERSION}_$(dpkg --print-architecture).deb
cp "$OUT/$DEB" "$SRC/build/"
echo "Built $SRC/build/$DEB"
