#!/usr/bin/env bash
# The version is declared in three places that cannot reference each other:
# CMake, the AppStream metainfo and the AUR PKGBUILD. CMake is the source of
# truth; this fails the build when the others drift out of step with it.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

cmake_version=$(sed -nE 's/^project\(lob VERSION ([0-9.]+).*/\1/p' "$root/CMakeLists.txt")
metainfo_version=$(sed -nE 's/.*<release version="([0-9.]+)".*/\1/p' "$root/data/io.github.puco.lob.metainfo.xml" | head -1)
pkgbuild_version=$(sed -nE 's/^pkgver=(.+)/\1/p' "$root/packaging/aur/lob/PKGBUILD")

status=0
for pair in "metainfo:$metainfo_version" "PKGBUILD:$pkgbuild_version"; do
    name=${pair%%:*}
    value=${pair#*:}
    if [[ "$value" != "$cmake_version" ]]; then
        echo "version mismatch: CMakeLists.txt says '$cmake_version', $name says '$value'" >&2
        status=1
    fi
done

[[ $status -eq 0 ]] && echo "version $cmake_version consistent across CMake, metainfo and PKGBUILD"
exit $status
