#!/usr/bin/env bash
# Build a local source archive through the real PKGBUILD, including check()
# and package(), without publishing or installing anything. Run as non-root.
set -euo pipefail

if [[ $# -ne 3 || $EUID -eq 0 ]]; then
    echo "usage (non-root): bash packaging/check-package.sh SOURCE.tar.gz PKGBUILD WORKDIR" >&2
    exit 2
fi
archive=$(realpath "$1")
recipe=$(realpath "$2")
mkdir -p "$3"
work=$(realpath "$3")
if [[ -e "$work/PKGBUILD" ]]; then
    echo "Choose a fresh package-check directory." >&2
    exit 2
fi
cp "$recipe" "$work/PKGBUILD"
cp "$archive" "$work/$(basename "$archive")"
sha=$(sha256sum "$archive" | cut -d' ' -f1)
sed -i "s|^sha256sums=.*|sha256sums=('$sha')|" "$work/PKGBUILD"

(
    cd "$work"
    # The source filename is already cached here, so no unpublished release
    # URL needs to exist. All build dependencies must be installed by the caller.
    makepkg --cleanbuild --nodeps --noconfirm
    mapfile -t packages < <(makepkg --packagelist)
    package="${packages[0]}"
    bsdtar -xOf "$package" usr/share/applications/io.github.puco.lob.desktop > io.github.puco.lob.desktop
    bsdtar -xOf "$package" usr/share/metainfo/io.github.puco.lob.metainfo.xml > io.github.puco.lob.metainfo.xml
    bsdtar -xOf "$package" usr/share/licenses/lob/LICENSE > package.LICENSE
    test -s package.LICENSE
    desktop-file-validate io.github.puco.lob.desktop
    appstreamcli validate --no-net io.github.puco.lob.metainfo.xml
)
